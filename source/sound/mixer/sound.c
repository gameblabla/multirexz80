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

/******************************************************************************
 *  Sega Master System / GameGear Emulator
 *  Copyright (C) 1998-2007  Charles MacDonald
 *
 *  additional code by Eke-Eke (SMS Plus GX)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *   Sound emulation.
 *
 ******************************************************************************/
/*
 * See git commit history for more information.
 * - Gameblabla
 * July 16th 2019 : Remove extra if condition.
 * June 6th 2019 : Correctly set the PSG parameters for SG-1000 and Colecovision.
 * March 13th 2019 : Partial revert due to CrabZ80. The switching to C99 datatypes had been done again.
 * March 7th 2019 : Minor changes to sound.c. (Some trimming down, more switching to C99 datatypes and more.
 * February 2nd 2019 : Change the names of some sound functions.
 * October 12th 2019 : Switching some variables in functions to c99 datatypes.
*/
#include "shared.h"
#include "config.h"

snd_t snd;
static int16_t **fm_buffer;
static int16_t **psg_buffer;
static int32_t *smptab;
static int32_t smptab_len;

static uint8_t machine_psg = 0;
static uint8_t systeme_dual_psg = 0;
static uint8_t arcade_dual_psg = 0;
static int16_t *systeme_psg2_stream[2] = { NULL, NULL };
static sn76489_t systeme_psg2;

extern int32_t system_cycles_per_line(void);
extern int32_t z80_get_elapsed_cycles(void);

typedef struct
{
	int32_t dc_x1;
	int32_t dc_y1;
	int32_t hp_alpha_q15;
	int32_t hp_hz;
	int32_t lp_y[3];
	int32_t lp_alpha_q15;
	int32_t lp_hz;
	int32_t rate;
} snk_audio_filter_t;

static snk_audio_filter_t snk_audio_filter;

static void snk_audio_filter_reset(void)
{
	memset(&snk_audio_filter, 0, sizeof(snk_audio_filter));
}

static void snk_audio_filter_update(void)
{
	int32_t hz = option.audio_lowpass_hz;
	int32_t hp_hz = option.audio_highpass_hz;
	int32_t rate = snd.sample_rate ? snd.sample_rate : SOUND_FREQUENCY;

	if (hz < 0) hz = 0;
	if (hz > rate / 2) hz = rate / 2;
	if (hp_hz < 0) hp_hz = 0;
	if (hp_hz > rate / 2) hp_hz = rate / 2;
	if (snk_audio_filter.lp_hz == hz && snk_audio_filter.hp_hz == hp_hz && snk_audio_filter.rate == rate)
		return;

	snk_audio_filter.lp_hz = hz;
	snk_audio_filter.hp_hz = hp_hz;
	snk_audio_filter.rate = rate;
	if (hp_hz > 0)
	{
		const double rc = (double)rate;
		const double omega = 6.28318530717958647692 * (double)hp_hz;
		int32_t alpha = (int32_t)((rc / (omega + rc)) * 32768.0 + 0.5);
		if (alpha < 1) alpha = 1;
		if (alpha > 32767) alpha = 32767;
		snk_audio_filter.hp_alpha_q15 = alpha;
	}
	else
	{
		snk_audio_filter.hp_alpha_q15 = 0;
	}

	if (hz > 0)
	{
		const double rc = (double)rate;
		const double omega = 6.28318530717958647692 * (double)hz;
		int32_t alpha = (int32_t)((omega / (omega + rc)) * 32768.0 + 0.5);
		if (alpha < 1) alpha = 1;
		if (alpha > 32768) alpha = 32768;
		snk_audio_filter.lp_alpha_q15 = alpha;
	}
	else
	{
		snk_audio_filter.lp_alpha_q15 = 0;
		snk_audio_filter.lp_y[0] = 0;
		snk_audio_filter.lp_y[1] = 0;
		snk_audio_filter.lp_y[2] = 0;
	}
}

static int16_t mix_saturate_i16(int32_t v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return (int16_t)v;
}

