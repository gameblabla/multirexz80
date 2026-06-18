/*
 * MultiRexZ80
 *
 * Default project license: GPL-2.0-or-later.
 *
 * Compact YM2203 (OPN) sound core for the Taito L-System driver.
 * See ym2203.h for details.
 */

#include "shared.h"
#include "ym2203.h"
#include <math.h>

/* ---- SSG (AY-3-8910 equivalent) --------------------------------------- */

#define SSG_TONE_A   0
#define SSG_TONE_B   1
#define SSG_TONE_C   2
#define SSG_CHANNELS 3

typedef struct
{
    uint16_t period;     /* 12-bit tone period (0 = half) */
    uint16_t count;
    uint8_t  level;      /* 4-bit volume / envelope-amplitude source */
    uint8_t  level_fixed;
    uint8_t  bit;
} ssg_tone;

typedef struct
{
    uint32_t period;     /* 5-bit noise period (0 = half) */
    uint32_t count;
    uint32_t rng;        /* 17-bit LFSR */
    uint8_t  bit;
} ssg_noise;

typedef struct
{
    uint16_t period;     /* 16-bit envelope period (0 = half) */
    uint16_t count;
    uint8_t  shape;      /* 4-bit shape */
    uint8_t  pos;        /* 0..31 position in the 32-step envelope */
    uint8_t  holding;
    uint8_t  amplitude;  /* current 5-bit envelope amplitude */
} ssg_env;

typedef struct
{
    ssg_tone tone[SSG_CHANNELS];
    ssg_noise noise;
    ssg_env   env;
    uint8_t   enable;        /* bit0/1/2 = tone A/B/C, bit3/4/5 = noise A/B/C */
    uint8_t   env_shape_cur; /* shape latched on continue/attack/alt/hold writes */
    uint8_t   port_a;        /* input port A (DSW) */
    uint8_t   port_b;        /* input port B (DSW) */
    uint8_t   port_a_dir;
    uint8_t   port_b_dir;
} ym_ssg;

/* ---- FM (compact 3-channel / 4-operator) ------------------------------ */

#define FM_CHANNELS 3
#define FM_OPS      4

typedef struct
{
    /* Per-operator state */
    uint32_t phase;          /* 10-bit phase accumulator (top 10 bits index sine) */
    uint16_t phase_freq;     /* frequency block + fnum combined into phase delta */
    uint16_t block;
    uint16_t fnum;
    int32_t  key_scale;      /* key scale rate */
    uint16_t detune;         /* detune value */
    uint16_t multiple;       /* frequency multiplier (0 = 0.5) */
    uint16_t total_level;    /* 7-bit output level (0 = loudest) */
    uint16_t attack_rate;
    uint16_t decay_rate;
    uint16_t sustain_rate;
    uint16_t release_rate;
    uint16_t sustain_level;  /* 4-bit */
    uint8_t  key_on;
    uint8_t  env_state;      /* 0=off 1=attack 2=decay 3=sustain 4=release */
    int32_t  env_phase;      /* 0..511 envelope phase */
    int32_t  env_level;      /* current envelope attenuation (0=loud, 1024=silent) */
    uint32_t env_rate;       /* current rate */
    int32_t  feedback;       /* 0..7 */
    int32_t  last_out;       /* feedback sample memory */
} fm_op;

typedef struct
{
    fm_op op[FM_OPS];
    uint8_t algorithm;       /* 0..7 */
    uint8_t feedback;        /* OP1 feedback (3 bits) */
    uint8_t ams;             /* amplitude modulation sensitivity */
    uint8_t pms;             /* phase modulation sensitivity */
} fm_channel;

typedef struct
{
    fm_channel ch[FM_CHANNELS];
    uint8_t    lfo_enable;
    uint8_t    lfo_freq;
    uint32_t   lfo_phase;
    uint8_t    ams_depth;
    uint8_t    pms_depth;
    uint8_t    status;       /* bit 7 = timer A, bit 6 = timer B, bit 5 = busy, etc */
    uint8_t    addr;         /* current register address latch */
    uint8_t    irq_mask;
    uint8_t    irq_pending;
    uint8_t    key_on[2];    /* 6 bits of key-on per byte (3 channels x 2 ops halves) */
    void      (*irq_cb)(void *opaque, int state);
    void      *irq_opaque;
} ym_fm;

