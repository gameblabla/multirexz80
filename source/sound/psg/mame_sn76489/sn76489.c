/*
 * MultiRexZ80
 *
 * Multi-system Z80 emulator based on SMS Plus GX by Eke-Eke, itself based on
 * SMS Plus by Charles MacDonald.
 *
 * Default project license: GPL-2.0-or-later.  File-specific notices below
 * are retained and take precedence for imported or derived components,
 * including MAME-derived code and other third-party modules.
 */

/*
 * Texas Instruments / Sega SN76489-family PSG emulation.
 *
 * Compact C backend for MultiRexZ80 when MAME_PSG is enabled.  This follows
 * current MAME sn76496_base_device semantics for the variants used here:
 * Sega VDP PSG, Game Gear PSG, TI SN76489, and SN76489A-style PSG.
 *
 * Based on behavior and structure from MAME's sn76496.cpp.
 * MAME source license: BSD-3-Clause
 * copyright-holders:Nicola Salmoria, with contributions by Lord Nightmare,
 * Michael Zapf, ValleyBell, Qbix, NewRisingSun, and other MAME contributors.
 *
 * This C adaptation and MultiRexZ80 integration:
 * Copyright (C) 2026 gameblabla.
 */

#include <stdint.h>
#include <string.h>
#include "sn76489.h"

#define MAX_OUTPUT 0x7fff

sn76489_t PSG;

static int sega_noise_mode(void)
{
    return (PSG.m_register[6] & 0x04) != 0;
}

static void update_ready_state(void)
{
    if (PSG.m_cycles_to_ready > 0)
    {
        PSG.m_cycles_to_ready--;
        PSG.m_ready_state = 0;
    }
    else
    {
        PSG.m_ready_state = 1;
    }
}

void SN76489_Init(uint32_t machine, uint32_t clock, uint32_t sample_rate)
{
    uint32_t i;
    int32_t gain = 0;
    double out;

    memset(&PSG, 0, sizeof(PSG));

    switch (machine)
    {
        default:
        case 0: /* MAME SEGAPSG: Master System / Mark III VDP PSG */
            PSG.m_feedback_mask = 0x8000;
            PSG.m_whitenoise_tap1 = 0x01;
            PSG.m_whitenoise_tap2 = 0x08;
            PSG.m_negate = 1;
            PSG.m_stereo = 0;
            PSG.m_clock_divider = 8;
            PSG.m_ncr_style_psg = 0;
            PSG.m_sega_style_psg = 1;
            break;

        case 1: /* MAME GAMEGEAR: Sega VDP PSG plus stereo register */
            PSG.m_feedback_mask = 0x8000;
            PSG.m_whitenoise_tap1 = 0x01;
            PSG.m_whitenoise_tap2 = 0x08;
            PSG.m_negate = 1;
            PSG.m_stereo = 1;
            PSG.m_clock_divider = 8;
            PSG.m_ncr_style_psg = 0;
            PSG.m_sega_style_psg = 1;
            break;

        case 2: /* MAME SN76489: ColecoVision / TI SN76489 */
            PSG.m_feedback_mask = 0x4000;
            PSG.m_whitenoise_tap1 = 0x01;
            PSG.m_whitenoise_tap2 = 0x02;
            PSG.m_negate = 1;
            PSG.m_stereo = 0;
            PSG.m_clock_divider = 8;
            PSG.m_ncr_style_psg = 0;
            PSG.m_sega_style_psg = 0;
            break;

        case 3: /* MAME SN76489A-style: SG-1000 / SC-3000 / SF-7000 */
            PSG.m_feedback_mask = 0x10000;
            PSG.m_whitenoise_tap1 = 0x04;
            PSG.m_whitenoise_tap2 = 0x08;
            PSG.m_negate = 0;
            PSG.m_stereo = 0;
            PSG.m_clock_divider = 8;
            PSG.m_ncr_style_psg = 0;
            PSG.m_sega_style_psg = 0;
            break;
    }

    PSG.m_last_register = PSG.m_sega_style_psg ? 3 : 0;
    for (i = 0; i < 8; i += 2)
    {
        PSG.m_register[i] = 0;
        PSG.m_register[i + 1] = PSG.m_sega_style_psg ? 0x0f : 0x00;
    }

    for (i = 0; i < 4; i++)
    {
        PSG.m_output[i] = 0;
        PSG.m_count[i] = 0;
        PSG.m_period[i] = (PSG.m_sega_style_psg || i == 3) ? 0 : 0x400;
    }

    PSG.m_RNG = PSG.m_feedback_mask;
    PSG.m_output[3] = PSG.m_RNG & 1;
    PSG.m_stereo_mask = 0xff;
    PSG.m_current_clock = PSG.m_clock_divider - 1;

    gain &= 0xff;
    out = MAX_OUTPUT / 4.0; /* MAME: four channels, each gets 1/4 total range. */
    while (gain-- > 0)
        out *= 1.023292992; /* 0.2 dB */

    for (i = 0; i < 15; i++)
    {
        PSG.m_vol_table[i] = (out > (MAX_OUTPUT / 4.0)) ? (MAX_OUTPUT / 4) : (int32_t)out;
        out /= 1.258925412; /* 2 dB */
    }
    PSG.m_vol_table[15] = 0;

    for (i = 0; i < 4; i++)
        PSG.m_volume[i] = PSG.m_vol_table[PSG.m_register[i * 2 + 1] & 0x0f];

    PSG.m_ready_state = 1;
    PSG.m_cycles_to_ready = 0;

    /* MAME runs the PSG stream at clock/2, then applies m_clock_divider
     * inside sound_stream_update.  The C port keeps the same internal clock
     * phase and samples the resulting output at MultiRexZ80's mixer rate. */
    PSG.m_internal_samples_per_output = sample_rate ? ((double)clock / 2.0) / (double)sample_rate : 0.0;
    PSG.m_internal_sample_phase = 0.0;
}

