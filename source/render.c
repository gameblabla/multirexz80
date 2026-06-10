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
 *   VDP rendering core
 *
 ******************************************************************************/
/*
 * See git commit history for more information.
 * - Gameblabla
 * June 3rd 2019 : Added NOBLANKING_LEFTCOLUM for platforms like the RS-90.
 * March 15th 2019 : Correct the brightness level in the Master system palette. It doesn't look dark anymore.
 * March 14th 2019 : Fix PAL code as it would cause issues with Fantastic Dizzy.
 * March 13th 2019 : Minor fixes as part of the CrabZ80's revert. (mostly whitepacing)
 * March 11th 2019 : Fixed scrolling issues with Gauntlet. Fixed PAL issues too.
 * December 7th 2018 : LIGHTGUN define and some whitepacing.
 * October 12th 2018 : Whitepacing and minor fixes.
*/

#include "shared.h"
/*** Vertical Counter Tables ***/
extern uint8_t *vc_table[2][3];

typedef struct
{
  uint16_t yrange;
  uint16_t xpos;
  uint16_t attr;
} object_info_t;

static object_info_t object_info[64];
static object_info_t *systeme_object_info;
static object_info_t *object_info_main_ptr = object_info;
static object_info_t *active_object_info = object_info;
#define object_info active_object_info

static vdp_t *vdp_main_ptr = &vdp;
static vdp_t *render_vdp = &vdp;
#define vdp (*render_vdp)

/* Background drawing function */
void (*render_bg)(int32_t line) = NULL;
void (*render_obj)(int32_t line) = NULL;

/* Pointer to output buffer */
uint8_t *linebuf;

/* Pixel 8-bit color tables */
uint8_t sms_cram_expand_table[4];
static uint8_t sms_cram_expand_r[4];
static uint8_t sms_cram_expand_g[4];
static uint8_t sms_cram_expand_b[4];
uint8_t gg_cram_expand_table[16];

/* Dirty pattern info */
uint8_t bg_name_dirty[0x200];     /* 1= This pattern is dirty */
uint16_t bg_name_list[0x200];     /* List of modified pattern indices */
uint16_t bg_list_index;           /* # of modified patterns in list */
static uint8_t *systeme_bg_name_dirty;
static uint16_t *systeme_bg_name_list;
static uint16_t systeme_bg_list_index;
static uint8_t *bg_name_dirty_main_ptr = bg_name_dirty;
static uint16_t *bg_name_list_main_ptr = bg_name_list;
static uint16_t *bg_list_index_main_ptr = &bg_list_index;
static uint8_t *active_bg_name_dirty = bg_name_dirty;
static uint16_t *active_bg_name_list = bg_name_list;
static uint16_t *active_bg_list_index = &bg_list_index;
#define bg_name_dirty active_bg_name_dirty
#define bg_name_list active_bg_name_list
#define bg_list_index (*active_bg_list_index)

/* Internal buffer for drawing non 8-bit displays */
#ifndef _8BPP_COLOR
static uint8_t internal_buffer[0x200];
/* Precalculated pixel table */
#ifdef MULTIREXZ80_RENDER_32BPP
static uint32_t pixel[PALETTE_SIZE];
static uint32_t *systeme_pixel;
#else
static uint16_t pixel[PALETTE_SIZE];
static uint16_t *systeme_pixel;
#ifndef MULTIREXZ80_DISABLE_FAST_REMAP
/*
 * Fast RGB565 remap table.  The renderer internally draws palette indices
 * into internal_buffer; this table converts two adjacent 8-bit indices to two
 * packed RGB565 pixels with a single 32-bit write.  It costs 256 KiB and is
 * only rebuilt after palette changes.
 */
static uint32_t remap16_pair_table[0x10000];
static uint8_t remap16_pair_dirty = 1;
#endif
#endif
#endif

static uint8_t bg_pattern_cache[0x20000];/* Cached and flipped patterns */
static uint8_t *systeme_bg_pattern_cache;
static uint8_t *systeme_back_line;
static uint8_t *systeme_front_line;
static uint8_t *bg_pattern_cache_main_ptr = bg_pattern_cache;
static uint8_t *active_bg_pattern_cache = bg_pattern_cache;
#define bg_pattern_cache active_bg_pattern_cache

/* Pixel look-up table */
static uint8_t lut[0x10000];

static uint8_t object_index_count;
static uint8_t systeme_object_index_count;
static uint8_t *object_index_count_main_ptr = &object_index_count;
static uint8_t *active_object_index_count = &object_index_count;
#define object_index_count (*active_object_index_count)

/* SMS/GG timed renderer state.  Gearsystem's corrected model parses and
 * renders sprites for line+1 at the late render point of the current line.
 * This renderer is line-buffered, so we capture the sprite list/mode for the
 * next display line at that point, then consume it when that line's background
 * is later rendered. */
static object_info_t sms_timed_object_info[64];
static uint8_t sms_timed_object_count;
static uint8_t sms_timed_sprite_mode;
static int32_t sms_timed_sprite_line = -1;

/* Top Border area height */
static uint8_t active_border[2][3] =
{
	{24, 8,  0},  /* NTSC VDP */
	{24, 8,  0}   /* PAL  VDP */
};

/* Active Scan Area height */
static uint16_t active_range[2] =
{
	243, /* NTSC VDP */
	294  /* PAL  VDP */
};

#define LCD_PERSISTENCE_MAX_PIXELS (256 * 313)
static uint32_t *lcd_persistence_buffer;
static uint8_t *lcd_persistence_valid;

#ifndef MULTIREXZ80_RENDER_32BPP
static uint16_t lcd_blend_rgb565(uint16_t cur, uint16_t prev)
{
	/* Game Gear LCD persistence approximation: 75% current frame, 25% previous
	 * displayed frame.  It preserves RGB565 channel widths and creates the
	 * intended flicker integration for titles that rely on the handheld panel. */
	uint32_t cr = (cur >> 11) & 0x1f, cg = (cur >> 5) & 0x3f, cb = cur & 0x1f;
	uint32_t pr = (prev >> 11) & 0x1f, pg = (prev >> 5) & 0x3f, pb = prev & 0x1f;
	uint32_t r = (cr * 3 + pr + 2) >> 2;
	uint32_t g = (cg * 3 + pg + 2) >> 2;
	uint32_t b = (cb * 3 + pb + 2) >> 2;
	return (uint16_t)((r << 11) | (g << 5) | b);
}
#else
static uint32_t lcd_blend_xrgb8888(uint32_t cur, uint32_t prev)
{
	uint32_t cr = (cur >> 16) & 0xff, cg = (cur >> 8) & 0xff, cb = cur & 0xff;
	uint32_t pr = (prev >> 16) & 0xff, pg = (prev >> 8) & 0xff, pb = prev & 0xff;
	uint32_t r = (cr * 3 + pr + 2) >> 2;
	uint32_t g = (cg * 3 + pg + 2) >> 2;
	uint32_t b = (cb * 3 + pb + 2) >> 2;
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}
#endif

static int lcd_persistence_alloc(void)
{
	if (!lcd_persistence_buffer)
		lcd_persistence_buffer = (uint32_t *)calloc(LCD_PERSISTENCE_MAX_PIXELS, sizeof(uint32_t));
	if (!lcd_persistence_valid)
		lcd_persistence_valid = (uint8_t *)calloc(LCD_PERSISTENCE_MAX_PIXELS, sizeof(uint8_t));
	return lcd_persistence_buffer && lcd_persistence_valid;
}

static void lcd_persistence_free(void)
{
	free(lcd_persistence_buffer);
	lcd_persistence_buffer = NULL;
	free(lcd_persistence_valid);
	lcd_persistence_valid = NULL;
}

static void lcd_persistence_clear(void)
{
	if (lcd_persistence_buffer)
		memset(lcd_persistence_buffer, 0, LCD_PERSISTENCE_MAX_PIXELS * sizeof(uint32_t));
	if (lcd_persistence_valid)
		memset(lcd_persistence_valid, 0, LCD_PERSISTENCE_MAX_PIXELS * sizeof(uint8_t));
}

/* CRAM palette in TMS compatibility mode */
static const uint8_t tms_crom[] =
{
	0x00, 0x00, 0x08, 0x0C,
	0x10, 0x30, 0x01, 0x3C,
	0x02, 0x03, 0x05, 0x0F,
	0x04, 0x33, 0x15, 0x3F
};

