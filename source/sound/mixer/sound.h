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

#ifndef SOUND_H_
#define SOUND_H_

enum {
  STREAM_PSG_L, /* PSG left channel */
  STREAM_PSG_R, /* PSG right channel */
  STREAM_FM_MO, /* YM2413 melody channel */
  STREAM_FM_RO, /* YM2413 rhythm channel */
  STREAM_MAX    /* Total # of sound streams */
};  

/* Sound emulation structure */
typedef struct
{
	void (*mixer_callback)(int16_t *output, int32_t length);
	int16_t *output;
	int16_t *stream[STREAM_MAX];
	int32_t fm_which;
	int32_t enabled;
	int32_t fps;
	uint32_t buffer_size;
	int32_t sample_count;
	int32_t sample_rate;
	int32_t done_so_far;
	int32_t fm_clock;
	int32_t psg_clock;
} snd_t;


/* Global data */
extern snd_t snd;

/* Function prototypes */
void psg_write(int32_t data);
void psg_write_chip(int32_t chip, int32_t data);
void psg_stereo_w(int32_t data);
uint32_t fmunit_detect_r(void);
void fmunit_detect_w(uint32_t data);
void fmunit_write(uint32_t offset, uint8_t data);
uint32_t MULTIREXZ80_sound_init(void);
void MULTIREXZ80_sound_shutdown(void);
void MULTIREXZ80_sound_reset(void);
void MULTIREXZ80_sound_update(int32_t line);
void MULTIREXZ80_sound_sync_to_cpu(void);
void MULTIREXZ80_sound_mixer_callback(int16_t *output, int32_t length);
void MULTIREXZ80_snk_psychos_mixer_callback(int16_t *output, int32_t length);

#endif /* SOUND_H_ */
