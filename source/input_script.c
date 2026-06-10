/*
 * MultiRexZ80
 *
 * Multi-system Z80 emulator based on SMS Plus GX by Eke-Eke, itself based on
 * SMS Plus by Charles MacDonald.
 *
 * Default project license: GPL-2.0-or-later.
 */

#include "input_script.h"
#include "sms.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef enum input_event_kind
{
    INPUT_EVENT_ACTION = 0,
    INPUT_EVENT_SNAPSHOT = 1,
    INPUT_EVENT_ANALOG0 = 2,
    INPUT_EVENT_ANALOG1 = 3
} input_event_kind_t;

typedef enum input_event_mode
{
    INPUT_MODE_TAP = 0,
    INPUT_MODE_PRESS = 1,
    INPUT_MODE_RELEASE = 2
} input_event_mode_t;

typedef enum input_action_id
{
    IA_UP,
    IA_DOWN,
    IA_LEFT,
    IA_RIGHT,
    IA_A,
    IA_B,
    IA_P2_UP,
    IA_P2_DOWN,
    IA_P2_LEFT,
    IA_P2_RIGHT,
    IA_P2_A,
    IA_P2_B,
    IA_P,
    IA_START,
    IA_PAUSE,
    IA_RESET,
    IA_COIN1,
    IA_COIN2,
    IA_START1,
    IA_START2,
    IA_SERVICE,
    IA_TEST,
    IA_M5_1,
    IA_M5_2,
    IA_M5_RESET,
    IA_COUNT
} input_action_id_t;

typedef struct input_event
{
    uint64_t frame;
    uint8_t kind;
    uint8_t mode;
    uint8_t action;
    uint32_t hold_frames;
    input_t snapshot;
    int32_t analog_x;
    int32_t analog_y;
} input_event_t;

typedef struct input_release
{
    uint64_t frame;
    uint8_t action;
} input_release_t;

struct multirexz80_input_script
{
    input_event_t *events;
    size_t count;
    size_t index;
    input_release_t *releases;
    size_t release_count;
    size_t release_cap;
    uint32_t default_tap_frames;
};

struct multirexz80_input_recorder
{
    FILE *fp;
    uint8_t compact;
    uint8_t have_prev;
    input_t prev;
};

typedef struct action_name
{
    const char *name;
    input_action_id_t action;
} action_name_t;

static const action_name_t action_names[] = {
    {"UP", IA_UP}, {"U", IA_UP},
    {"DOWN", IA_DOWN}, {"D", IA_DOWN},
    {"LEFT", IA_LEFT}, {"L", IA_LEFT},
    {"RIGHT", IA_RIGHT}, {"R", IA_RIGHT},
    {"A", IA_A}, {"BUTTON1", IA_A}, {"B1", IA_A}, {"1", IA_A},
    {"B", IA_B}, {"BUTTON2", IA_B}, {"B2", IA_B}, {"2", IA_B},
    {"P2_UP", IA_P2_UP}, {"2P_UP", IA_P2_UP},
    {"P2_DOWN", IA_P2_DOWN}, {"2P_DOWN", IA_P2_DOWN},
    {"P2_LEFT", IA_P2_LEFT}, {"2P_LEFT", IA_P2_LEFT},
    {"P2_RIGHT", IA_P2_RIGHT}, {"2P_RIGHT", IA_P2_RIGHT},
    {"P2_A", IA_P2_A}, {"P2_BUTTON1", IA_P2_A},
    {"P2_B", IA_P2_B}, {"P2_BUTTON2", IA_P2_B},
    {"P", IA_P}, {"PAUSE_START", IA_P},
    {"START", IA_START},
    {"PAUSE", IA_PAUSE},
    {"RESET", IA_RESET},
    {"COIN", IA_COIN1}, {"COIN1", IA_COIN1}, {"CREDIT", IA_COIN1},
    {"COIN2", IA_COIN2},
    {"START1", IA_START1}, {"1P", IA_START1},
    {"START2", IA_START2}, {"2P", IA_START2},
    {"SERVICE", IA_SERVICE},
    {"TEST", IA_TEST},
    {"M5_1", IA_M5_1},
    {"M5_2", IA_M5_2},
    {"M5_RESET", IA_M5_RESET},
};