struct ym2203_state
{
    ym_ssg  ssg;
    ym_fm   fm;
    uint32_t clock;
    uint32_t sample_rate;
    uint32_t ssg_step;       /* SSG phase step per output sample */
    uint32_t fm_step;        /* FM phase step per output sample */
    uint32_t lfo_step;
    uint32_t ssg_accum;
    uint32_t fm_accum;
    uint32_t lfo_accum;
};

/* ---- SSG envelope shapes (32 steps each) ------------------------------ */
/* The AY-3-8910 envelope has 16 shapes, each generating a 32-step amplitude
 * pattern.  We build the active pattern on shape write. */

static const uint8_t ssg_env_continue_bit = 0x08;
static const uint8_t ssg_env_attack_bit   = 0x04;
static const uint8_t ssg_env_alt_bit      = 0x02;
static const uint8_t ssg_env_hold_bit     = 0x01;

static void ssg_compute_env(ym_ssg *s)
{
    /* Compute the 32-step amplitude for the current shape.  The standard
     * AY-3-8910 envelope: starts at 0 or 31, ramps to 31 or 0, then holds
     * or repeats (alternate).  When hold+continue are both off, the shape
     * is a single decay/attack then zero. */
    uint8_t shape = s->env.shape;
    uint8_t attack = (shape & ssg_env_attack_bit) ? 1 : 0;
    uint8_t alt    = (shape & ssg_env_alt_bit)    ? 1 : 0;
    uint8_t hold   = (shape & ssg_env_hold_bit)   ? 1 : 0;
    uint8_t cont   = (shape & ssg_env_continue_bit) ? 1 : 0;
    uint8_t pos = s->env.pos;
    uint8_t amp;

    if (s->env.holding)
    {
        s->env.amplitude = (hold && (shape & ssg_env_attack_bit)) ? 31 : 0;
        return;
    }

    if (attack)
        amp = pos;                 /* 0 -> 31 */
    else
        amp = (uint8_t)(31 - pos); /* 31 -> 0 */

    s->env.amplitude = amp;

    pos++;
    if (pos >= 32)
    {
        if (hold)
        {
            s->env.holding = 1;
            s->env.amplitude = (alt ^ attack) ? 0 : 31;
            if (!cont) s->env.amplitude = 0;
        }
        else
        {
            pos = 0;
            if (!cont && !alt)
            {
                /* Single decay then silence. */
                s->env.holding = 1;
                s->env.amplitude = 0;
            }
        }
    }
    s->env.pos = pos;
}

static void ssg_write(ym_ssg *s, uint8_t addr, uint8_t data)
{
    switch (addr & 0x0f)
    {
        case 0: case 1: case 2: case 3:
        {
            int ch = addr >> 1;
            if (addr & 1) s->tone[ch].period = (uint16_t)((s->tone[ch].period & 0x00ff) | ((data & 0x0f) << 8));
            else          s->tone[ch].period = (uint16_t)((s->tone[ch].period & 0x0f00) | data);
            break;
        }
        case 4: case 5:
        {
            int ch = addr - 4;
            if (data & 0x10) { s->tone[ch].level_fixed = 0; s->tone[ch].level = data & 0x0f; }
            else             { s->tone[ch].level_fixed = 1;  s->tone[ch].level = 0; } /* env-driven */
            /* MAME uses the lower nibble directly for fixed level. */
            s->tone[ch].level_fixed = (data & 0x10) ? 1 : 0;
            s->tone[ch].level = data & 0x0f;
            break;
        }
        case 6:
            s->noise.period = (uint32_t)(data & 0x1f);
            break;
        case 7:
            s->enable = data;
            break;
        case 8:
            /* Envelope period fine */
            s->env.period = (uint16_t)((s->env.period & 0xff00) | data);
            break;
        case 9:
            s->env.period = (uint16_t)((s->env.period & 0x00ff) | ((uint16_t)data << 8));
            break;
        case 10:
            s->env.shape = data & 0x0f;
            s->env.pos = 0;
            s->env.holding = 0;
            s->env.count = 0;
            ssg_compute_env(s);
            break;
        case 12:
            s->port_a_dir = data;
            break;
        case 13:
            s->port_b_dir = data;
            break;
        case 14:
            if (!(s->port_a_dir & 0xff)) s->port_a = data;
            break;
        case 15:
            if (!(s->port_b_dir & 0xff)) s->port_b = data;
            break;
        default: break;
    }
}