/* original TMS palette for SG-1000 & Colecovision */
static uint8_t tms_palette[16*3][3] =
{
	/* from Sean Young (http://www.smspower.org/dev/docs/tms9918a.txt) */
	{  0,  0,  0},
	{  0,  0,  0},
	{ 33,200, 66},
	{ 94,220,120},
	{ 84, 85,237},
	{125,118,252},
	{212, 82, 77},
	{ 66,235,245},
	{252, 85, 84},
	{255,121,120},
	{212,193, 84},
	{230,206,128},
	{ 33,176, 59},
	{201, 91,186},
	{204,204,204},
	{255,255,255},
  /* from Omar Cornut (http://www.smspower.org/dev/docs/sg1000.txt) */
	{  0,  0,  0},
	{  0,  0,  0},
	{ 32,192, 30},
	{ 96,224, 96},
	{ 32, 32,224},
	{ 64, 96,224},
	{160, 32, 32},
	{ 64,192,224},
	{224, 32, 32},
	{224, 64, 64},
	{192,192, 32},
	{192,192,128},
	{ 32,128, 32},
	{192, 64,160},
	{160,160,160},
	{224,224,224},
  /* from Richard F. Drushel (http://users.stargate.net/~drushel/pub/coleco/twwmca/wk961118.html) */
	{  0,  0,  0},
	{  0,  0,  0},
	{ 71,183, 59},
	{124,207,111},
	{ 93, 78,255},
	{128,114,255},
	{182, 98, 71},
	{ 93,200,237},
	{215,107, 72},
	{251,143,108},
	{195,205, 65},
	{211,218,118},
	{ 62,159, 47},
	{182,100,199},
	{204,204,204},
	{255,255,255}
};

/* Attribute expansion table */
static const uint32_t atex[4] =
{
	0x00000000,
	0x10101010,
	0x20202020,
	0x30303030,
};

/* Bitplane to packed pixel LUT */
static uint32_t bp_lut[0x10000];

static void parse_satb(int32_t line);
static void sms_timed_capture_sprites(int32_t line);
static void sms_timed_capture_sprites_for_pipeline_only(int32_t line);
static void sms_timed_render_captured_sprites(int32_t line);
static void sms_timed_process_hidden_sprites(int32_t line, int display_disabled);
static void update_bg_pattern_cache(void);
#ifndef _8BPP_COLOR
#ifdef MULTIREXZ80_RENDER_32BPP
static void remap_8_to_32(int32_t line);
#else
static void remap_8_to_16(int32_t line);
#endif
#endif

/* Macros to access memory 32-bits at a time (from MAME's drawgfx.c) */

#ifdef ALIGN_DWORD

static __inline__ uint32_t read_dword(void *address)
{
  if ((uintptr_t)address & 3)
  {
#ifdef LSB_FIRST  /* little endian version */
    return ( *((uint8_t *)address) +
            (*((uint8_t *)address+1) << 8)  +
            (*((uint8_t *)address+2) << 16) +
            (*((uint8_t *)address+3) << 24) );
#else       /* big endian version */
    return ( *((uint8_t *)address+3) +
            (*((uint8_t *)address+2) << 8)  +
            (*((uint8_t *)address+1) << 16) +
            (*((uint8_t *)address)   << 24) );
#endif
  }
  else
    return *(uint32_t *)address;
}


static __inline__ void write_dword(void *address, uint32_t data)
{
  if ((uintptr_t)address & 3)
  {
#ifdef LSB_FIRST
    *((uint8_t *)address) =  data;
    *((uint8_t *)address+1) = (data >> 8);
    *((uint8_t *)address+2) = (data >> 16);
    *((uint8_t *)address+3) = (data >> 24);
#else
    *((uint8_t *)address+3) =  data;
    *((uint8_t *)address+2) = (data >> 8);
    *((uint8_t *)address+1) = (data >> 16);
    *((uint8_t *)address)   = (data >> 24);
#endif
    return;
  }
  else
    *(uint32_t *)address = data;
}
#else
#define read_dword(address) *(uint32_t *)address
#define write_dword(address,data) *(uint32_t *)address=data
#endif


/****************************************************************************/

static int systeme_render_alloc(void)
{
	if (!systeme_object_info)
		systeme_object_info = (object_info_t *)calloc(64, sizeof(object_info_t));
	if (!systeme_bg_name_dirty)
		systeme_bg_name_dirty = (uint8_t *)calloc(0x200, sizeof(uint8_t));
	if (!systeme_bg_name_list)
		systeme_bg_name_list = (uint16_t *)calloc(0x200, sizeof(uint16_t));
	if (!systeme_bg_pattern_cache)
		systeme_bg_pattern_cache = (uint8_t *)calloc(0x20000, sizeof(uint8_t));
	if (!systeme_back_line)
		systeme_back_line = (uint8_t *)calloc(0x200, sizeof(uint8_t));
	if (!systeme_front_line)
		systeme_front_line = (uint8_t *)calloc(0x200, sizeof(uint8_t));
#ifndef _8BPP_COLOR
	if (!systeme_pixel)
		systeme_pixel = calloc(PALETTE_SIZE, sizeof(*systeme_pixel));
#endif

	return systeme_object_info && systeme_bg_name_dirty && systeme_bg_name_list &&
		systeme_bg_pattern_cache && systeme_back_line && systeme_front_line
#ifndef _8BPP_COLOR
		&& systeme_pixel
#endif
		;
}

static void systeme_render_free(void)
{
	free(systeme_object_info);
	systeme_object_info = NULL;
	free(systeme_bg_name_dirty);
	systeme_bg_name_dirty = NULL;
	free(systeme_bg_name_list);
	systeme_bg_name_list = NULL;
	free(systeme_bg_pattern_cache);
	systeme_bg_pattern_cache = NULL;
	free(systeme_back_line);
	systeme_back_line = NULL;
	free(systeme_front_line);
	systeme_front_line = NULL;
#ifndef _8BPP_COLOR
	free(systeme_pixel);
	systeme_pixel = NULL;
#endif
	systeme_bg_list_index = 0;
	systeme_object_index_count = 0;
}

static int systeme_render_ready(void)
{
	return systeme_object_info && systeme_bg_name_dirty && systeme_bg_name_list &&
		systeme_bg_pattern_cache && systeme_back_line && systeme_front_line
#ifndef _8BPP_COLOR
		&& systeme_pixel
#endif
		;
}

static void render_select_context(int chip)
{
	if (chip && systeme_render_ready())
	{
		render_vdp = &vdp2;
		active_object_info = systeme_object_info;
		active_bg_name_dirty = systeme_bg_name_dirty;
		active_bg_name_list = systeme_bg_name_list;
		active_bg_list_index = &systeme_bg_list_index;
		active_bg_pattern_cache = systeme_bg_pattern_cache;
		active_object_index_count = &systeme_object_index_count;
	}
	else
	{
		render_vdp = vdp_main_ptr;
		active_object_info = object_info_main_ptr;
		active_bg_name_dirty = bg_name_dirty_main_ptr;
		active_bg_name_list = bg_name_list_main_ptr;
		active_bg_list_index = bg_list_index_main_ptr;
		active_bg_pattern_cache = bg_pattern_cache_main_ptr;
		active_object_index_count = object_index_count_main_ptr;
	}
}

void render_mark_bg_dirty_chip(int chip, uint16_t addr)
{
	uint8_t *dirty = chip ? systeme_bg_name_dirty : bg_name_dirty_main_ptr;
	uint16_t *list = chip ? systeme_bg_name_list : bg_name_list_main_ptr;
	uint16_t *index = chip ? &systeme_bg_list_index : bg_list_index_main_ptr;
	int32_t name = (addr >> 5) & 0x1FF;

	if (chip && !systeme_render_ready())
		return;

	if (dirty[name] == 0 && *index < 0x200)
	{
		list[*index] = name;
		(*index)++;
	}
	dirty[name] |= (1 << ((addr >> 2) & 7));
}