void SN76489_GGStereoWrite(uint8_t st)
{
    if (PSG.m_stereo)
        PSG.m_stereo_mask = st;
}

void SN76489_Write(uint8_t data)
{
    int32_t n, r, c;

    if (data & 0x80)
    {
        r = (data & 0x70) >> 4;
        PSG.m_last_register = (uint8_t)r;
        if (PSG.m_ncr_style_psg && r == 6 && ((data & 0x04) != (PSG.m_register[6] & 0x04)))
            PSG.m_RNG = PSG.m_feedback_mask;
        PSG.m_register[r] = (PSG.m_register[r] & 0x3f0) | (data & 0x0f);
    }
    else
    {
        r = PSG.m_last_register;
        /* Current MAME leaves these NCR ignores disabled pending hardware
         * verification, so this C port does not early-return here either. */
    }

    c = r >> 1;
    switch (r)
    {
        case 0:
        case 2:
        case 4:
            if ((data & 0x80) == 0)
                PSG.m_register[r] = (PSG.m_register[r] & 0x0f) | ((data & 0x3f) << 4);

            if ((PSG.m_register[r] != 0) || PSG.m_sega_style_psg)
                PSG.m_period[c] = PSG.m_register[r];
            else
                PSG.m_period[c] = 0x400;

            if (r == 4 && ((PSG.m_register[6] & 0x03) == 0x03))
                PSG.m_period[3] = PSG.m_period[2] << 1;
            break;

        case 1:
        case 3:
        case 5:
        case 7:
            PSG.m_volume[c] = PSG.m_vol_table[data & 0x0f];
            if ((data & 0x80) == 0)
                PSG.m_register[r] = (PSG.m_register[r] & 0x3f0) | (data & 0x0f);
            break;

        case 6:
            if ((data & 0x80) == 0)
                PSG.m_register[r] = (PSG.m_register[r] & 0x3f0) | (data & 0x0f);
            n = PSG.m_register[6];
            PSG.m_period[3] = ((n & 3) == 3) ? (PSG.m_period[2] << 1) : (1 << (5 + (n & 3)));
            if (!PSG.m_ncr_style_psg)
                PSG.m_RNG = PSG.m_feedback_mask;
            PSG.m_output[3] = PSG.m_RNG & 1;
            break;
    }

    PSG.m_cycles_to_ready = 1;
    PSG.m_ready_state = 0;
}