static int32_t mix_soft_limit_i16(int32_t v)
{
	const int32_t knee = 28672;
	const int32_t room = 32767 - knee;
	int32_t sign = 1;
	int64_t magnitude = v;

	if (magnitude < 0)
	{
		sign = -1;
		magnitude = -magnitude;
	}
	if (magnitude <= knee)
		return v;

	/* Unity slope at the knee and an asymptotic 16-bit ceiling.  The former
	 * quarter-slope limiter could still exceed 32767 and fall through to hard
	 * clipping, which is especially audible after Psycho Soldier's quiet OPL
	 * output is normalized. */
	magnitude = knee + ((magnitude - knee) * room) /
	                       ((magnitude - knee) + room);
	return sign * (int32_t)magnitude;
}

static void psg_write_backend(uint8_t chip, uint8_t data)
{
	if (chip == 0)
	{
		SN76489_Write(data);
		return;
	}
	{
		sn76489_t saved = PSG;
		PSG = systeme_psg2;
		SN76489_Write(data);
		systeme_psg2 = PSG;
		PSG = saved;
	}
}

static void psg_update_backend(uint8_t chip, int16_t **buffer, int32_t length)
{
	if (length <= 0)
		return;
	if (chip == 0)
	{
		SN76489_Update(buffer, length);
		return;
	}
	{
		sn76489_t saved = PSG;
		PSG = systeme_psg2;
		SN76489_Update(buffer, length);
		systeme_psg2 = PSG;
		PSG = saved;
	}
}

static void systeme_psg2_init(void)
{
	if (arcade_dual_psg)
	{
		sn76489_t saved = PSG;
		SN76489_Init(0, 4000000, snd.sample_rate);
		systeme_psg2 = PSG;
		PSG = saved;
	}
	else
	{
		systeme_psg2 = PSG;
	}
}

static void systeme_psg2_reset(void)
{
	if (systeme_dual_psg)
		systeme_psg2_init();
}

static void systeme_mix_second_psg(int16_t **dst, int32_t start, int32_t length)
{
	int32_t i;
	int16_t *psg2[2];

	if (!systeme_dual_psg || !systeme_psg2_stream[0] || !systeme_psg2_stream[1] || length <= 0)
		return;

	psg2[0] = systeme_psg2_stream[0] + start;
	psg2[1] = systeme_psg2_stream[1] + start;
	psg_update_backend(1, psg2, length);

	for (i = 0; i < length; i++)
	{
		dst[0][i] = mix_saturate_i16((int32_t)dst[0][i] + (int32_t)psg2[0][i]);
		dst[1][i] = mix_saturate_i16((int32_t)dst[1][i] + (int32_t)psg2[1][i]);
	}
}

static void sound_generate_range(int32_t start, int32_t length)
{
	int16_t *fm[2], *psg[2];

	if (length <= 0 || !snd.enabled || !psg_buffer || !fm_buffer)
		return;

	if (start < 0) start = 0;
	if (start + length > snd.sample_count)
		length = snd.sample_count - start;
	if (length <= 0)
		return;

	psg[0] = psg_buffer[0] + start;
	psg[1] = psg_buffer[1] + start;
	fm[0]  = fm_buffer[0] + start;
	fm[1]  = fm_buffer[1] + start;

	if (sms.console != CONSOLE_SNKPSYCHOS && sms.console != CONSOLE_TAITOL)
	{
		psg_update_backend(0, psg, length);
		if (systeme_dual_psg)
			systeme_mix_second_psg(psg, start, length);
	}

	if (sms.console == CONSOLE_SNKPSYCHOS)
		snk_psychos_sound_update(fm, length);
	else if (sms.console == CONSOLE_TAITOL)
		taitol_sound_update(fm, length);
	else if (sms.use_fm)
		FM_Update(fm, length);
}

static void sound_update_to_sample(int32_t target_sample)
{
	if (!snd.enabled)
		return;

	if (target_sample < 0) target_sample = 0;
	if (target_sample > snd.sample_count) target_sample = snd.sample_count;
	if (target_sample <= snd.done_so_far)
		return;

	sound_generate_range(snd.done_so_far, target_sample - snd.done_so_far);
	snd.done_so_far = target_sample;
}

void MULTIREXZ80_sound_sync_to_cpu(void)
{
	int32_t cycles_per_line, total_cycles, elapsed;
	int64_t sample;

	if (!snd.enabled || snd.sample_count <= 0)
		return;

	cycles_per_line = system_cycles_per_line();
	total_cycles = cycles_per_line * smptab_len;
	if (total_cycles <= 0)
		return;

	elapsed = z80_get_elapsed_cycles();
	if (elapsed < 0) elapsed = 0;
	if (elapsed > total_cycles) elapsed = total_cycles;

	sample = ((int64_t)elapsed * (int64_t)snd.sample_count) / (int64_t)total_cycles;
	sound_update_to_sample((int32_t)sample);
}