static int ascii_ieq(const char *a, const char *b)
{
    while (*a && *b)
    {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'a' && ca <= 'z') ca = (unsigned char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (unsigned char)(cb - 'a' + 'A');
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static void trim(char *s)
{
    char *end;
    while (isspace((unsigned char)*s)) memmove(s, s + 1, strlen(s));
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
}

static int parse_u64_full(const char *s, uint64_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno || end == s || *end) return 0;
    *out = (uint64_t)v;
    return 1;
}

static int parse_i32_full(const char *s, int32_t *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || end == s || *end) return 0;
    *out = (int32_t)v;
    return 1;
}

static int lookup_action(const char *name, uint8_t *out)
{
    char clean[80];
    size_t j = 0;
    for (size_t i = 0; name[i] && j + 1 < sizeof(clean); i++)
    {
        unsigned char c = (unsigned char)name[i];
        if (c == '-' || c == ' ' || c == '.') c = '_';
        clean[j++] = (char)c;
    }
    clean[j] = 0;
    for (size_t i = 0; i < ARRAY_SIZE(action_names); i++)
    {
        if (ascii_ieq(clean, action_names[i].name))
        {
            *out = (uint8_t)action_names[i].action;
            return 1;
        }
    }
    return 0;
}

static int append_event(multirexz80_input_script_t *script, const input_event_t *event)
{
    size_t cap = 0;
    if (script->count)
    {
        cap = 1;
        while (cap < script->count) cap <<= 1;
    }
    if (script->count == cap)
    {
        size_t next_cap = cap ? cap * 2 : 256;
        input_event_t *next = (input_event_t *)realloc(script->events, next_cap * sizeof(*next));
        if (!next) return 0;
        script->events = next;
    }
    script->events[script->count++] = *event;
    return 1;
}

static int cmp_events(const void *a, const void *b)
{
    const input_event_t *ea = (const input_event_t *)a;
    const input_event_t *eb = (const input_event_t *)b;
    if (ea->frame < eb->frame) return -1;
    if (ea->frame > eb->frame) return 1;
    if (ea->kind < eb->kind) return -1;
    if (ea->kind > eb->kind) return 1;
    return 0;
}

static void set_bit(uint8_t *dst, uint8_t mask, int pressed)
{
    if (pressed) *dst = (uint8_t)(*dst | mask);
    else *dst = (uint8_t)(*dst & (uint8_t)~mask);
}

static void apply_action(input_t *dst, uint8_t action, int pressed)
{
    switch ((input_action_id_t)action)
    {
        case IA_UP:       set_bit(&dst->pad[0], INPUT_UP, pressed); break;
        case IA_DOWN:     set_bit(&dst->pad[0], INPUT_DOWN, pressed); break;
        case IA_LEFT:     set_bit(&dst->pad[0], INPUT_LEFT, pressed); break;
        case IA_RIGHT:    set_bit(&dst->pad[0], INPUT_RIGHT, pressed); break;
        case IA_A:        set_bit(&dst->pad[0], INPUT_BUTTON1, pressed); break;
        case IA_B:        set_bit(&dst->pad[0], INPUT_BUTTON2, pressed); break;
        case IA_P2_UP:    set_bit(&dst->pad[1], INPUT_UP, pressed); break;
        case IA_P2_DOWN:  set_bit(&dst->pad[1], INPUT_DOWN, pressed); break;
        case IA_P2_LEFT:  set_bit(&dst->pad[1], INPUT_LEFT, pressed); break;
        case IA_P2_RIGHT: set_bit(&dst->pad[1], INPUT_RIGHT, pressed); break;
        case IA_P2_A:     set_bit(&dst->pad[1], INPUT_BUTTON1, pressed); break;
        case IA_P2_B:     set_bit(&dst->pad[1], INPUT_BUTTON2, pressed); break;
        case IA_P:
            set_bit(&dst->system, IS_GG ? INPUT_START : INPUT_PAUSE, pressed);
            break;
        case IA_START:    set_bit(&dst->system, INPUT_START, pressed); break;
        case IA_PAUSE:    set_bit(&dst->system, INPUT_PAUSE, pressed); break;
        case IA_RESET:    set_bit(&dst->system, INPUT_RESET, pressed); break;
        case IA_COIN1:    set_bit(&dst->arcade, INPUT_ARCADE_COIN1, pressed); break;
        case IA_COIN2:    set_bit(&dst->arcade, INPUT_ARCADE_COIN2, pressed); break;
        case IA_START1:   set_bit(&dst->arcade, INPUT_ARCADE_START1, pressed); break;
        case IA_START2:   set_bit(&dst->arcade, INPUT_ARCADE_START2, pressed); break;
        case IA_SERVICE:  set_bit(&dst->arcade, INPUT_ARCADE_SERVICE, pressed); break;
        case IA_TEST:     set_bit(&dst->arcade, INPUT_ARCADE_TEST, pressed); break;
        case IA_M5_1:     set_bit(&dst->m5_key[1], 0x01, pressed); break;
        case IA_M5_2:     set_bit(&dst->m5_key[1], 0x02, pressed); break;
        case IA_M5_RESET: dst->m5_reset = pressed ? SORDM5_KEY_RESET : 0; break;
        default: break;
    }
}

