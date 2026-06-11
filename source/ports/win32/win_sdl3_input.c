#include "win_sdl3_input.h"

#ifdef MULTIREXZ80_HAVE_SDL3

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Action bit positions must match action_id_t in multirexz80_win32.c:
 *   UP=0 DOWN=1 LEFT=2 RIGHT=3 BUTTON1=4 BUTTON2=5 PAUSE_BUTTON=6
 *   COIN1=7 START1=8 COIN2=9 START2=10 M5_1=11 M5_2=12
 */
enum {
    A_UP = 0, A_DOWN, A_LEFT, A_RIGHT, A_BUTTON1, A_BUTTON2, A_PAUSE,
    A_COIN1, A_START1, A_COIN2, A_START2, A_M5_1, A_M5_2
};
#define BIT(n) (1u << (n))

#define HK_SAVE_STATE_BIT 0x1u
#define HK_LOAD_STATE_BIT 0x2u

struct win_sdl3_input {
    int initialized;
    SDL_Joystick *joy;
    int num_axes, num_buttons, num_hats;
    int axis_centered[2];
    int axis_state[2];
    int axis_pending[2];
    unsigned axis_pending_count[2];
    uint32_t pending_hotkeys;
    char status[128];
};

static void open_first_joystick(win_sdl3_input_t *s) {
    int count = 0;
    SDL_JoystickID *ids;
    if (!s || s->joy) return;
    ids = SDL_GetJoysticks(&count);
    if (ids && count > 0) {
        s->joy = SDL_OpenJoystick(ids[0]);
        if (s->joy) {
            SDL_UpdateJoysticks();
            s->num_axes = SDL_GetNumJoystickAxes(s->joy);
            s->num_buttons = SDL_GetNumJoystickButtons(s->joy);
            s->num_hats = SDL_GetNumJoystickHats(s->joy);
            for (int i = 0; i < 2; ++i) {
                if (i < s->num_axes) {
                    int raw = (int)SDL_GetJoystickAxis(s->joy, i);
                    if (raw > -8000 && raw < 8000) s->axis_centered[i] = 1;
                }
            }
            snprintf(s->status, sizeof(s->status), "SDL3: %s", SDL_GetJoystickName(s->joy) ? SDL_GetJoystickName(s->joy) : "joystick");
        }
    }
    if (ids) SDL_free(ids);
    if (!s->joy) snprintf(s->status, sizeof(s->status), "SDL3: no joystick");
}

static void close_joystick_id(win_sdl3_input_t *s, SDL_JoystickID id) {
    if (!s || !s->joy) return;
    if (SDL_GetJoystickID(s->joy) == id) {
        SDL_CloseJoystick(s->joy);
        s->joy = NULL;
        s->num_axes = s->num_buttons = s->num_hats = 0;
        memset(s->axis_centered, 0, sizeof(s->axis_centered));
        memset(s->axis_state, 0, sizeof(s->axis_state));
        memset(s->axis_pending, 0, sizeof(s->axis_pending));
        memset(s->axis_pending_count, 0, sizeof(s->axis_pending_count));
        snprintf(s->status, sizeof(s->status), "SDL3: no joystick");
    }
}

win_sdl3_input_t *win_sdl3_input_create(void) {
    win_sdl3_input_t *s = (win_sdl3_input_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    strcpy(s->status, "SDL3: disabled");
    SDL_SetMainReady();
    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS)) {
        const char *err = SDL_GetError();
        snprintf(s->status, sizeof(s->status), "SDL3 init failed: %s", err ? err : "unknown");
        free(s);
        return NULL;
    }
    s->initialized = 1;
    open_first_joystick(s);
    return s;
}

void win_sdl3_input_destroy(win_sdl3_input_t *s) {
    if (!s) return;
    if (s->joy) SDL_CloseJoystick(s->joy);
    if (s->initialized) SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS);
    free(s);
}

/* Axis -> discrete direction with center calibration, deadzone hysteresis and a
 * short debounce so noisy analog sticks do not chatter (ported from gp32emu). */