void render_invalidate_bg_cache(void)
{
	uint16_t i;

	*bg_list_index_main_ptr = 0x200;
	for (i = 0; i < 0x200; i++)
	{
		bg_name_list_main_ptr[i] = i;
		bg_name_dirty_main_ptr[i] = 255;
	}

	if (sms.console == CONSOLE_SYSTEME && systeme_render_ready())
	{
		systeme_bg_list_index = 0x200;
		for (i = 0; i < 0x200; i++)
		{
			systeme_bg_name_list[i] = i;
			systeme_bg_name_dirty[i] = 255;
		}
	}
}

void render_shutdown(void)
{
	systeme_render_free();
	lcd_persistence_free();
}

/* Initialize the rendering data */
void render_init(void)
{
	int32_t j;
	int32_t bx, sx, b, s, bp, bf, sf, c;

	make_tms_tables();

	/* Generate 64k of data for the look up table */
	for(bx = 0; bx < 0x100; bx++)
	{
		for(sx = 0; sx < 0x100; sx++)
		{
			/* Background pixel */
			b  = (bx & 0x0F);

			/* Background priority */
			bp = (bx & 0x20) ? 1 : 0;

			/* Full background pixel + priority + sprite marker */
			bf = (bx & 0x7F);

			/* Sprite pixel */
			s  = (sx & 0x0F);

			/* Full sprite pixel, w/ palette and marker bits added */
			sf = (sx & 0x0F) | 0x10 | 0x40;

			/* Overwriting a sprite pixel ? */
			if(bx & 0x40)
			{
				/* Return the input */
				c = bf;
			}
			else
			{
				/* Work out priority and transparency for both pixels */
				if(bp)
				{
					/* Underlying pixel is high priority */
					if(b)
					{
						c = bf | 0x40;
					}
					else
					{
						if(s) c = sf;
						else c = bf;
					}
				}
				else
				{
					/* Underlying pixel is low priority */
					if(s) c = sf;
					else c = bf;
				}
			}
			/* Store result */
			lut[(bx << 8) | (sx)] = c;
		}
	}

	/* Make bitplane to pixel lookup table */
	for(int32_t i = 0; i < 0x100; i++)
	for(j = 0; j < 0x100; j++)
	{
		int32_t x;
		uint32_t out = 0;
		for(x = 0; x < 8; x++)
		{
			out |= (j & (0x80 >> x)) ? (uint32_t)(8 << (x << 2)) : 0;
			out |= (i & (0x80 >> x)) ? (uint32_t)(4 << (x << 2)) : 0;
		}
#if LSB_FIRST
		bp_lut[(j << 8) | (i)] = out;
#else
		bp_lut[(i << 8) | (j)] = out;
#endif
	}

	/* SMS VDP 2-bit DAC levels. These are intentionally per-channel,
	 * matching the analog SMS palette expansion used by common reference
	 * captures rather than simple 0/85/170/255 scaling. Keep the legacy
	 * table populated for non-render users, but use the channel-specific
	 * tables for actual RGB conversion. */
	sms_cram_expand_table[0] = 0;
	sms_cram_expand_table[1] = 0x55;
	sms_cram_expand_table[2] = 0xAA;
	sms_cram_expand_table[3] = 0xFF;

	sms_cram_expand_r[0] = 0;
	sms_cram_expand_r[1] = 82;
	sms_cram_expand_r[2] = 172;
	sms_cram_expand_r[3] = 255;
	sms_cram_expand_g[0] = 0;
	sms_cram_expand_g[1] = 84;
	sms_cram_expand_g[2] = 169;
	sms_cram_expand_g[3] = 254;
	sms_cram_expand_b[0] = 0;
	sms_cram_expand_b[1] = 82;
	sms_cram_expand_b[2] = 172;
	sms_cram_expand_b[3] = 255;

	for(uint8_t i = 0; i < 16; i++)
	{
		gg_cram_expand_table[i] = i << 4 | i;    
	}
}


/* Reset the rendering data */
void render_reset(void)
{
	int32_t i;

	/* Clear display bitmap */
	memset(bitmap.data, 0, bitmap.pitch * bitmap.height);
	if ((sms.console == CONSOLE_GG) && option.lcd_persistence)
	{
		if (lcd_persistence_alloc())
			lcd_persistence_clear();
	}
	else
	{
		lcd_persistence_free();
	}

	if (sms.console == CONSOLE_SYSTEME)
	{
		if (!systeme_render_alloc())
		{
			fprintf(stderr, "System E renderer allocation failed\n");
			abort();
		}
	}
	else
	{
		systeme_render_free();
	}

	render_select_context(0);

	/* Clear palette */
	for(i = 0; i < PALETTE_SIZE; i++)
	{
		palette_sync_chip(0, i);
		if (sms.console == CONSOLE_SYSTEME)
			palette_sync_chip(1, i);
	}

	/* Invalidate pattern cache */
	memset(bg_name_dirty_main_ptr, 0, 0x200);
	memset(bg_name_list_main_ptr, 0, 0x200 * sizeof(uint16_t));
	*bg_list_index_main_ptr = 0;
	memset(bg_pattern_cache_main_ptr, 0, 0x20000);
	if (sms.console == CONSOLE_SYSTEME && systeme_render_ready())
	{
		memset(systeme_bg_name_dirty, 0, 0x200);
		memset(systeme_bg_name_list, 0, 0x200 * sizeof(uint16_t));
		systeme_bg_list_index = 0;
		memset(systeme_bg_pattern_cache, 0, 0x20000);
	}
	
#ifdef _8BPP_COLOR
    /* Mark all colors as dirty */
    bitmap.pal.update = 1;
    memset(bitmap.pal.dirty, 1, PALETTE_SIZE);
    memset(bitmap.pal.color, 0, PALETTE_SIZE * 3);
#endif

	/* Pick default render routine */
	if ((sms.console == CONSOLE_SYSTEME) || (vdp.reg[0] & 4))
	{
		render_bg = render_bg_sms;
		render_obj = render_obj_sms;
	}
	else
	{
		render_bg = render_bg_tms;
		render_obj = render_obj_tms;
	}
}

static int32_t prev_line = -1;

static uint8_t sms_sprite_mode_for_parse(void)
{
	if (sms.console == CONSOLE_SYSTEME)
		return (uint8_t)((vdp.reg[1] & 0x03) | (vdp.reg[0] & 0x08));
	return vdp.sprite_mode_latch;
}

static uint8_t sms_sprite_mode_for_draw(void)
{
	if (sms.console == CONSOLE_SYSTEME)
		return (uint8_t)((vdp.reg[1] & 0x03) | (vdp.reg[0] & 0x08));
	return vdp.sprite_mode_draw;
}

static void sms_mode4_refresh_sprite_status(int32_t line, int display_disabled)
{
	uint8_t *sat = &vdp.vram[vdp.satb & 0x3f00];
	int phase = line & 7;
	int start = phase << 5;
	int visible_y_count = 0;
	int offscreen_y_count = 0;
	int list_end = 64;
	int i;

	if ((sms.console == CONSOLE_SYSTEME) || !vdp_timed_render_active() || (vdp.mode <= 7))
		return;

	if (vdp.extended == 0)
	{
		for (i = 0; i < 64; i++)
		{
			if (sat[i] == 0xd0)
			{
				list_end = i;
				break;
			}
		}
	}

	/* When Mode 4 display fetches are disabled by blanking or R1 bit 6, the
	 * SMS/GG VDP does not run the normal two-stage sprite renderer.  Recent
	 * GG hardware tests show that it walks the SAT refresh stream instead: 256
	 * SAT bytes, 32 bytes per scanline phase, repeated every eight scanlines.
	 * Keep the visible sprite parser out of this path; only model the status
	 * side effects that VDPTEST and hardware expose. */
	for (i = 0; i < 32; i++)
	{
		int sat_index = (start + i) & 0xff;
		uint8_t y = sat[sat_index];

		/* The first 64 SAT bytes are Y entries.  Attribute bytes in the second
		 * half are still fetched, but do not participate in the Y comparator. */
		if ((sat_index >= 64) || (sat_index >= list_end))
			continue;

		if (y < 0xe0)
			visible_y_count++;
		else if (y >= 0xf0)
			offscreen_y_count++;

	}

	/* Disabled-display sprite overflow is not the normal active renderer's
	 * overflow test.  It is nevertheless observable when the SAT refresh phase
	 * contains more than eight visible-range Y entries, and not when the entries
	 * are all offscreen high-Y values. */
	if (display_disabled && (visible_y_count > 8))
		vdp_request_sprite_overflow(line);

	/* Offscreen-high Y entries can feed stale/refresh data into the sprite
	 * collision path even though there is no corresponding pixel fetch.  This
	 * covers the SMS1/GG hardware behaviour behind VDPTEST #46 without drawing
	 * or colliding normal offscreen-X/transparent sprites. */
	if (offscreen_y_count >= 2)
		vdp_request_sprite_collision(line, 0);
}