static void mame_internal_tick(void)
{
    int i;

    update_ready_state();

    if (PSG.m_current_clock > 0)
    {
        PSG.m_current_clock--;
        return;
    }

    PSG.m_current_clock = PSG.m_clock_divider - 1;

    for (i = 0; i < 3; i++)
    {
        PSG.m_count[i]--;
        if (PSG.m_count[i] <= 0)
        {
            PSG.m_output[i] ^= 1;
            PSG.m_count[i] = PSG.m_period[i];
        }
    }

    PSG.m_count[3]--;
    if (PSG.m_count[3] <= 0)
    {
        int tap1 = (PSG.m_RNG & PSG.m_whitenoise_tap1) != 0;
        int tap2 = (PSG.m_RNG & PSG.m_whitenoise_tap2) != (PSG.m_ncr_style_psg ? PSG.m_whitenoise_tap2 : 0);

        if (tap1 != (tap2 && sega_noise_mode()))
        {
            PSG.m_RNG >>= 1;
            PSG.m_RNG |= PSG.m_feedback_mask;
        }
        else
        {
            PSG.m_RNG >>= 1;
        }

        PSG.m_output[3] = PSG.m_RNG & 1;
        PSG.m_count[3] = PSG.m_period[3];
    }
}

static void mame_current_output(int32_t *left, int32_t *right)
{
    int32_t out;
    int32_t out2;

    if (PSG.m_stereo)
    {
        out = ((((PSG.m_stereo_mask & 0x10) != 0) && PSG.m_output[0]) ? PSG.m_volume[0] : 0)
            + ((((PSG.m_stereo_mask & 0x20) != 0) && PSG.m_output[1]) ? PSG.m_volume[1] : 0)
            + ((((PSG.m_stereo_mask & 0x40) != 0) && PSG.m_output[2]) ? PSG.m_volume[2] : 0)
            + ((((PSG.m_stereo_mask & 0x80) != 0) && PSG.m_output[3]) ? PSG.m_volume[3] : 0);

        out2 = ((((PSG.m_stereo_mask & 0x01) != 0) && PSG.m_output[0]) ? PSG.m_volume[0] : 0)
             + ((((PSG.m_stereo_mask & 0x02) != 0) && PSG.m_output[1]) ? PSG.m_volume[1] : 0)
             + ((((PSG.m_stereo_mask & 0x04) != 0) && PSG.m_output[2]) ? PSG.m_volume[2] : 0)
             + ((((PSG.m_stereo_mask & 0x08) != 0) && PSG.m_output[3]) ? PSG.m_volume[3] : 0);
    }
    else
    {
        out = (PSG.m_output[0] ? PSG.m_volume[0] : 0)
            + (PSG.m_output[1] ? PSG.m_volume[1] : 0)
            + (PSG.m_output[2] ? PSG.m_volume[2] : 0)
            + (PSG.m_output[3] ? PSG.m_volume[3] : 0);
        out2 = out;
    }

    if (PSG.m_negate)
    {
        out = -out;
        out2 = -out2;
    }

    *left = out;
    *right = out2;
}

void SN76489_Update(int16_t **outputs, int samples)
{
    int16_t *lbuffer = outputs[0];
    int16_t *rbuffer = outputs[1] ? outputs[1] : outputs[0];

    while (samples-- > 0)
    {
        int32_t tick_count;
        int64_t acc_l = 0;
        int64_t acc_r = 0;
        int32_t i;

        if (PSG.m_internal_samples_per_output <= 0.0)
        {
            int32_t out, out2;
            mame_current_output(&out, &out2);
            *(lbuffer++) = (int16_t)out;
            *(rbuffer++) = (int16_t)out2;
            continue;
        }

        PSG.m_internal_sample_phase += PSG.m_internal_samples_per_output;
        tick_count = (int32_t)PSG.m_internal_sample_phase;
        PSG.m_internal_sample_phase -= (double)tick_count;
        if (tick_count < 1) tick_count = 1;

        for (i = 0; i < tick_count; i++)
        {
            int32_t out, out2;
            mame_internal_tick();
            mame_current_output(&out, &out2);
            acc_l += out;
            acc_r += out2;
        }

        *(lbuffer++) = (int16_t)(acc_l / tick_count);
        *(rbuffer++) = (int16_t)(acc_r / tick_count);
    }
}
