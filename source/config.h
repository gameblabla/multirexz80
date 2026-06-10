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

#ifndef CONFIG_H__
#define CONFIG_H__

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

typedef struct {
	int32_t fullscreen;
	int32_t fullspeed;
	int32_t nosound;
	int32_t joystick;
	/* Don't use below (sndrate), deprecated */
	int32_t sndrate;
	int32_t country;
	int32_t console;
	int32_t fm;
	int32_t ntsc;
	int32_t spritelimit;
	int32_t extra_gg;
	int32_t tms_pal;
	int32_t lcd_persistence; /* Game Gear LCD persistence filter */
	int32_t lightgun_cursor; /* Draw a software cursor for Light Phaser games */
	int32_t lightgun_dpad_speed; /* Pixels per frame for d-pad cursor fallback */
	int32_t audio_dc_blocker;
	int32_t audio_highpass_hz; /* 0 disables the post-mix high-pass filter */
	int32_t audio_lowpass_hz; /* 0 disables the post-mix low-pass filter */
	int32_t audio_limiter;
	int32_t audio_headroom_db;
	char game_name[0x100];
	uint8_t use_bios;
	uint8_t soundlevel;
	/* For input remapping */
	uint32_t config_buttons[32];
} t_config;
extern t_config option;

#endif