static void sms_timed_process_hidden_sprites(int32_t line, int display_disabled)
{
	if ((sms.console == CONSOLE_SYSTEME) || !vdp_timed_render_active() || (vdp.mode <= 7))
		return;

	sms_mode4_refresh_sprite_status(line, display_disabled);

	/* Keep the one-line sprite-list pipeline primed for the first visible line
	 * after a border/blank interval, but do not render hidden sprites into a
	 * dummy line.  Hidden/display-off status comes from the refresh path above. */
	sms_timed_capture_sprites_for_pipeline_only((line + 1) % vdp.lpf);
}

static int render_line_to_index_buffer(int chip, int32_t line, uint8_t *dst, int transparent_blank, int32_t *out_vline)
{
	int32_t view = 1;
	int32_t width = bitmap.viewport.w + 2 * bitmap.viewport.x;

	render_select_context(chip);
	vdp.line = line;

	/* Ensure we're within the VDP active area (incl. overscan) */
	int32_t top_border = active_border[sms.display][vdp.extended];
	int32_t vline = (line + top_border) % vdp.lpf;
	
	if (vline >= active_range[sms.display])
	{
		if ((sms.console != CONSOLE_SYSTEME) && vdp_timed_render_active() && (vdp.mode > 7))
			sms_timed_capture_sprites_for_pipeline_only((line + 1) % vdp.lpf);
		return 0;
	}

	/* adjust for Game Gear screen */
	top_border = top_border + (vdp.height - bitmap.viewport.h) / 2;

	/* Point to current line in output buffer */
	linebuf = dst;

	/* Sprite limit flag is set at the beginning of the line */
	if (vdp.spr_ovr)
	{
		vdp.spr_ovr = 0;
		vdp_request_sprite_overflow(line);
	}

	/* TMS9918-family sprites are evaluated for the line being drawn.
	 * Parsing them after rendering makes line 0 consume stale/empty sprite
	 * state and drops the first visible row of sprites whose encoded Y is
	 * $ff (display Y=0). */
	if ((sms.console != CONSOLE_SYSTEME) && (vdp.mode <= 7))
		parse_line(line);

	/* Vertical borders */
	if ((vline < top_border) || (vline >= (bitmap.viewport.h + top_border)))
	{
		/* Sprites are still processed offscreen.  In the timed SMS/GG
		 * renderer, keep sprite status evaluation alive even when the
		 * border line itself is not output. */
		if ((sms.console != CONSOLE_SYSTEME) && vdp_timed_render_active() && (vdp.mode > 7))
			sms_timed_process_hidden_sprites(line, 0);
		else if ((vdp.mode > 7) && (vdp.reg[1] & 0x40))
			render_obj(line);
		/* Line is only displayed where overscan is emulated */
		view = 0;
	}
	/* Active display */
	else
	{
		/* Display enabled ? */
		if (vdp.reg[1] & 0x40)
		{
			/* Update pattern cache */
			update_bg_pattern_cache();

			/* Draw background */
			render_bg(line);

			if ((sms.console != CONSOLE_SYSTEME) && vdp_timed_render_active() && (vdp.mode > 7))
			{
				/* Consume the sprite list that was captured at the previous
				 * late render event for this exact display line, then capture
				 * the next line's list for the following scanline.  This maps
				 * Gearsystem's ParseSprites(line+1)/RenderSprites(line+1)
				 * ordering onto this line-buffered renderer without drawing the
				 * line one pixel early. */
				sms_timed_render_captured_sprites(line);
				sms_timed_capture_sprites((line + 1) % vdp.lpf);
			}
			else
			{
				/* Draw sprites */
				render_obj(line);
			}

#ifndef NOBLANKING_LEFTCOLUM
			/* Blank leftmost column of display */
			if((vdp.reg[0] & 0x20) && (IS_SMS || IS_MD))
				memset(linebuf, BACKDROP_COLOR, 8);
#endif
		}
		else
		{
			/* Background color or transparent disabled front layer */
			memset(linebuf, transparent_blank ? 0 : BACKDROP_COLOR, width);
			if ((sms.console != CONSOLE_SYSTEME) && vdp_timed_render_active() && (vdp.mode > 7))
				sms_timed_process_hidden_sprites(line, 1);
		}
	}

	/* If display is disabled again after the pre-active top-row fetch has
	 * closed, the SMS status/playfield transition emits backdrop for two
	 * scanlines.  This is keyed off the display-enable latch sequence, not
	 * off the number or value of R8 writes. */
	if (((sms.console == CONSOLE_SMS) || (sms.console == CONSOLE_SMS2)) &&
	    (vdp.hscroll_top_next_armed & 0x20) && (vdp.mode > 7) &&
	    (line >= 31) && (line < 33))
	{
		memset(linebuf, BACKDROP_COLOR, width);
	}

	/* Parse sprites for the legacy line-start renderer.  The timed SMS/GG
	 * path evaluates the current sprite line immediately before drawing. */
	if (!((sms.console != CONSOLE_SYSTEME) && vdp_timed_render_active() && (vdp.mode > 7)))
	{
		if (sms.console != CONSOLE_SYSTEME)
			vdp_latch_sprite_mode();
		if (vdp.mode > 7)
			parse_satb(line);
	}

	/* Only draw lines within the video output range ! */
	if (view)
	{
		/* adjust output line */
		vline -= top_border;
		if (out_vline) *out_vline = vline;
	}
	return view;
}

#ifndef _8BPP_COLOR
static void systeme_remap_composited_line(int32_t line, uint8_t *back, uint8_t *front)
{
	int32_t i;
	int32_t width = bitmap.viewport.w + 2 * bitmap.viewport.x;

	LOCK_VIDEO

#ifdef MULTIREXZ80_RENDER_32BPP
	uint32_t *p = (uint32_t *)&bitmap.data[(line * bitmap.pitch)];
	for (i = 0; i < width; i++)
	{
		uint8_t f = front[i];
		uint8_t b = back[i];
		p[i] = (f & 0x0f) ? systeme_pixel[f & PIXEL_MASK] : pixel[b & PIXEL_MASK];
	}
#else
	uint16_t *p = (uint16_t *)&bitmap.data[(line * bitmap.pitch)];
	for (i = 0; i < width; i++)
	{
		uint8_t f = front[i];
		uint8_t b = back[i];
		p[i] = (f & 0x0f) ? systeme_pixel[f & PIXEL_MASK] : pixel[b & PIXEL_MASK];
	}
#endif

	UNLOCK_VIDEO
}
#endif

static void render_line_systeme(int32_t line)
{
	int32_t vline0 = 0, vline1 = 0;
	int view0, view1;

	if (!systeme_render_ready())
		return;

	memset(systeme_back_line, 0, 0x200);
	memset(systeme_front_line, 0, 0x200);

	/* System E's 0xba/0xbb VDP is the back layer and 0xbe/0xbf is the front layer.
	 * Skip the front-layer renderer while the second VDP display is disabled;
	 * this keeps single-VDP games such as Tetris on the cheaper path while
	 * preserving the dual-VDP path for Transformer and the other layered games. */
	view0 = render_line_to_index_buffer(0, line, systeme_back_line, 0, &vline0);
	view1 = (vdp2.reg[1] & 0x40) ? render_line_to_index_buffer(1, line, systeme_front_line, 1, &vline1) : 0;

	if (view0 || view1)
	{
		int32_t out_line = view0 ? vline0 : vline1;
#ifdef _8BPP_COLOR
		int32_t i;
		uint8_t *p = &bitmap.data[(out_line * bitmap.pitch)];
		int32_t width = bitmap.viewport.w + 2 * bitmap.viewport.x;
		for (i = 0; i < width; i++)
			p[i] = (systeme_front_line[i] & 0x0f) ? systeme_front_line[i] : systeme_back_line[i];
#else
		systeme_remap_composited_line(out_line, systeme_back_line, systeme_front_line);
#endif
	}

	render_select_context(0);
}

