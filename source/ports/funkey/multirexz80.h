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

#ifndef MULTIREXZ80_H
#define MULTIREXZ80_H

#include <SDL/SDL.h>

#define HOST_WIDTH_RESOLUTION 240
#define HOST_HEIGHT_RESOLUTION 240

#define VIDEO_WIDTH_SMS 256
#define VIDEO_HEIGHT_SMS 192
#define VIDEO_WIDTH_GG 160
#define VIDEO_HEIGHT_GG 144

/* Input defines for custom remapping */
#define CONFIG_BUTTON_UP 0
#define CONFIG_BUTTON_DOWN 1
#define CONFIG_BUTTON_LEFT 2
#define CONFIG_BUTTON_RIGHT 3
#define CONFIG_BUTTON_BUTTON1 4
#define CONFIG_BUTTON_BUTTON2 5
#define CONFIG_BUTTON_START 6

/* Colecovision specific */
#define CONFIG_BUTTON_DOLLARS 7
#define CONFIG_BUTTON_ASTERISK 8
#define CONFIG_BUTTON_ONE 9
#define CONFIG_BUTTON_TWO 10
#define CONFIG_BUTTON_THREE 11
#define CONFIG_BUTTON_FOUR 12
#define CONFIG_BUTTON_FIVE 13
#define CONFIG_BUTTON_SIX 14
#define CONFIG_BUTTON_SEVEN 15
#define CONFIG_BUTTON_EIGHT 16
#define CONFIG_BUTTON_NINE 17
/* Arcade and keyboard overlay mappings.  These live above the historical
 * Coleco keypad range so old 19-entry config files remain readable; missing
 * values simply keep the defaults from sdl12_common. */
#define CONFIG_BUTTON_ARCADE_COIN1 19
#define CONFIG_BUTTON_ARCADE_COIN2 20
#define CONFIG_BUTTON_ARCADE_START1 21
#define CONFIG_BUTTON_ARCADE_START2 22
#define CONFIG_BUTTON_ARCADE_SERVICE 23
#define CONFIG_BUTTON_ARCADE_TEST 24
#define CONFIG_BUTTON_VKBD 25

/* End of Defines for input remapping */

extern SDL_Surface* sms_bitmap;

#define LOCK_VIDEO SDL_LockSurface(sms_bitmap);
#define UNLOCK_VIDEO SDL_UnlockSurface(sms_bitmap);


typedef struct {
	char gamename[256];
	char sramdir[256];
	char sramfile[256];
	char stdir[256];
	char scrdir[256];
	char biosdir[256];
} gamedata_t;

void smsp_state(uint8_t slot_number, uint8_t mode);

#define SOUND_FREQUENCY 44100

#endif