static uint8_t ssg_read(ym_ssg *s, uint8_t addr)
{
    switch (addr & 0x0f)
    {
        case 14: return s->port_a;
        case 15: return s->port_b;
        default: return 0xff;
    }
}

/* ---- FM sine + envelope tables ---------------------------------------- */
/* The OPN uses a sine ROM with log-scaled output.  We use a 256-entry sine
 * table scaled to int32.  Envelope attenuation maps an index (0..1023) to a
 * gain via a decaying exponential. */

#define FM_SINE_LEN 256

static int32_t fm_sine_table[FM_SINE_LEN];
static int32_t fm_pow_table[256]; /* 0..1.0 attenuation -> 0..32767 */
static int ym2203_tables_init = 0;

static void ym2203_init_tables(void)
{
    int i;
    if (ym2203_tables_init) return;
    for (i = 0; i < FM_SINE_LEN; i++)
    {
        double v = sin((6.283185307179586 * (double)i) / (double)FM_SINE_LEN);
        fm_sine_table[i] = (int32_t)(v * 8192.0);
    }
    /* pow_table[i] = 2^(-i/256) * 32767, i.e. -0.375 dB per step. */
    for (i = 0; i < 256; i++)
    {
        double dB = -(double)i * (6.0 * 256.0 / 1024.0) / 256.0; /* approx */
        double g = pow(10.0, dB / 20.0);
        if (g > 1.0) g = 1.0;
        fm_pow_table[i] = (int32_t)(g * 32767.0);
    }
    ym2203_tables_init = 1;
}

static const uint8_t fm_op_select[8][4] =
{
    /* For each algorithm (0..7), which operators contribute to the output.
     * OPN algorithms route OP1->OP2->OP3->OP4 with feedback on OP1; the
     * "output" operator is the last in the chain that reaches the DAC. */
    { 0, 0, 0, 1 },  /* alg 0: OP4 out */
    { 0, 0, 0, 1 },  /* alg 1: OP4 out (OP3 added) */
    { 0, 0, 1, 1 },  /* alg 2: OP3+OP4 out */
    { 0, 0, 0, 1 },  /* alg 3 */
    { 0, 0, 1, 1 },  /* alg 4: OP3, OP4 out */
    { 0, 0, 1, 1 },  /* alg 5: OP2, OP3, OP4 out */
    { 0, 1, 1, 1 },  /* alg 6: OP2, OP3, OP4 out */
    { 1, 1, 1, 1 },  /* alg 7: all out */
};

