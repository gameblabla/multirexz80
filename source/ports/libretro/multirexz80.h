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

#ifndef MULTIREXZ80_LIBRETRO_MULTIREXZ80_H_
#define MULTIREXZ80_LIBRETRO_MULTIREXZ80_H_

#include <stdint.h>

#define HOST_WIDTH_RESOLUTION  320
#define HOST_HEIGHT_RESOLUTION 240

#define VIDEO_WIDTH_SMS  256
#define VIDEO_HEIGHT_SMS 192
#define VIDEO_WIDTH_GG   160
#define VIDEO_HEIGHT_GG  144

#define LOCK_VIDEO   do { } while (0);
#define UNLOCK_VIDEO do { } while (0);

#define SOUND_FREQUENCY 44100

void smsp_state(uint8_t slot_number, uint8_t mode);

#endif /* MULTIREXZ80_LIBRETRO_MULTIREXZ80_H_ */