uint32_t MULTIREXZ80_sound_init(void)
{
	static uint8_t *fmbuf = NULL;
	static uint8_t *psgbuf = NULL;
	int32_t restore_sound = 0;
	int32_t i;

	snd.fm_which = option.fm;
	snd.fps = (sms.display == DISPLAY_NTSC) ? FPS_NTSC : FPS_PAL;
	snd.fm_clock = (sms.display == DISPLAY_NTSC) ? CLOCK_NTSC : CLOCK_PAL;
	snd.psg_clock = (sms.display == DISPLAY_NTSC) ? CLOCK_NTSC : CLOCK_PAL;
	if (sms.console == CONSOLE_SYSTEM1)
		snd.psg_clock = 2000000;
	snd.sample_rate = SOUND_FREQUENCY;
	snd.mixer_callback = NULL;
	arcade_dual_psg = (sms.console == CONSOLE_SYSTEM1);
	systeme_dual_psg = (sms.console == CONSOLE_SYSTEME) || arcade_dual_psg;
	
	/* Save register settings */
	if(snd.enabled)
	{
		restore_sound = 1;
		psgbuf = malloc(sizeof(PSG));
		if (!psgbuf) return 0;
		memcpy(psgbuf, &PSG, sizeof(PSG));
		fmbuf = malloc(FM_GetContextSize());
		if (!fmbuf) return 0;
		FM_GetContext(fmbuf);
		
		/* If we are reinitializing, shut down sound emulation */
		MULTIREXZ80_sound_shutdown();
	}
	
	/* Disable sound until initialization is complete */
	snd.enabled = 0;

	/* Check if sample rate is invalid */
	if(snd.sample_rate < 8000 || snd.sample_rate > 48000)
		return 0;

	/* Assign stream mixing callback if none provided */
	if(!snd.mixer_callback)
		snd.mixer_callback = MULTIREXZ80_sound_mixer_callback;
	if(sms.console == CONSOLE_SNKPSYCHOS)
		snd.mixer_callback = MULTIREXZ80_snk_psychos_mixer_callback;
	if (sms.console == CONSOLE_TAITOL)
		snd.mixer_callback = MULTIREXZ80_taitol_mixer_callback;
	snk_audio_filter_reset();

	/* Calculate number of samples generated per frame */
	snd.sample_count = (snd.sample_rate / snd.fps);

	/* Calculate size of sample buffer */
	snd.buffer_size = snd.sample_count * 2;
	
	/* Free sample buffer position table if previously allocated */
	if(smptab)
	{
		free(smptab);
		smptab = NULL;
	}

	/* Prepare incremental info */
	snd.done_so_far = 0;
	smptab_len = (sms.display == DISPLAY_NTSC) ? 262 : 313;
	smptab = malloc(smptab_len * sizeof(int32_t));
	
	if(!smptab)
	{
		printf("Failed to malloc smptab\n");
		return 0;
	}
	
	for (i = 0; i < smptab_len; i++)
	{
		float calc = (snd.sample_count * i);
		calc = calc / (float)smptab_len;
		smptab[i] = (int32_t)calc;
	}

	/* Allocate emulated sound streams */
	for(i = 0; i < STREAM_MAX; i++)
	{
		snd.stream[i] = malloc(snd.buffer_size);
		if(!snd.stream[i]) return 0;
		memset(snd.stream[i], 0, snd.buffer_size);
	}

	/* Allocate sound output streams */
	snd.output = malloc(snd.buffer_size*2);
	
	if(!snd.output)
		return 0;

	/* Set up buffer pointers */
	fm_buffer = (int16_t **)&snd.stream[STREAM_FM_MO];
	psg_buffer = (int16_t **)&snd.stream[STREAM_PSG_L];

	if (systeme_dual_psg)
	{
		systeme_psg2_stream[0] = malloc(snd.buffer_size);
		systeme_psg2_stream[1] = malloc(snd.buffer_size);
		if (!systeme_psg2_stream[0] || !systeme_psg2_stream[1]) return 0;
		memset(systeme_psg2_stream[0], 0, snd.buffer_size);
		memset(systeme_psg2_stream[1], 0, snd.buffer_size);
	}

	/* Set up SN76489 emulation */
	switch(sms.console)
	{
		default: machine_psg = 0; break;
		case CONSOLE_GG:
		case CONSOLE_GGMS: machine_psg = 1; break;
		case CONSOLE_COLECO: machine_psg = 2; break;
		case CONSOLE_SG1000:
		case CONSOLE_SC3000:
		case CONSOLE_SF7000: machine_psg = 3; break;
	}
	SN76489_Init(machine_psg, snd.psg_clock, snd.sample_rate);
	if (systeme_dual_psg)
		systeme_psg2_init();

	/* Set up YM2413 emulation */
	FM_Init();
	if (sms.console == CONSOLE_SNKPSYCHOS)
		snk_psychos_sound_reset();
	if (sms.console == CONSOLE_TAITOL)
		taitol_sound_reset();

	/* Restore YM2413 register settings */
	if(restore_sound)
	{
		memcpy(&PSG, psgbuf, sizeof(PSG));
		FM_SetContext(fmbuf);
		free(fmbuf);
		free(psgbuf);
	}

	/* Inform other functions that we can use sound */
	snd.enabled = 1;
	
	return 1;
}