/* FM register write (OPN register map, 0x00..0xff) */
static void fm_write(ym_fm *f, uint8_t addr, uint8_t data)
{
    f->addr = addr;
    if (addr >= 0x21 && addr <= 0x9e && (addr & 3) != 3)
    {
        int op = (addr >> 2) & 3;
        int ch = (addr >> 0) & 3;
        if (ch >= FM_CHANNELS) return;
        if (op >= FM_OPS) return;
        {
            fm_op *o = &f->ch[ch].op[op];
            switch (addr & 0xf0)
            {
                case 0x20: /* detune / multiple */
                    if ((addr & 0x0f) < 0x08)
                    {
                        o->multiple = data & 0x0f;
                        o->detune = (data >> 4) & 0x07;
                    }
                    else if (addr == 0x27)
                    {
                        f->key_on[0] = data;
                        /* update key_on for channels 0..2 */
                    }
                    else if (addr == 0x28)
                    {
                        f->key_on[1] = data;
                    }
                    break;
                case 0x40: /* total level */
                    o->total_level = data & 0x7f;
                    break;
                case 0x50: /* key scale / attack rate */
                    o->key_scale = data >> 6;
                    o->attack_rate = data & 0x1f;
                    break;
                case 0x60: /* decay rate */
                    o->decay_rate = data & 0x1f;
                    break;
                case 0x70: /* sustain rate */
                    o->sustain_rate = data & 0x1f;
                    break;
                case 0x80: /* sustain level / release rate */
                    o->sustain_level = (data >> 4) & 0x0f;
                    o->release_rate = data & 0x0f;
                    break;
                case 0xa0: /* fnum low (channel-scoped) */
                    if (op == 0)
                        o->fnum = (uint16_t)((o->fnum & 0xff00) | data);
                    break;
                case 0xb0:
                    if (op == 0)
                    {
                        o->block = (data >> 3) & 0x07;
                        o->fnum = (uint16_t)((o->fnum & 0x00ff) | ((data & 0x07) << 8));
                    }
                    break;
                default: break;
            }
        }
    }
    else
    {
        switch (addr)
        {
            case 0x22: /* LFO */
                f->lfo_enable = (data >> 3) & 1;
                f->lfo_freq = data & 0x07;
                break;
            case 0x24: /* timer A period (high) */
            case 0x25: /* timer A period (low) */
                break;
            case 0x26: /* timer B period */
                break;
            case 0x27: /* timer control */
                f->irq_mask = data & 0x07;
                break;
            case 0x28: /* key on/off */
                f->key_on[0] = data;
                break;
            case 0xb0: /* algorithm / feedback (channel 0..2 at 0xb0/1/2) */
                /* handled in operator path above when addr<0xb4 */
                break;
            default: break;
        }
    }
}

static uint8_t fm_read(ym_fm *f, uint8_t addr)
{
    if (addr == 0) return f->status;
    return 0;
}

/* ---- Public API ------------------------------------------------------- */

ym2203_state *ym2203_create(uint32_t clock, uint32_t sample_rate)
{
    ym2203_state *st = (ym2203_state *)calloc(1, sizeof(ym2203_state));
    if (!st) return NULL;
    ym2203_init_tables();
    st->clock = clock;
    st->sample_rate = sample_rate ? sample_rate : 44100;
    /* SSG runs at clock/8 internally; we step the SSG counters per output
     * sample at (clock/8) / sample_rate steps. */
    st->ssg_step = (clock / 8u) / st->sample_rate;
    st->fm_step  = (clock / 6u) / st->sample_rate; /* OPN FM engine clock = clock/6 per channel */
    st->lfo_step = (clock / 660u) / st->sample_rate;
    ym2203_reset(st);
    return st;
}

void ym2203_destroy(ym2203_state *st) { free(st); }

void ym2203_reset(ym2203_state *st)
{
    int i;
    if (!st) return;
    memset(&st->ssg, 0, sizeof(st->ssg));
    memset(&st->fm, 0, sizeof(st->fm));
    for (i = 0; i < SSG_CHANNELS; i++)
    {
        st->ssg.tone[i].period = 0;
        st->ssg.tone[i].level = 0;
        st->ssg.tone[i].level_fixed = 1;
    }
    st->ssg.noise.rng = 1;
    st->ssg.enable = 0xff;
    st->ssg.port_a = 0xff;
    st->ssg.port_b = 0xff;
    st->fm.status = 0;
}

void ym2203_write(ym2203_state *st, uint32_t offset, uint8_t data)
{
    if (!st) return;
    if ((offset & 1) == 0)
    {
        /* Address latch.  OPN uses even addresses for the register select
         * and odd addresses for the data write. */
        if (offset & 2)
            st->fm.addr = data;
        else
            st->ssg.port_a_dir = data; /* not used */
        return;
    }
    /* Data write.  The A0 line distinguishes SSG vs FM address spaces. */
    if (offset & 2)
    {
        fm_write(&st->fm, st->fm.addr, data);
    }
    else
    {
        ssg_write(&st->ssg, st->fm.addr, data);
    }
}

