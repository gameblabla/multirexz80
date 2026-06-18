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
 *   Sega Master System manager
 *
 ******************************************************************************/
#ifndef SYSTEM_H_
#define SYSTEM_H_

#define APP_NAME            "MultiRexZ80"
#define APP_VERSION         "1.8"

#define PALETTE_SIZE        0x20

/* Mask for removing unused pixel data */
#define PIXEL_MASK          0x1F

/* These can be used for 'input.pad[]' */
#define INPUT_UP            0x00000001
#define INPUT_DOWN          0x00000002
#define INPUT_LEFT          0x00000004
#define INPUT_RIGHT         0x00000008
#define INPUT_BUTTON1       0x00000010
#define INPUT_BUTTON2       0x00000020
#define INPUT_ROTATE_LEFT    0x00000040  /* SNK LS-30 rotary: rotate aim/cursor counter-clockwise */
#define INPUT_ROTATE_RIGHT   0x00000080  /* SNK LS-30 rotary: rotate aim/cursor clockwise */

/* These are held in input.rotary_aim[] rather than input.pad[] because the
 * LS-30 rotary joystick is independent from the 8-way movement stick. */
#define INPUT_AIM_UP          0x01
#define INPUT_AIM_DOWN        0x02
#define INPUT_AIM_LEFT        0x04
#define INPUT_AIM_RIGHT       0x08

/* These can be used for 'input.system' */
#define INPUT_START         0x00000001  /* Game Gear only */
#define INPUT_PAUSE         0x00000002  /* Master System only */
#define INPUT_RESET         0x00000004  /* Master System only */

/* These can be used for 'input.arcade' (active-high host state, active-low PCB inputs). */
#define INPUT_ARCADE_COIN1   0x01
#define INPUT_ARCADE_COIN2   0x02
#define INPUT_ARCADE_TEST    0x04
#define INPUT_ARCADE_SERVICE 0x08
#define INPUT_ARCADE_START1  0x10
#define INPUT_ARCADE_START2  0x20

#define SORDM5_KEY_ROWS     7
#define SORDM5_KEY_RESET    0x80

enum 
{
	SRAM_SAVE   = 0,
	SRAM_LOAD   = 1
};

/* User input structure */
typedef struct
{
	int32_t analog[2][2];
	uint8_t pad[2];
	/* SNK LS-30 rotary input. rotary_pos is a 12-position absolute dial
	 * (0 = up, then clockwise), and rotary_aim is a transient 4-way/8-way
	 * absolute aim bitfield used by keyboard/gamepad frontends. */
	uint8_t rotary_pos[2];
	uint8_t rotary_aim[2];
	uint8_t system;
	uint8_t arcade;
	/* Sord M5 keyboard matrix, active-high rows Y0-Y6, matching MAME's m5.cpp input ports. */
	uint8_t m5_key[SORDM5_KEY_ROWS];
	uint8_t m5_reset;
	/* Sega Graphic Board v2 state.  x/y are 0-255 pen coordinates,
	 * buttons is a 3-bit field (bit0=start, bit1=button1, bit2=button2),
	 * read_index tracks the 0x3F-controlled nibble read sequence,
	 * and port3f_old stores the previous port 0x3F value for edge
	 * detection. */
	struct {
		uint8_t x, y;
		uint8_t buttons;
		uint8_t read_index;
		uint8_t port3f_old;
		uint8_t unknown;
	} graphic_board[2];
} input_t;

/* Game image structure */
typedef struct
{
	uint8_t *rom;
	uint8_t loaded;
	uint32_t size;
	/* We need to use an unsigned short for pages, as Bad Apple SMS requires it !*/
	uint16_t pages;
	uint32_t crc;
	uint32_t sram_crc;
	uint8_t mapper;
	uint8_t sram[0x8000];
	uint8_t fcr[4];
} cart_t;

/* Bitmap structure */
typedef struct
{
	uint8_t *data;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t depth;
	struct 
	{
		int32_t x, y, w, h;
		int32_t ox, oy, ow, oh;
		int32_t changed;
	} viewport;
    struct
    {
        uint8_t color[PALETTE_SIZE][3];
        uint8_t dirty[PALETTE_SIZE];
        uint8_t update;
    } pal;
} bitmap_t;

/* Global variables */
extern bitmap_t bitmap;   /* Display bitmap */
extern cart_t cart;       /* Game cartridge data */
extern input_t input;     /* Controller input */

/* Function prototypes */
extern int32_t system_cycles_per_line(void);
extern int32_t system_hcounter_index(void);
extern void system_frame(uint32_t skip_render);
extern void system_init(void);
extern void system_shutdown(void);
extern void system_reset(void);
extern void system_manage_sram(uint8_t *sram, uint8_t slot_number, uint8_t mode);
extern void system_poweron(void);
extern void system_poweroff(void);

#endif /* _SYSTEM_H_ */