static int add_release(multirexz80_input_script_t *script, uint64_t frame, uint8_t action)
{
    if (script->release_count == script->release_cap)
    {
        size_t next_cap = script->release_cap ? script->release_cap * 2 : 16;
        input_release_t *next = (input_release_t *)realloc(script->releases, next_cap * sizeof(*next));
        if (!next) return 0;
        script->releases = next;
        script->release_cap = next_cap;
    }
    script->releases[script->release_count].frame = frame;
    script->releases[script->release_count].action = action;
    script->release_count++;
    return 1;
}

static void release_due(multirexz80_input_script_t *script, uint64_t frame, input_t *dst)
{
    size_t out = 0;
    for (size_t i = 0; i < script->release_count; i++)
    {
        if (script->releases[i].frame <= frame)
            apply_action(dst, script->releases[i].action, 0);
        else
            script->releases[out++] = script->releases[i];
    }
    script->release_count = out;
}

static int parse_compact_token(multirexz80_input_script_t *script, uint64_t frame, char *tok)
{
    input_event_t ev;
    char *hold_at;
    char *eq;
    memset(&ev, 0, sizeof(ev));
    trim(tok);
    if (!tok[0]) return 1;
    ev.frame = frame;

    eq = strchr(tok, '=');
    if (eq)
    {
        char *comma;
        *eq++ = 0;
        trim(tok);
        trim(eq);
        comma = strchr(eq, ',');
        if (comma) *comma++ = 0;
        if (ascii_ieq(tok, "ANALOG0") || ascii_ieq(tok, "LIGHTGUN0")) ev.kind = INPUT_EVENT_ANALOG0;
        else if (ascii_ieq(tok, "ANALOG1") || ascii_ieq(tok, "LIGHTGUN1")) ev.kind = INPUT_EVENT_ANALOG1;
        else return 1;
        if (!parse_i32_full(eq, &ev.analog_x)) return 1;
        if (comma)
        {
            trim(comma);
            if (!parse_i32_full(comma, &ev.analog_y)) return 1;
        }
        return append_event(script, &ev) ? 1 : -1;
    }

    ev.kind = INPUT_EVENT_ACTION;
    ev.mode = INPUT_MODE_TAP;
    if (tok[0] == '+') { ev.mode = INPUT_MODE_PRESS; tok++; trim(tok); }
    else if (tok[0] == '-') { ev.mode = INPUT_MODE_RELEASE; tok++; trim(tok); }
    hold_at = strchr(tok, '@');
    if (!hold_at) hold_at = strchr(tok, '*');
    if (hold_at)
    {
        uint64_t hold = 0;
        *hold_at++ = 0;
        trim(tok);
        trim(hold_at);
        if (parse_u64_full(hold_at, &hold) && hold > 0 && hold < 1000000000ull)
            ev.hold_frames = (uint32_t)hold;
    }
    if (!lookup_action(tok, &ev.action)) return 1;
    return append_event(script, &ev) ? 1 : -1;
}