uint8_t ym2203_read(ym2203_state *st, uint32_t offset)
{
    if (!st) return 0xff;
    if ((offset & 1) == 0) return st->fm.status; /* status read */
    if (offset & 2) return fm_read(&st->fm, st->fm.addr);
    return ssg_read(&st->ssg, st->fm.addr);
}

/* ---- Sample generation ------------------------------------------------ */

static int32_t ssg_tone_amp(ssg_tone *t, uint8_t env_amp)
{
    if (t->level_fixed)
        return t->level << 1;             /* 0..30 */
    return (env_amp >> 1);                /* envelope-driven, 0..15 */
}

static void ssg_step(ym_ssg *s, uint32_t steps)
{
    uint32_t i;
    for (i = 0; i < steps; i++)
    {
        int c;
        for (c = 0; c < SSG_CHANNELS; c++)
        {
            uint16_t per = s->tone[c].period ? s->tone[c].period : 1;
            s->tone[c].count++;
            if (s->tone[c].count >= per)
            {
                s->tone[c].count = 0;
                s->tone[c].bit ^= 1;
            }
        }
        /* Noise */
        {
            uint32_t per = s->noise.period ? (s->noise.period << 1) : 1;
            s->noise.count++;
            if (s->noise.count >= per)
            {
                s->noise.count = 0;
                /* 17-bit LFSR: bit = bit17 ^ (bit14 & bit0) shift */
                uint32_t r = s->noise.rng;
                uint32_t bit = ((r >> 0) ^ (r >> 3) ^ (r >> 5)) & 1;
                r = (r >> 1) | (bit << 16);
                s->noise.rng = r;
                s->noise.bit = (uint8_t)(r & 1);
            }
        }
        /* Envelope */
        {
            uint16_t per = s->env.period ? s->env.period : 1;
            s->env.count++;
            if (s->env.count >= per)
            {
                s->env.count = 0;
                ssg_compute_env(s);
            }
        }
    }
}

static void fm_advance_env(fm_op *o, uint32_t samples)
{
    /* Compact ADSR: states 0=off 1=attack 2=decay 3=sustain 4=release. */
    uint32_t i;
    for (i = 0; i < samples; i++)
    {
        int32_t rate;
        switch (o->env_state)
        {
            case 1: rate = o->attack_rate;  break;
            case 2: rate = o->decay_rate;   break;
            case 3: rate = o->sustain_rate; break;
            case 4: rate = o->release_rate; break;
            default: return;
        }
        if (rate == 0) { if (o->env_state == 1) { o->env_level = 0; o->env_state = 2; } continue; }
        {
            int32_t step = rate << 1;
            if (o->env_state == 1)
            {
                o->env_level -= step;
                if (o->env_level <= 0)
                {
                    o->env_level = 0;
                    o->env_state = 2;
                }
            }
            else
            {
                o->env_level += step;
                if (o->env_state == 2 && (o->env_level >> 4) >= o->sustain_level)
                    o->env_state = 3;
                if (o->env_level > 1024) o->env_level = 1024;
            }
        }
    }
}

static int32_t fm_op_sample(fm_op *o, int32_t phase_in, int32_t feedback)
{
    int32_t phase = (int32_t)(o->phase >> 22) + phase_in;
    int32_t idx = phase & 0xff;
    int32_t sine = fm_sine_table[idx];
    int32_t amp;
    int32_t env = o->env_level;
    if (env > 1024) env = 1024;
    /* total_level adds attenuation. */
    env += o->total_level << 3;
    if (env > 1024) env = 1024;
    /* Approximate exponential attenuation. */
    {
        int32_t gi = env >> 2;
        if (gi > 255) gi = 255;
        amp = (sine * fm_pow_table[gi]) >> 15;
    }
    if (o->feedback && feedback)
    {
        amp += (feedback * o->feedback) >> 3;
    }
    return amp;
}