void MULTIREXZ80_sound_shutdown(void)
{
	uint32_t i;
	
	if(!snd.enabled)
		return;

	/* Free emulated sound streams */
	for(i = 0; i < STREAM_MAX; i++)
	{
		if(snd.stream[i])
		{
			free(snd.stream[i]);
			snd.stream[i] = NULL;
		}
	}

	if(systeme_psg2_stream[0])
	{
		free(systeme_psg2_stream[0]);
		systeme_psg2_stream[0] = NULL;
	}
	if(systeme_psg2_stream[1])
	{
		free(systeme_psg2_stream[1]);
		systeme_psg2_stream[1] = NULL;
	}

	/* Free sound output buffers */
	if(snd.output)
	{
		free(snd.output);
		snd.output = NULL;
	}
	
	/* Free sample buffer position table if previously allocated */
	if (smptab)
	{
		free(smptab);
		smptab = NULL;
	}
	
	/* Shut down SN76489 emulation: no heap-backed PSG resources in the MAME backend. */
	/* Shut down YM2413 emulation */
	FM_Shutdown();
}


void MULTIREXZ80_sound_reset(void)
{
	if(!snd.enabled)
		return;

	/* Reset SN76489 emulator */
	SN76489_Init(machine_psg, snd.psg_clock, snd.sample_rate);
	systeme_psg2_reset();
	
	/* Reset YM2413 emulator */
	FM_Reset();
	if (sms.console == CONSOLE_SNKPSYCHOS)
		snk_psychos_sound_reset();
	if (sms.console == CONSOLE_TAITOL)
		taitol_sound_reset();
	snk_audio_filter_reset();
}


void MULTIREXZ80_sound_update(int32_t line)
{
	int32_t target;

	if (!snd.enabled)
		return;

	if (line == smptab_len - 1)
	{
		target = snd.sample_count;
		sound_update_to_sample(target);

		/* Mix streams into output buffer */
		snd.mixer_callback(snd.output, snd.sample_count);

		/* Reset for next frame */
		snd.done_so_far = 0;
	}
	else
	{
		if (!smptab)
			return;
		target = smptab[line];
		sound_update_to_sample(target);
	}
}

/* Generic FM+PSG stereo mixer callback */
void MULTIREXZ80_sound_mixer_callback(int16_t *output, int32_t length)
{
	int32_t i;
	int32_t level = option.soundlevel ? option.soundlevel : 1;
	for(i = 0; i < length; i++)
	{
		/* FM buffers are YM2413 melody/rhythm buses, not stereo channels. */
		int32_t temp = (int32_t)fm_buffer[0][i] + (int32_t)fm_buffer[1][i];
		output[i * 2] = mix_saturate_i16((temp + (int32_t)psg_buffer[0][i]) * level);
		output[i * 2 + 1] = mix_saturate_i16((temp + (int32_t)psg_buffer[1][i]) * level);
	}
}