static int parse_compact_segment(multirexz80_input_script_t *script, uint64_t frame, char *segment)
{
    trim(segment);
    if (!segment[0]) return 1;
    if (strchr(segment, '='))
        return parse_compact_token(script, frame, segment);

    char *save = NULL;
    for (char *tok = strtok_r(segment, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
    {
        int ret = parse_compact_token(script, frame, tok);
        if (ret < 0) return ret;
    }
    return 1;
}

static int parse_compact_line(multirexz80_input_script_t *script, char *line)
{
    char *colon = strchr(line, ':');
    char *fmark;
    uint64_t frame = 0;
    if (!colon) return 0;
    *colon++ = 0;
    trim(line);
    fmark = line + strlen(line);
    if (fmark == line) return 0;
    if (fmark[-1] == 'f' || fmark[-1] == 'F') fmark[-1] = 0;
    if (!parse_u64_full(line, &frame)) return 0;

    char *save = NULL;
    for (char *segment = strtok_r(colon, ";\t\r\n", &save); segment; segment = strtok_r(NULL, ";\t\r\n", &save))
    {
        int ret = parse_compact_segment(script, frame, segment);
        if (ret < 0) return ret;
    }
    return 1;
}

static int parse_snapshot_line(multirexz80_input_script_t *script, char *line)
{
    char *tok[17];
    size_t ntok = 0;
    char *save = NULL;
    char *s = strtok_r(line, " \t\r\n,", &save);
    input_event_t ev;
    uint64_t frame = 0, pad0 = 0, pad1 = 0, system = 0;
    uint64_t m5row[SORDM5_KEY_ROWS] = {0, 0, 0, 0, 0, 0, 0};
    uint64_t m5reset = 0;
    uint64_t arcade = 0;
    int32_t analog[4] = {0, 0, 0, 0};

    if (!s || s[0] == '#') return 1;
    if (ascii_ieq(s, "frame")) return 1;
    while (s && ntok < ARRAY_SIZE(tok))
    {
        tok[ntok++] = s;
        s = strtok_r(NULL, " \t\r\n,", &save);
    }
    if (ntok < 4) return 1;
    if (!parse_u64_full(tok[0], &frame) || !parse_u64_full(tok[1], &pad0) ||
        !parse_u64_full(tok[2], &pad1) || !parse_u64_full(tok[3], &system))
        return 1;
    for (size_t i = 4; i < ntok && i < 8; i++)
        parse_i32_full(tok[i], &analog[i - 4]);
    for (size_t i = 8; i < ntok && i < 15; i++)
        parse_u64_full(tok[i], &m5row[i - 8]);
    if (ntok > 15) parse_u64_full(tok[15], &m5reset);
    if (ntok > 16) parse_u64_full(tok[16], &arcade);

    memset(&ev, 0, sizeof(ev));
    ev.kind = INPUT_EVENT_SNAPSHOT;
    ev.frame = frame;
    ev.snapshot.pad[0] = (uint8_t)pad0;
    ev.snapshot.pad[1] = (uint8_t)pad1;
    ev.snapshot.system = (uint8_t)system;
    ev.snapshot.arcade = (uint8_t)arcade;
    ev.snapshot.analog[0][0] = analog[0];
    ev.snapshot.analog[0][1] = analog[1];
    ev.snapshot.analog[1][0] = analog[2];
    ev.snapshot.analog[1][1] = analog[3];
    for (size_t i = 0; i < SORDM5_KEY_ROWS; i++) ev.snapshot.m5_key[i] = (uint8_t)m5row[i];
    ev.snapshot.m5_reset = (uint8_t)m5reset;
    return append_event(script, &ev) ? 1 : -1;
}

int multirexz80_input_script_load(multirexz80_input_script_t **out, const char *path, uint32_t default_tap_frames)
{
    FILE *fp;
    char line[512];
    multirexz80_input_script_t *script;

    if (!out || !path) return 0;
    *out = NULL;
    fp = fopen(path, "rb");
    if (!fp) return 0;
    script = (multirexz80_input_script_t *)calloc(1, sizeof(*script));
    if (!script)
    {
        fclose(fp);
        return 0;
    }
    script->default_tap_frames = default_tap_frames ? default_tap_frames : MULTIREXZ80_INPUT_TAP_FRAMES_DEFAULT;

    while (fgets(line, sizeof(line), fp))
    {
        char work[sizeof(line)];
        char *hash;
        int ret;
        memcpy(work, line, sizeof(work));
        work[sizeof(work) - 1] = 0;
        hash = strchr(work, '#');
        if (hash) *hash = 0;
        trim(work);
        if (!work[0]) continue;
        ret = strchr(work, ':') ? parse_compact_line(script, work) : parse_snapshot_line(script, work);
        if (ret < 0)
        {
            multirexz80_input_script_free(script);
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    if (script->count > 1) qsort(script->events, script->count, sizeof(script->events[0]), cmp_events);
    *out = script;
    return 1;
}

void multirexz80_input_script_free(multirexz80_input_script_t *script)
{
    if (!script) return;
    free(script->events);
    free(script->releases);
    free(script);
}

void multirexz80_input_script_reset(multirexz80_input_script_t *script)
{
    if (!script) return;
    script->index = 0;
    script->release_count = 0;
}

int multirexz80_input_script_active(const multirexz80_input_script_t *script)
{
    return script && script->count > 0;
}

uint32_t multirexz80_input_script_event_count(const multirexz80_input_script_t *script)
{
    return script ? (uint32_t)script->count : 0u;
}

void multirexz80_input_script_apply_frame(multirexz80_input_script_t *script, uint64_t frame, input_t *dst)
{
    if (!script || !dst) return;
    release_due(script, frame, dst);
    while (script->index < script->count && script->events[script->index].frame <= frame)
    {
        const input_event_t *ev = &script->events[script->index++];
        switch ((input_event_kind_t)ev->kind)
        {
            case INPUT_EVENT_SNAPSHOT:
                *dst = ev->snapshot;
                break;
            case INPUT_EVENT_ANALOG0:
                dst->analog[0][0] = ev->analog_x;
                dst->analog[0][1] = ev->analog_y;
                break;
            case INPUT_EVENT_ANALOG1:
                dst->analog[1][0] = ev->analog_x;
                dst->analog[1][1] = ev->analog_y;
                break;
            case INPUT_EVENT_ACTION:
                if (ev->mode == INPUT_MODE_RELEASE)
                    apply_action(dst, ev->action, 0);
                else
                {
                    apply_action(dst, ev->action, 1);
                    if (ev->mode == INPUT_MODE_TAP)
                    {
                        uint32_t hold = ev->hold_frames ? ev->hold_frames : script->default_tap_frames;
                        add_release(script, frame + hold, ev->action);
                    }
                }
                break;
        }
    }
}

static const char *action_output_name(input_action_id_t action)
{
    switch (action)
    {
        case IA_UP: return "UP";
        case IA_DOWN: return "DOWN";
        case IA_LEFT: return "LEFT";
        case IA_RIGHT: return "RIGHT";
        case IA_A: return "A";
        case IA_B: return "B";
        case IA_P2_UP: return "P2_UP";
        case IA_P2_DOWN: return "P2_DOWN";
        case IA_P2_LEFT: return "P2_LEFT";
        case IA_P2_RIGHT: return "P2_RIGHT";
        case IA_P2_A: return "P2_A";
        case IA_P2_B: return "P2_B";
        case IA_P: return "P";
        case IA_START: return "START";
        case IA_PAUSE: return "PAUSE";
        case IA_RESET: return "RESET";
        case IA_COIN1: return "COIN1";
        case IA_COIN2: return "COIN2";
        case IA_START1: return "START1";
        case IA_START2: return "START2";
        case IA_SERVICE: return "SERVICE";
        case IA_TEST: return "TEST";
        case IA_M5_1: return "M5_1";
        case IA_M5_2: return "M5_2";
        case IA_M5_RESET: return "M5_RESET";
        default: return NULL;
    }
}

int multirexz80_input_recorder_open(multirexz80_input_recorder_t **out, const char *path, uint8_t compact)
{
    multirexz80_input_recorder_t *rec;
    if (!out || !path) return 0;
    *out = NULL;
    rec = (multirexz80_input_recorder_t *)calloc(1, sizeof(*rec));
    if (!rec) return 0;
    rec->fp = fopen(path, "wb");
    if (!rec->fp)
    {
        free(rec);
        return 0;
    }
    rec->compact = compact ? 1 : 0;
    if (rec->compact)
    {
        fprintf(rec->fp,
                "# MultiRexZ80 input script v1\n"
                "# Taps:     1550f:A          (held for the player's default tap length)\n"
                "# Holds:    1550f:+A / 1580f:-A\n"
                "# Multiple: 1550f:A,DOWN,START1\n");
    }
    else
    {
        fprintf(rec->fp, "frame pad0 pad1 system analog00 analog01 analog10 analog11 m5y0 m5y1 m5y2 m5y3 m5y4 m5y5 m5y6 m5reset arcade\n");
    }
    *out = rec;
    return 1;
}

void multirexz80_input_recorder_close(multirexz80_input_recorder_t *recorder)
{
    if (!recorder) return;
    if (recorder->fp) fclose(recorder->fp);
    free(recorder);
}

void multirexz80_input_recorder_write_action(multirexz80_input_recorder_t *recorder, uint64_t frame, const char *name, int pressed)
{
    if (!recorder || !recorder->fp || !name || !name[0]) return;
    fprintf(recorder->fp, "%lluf:%c%s\n", (unsigned long long)frame, pressed ? '+' : '-', name);
    fflush(recorder->fp);
}

static void write_if_changed(multirexz80_input_recorder_t *recorder, uint64_t frame, uint8_t oldv, uint8_t newv, uint8_t mask, input_action_id_t action)
{
    const char *name;
    if ((oldv & mask) == (newv & mask)) return;
    name = action_output_name(action);
    if (!name) return;
    multirexz80_input_recorder_write_action(recorder, frame, name, (newv & mask) != 0);
}

void multirexz80_input_recorder_write_state_changes(multirexz80_input_recorder_t *recorder, uint64_t frame, const input_t *state)
{
    const input_t *old;
    input_t zero;
    if (!recorder || !recorder->fp || !state) return;
    if (!recorder->compact)
    {
        fprintf(recorder->fp, "%llu 0x%02X 0x%02X 0x%02X %d %d %d %d 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
                (unsigned long long)frame, state->pad[0], state->pad[1], state->system,
                state->analog[0][0], state->analog[0][1], state->analog[1][0], state->analog[1][1],
                state->m5_key[0], state->m5_key[1], state->m5_key[2], state->m5_key[3],
                state->m5_key[4], state->m5_key[5], state->m5_key[6], state->m5_reset, state->arcade);
        return;
    }

    memset(&zero, 0, sizeof(zero));
    old = recorder->have_prev ? &recorder->prev : &zero;
    write_if_changed(recorder, frame, old->pad[0], state->pad[0], INPUT_UP, IA_UP);
    write_if_changed(recorder, frame, old->pad[0], state->pad[0], INPUT_DOWN, IA_DOWN);
    write_if_changed(recorder, frame, old->pad[0], state->pad[0], INPUT_LEFT, IA_LEFT);
    write_if_changed(recorder, frame, old->pad[0], state->pad[0], INPUT_RIGHT, IA_RIGHT);
    write_if_changed(recorder, frame, old->pad[0], state->pad[0], INPUT_BUTTON1, IA_A);
    write_if_changed(recorder, frame, old->pad[0], state->pad[0], INPUT_BUTTON2, IA_B);
    write_if_changed(recorder, frame, old->pad[1], state->pad[1], INPUT_UP, IA_P2_UP);
    write_if_changed(recorder, frame, old->pad[1], state->pad[1], INPUT_DOWN, IA_P2_DOWN);
    write_if_changed(recorder, frame, old->pad[1], state->pad[1], INPUT_LEFT, IA_P2_LEFT);
    write_if_changed(recorder, frame, old->pad[1], state->pad[1], INPUT_RIGHT, IA_P2_RIGHT);
    write_if_changed(recorder, frame, old->pad[1], state->pad[1], INPUT_BUTTON1, IA_P2_A);
    write_if_changed(recorder, frame, old->pad[1], state->pad[1], INPUT_BUTTON2, IA_P2_B);
    write_if_changed(recorder, frame, old->system, state->system, INPUT_START, IA_START);
    write_if_changed(recorder, frame, old->system, state->system, INPUT_PAUSE, IA_PAUSE);
    write_if_changed(recorder, frame, old->system, state->system, INPUT_RESET, IA_RESET);
    write_if_changed(recorder, frame, old->arcade, state->arcade, INPUT_ARCADE_COIN1, IA_COIN1);
    write_if_changed(recorder, frame, old->arcade, state->arcade, INPUT_ARCADE_COIN2, IA_COIN2);
    write_if_changed(recorder, frame, old->arcade, state->arcade, INPUT_ARCADE_START1, IA_START1);
    write_if_changed(recorder, frame, old->arcade, state->arcade, INPUT_ARCADE_START2, IA_START2);
    write_if_changed(recorder, frame, old->arcade, state->arcade, INPUT_ARCADE_SERVICE, IA_SERVICE);
    write_if_changed(recorder, frame, old->arcade, state->arcade, INPUT_ARCADE_TEST, IA_TEST);

    if (old->analog[0][0] != state->analog[0][0] || old->analog[0][1] != state->analog[0][1])
        fprintf(recorder->fp, "%lluf:ANALOG0=%d,%d\n", (unsigned long long)frame, state->analog[0][0], state->analog[0][1]);
    if (old->analog[1][0] != state->analog[1][0] || old->analog[1][1] != state->analog[1][1])
        fprintf(recorder->fp, "%lluf:ANALOG1=%d,%d\n", (unsigned long long)frame, state->analog[1][0], state->analog[1][1]);

    recorder->prev = *state;
    recorder->have_prev = 1;
}