static int filtered_axis_dir(win_sdl3_input_t *s, int axis) {
    const int center = 8000, press = 24000, release = 18000;
    int raw, next = 0;
    if (!s->joy || axis < 0 || axis >= 2 || axis >= s->num_axes) return 0;
    raw = (int)SDL_GetJoystickAxis(s->joy, axis);
    if (!s->axis_centered[axis]) {
        if (raw > -center && raw < center) s->axis_centered[axis] = 1;
        else { s->axis_state[axis] = 0; s->axis_pending[axis] = 0; s->axis_pending_count[axis] = 0; return 0; }
    }
    if (s->axis_state[axis] < 0) next = (raw < -release) ? -1 : 0;
    else if (s->axis_state[axis] > 0) next = (raw > release) ? 1 : 0;
    else if (raw < -press) next = -1;
    else if (raw > press) next = 1;
    if (next != s->axis_state[axis]) {
        if (next == s->axis_pending[axis]) ++s->axis_pending_count[axis];
        else { s->axis_pending[axis] = next; s->axis_pending_count[axis] = 1; }
        if (s->axis_pending_count[axis] >= 2u) { s->axis_state[axis] = next; s->axis_pending_count[axis] = 0; }
    } else { s->axis_pending[axis] = next; s->axis_pending_count[axis] = 0; }
    return s->axis_state[axis];
}

static int joy_button(win_sdl3_input_t *s, int index) {
    return s->joy && index >= 0 && index < s->num_buttons && SDL_GetJoystickButton(s->joy, index);
}

/* Directional actions (bits 0-3) from the hat and first two analog axes. */
static uint32_t joystick_dir_actions(win_sdl3_input_t *s) {
    uint32_t m = 0;
    if (!s->joy) return 0;
    if (s->num_hats > 0) {
        Uint8 hat = SDL_GetJoystickHat(s->joy, 0);
        if (hat & SDL_HAT_LEFT)  m |= BIT(A_LEFT);
        if (hat & SDL_HAT_RIGHT) m |= BIT(A_RIGHT);
        if (hat & SDL_HAT_UP)    m |= BIT(A_UP);
        if (hat & SDL_HAT_DOWN)  m |= BIT(A_DOWN);
    }
    {
        int ax0 = filtered_axis_dir(s, 0);
        int ax1 = filtered_axis_dir(s, 1);
        if (ax0 < 0) m |= BIT(A_LEFT);
        if (ax0 > 0) m |= BIT(A_RIGHT);
        if (ax1 < 0) m |= BIT(A_UP);
        if (ax1 > 0) m |= BIT(A_DOWN);
    }
    return m;
}

/* Raw button bitmask (bit i == button i held). */
static uint32_t joystick_raw_buttons(win_sdl3_input_t *s) {
    uint32_t b = 0;
    int n = s->num_buttons;
    if (!s->joy) return 0;
    if (n > 32) n = 32;
    for (int i = 0; i < n; ++i)
        if (joy_button(s, i)) b |= (1u << i);
    return b;
}

uint32_t win_sdl3_input_poll(win_sdl3_input_t *s, int *quit_requested, uint32_t *out_dir_actions) {
    SDL_Event ev;
    if (out_dir_actions) *out_dir_actions = 0;
    if (!s || !s->initialized) return 0;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            if (quit_requested) *quit_requested = 1;
            break;
        case SDL_EVENT_JOYSTICK_ADDED:
            if (!s->joy) open_first_joystick(s);
            break;
        case SDL_EVENT_JOYSTICK_REMOVED:
            close_joystick_id(s, ev.jdevice.which);
            break;
        default:
            break;
        }
    }
    SDL_PumpEvents();
    if (s->joy) SDL_UpdateJoysticks();
    if (out_dir_actions) *out_dir_actions = joystick_dir_actions(s);
    return joystick_raw_buttons(s);
}

uint32_t win_sdl3_input_take_hotkeys(win_sdl3_input_t *s) {
    uint32_t h;
    if (!s) return 0;
    h = s->pending_hotkeys;
    s->pending_hotkeys = 0;
    return h;
}

const char *win_sdl3_input_status(const win_sdl3_input_t *s) { return s ? s->status : "SDL3: disabled"; }

#else /* !MULTIREXZ80_HAVE_SDL3 : Win32 build has no SDL dependency */

typedef int win_sdl3_input_translation_unit_not_empty;

#endif /* MULTIREXZ80_HAVE_SDL3 */