/* Draw a line of the display */
void render_line(int32_t line)
{
	int32_t vline = 0;

	/* Ensure we have not already rendered this line */
	if (prev_line == line) return;
	prev_line = line;

	if (sms.console == CONSOLE_SYSTEME)
	{
		render_line_systeme(line);
		return;
	}

#ifdef _8BPP_COLOR
	if (render_line_to_index_buffer(0, line, &bitmap.data[(line * bitmap.pitch)], 0, &vline))
	{
		(void)vline;
	}
#else
	if (render_line_to_index_buffer(0, line, &internal_buffer[0], 0, &vline))
	{
#ifdef MULTIREXZ80_RENDER_32BPP
		remap_8_to_32(vline);
#else
		remap_8_to_16(vline);
#endif
	}
#endif

	render_select_context(0);
}


static int sms_gg_bg_fetch_cycle_for_column(int32_t column)
{
	/* Mode 4 background fetch advances at the VDP pixel rate, not at the Z80
	 * instruction clock.  One Z80 cycle is 1.5 display pixels on SMS/GG, so an
	 * 8-pixel tile column spans 16/3 Z80 cycles.  R2/VRAM writes made by raster
	 * code become visible when the nametable entry for that tile column is
	 * fetched.  Sampling every 12 Z80 cycles compressed mid-line table effects
	 * horizontally, which made Tables Have Turned's spotlight too skinny.
	 */
	int32_t c = 59 + ((column * 16) / 3);
	if (c < 0) c = 0;
	if (c > 220) c = 220;
	return c;
}

static uint8_t sms_gg_fetch_bg_pixel(uint16_t attr, int32_t row, int32_t x, int32_t fetch_cycle)
{
	uint16_t tile = attr & 0x01FF;
	int32_t hflip = attr & 0x0200;
	int32_t vflip = attr & 0x0400;
	int32_t src_row = vflip ? (row ^ 7) : row;
	int32_t src_x = hflip ? x : (x ^ 7);
	uint16_t base = (uint16_t)((tile << 5) | (src_row << 2));
	uint8_t p0 = vdp_vram_byte_for_bg_fetch((uint16_t)(base + 0), fetch_cycle);
	uint8_t p1 = vdp_vram_byte_for_bg_fetch((uint16_t)(base + 1), fetch_cycle);
	uint8_t p2 = vdp_vram_byte_for_bg_fetch((uint16_t)(base + 2), fetch_cycle);
	uint8_t p3 = vdp_vram_byte_for_bg_fetch((uint16_t)(base + 3), fetch_cycle);
	uint8_t bit = (uint8_t)(1u << src_x);
	uint8_t c = (uint8_t)(((p0 & bit) ? 1 : 0) |
	                    ((p1 & bit) ? 2 : 0) |
	                    ((p2 & bit) ? 4 : 0) |
	                    ((p3 & bit) ? 8 : 0));
	return (uint8_t)(c | ((attr >> 7) & 0x30));
}


static int sms_top_scroll_active_for_line(int32_t line)
{
	return ((sms.console == CONSOLE_SMS) || (sms.console == CONSOLE_SMS2)) &&
	       (vdp.hscroll_top_next_armed & 0x80) && (vdp.mode > 7) &&
	       (line >= 0) && (line < 31) &&
	       (vdp.hscroll_top != vdp.hscroll);
}

static int sms_top_cram_active_for_line(int32_t line)
{
	return ((sms.console == CONSOLE_SMS) || (sms.console == CONSOLE_SMS2)) &&
	       vdp.cram_top_valid && (vdp.mode > 7) &&
	       (line >= 0) && (line < 31);
}

static uint16_t sms_gg_ntab_from_reg2(uint8_t reg2)
{
	if (vdp.extended)
		return (uint16_t)((((uint16_t)reg2 << 10) & 0x3000) | 0x0700);
	return (uint16_t)(((uint16_t)reg2 << 10) & 0x3800);
}