static void fm_step(ym_fm *f, uint32_t samples)
{
    uint32_t i;
    int ch;
    for (i = 0; i < samples; i++)
    {
        for (ch = 0; ch < FM_CHANNELS; ch++)
        {
            int op_i;
            fm_channel *c = &f->ch[ch];
            int32_t feedback = 0;
            int32_t chain[FM_OPS];
            for (op_i = 0; op_i < FM_OPS; op_i++)
            {
                fm_op *o = &c->op[op_i];
                uint32_t mult = o->multiple ? o->multiple : 1;
                uint32_t delta = ((uint32_t)o->fnum << (o->block)) * mult;
                o->phase += delta << 2;
                chain[op_i] = fm_op_sample(o, op_i ? chain[op_i-1] : 0, op_i == 0 ? o->last_out : 0);
                if (op_i == 0) o->last_out = chain[0];
                fm_advance_env(o, 1);
            }
            (void)feedback;
        }
        if (f->lfo_enable) f->lfo_phase += f->lfo_freq + 1;
    }
}

void ym2203_update(ym2203_state *st, int16_t *left, int16_t *right, int32_t samples)
{
    int32_t i;
    if (!st || !left || !right || samples <= 0) return;

    for (i = 0; i < samples; i++)
    {
        /* Step the SSG and FM by one output sample. */
        ssg_step(&st->ssg, st->ssg_step);
        fm_step(&st->fm, 1);

        /* Mix SSG: 3 channels, mono (YM2203 SSG is mono). */
        {
            int32_t mix = 0;
            int c;
            uint8_t env = st->ssg.env.amplitude;
            for (c = 0; c < SSG_CHANNELS; c++)
            {
                int32_t amp = ssg_tone_amp(&st->ssg.tone[c], env);
                int32_t tone_bit = st->ssg.tone[c].bit;
                int32_t noise_bit = st->ssg.noise.bit;
                int32_t tone_en = (st->ssg.enable >> c) & 1;
                int32_t noise_en = (st->ssg.enable >> (c + 3)) & 1;
                int32_t v = 0;
                if (tone_en && tone_bit) v += amp;
                if (noise_en && noise_bit) v += amp;
                mix += v;
            }
            /* SSG output is 0..90; scale to int16 with some gain. */
            mix = (mix * 1400) >> 4;
            if (mix > 32767) mix = 32767;
            if (mix < -32768) mix = -32768;
            left[i] = (int16_t)mix;
            right[i] = (int16_t)mix;
        }

        /* Mix FM: sum the output operators across channels. */
        {
            int32_t mix = 0;
            int ch;
            for (ch = 0; ch < FM_CHANNELS; ch++)
            {
                fm_channel *c = &st->fm.ch[ch];
                int op_i;
                for (op_i = 0; op_i < FM_OPS; op_i++)
                {
                    if (fm_op_select[c->algorithm & 7][op_i])
                    {
                        int32_t v = c->op[op_i].last_out;
                        mix += v;
                    }
                }
            }
            mix = (mix * 3) >> 2;
            if (mix > 32767) mix = 32767;
            if (mix < -32768) mix = -32768;
            left[i] = (int16_t)(((int32_t)left[i] + mix) >> 1);
            right[i] = (int16_t)(((int32_t)right[i] + mix) >> 1);
        }
    }
}

/* ---- State save / load ------------------------------------------------ */

uint32_t ym2203_state_size(ym2203_state *st)
{
    (void)st;
    return (uint32_t)(sizeof(ym_ssg) + sizeof(ym_fm) + sizeof(uint32_t) * 8);
}

int ym2203_save_state(ym2203_state *st, void *data, uint32_t size)
{
    uint8_t *p = (uint8_t *)data;
    if (!st || !data || size < ym2203_state_size(st)) return 0;
    memcpy(p, &st->ssg, sizeof(st->ssg)); p += sizeof(st->ssg);
    memcpy(p, &st->fm,  sizeof(st->fm));  p += sizeof(st->fm);
    memcpy(p, &st->ssg_step,  sizeof(uint32_t) * 8);
    return 1;
}

int ym2203_load_state(ym2203_state *st, const void *data, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!st || !data || size < ym2203_state_size(st)) return 0;
    memcpy(&st->ssg, p, sizeof(st->ssg)); p += sizeof(st->ssg);
    memcpy(&st->fm,  p, sizeof(st->fm));  p += sizeof(st->fm);
    memcpy(&st->ssg_step, p, sizeof(uint32_t) * 8);
    return 1;
}