void MULTIREXZ80_snk_psychos_mixer_callback(int16_t *output, int32_t length)
{
	int32_t i;
	int32_t level = option.soundlevel ? option.soundlevel : 1;
	int32_t gain_num = snk_psychos_audio_mixer_gain_num(option.audio_headroom_db);

	snk_audio_filter_update();

	for(i = 0; i < length; i++)
	{
		int32_t v = ((int32_t)fm_buffer[0][i] + (int32_t)fm_buffer[1][i]) * gain_num / 512;

		if (option.audio_dc_blocker)
		{
			int32_t alpha = snk_audio_filter.hp_alpha_q15 ? snk_audio_filter.hp_alpha_q15 : 32604;
			int64_t y = ((int64_t)alpha * ((int64_t)snk_audio_filter.dc_y1 + v - snk_audio_filter.dc_x1)) >> 15;
			if (y > 131071) y = 131071;
			if (y < -131072) y = -131072;
			snk_audio_filter.dc_x1 = v;
			snk_audio_filter.dc_y1 = (int32_t)y;
			v = (int32_t)y;
		}

		if (snk_audio_filter.lp_alpha_q15)
		{
			snk_audio_filter.lp_y[0] += (int32_t)(((int64_t)snk_audio_filter.lp_alpha_q15 * (v - snk_audio_filter.lp_y[0])) >> 15);
			snk_audio_filter.lp_y[1] += (int32_t)(((int64_t)snk_audio_filter.lp_alpha_q15 * (snk_audio_filter.lp_y[0] - snk_audio_filter.lp_y[1])) >> 15);
			snk_audio_filter.lp_y[2] += (int32_t)(((int64_t)snk_audio_filter.lp_alpha_q15 * (snk_audio_filter.lp_y[1] - snk_audio_filter.lp_y[2])) >> 15);
			v = snk_audio_filter.lp_y[2];
		}

		v *= level;

		if (option.audio_limiter)
			v = mix_soft_limit_i16(v);

		output[i * 2] = output[i * 2 + 1] = mix_saturate_i16(v);
	}
}

void MULTIREXZ80_taitol_mixer_callback(int16_t *output, int32_t length)
{
	int32_t i;
	int32_t level = option.soundlevel ? option.soundlevel : 1;
	/* Taito L YM2203 produces left/right directly in fm_buffer[0]/[1].
	 * No PSG bus is used. */
	for(i = 0; i < length; i++)
	{
		int32_t l = (int32_t)fm_buffer[0][i];
		int32_t r = (int32_t)fm_buffer[1][i];
		output[i * 2]     = mix_saturate_i16(l * level);
		output[i * 2 + 1] = mix_saturate_i16(r * level);
	}
}

/*--------------------------------------------------------------------------*/
/* Sound chip access handlers                                               */
/*--------------------------------------------------------------------------*/

void psg_stereo_w(int32_t data)
{
	MULTIREXZ80_sound_sync_to_cpu();
	MULTIREXZ80_TRACE_PSG_WRITE(0x0001, (uint8_t)data);
	/*if(!snd.enabled) return;*/
	SN76489_GGStereoWrite(data);
}


void psg_write_chip(int32_t chip, int32_t data)
{
	uint8_t target = (systeme_dual_psg && chip) ? 1 : 0;
	MULTIREXZ80_sound_sync_to_cpu();
	MULTIREXZ80_TRACE_PSG_WRITE(target ? 0x0002 : 0x0000, (uint8_t)data);
	/*if(!snd.enabled) return;*/
	psg_write_backend(target, (uint8_t)data);
}

void psg_write(int32_t data)
{
	psg_write_chip(0, data);
}

/*--------------------------------------------------------------------------*/
/* Mark III FM Unit / Master System (J) built-in FM handlers                */
/*--------------------------------------------------------------------------*/

uint32_t fmunit_detect_r(void)
{
	return sms.fm_detect;
}

void fmunit_detect_w(uint32_t data)
{
	MULTIREXZ80_TRACE_YM_WRITE(0x00f2, (uint8_t)data);
	if(/* !snd.enabled || */ !sms.use_fm) return;
	sms.fm_detect = data;
}

void fmunit_write(uint32_t offset, uint8_t data)
{
	MULTIREXZ80_sound_sync_to_cpu();
	MULTIREXZ80_TRACE_YM_WRITE((uint16_t)(0x00f0 | (offset & 1)), data);
	if(/* !snd.enabled || */ !sms.use_fm) return;
	FM_Write(offset, data);
}