static uint16_t sms_gg_fetch_nt_attr(int32_t line, int32_t v_line, int32_t column, int locked, int32_t fetch_cycle)
{
	uint8_t reg2 = vdp_reg_byte_for_bg_fetch(2, fetch_cycle);
	uint16_t mask = (((sms.console == CONSOLE_SMS) && !(reg2 & 1)) ? (uint16_t)~0x400 : 0xFFFF);
	uint16_t nt_base = sms_gg_ntab_from_reg2(reg2);
	int32_t map_line = locked ? line : v_line;
	uint16_t addr = (uint16_t)((nt_base + ((map_line >> 3) << 6) + ((column & 0x1F) << 1)) & mask);
	uint8_t lo = vdp_vram_byte_for_bg_fetch(addr, fetch_cycle);
	uint8_t hi = vdp_vram_byte_for_bg_fetch((uint16_t)(addr + 1), fetch_cycle);
	return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint8_t sms_gg_fetch_sprite_pixel(uint16_t tile, int32_t row, int32_t x, int32_t fetch_cycle)
{
	uint16_t base = (uint16_t)(((tile & 0x01FF) << 5) | ((row & 0x0F) << 2));
	uint8_t p0 = vdp_vram_byte_for_bg_fetch((uint16_t)(base + 0), fetch_cycle);
	uint8_t p1 = vdp_vram_byte_for_bg_fetch((uint16_t)(base + 1), fetch_cycle);
	uint8_t p2 = vdp_vram_byte_for_bg_fetch((uint16_t)(base + 2), fetch_cycle);
	uint8_t p3 = vdp_vram_byte_for_bg_fetch((uint16_t)(base + 3), fetch_cycle);
	uint8_t bit = (uint8_t)(1u << (7 - (x & 7)));
	return (uint8_t)(((p0 & bit) ? 1 : 0) |
	               ((p1 & bit) ? 2 : 0) |
	               ((p2 & bit) ? 4 : 0) |
	               ((p3 & bit) ? 8 : 0));
}

static int sms_gg_sprite_fetch_cycle_for_x(int32_t x)
{
	(void)x;
	return 28;
}

static int render_bg_sms_gg_timed_fetch(int32_t line)
{
	int32_t locked = 0;
	int32_t yscroll_mask = (vdp.extended) ? 256 : 224;
	int32_t v_line = (line + vdp.vscroll) % yscroll_mask;
	int32_t row = v_line & 7;
	int32_t hscroll = sms_top_scroll_active_for_line(line) ? (0x100 - vdp.hscroll_top) : (((vdp.reg[0] & 0x40) && (line < 0x10) && (sms.console != CONSOLE_GG)) ? 0 : (0x100 - vdp.hscroll));
	int32_t shift = hscroll & 7;
	int32_t x;

	if (!vdp_timed_render_active() || (vdp.mode <= 7))
		return 0;

	for (x = 0; x < 256; x++)
	{
		int32_t scrolled_x = x + hscroll;
		int32_t column = (scrolled_x >> 3) & 0x1F;
		int32_t tile_x = scrolled_x & 7;
		uint16_t attr;
		int32_t fetch_cycle = sms_gg_bg_fetch_cycle_for_column(x >> 3);

		if ((vdp.reg[0] & 0x80) && (!locked) && ((x >> 3) >= 24))
		{
			locked = 1;
			row = line & 7;
		}

		if (shift && x < (8 - shift))
		{
			linebuf[x] = 0;
			continue;
		}

		attr = sms_gg_fetch_nt_attr(line, v_line, column, locked, fetch_cycle);
		linebuf[x] = sms_gg_fetch_bg_pixel(attr, row, tile_x, fetch_cycle);
	}
	return 1;
}

/* Draw the Master System background */
void render_bg_sms(int32_t line)
{
	if (render_bg_sms_gg_timed_fetch(line)) return;

	int32_t locked = 0;
	int32_t yscroll_mask = (vdp.extended) ? 256 : 224;
	int32_t v_line = (line + vdp.vscroll) % yscroll_mask;
	int32_t v_row  = (v_line & 7) << 3;
	int32_t hscroll = sms_top_scroll_active_for_line(line) ? (0x100 - vdp.hscroll_top) : (((vdp.reg[0] & 0x40) && (line < 0x10) && (sms.console != CONSOLE_GG)) ? 0 : (0x100 - vdp.hscroll));
	int32_t column = 0;
	uint16_t attr;
	uint16_t SMS_VDP_BUG = (((sms.console == CONSOLE_SMS) && !(vdp.reg[2] & 1)) ? ~0x400 :0xFFFF);
	uint16_t nt_addr = (vdp.ntab + ((v_line >> 3) << 6)) & SMS_VDP_BUG;
	uint16_t nt_addr_stop_verticalscroll = (vdp.ntab + ((line >> 3) << 6)) & SMS_VDP_BUG;
	uint16_t *nt = (uint16_t *)&vdp.vram[nt_addr];
	int32_t nt_scroll = (hscroll >> 3);
	int32_t shift = (hscroll & 7);
	uint32_t atex_mask;
	uint32_t *cache_ptr;
	uint32_t *linebuf_ptr = (uint32_t *)&linebuf[0 - shift];

	/* Draw first column (clipped) */
	if(shift)
	{
		int32_t x;
		for(x = shift; x < 8; x++)
			linebuf[(0 - shift) + (x)] = 0;
		column++;
	}

	/* Draw a line of the background */
	for(; column < 32; column++)
	{
		/* Stop vertical scrolling for leftmost eight columns */
		if((vdp.reg[0] & 0x80) && (!locked) && (column >= 24))
		{
			locked = 1;
			v_row = (line & 7) << 3;
			nt = (uint16_t *)&vdp.vram[nt_addr_stop_verticalscroll];
		}

		/* Get name table attribute word */
		attr = nt[(column + nt_scroll) & 0x1F];

#ifndef LSB_FIRST
		attr = (((attr & 0xFF) << 8) | ((attr & 0xFF00) >> 8));
#endif
		/* Expand priority and palette bits */
		atex_mask = atex[(attr >> 11) & 3];

		/* Point to a line of pattern data in cache */
		cache_ptr = (uint32_t *)&bg_pattern_cache[((attr & 0x7FF) << 6) | (v_row)];
    
		/* Copy the left half, adding the attribute bits in */
		write_dword( &linebuf_ptr[(column << 1)] , read_dword( &cache_ptr[0] ) | (atex_mask));

		/* Copy the right half, adding the attribute bits in */
		write_dword( &linebuf_ptr[(column << 1) | (1)], read_dword( &cache_ptr[1] ) | (atex_mask));
	}

	/* Draw last column (clipped) */
	if(shift)
	{
		int32_t x;
		uint8_t c, a;
		uint8_t *p = &linebuf[(0 - shift)+(column << 3)];
		attr = nt[(column + nt_scroll) & 0x1F];
#ifndef LSB_FIRST
		attr = (((attr & 0xFF) << 8) | ((attr & 0xFF00) >> 8));
#endif
		a = (attr >> 7) & 0x30;

		for(x = 0; x < shift; x++)
		{
			c = bg_pattern_cache[((attr & 0x7FF) << 6) | (v_row) | (x)];
			p[x] = ((c) | (a));
		}
	}
}

/* Draw sprites */
void render_obj_sms(int32_t line)
{
	int32_t i,x,start,end,xp,yp,n;
	uint8_t sp,bg;
	uint8_t *linebuf_ptr;
	uint8_t *cache_ptr;

	uint8_t sprite_mode = sms_sprite_mode_for_draw();
	int32_t width = 8;

	/* Adjust dimensions for double size sprites */
	if(sprite_mode & 0x01)
		width *= 2;

	/* Draw sprites in front-to-back order */
	for(i = 0; i < object_index_count; i++)
	{
		/* Width of sprite */
		start = 0;
		end = width;

		/* Sprite X position */
		xp = object_info[i].xpos;

		/* Sprite Y range */
		yp = object_info[i].yrange;

		/* Pattern name */
		n = object_info[i].attr;

		/* X position shift */
		if(sprite_mode & 0x08) xp -= 8;

		/* Add MSB of pattern name */
		if(vdp.reg[6] & 0x04) n |= 0x0100;

		/* Mask LSB for 8x16 sprites */
		if(sprite_mode & 0x02) n &= 0x01FE;

		/* Point to offset in line buffer */
		linebuf_ptr = (uint8_t *)&linebuf[xp];

		/* Clip sprites on left edge */
		if(xp < 0)
		  start = (0 - xp);

		/* Clip sprites on right edge */
		if((xp + width) > 256)
		  end = (256 - xp);

		/* Draw double size sprite */
		if(sprite_mode & 0x01)
		{
			/* Retrieve tile data.  Timed GG rendering keeps a per-line VRAM
			 * write log so sprite pattern fetches can see the value that was on
			 * the bus when the sprite unit actually fetched this pixel. */
			if (vdp_gamegear_timing_active() && vdp_timed_render_active())
				cache_ptr = NULL;
			else
				cache_ptr = (uint8_t *)&bg_pattern_cache[(n << 6) | ((yp >> 1) << 3)];
			
			/* Draw sprite line (at 1/2 dot rate) */
			for(x = start; x < end; x+=2)
			{
				/* Source pixel from cache or timed VRAM fetch */
				sp = cache_ptr ? cache_ptr[(x >> 1)] : sms_gg_fetch_sprite_pixel((uint16_t)n, (yp >> 1), (x >> 1), sms_gg_sprite_fetch_cycle_for_x(xp + x));

				/* Only draw opaque sprite pixels */
				if(sp)
				{
					/* Background pixel from line buffer */
					bg = linebuf_ptr[x];

					/* Look up result */
					linebuf_ptr[x] = linebuf_ptr[x+1] = lut[(bg << 8) | (sp)];

					/* Check sprite collision */
					/* No sprite collision for 9th sprite. This passes Flubba's test */
					if ((bg & 0x40) && object_index_count != 9)
					{
						vdp_request_sprite_collision(line, xp + x);
					}
				}
			}
		}
		else /* Regular size sprite (8x8 / 8x16) */
		{
			/* Retrieve tile data */
			if (vdp_gamegear_timing_active() && vdp_timed_render_active())
				cache_ptr = NULL;
			else
				cache_ptr = (uint8_t *)&bg_pattern_cache[(n << 6) | (yp << 3)];

			/* Draw sprite line */
			for(x = start; x < end; x++)
			{
				/* Source pixel from cache or timed VRAM fetch */
				sp = cache_ptr ? cache_ptr[x] : sms_gg_fetch_sprite_pixel((uint16_t)n, yp, x, sms_gg_sprite_fetch_cycle_for_x(xp + x));

				/* Only draw opaque sprite pixels */
				if(sp)
				{
					/* Background pixel from line buffer */
					bg = linebuf_ptr[x];

					/* Look up result */
					linebuf_ptr[x] = lut[(bg << 8) | (sp)];

					/* Check sprite collision */
					/* No sprite collision for 9th sprite. This passes Flubba's test */
					if ((bg & 0x40) && object_index_count != 9)
					{
						vdp_request_sprite_collision(line, xp + x);
					}
				}
			}
		}
	}
}

/* Update a palette entry */
#ifndef _8BPP_COLOR
#ifdef MULTIREXZ80_RENDER_32BPP
typedef uint32_t native_pixel_t;
#else
typedef uint16_t native_pixel_t;
#endif
#endif

#ifndef _8BPP_COLOR
static native_pixel_t sms_native_pixel_from_cram_byte(uint8_t data)
{
	int32_t r = sms_cram_expand_r[(data >> 0) & 3];
	int32_t g = sms_cram_expand_g[(data >> 2) & 3];
	int32_t b = sms_cram_expand_b[(data >> 4) & 3];
	return MAKE_PIXEL(r, g, b);
}

static const uint8_t *sms_cram_for_output_line(int32_t line)
{
	if (((sms.console == CONSOLE_SMS) || (sms.console == CONSOLE_SMS2)) &&
	    vdp_timed_render_active() && (vdp.mode > 7))
	{
		if (sms_top_cram_active_for_line(line))
			return vdp.cram_top;
		return vdp.cram_line_latch;
	}
	return NULL;
}

#endif

static void palette_sync_target(vdp_t *ctx, int32_t index
#ifndef _8BPP_COLOR
	, native_pixel_t *dst
#endif
)
{
	int32_t r, g, b;
	int32_t mode4 = (ctx->reg[0] & 4) || IS_GG;
	
	/* VDP Mode */
	if (mode4)
	{
		/* Mode 4 or Game Gear TMS mode*/
		if(sms.console == CONSOLE_GG)
		{
			/* GG palette */
			/* ----BBBBGGGGRRRR */
			r = (ctx->cram[(index << 1) | (0)] >> 0) & 0x0F;
			g = (ctx->cram[(index << 1) | (0)] >> 4) & 0x0F;
			b = (ctx->cram[(index << 1) | (1)] >> 0) & 0x0F;

			r = gg_cram_expand_table[r];
			g = gg_cram_expand_table[g];
			b = gg_cram_expand_table[b];
		}
		else
		{
			/* SMS/System E palette */
			/* --BBGGRR */
			r = (ctx->cram[index] >> 0) & 3;
			g = (ctx->cram[index] >> 2) & 3;
			b = (ctx->cram[index] >> 4) & 3;
			
			r = sms_cram_expand_r[r];
			g = sms_cram_expand_g[g];
			b = sms_cram_expand_b[b];
		}
	}
	else
	{
		/* TMS Mode (16 colors only) */
		int32_t color = index & 0x0F;

		if (sms.console < CONSOLE_SMS)
		{
			/* pick one of the original TMS9918 palettes */
			color += option.tms_pal * 16;

			r = tms_palette[color][0];
			g = tms_palette[color][1];
			b = tms_palette[color][2];
		}
		else
		{
			/* fixed CRAM palette in TMS mode */ 
			r = (tms_crom[color] >> 0) & 3;
			g = (tms_crom[color] >> 2) & 3;
			b = (tms_crom[color] >> 4) & 3;

			r = sms_cram_expand_r[r];
			g = sms_cram_expand_g[g];
			b = sms_cram_expand_b[b];
		}
	}
	
	#ifdef _8BPP_COLOR
    bitmap.pal.color[index][0] = r;
    bitmap.pal.color[index][1] = g;
    bitmap.pal.color[index][2] = b;
	bitmap.pal.dirty[index] = bitmap.pal.update = 1;
	#else
	dst[index] = MAKE_PIXEL(r, g, b);
#ifndef MULTIREXZ80_RENDER_32BPP
#ifndef MULTIREXZ80_DISABLE_FAST_REMAP
	remap16_pair_dirty = 1;
#endif
#endif
	#endif
}

void palette_sync_chip(int chip, int32_t index)
{
	if (chip && !systeme_render_ready())
		return;
#ifndef _8BPP_COLOR
	palette_sync_target(chip ? &vdp2 : vdp_main_ptr, index, chip ? systeme_pixel : pixel);
#else
	palette_sync_target(chip ? &vdp2 : vdp_main_ptr, index);
#endif
}

void palette_sync(int32_t index)
{
	palette_sync_chip(0, index);
}

static void parse_satb(int32_t line)
{
	/* Pointer to sprite attribute table */
	uint8_t *st = (uint8_t *)&vdp.vram[vdp.satb];

	/* Sprite counter (64 max.) */
	int32_t i = 0;


	/* Sprite height (8x8 by default) */
	uint8_t yp;
	uint8_t height = 8;
	uint8_t sprite_mode = sms_sprite_mode_for_parse();
	uint8_t zoomed = sprite_mode & 0x01;

	if (sms.console != CONSOLE_SYSTEME)
		vdp.sprite_mode_draw = sprite_mode;
  
	/* Adjust height for 8x16 sprites */
	if(sprite_mode & 0x02) 
		height <<= 1;

	/* Adjust height for zoomed sprites */
	if(zoomed)
		height <<= 1;

	/* Sprite count for current line (8 max.) */
	object_index_count = 0;

	for(i = 0; i < 64; i++)
	{
		/* Sprite Y position */
		yp = st[i];

		/* Found end of sprite list marker for non-extended modes? */
		if(vdp.extended == 0 && yp == 208)
			return;

		/* Wrap Y coordinate for sprites > 240 */
		if(yp > 240) yp -= 256;

		/* Compare sprite position with current line counter */
		yp |= (zoomed);
 		yp = line - yp;
		
		/* Sprite is within vertical range? */
		if(yp < height)
		{
			/* Sprite limit reached? */
			if (object_index_count == 8)
			{
				/* Flag is set only during active area */
				if (line < vdp.height)
				vdp_request_sprite_overflow(line);

				/* End of sprite parsing */
				if (option.spritelimit)
					return;
			}

			/* Store sprite attributes for later processing */
			object_info[object_index_count].yrange = yp;
			object_info[object_index_count].xpos = st[0x80 + (i << 1)];
			object_info[object_index_count].attr = st[0x81 + (i << 1)];

			/* Increment Sprite count for current line */
			++object_index_count;
		}
	}
}


static uint8_t parse_satb_timed_to(object_info_t *dst, int32_t line, uint8_t *out_sprite_mode, int request_status)
{
	uint8_t *st = (uint8_t *)&vdp.vram[vdp.satb];
	uint8_t sprite_mode = (uint8_t)((vdp.reg[1] & 0x03) | (vdp.reg[0] & 0x08));
	uint8_t zoomed = (uint8_t)(sprite_mode & 0x01);
	int32_t height = (sprite_mode & 0x02) ? 16 : 8;
	uint8_t count = 0;
	int32_t i;

	if (zoomed)
		height <<= 1;

	*out_sprite_mode = sprite_mode;

	for (i = 0; i < 64; i++)
	{
		int32_t sprite_y = (int32_t)st[i] + 1;
		int32_t sprite_y2;
		int32_t sprite_y_offscreen;
		int32_t sprite_y_offscreen2;

		if (vdp.extended == 0 && st[i] == 208)
			return count;

		sprite_y2 = sprite_y + height;
		sprite_y_offscreen = ((sprite_y > 240) && (sprite_y <= 256)) ? (sprite_y - 256) : sprite_y;
		sprite_y_offscreen2 = sprite_y_offscreen + height;

		if (((line >= sprite_y) && (line < sprite_y2)) ||
		    ((line >= sprite_y_offscreen) && (line < sprite_y_offscreen2)))
		{
			int32_t source_y = line - sprite_y;

			if ((sprite_y > 240) && (sprite_y <= 256) && (line < vdp.height))
				source_y = line - sprite_y_offscreen;

			if (count == 8)
			{
				if (request_status && (line < vdp.height))
					vdp_request_sprite_overflow(line);
				if (option.spritelimit)
					return count;
			}

			dst[count].yrange = (uint16_t)source_y;
			dst[count].xpos = st[0x80 + (i << 1)];
			dst[count].attr = st[0x81 + (i << 1)];
			++count;
		}
	}

	return count;
}


static void sms_timed_capture_sprites_ex(int32_t line, int request_status)
{
	sms_timed_object_count = parse_satb_timed_to(sms_timed_object_info, line, &sms_timed_sprite_mode, request_status);
	sms_timed_sprite_line = line;
}

static void sms_timed_capture_sprites(int32_t line)
{
	sms_timed_capture_sprites_ex(line, 1);
}

static void sms_timed_capture_sprites_for_pipeline_only(int32_t line)
{
	sms_timed_capture_sprites_ex(line, 0);
}

static void sms_timed_render_captured_sprites(int32_t line)
{
	object_info_t *saved_info;
	uint8_t *saved_count;
	uint8_t saved_mode;

	if (sms_timed_sprite_line != line)
		return;

	saved_info = active_object_info;
	saved_count = active_object_index_count;
	saved_mode = vdp.sprite_mode_draw;

	active_object_info = sms_timed_object_info;
	active_object_index_count = &sms_timed_object_count;
	vdp.sprite_mode_draw = sms_timed_sprite_mode;
	render_obj_sms(line);

	active_object_info = saved_info;
	active_object_index_count = saved_count;
	vdp.sprite_mode_draw = saved_mode;
}

static void update_bg_pattern_cache(void)
{
	int32_t i;
	uint8_t x, y;
	uint16_t name;

	if(!bg_list_index) return;

	for(i = 0; i < bg_list_index; i++)
	{
		name = bg_name_list[i];
		bg_name_list[i] = 0;

		for(y = 0; y < 8; y++)
		{
			if(bg_name_dirty[name] & (1 << y))
			{
				uint8_t *dst = &bg_pattern_cache[name << 6];
				uint16_t bp01 = *(uint16_t *)&vdp.vram[(name << 5) | (y << 2) | (0)];
				uint16_t bp23 = *(uint16_t *)&vdp.vram[(name << 5) | (y << 2) | (2)];
				uint32_t temp = (bp_lut[bp01] >> 2) | (bp_lut[bp23]);

				for(x = 0; x < 8; x++)
				{
					uint8_t c = (temp >> (x << 2)) & 0x0F;
					dst[0x00000 | (y << 3) | (x)] = (c);
					dst[0x08000 | (y << 3) | (x ^ 7)] = (c);
					dst[0x10000 | ((y ^ 7) << 3) | (x)] = (c);
					dst[0x18000 | ((y ^ 7) << 3) | (x ^ 7)] = (c);
				}
			}
		}
		bg_name_dirty[name] = 0;
	}
	
	bg_list_index = 0;
}

static int32_t lightgun_cursor_port(void)
{
	if (!option.lightgun_cursor) return -1;
	if (sms.device[0] == DEVICE_LIGHTGUN) return 0;
	if (sms.device[1] == DEVICE_LIGHTGUN) return 1;
	return -1;
}

static int32_t lightgun_cursor_line_info(int32_t line, int32_t *x_out, int32_t *dy_out)
{
	int32_t port = lightgun_cursor_port();
	if (port < 0) return 0;
	int32_t x = input.analog[port][0];
	int32_t y = input.analog[port][1];
	int32_t dy = line - y;
	if (x < 0) x = 0;
	if (x > 255) x = 255;
	if (dy < -7 || dy > 7) return 0;
	*x_out = x;
	*dy_out = dy;
	return 1;
}

#ifndef MULTIREXZ80_RENDER_32BPP
static void draw_lightgun_cursor_16(uint16_t *p, int32_t width, int32_t line)
{
	int32_t x, dy, i;
	uint16_t white = MAKE_PIXEL(255, 255, 255);
	uint16_t black = MAKE_PIXEL(0, 0, 0);
	if (!lightgun_cursor_line_info(line, &x, &dy)) return;
	if (dy == 0 || dy == -1 || dy == 1)
	{
		for (i = x - 8; i <= x + 8; i++)
			if (i >= 0 && i < width) p[i] = (dy == 0) ? white : black;
	}
	if (x >= 0 && x < width) p[x] = white;
	if (x - 1 >= 0 && x - 1 < width) p[x - 1] = black;
	if (x + 1 >= 0 && x + 1 < width) p[x + 1] = black;
}
#else
static void draw_lightgun_cursor_32(uint32_t *p, int32_t width, int32_t line)
{
	int32_t x, dy, i;
	uint32_t white = MAKE_PIXEL(255, 255, 255);
	uint32_t black = MAKE_PIXEL(0, 0, 0);
	if (!lightgun_cursor_line_info(line, &x, &dy)) return;
	if (dy == 0 || dy == -1 || dy == 1)
	{
		for (i = x - 8; i <= x + 8; i++)
			if (i >= 0 && i < width) p[i] = (dy == 0) ? white : black;
	}
	if (x >= 0 && x < width) p[x] = white;
	if (x - 1 >= 0 && x - 1 < width) p[x - 1] = black;
	if (x + 1 >= 0 && x + 1 < width) p[x + 1] = black;
}
#endif

#ifndef _8BPP_COLOR
#ifndef MULTIREXZ80_RENDER_32BPP
#ifndef MULTIREXZ80_DISABLE_FAST_REMAP
static void rebuild_remap16_pair_table(void)
{
	uint32_t a, b;
	for (a = 0; a < 0x100; a++)
	{
		uint16_t pa = pixel[a & PIXEL_MASK];
		for (b = 0; b < 0x100; b++)
		{
			uint16_t pb = pixel[b & PIXEL_MASK];
#ifdef LSB_FIRST
			remap16_pair_table[a | (b << 8)] = (uint32_t)pa | ((uint32_t)pb << 16);
#else
			remap16_pair_table[a | (b << 8)] = ((uint32_t)pa << 16) | (uint32_t)pb;
#endif
		}
	}
	remap16_pair_dirty = 0;
}
#endif

static void remap_8_to_16(int32_t line)
{
	int32_t i;
	uint8_t *src = internal_buffer;
	uint16_t *p = (uint16_t *)&bitmap.data[(line * bitmap.pitch)];
	int32_t width = (bitmap.viewport.w) + 2 * bitmap.viewport.x;
	
	LOCK_VIDEO

	if ((sms.console == CONSOLE_GG) && option.lcd_persistence && lcd_persistence_alloc())
	{
		uint32_t pitch_pixels = bitmap.pitch >> 1;
		for(i = 0; i < width; i++)
		{
			const uint8_t *line_cram = sms_cram_for_output_line(line);
			uint16_t out = line_cram ? sms_native_pixel_from_cram_byte(line_cram[src[i] & PIXEL_MASK]) : pixel[src[i] & PIXEL_MASK];
			uint32_t idx = (uint32_t)line * pitch_pixels + (uint32_t)i;
			if (idx < LCD_PERSISTENCE_MAX_PIXELS)
			{
				if (lcd_persistence_valid[idx])
					out = lcd_blend_rgb565(out, (uint16_t)lcd_persistence_buffer[idx]);
				lcd_persistence_buffer[idx] = out;
				lcd_persistence_valid[idx] = 1;
			}
			p[i] = out;
		}
	}
	else
	{
#ifndef MULTIREXZ80_DISABLE_FAST_REMAP
		const uint8_t *line_cram = sms_cram_for_output_line(line);
		if (line_cram)
		{
			for(i = 0; i < width; i++)
				p[i] = sms_native_pixel_from_cram_byte(line_cram[src[i] & PIXEL_MASK]);
		}
		else
		{
			uint32_t *dst32 = (uint32_t *)(void *)p;
			int32_t pairs = width >> 1;
			if (remap16_pair_dirty) rebuild_remap16_pair_table();
			for (i = 0; i < pairs; i++)
			{
				uint32_t key = (uint32_t)src[(i << 1) + 0] | ((uint32_t)src[(i << 1) + 1] << 8);
				write_dword(&dst32[i], remap16_pair_table[key]);
			}
			if (width & 1)
				p[width - 1] = pixel[src[width - 1] & PIXEL_MASK];
		}
#else
		for(i = 0; i < width; i++)
			p[i] = pixel[src[i] & PIXEL_MASK];
#endif
	}

	draw_lightgun_cursor_16(p, width, line);
	
	UNLOCK_VIDEO
}
#else
static void remap_8_to_32(int32_t line)
{
	int32_t i;
	uint8_t *src = internal_buffer;
	uint32_t *p = (uint32_t *)&bitmap.data[(line * bitmap.pitch)];
	int32_t width = (bitmap.viewport.w) + 2 * bitmap.viewport.x;
	
	LOCK_VIDEO

	if ((sms.console == CONSOLE_GG) && option.lcd_persistence && lcd_persistence_alloc())
	{
		uint32_t pitch_pixels = bitmap.pitch >> 2;
		for(i = 0; i < width; i++)
		{
			const uint8_t *line_cram = sms_cram_for_output_line(line);
			uint32_t out = line_cram ? sms_native_pixel_from_cram_byte(line_cram[src[i] & PIXEL_MASK]) : pixel[src[i] & PIXEL_MASK];
			uint32_t idx = (uint32_t)line * pitch_pixels + (uint32_t)i;
			if (idx < LCD_PERSISTENCE_MAX_PIXELS)
			{
				if (lcd_persistence_valid[idx])
					out = lcd_blend_xrgb8888(out, lcd_persistence_buffer[idx]);
				lcd_persistence_buffer[idx] = out;
				lcd_persistence_valid[idx] = 1;
			}
			p[i] = out;
		}
	}
	else
	{
		const uint8_t *line_cram = sms_cram_for_output_line(line);
		if (line_cram)
		{
			for(i = 0; i < width; i++)
				p[i] = sms_native_pixel_from_cram_byte(line_cram[src[i] & PIXEL_MASK]);
		}
		else
		{
			for(i = 0; i < width; i++)
				p[i] = pixel[src[i] & PIXEL_MASK];
		}
	}

	draw_lightgun_cursor_32(p, width, line);
	
	UNLOCK_VIDEO
}
#endif
#endif
