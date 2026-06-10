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
 *  Video Display Processor (VDP) emulation.
 *
 ******************************************************************************/
/*
 * See git commit history for more information.
 * - Gameblabla
 * March 15th 2019 : Fix yet more issues with datatypes in vdp.c
 * March 14th 2019 : Fix issues as reported by Clang. (in vdp_write)
 * March 13th 2019 : Minor fixes as part of the CrabZ80's revert. (mostly whitepacing)
 * March 11th 2019 : Fixed scrolling issues with Gauntlet. Fixed PAL issues too.
 * March 9th 2019 : Set VDP register to 0xE0 after multiple testings against BIOSes. Fixes Sonic's Edusoft and i think California Games 2.
 * March 7th 2019 : Whitepacing and minor fixes.
*/

#include "shared.h"
#include "hvc.h"

/* Mark a pattern as dirty */
#define MARK_BG_DIRTY(addr) render_mark_bg_dirty_chip(0, (addr))

static int vdp_addr_in_current_sat(uint16_t index);
static void vdp_invalidate_sprite_status_pipeline(uint16_t index);
static void vdp_record_vram_write(uint16_t index, uint8_t old_value);

void vdp_vram_direct_write(uint16_t address, uint8_t data)
{
	int32_t index;
	int32_t cycles_per_line = system_cycles_per_line();

	vdp_render_scanline_if_due();
	if (!vdp_timed_render_active() && (((z80_get_elapsed_cycles() + 1) / cycles_per_line) > vdp.line))
	{
		render_line((vdp.line + 1) % vdp.lpf);
	}

	index = address & 0x3FFF;
	if (data != vdp.vram[index])
	{
		vdp_record_vram_write((uint16_t)index, vdp.vram[index]);
		vdp.vram[index] = data;
		render_mark_bg_dirty_chip(0, index);
		vdp_invalidate_sprite_status_pipeline((uint16_t)index);
	}
}

/*** Vertical Counter Tables ***/
extern uint8_t *vc_table[2][3];

/* VDP context */
vdp_t vdp;
vdp_t *vdp2_ptr = NULL;

static uint8_t *systeme_vram_banks = NULL;
static uint8_t systeme_active_vram_bank[2];
static uint8_t sms_vdp_line_rendered;
static uint8_t sms_vdp_line_render_skip;

#define SMS_VDP_WRITE_LOG_CAP 256
typedef struct
{
    uint16_t addr;
    uint8_t old_value;
    int16_t dot;
} sms_vdp_write_log_t;
static sms_vdp_write_log_t sms_vdp_write_log[SMS_VDP_WRITE_LOG_CAP];
static uint16_t sms_vdp_write_log_count;

#define SMS_VDP_REG_WRITE_LOG_CAP 128
typedef struct
{
    uint8_t reg;
    uint8_t old_value;
    int16_t dot;
} sms_vdp_reg_write_log_t;
static sms_vdp_reg_write_log_t sms_vdp_reg_write_log[SMS_VDP_REG_WRITE_LOG_CAP];
static uint16_t sms_vdp_reg_write_log_count;

static void vdp_record_vram_write(uint16_t index, uint8_t old_value)
{
    int32_t cycles_per_line;
    int32_t dot;

    if (!vdp_timed_render_active())
        return;

    cycles_per_line = system_cycles_per_line();
    if (cycles_per_line <= 0)
        return;

    dot = z80_get_elapsed_cycles() % cycles_per_line;
    if (sms_vdp_write_log_count < SMS_VDP_WRITE_LOG_CAP)
    {
        sms_vdp_write_log[sms_vdp_write_log_count].addr = (uint16_t)(index & 0x3FFF);
        sms_vdp_write_log[sms_vdp_write_log_count].old_value = old_value;
        sms_vdp_write_log[sms_vdp_write_log_count].dot = (int16_t)dot;
        sms_vdp_write_log_count++;
    }
}

uint8_t vdp_vram_byte_for_bg_fetch(uint16_t address, int32_t fetch_cycle)
{
    uint16_t addr = (uint16_t)(address & 0x3FFF);
    uint8_t value = vdp.vram[addr];
    int i;

    if (!vdp_timed_render_active())
        return value;

    for (i = (int)sms_vdp_write_log_count - 1; i >= 0; i--)
    {
        if ((sms_vdp_write_log[i].addr == addr) && (sms_vdp_write_log[i].dot > fetch_cycle))
            value = sms_vdp_write_log[i].old_value;
    }
    return value;
}

static void vdp_record_reg_write(uint8_t reg, uint8_t old_value)
{
    int32_t dot;

    if (!vdp_timed_render_active())
        return;

    dot = z80_get_elapsed_cycles() % system_cycles_per_line();
    if (dot < 0)
        dot = 0;
    if (dot > 32767)
        dot = 32767;

    if (sms_vdp_reg_write_log_count < SMS_VDP_REG_WRITE_LOG_CAP)
    {
        sms_vdp_reg_write_log[sms_vdp_reg_write_log_count].reg = (uint8_t)(reg & 0x0f);
        sms_vdp_reg_write_log[sms_vdp_reg_write_log_count].old_value = old_value;
        sms_vdp_reg_write_log[sms_vdp_reg_write_log_count].dot = (int16_t)dot;
        sms_vdp_reg_write_log_count++;
    }
}

uint8_t vdp_reg_byte_for_bg_fetch(uint8_t reg, int32_t fetch_cycle)
{
    uint8_t r = (uint8_t)(reg & 0x0f);
    uint8_t value = vdp.reg[r];
    int i;

    if (!vdp_timed_render_active())
        return value;

    for (i = (int)sms_vdp_reg_write_log_count - 1; i >= 0; i--)
    {
        if ((sms_vdp_reg_write_log[i].reg == r) && (sms_vdp_reg_write_log[i].dot > fetch_cycle))
            value = sms_vdp_reg_write_log[i].old_value;
    }
    return value;
}

static int vdp_addr_in_current_sat(uint16_t index)
{
    uint16_t sat = (uint16_t)(vdp.satb & 0x3F00);
    return (index >= sat) && (index < (uint16_t)(sat + 0x0100));
}

static void vdp_invalidate_sprite_status_pipeline(uint16_t index)
{
    if (vdp_addr_in_current_sat(index))
    {
        vdp.spr_col_pending = 0;
        vdp.spr_ovr_pending = 0;
    }
}

#define SMS_VDP_SPRITE_MODE_LATCH_CYCLE (CYCLES_PER_LINE - 15 + 2)

int vdp_gamegear_timing_active(void)
{
	/* CONSOLE_GGMS is SMS compatibility mode reached through the Game Gear
	 * frontend. It still uses the SMS 256-pixel timing model here; only native
	 * Game Gear mode gets the GG-specific VDP event phase. */
	return sms.console == CONSOLE_GG;
}

int vdp_render_event_cycle(void)
{
	/* The line-buffered renderer samples the visible GG background at the early
	 * display-fetch point.  Tarzan updates HUD tile pattern bytes during the
	 * visible status-bar scanlines; sampling the whole line at the later sprite
	 * timing point incorrectly lets those writes skip/replace rows of the HUD. */
	return vdp_gamegear_timing_active() ? ((vdp.reg[8] || vdp.reg[9]) ? 28 : 186) : 195;
}

int vdp_hint_event_cycle(void)
{
	return vdp_gamegear_timing_active() ? 28 : 15;
}

int vdp_xscroll_event_cycle(void)
{
	/* Register 8 is not a live horizontal-scroll value.  It is latched near
	 * the beginning of each scanline, before the eventual render event.  VDPTEST
	 * writes around this boundary; without a separate latched value the left
	 * column in the X-scroll latch page gets a visible discontinuity. */
	return 0;
}

void vdp_latch_hscroll(void)
{
	vdp.hscroll = vdp.reg[8];
	/* Match the Genesis Plus GX SMS renderer model: CRAM writes are
	 * visible to scanlines only after the line has latched/fetched its
	 * palette state.  The actual remap still happens when the line is
	 * emitted, but it uses this per-scanline CRAM snapshot rather than
	 * the later global CRAM state. */
	memcpy(vdp.cram_line_latch, vdp.cram, sizeof(vdp.cram_line_latch));
}

static int vdp_sms_top_latch_console(void)
{
	return (sms.console == CONSOLE_SMS) || (sms.console == CONSOLE_SMS2);
}

static int vdp_sms_top_latch_capture_window(void)
{
	/* This latch is the pre-active-display top/status-row fetch state.
	 * Active-display splits (for example the Fantastic Dizzy language-logo
	 * wave) must remain normal per-line register/palette effects. */
	return vdp_sms_top_latch_console() && vdp_timed_render_active() &&
	       (vdp.line >= vdp.height);
}

static void vdp_top_cram_capture_write(uint8_t index, uint8_t data)
{
	if (!vdp_sms_top_latch_capture_window())
		return;
	if (!vdp.cram_top_next_armed || (vdp.reg[1] & 0x40))
		return;
	if (!vdp.cram_top_capture_active)
	{
		memcpy(vdp.cram_top_next, vdp.cram, sizeof(vdp.cram_top_next));
		vdp.cram_top_capture_active = 1;
	}
	vdp.cram_top_next[index & 0x3f] = data;
}

void vdp_frame_scroll_latch_start(void)
{
	/* The SMS-family VDP has an early pre-active-display fetch/latch window
	 * for the first status tile rows. Keep that window as a normal latched
	 * copy of R8: later register writes update the live per-scanline latch,
	 * but they do not rewrite the already-prefetched top-row state. */
	{
		uint8_t hscroll_top_valid = vdp.hscroll_top_next_valid;
		uint8_t top_transition_blank = (uint8_t)((vdp.hscroll_top_next_armed & 0x40) ? 0x20 : 0);
		vdp.hscroll_top = hscroll_top_valid ? vdp.hscroll_top_next : vdp.reg[8];
		vdp.hscroll_top_next = vdp.reg[8];
		vdp.hscroll_top_next_valid = 0;
		/* bit 0: arm next disabled-display R8 capture
		 * bit 1: next-frame top prefetch closed by display-enable rising edge
		 * bit 5: current-frame top/playfield transition blank
		 * bit 6: next-frame transition blank, produced by a later display-disable
		 *        edge after the top prefetch has already closed
		 * bit 7: current-frame top hscroll latch is valid */
		vdp.hscroll_top_next_armed = (uint8_t)(1 | (hscroll_top_valid ? 0x80 : 0) | top_transition_blank);
	}

	/* Some Codemasters SMS games split the top status band by loading a HUD
	 * palette during the first disabled-display window, then replacing CRAM for
	 * the scrolling playfield before normal active rendering.  The top band uses
	 * that first disabled-window CRAM state; keep it separate from live CRAM. */
	if (vdp_sms_top_latch_console() && vdp.cram_top_next_valid)
	{
		memcpy(vdp.cram_top, vdp.cram_top_next, sizeof(vdp.cram_top));
		vdp.cram_top_valid = 1;
	}
	else
	{
		vdp.cram_top_valid = 0;
	}
	memcpy(vdp.cram_top_next, vdp.cram, sizeof(vdp.cram_top_next));
	vdp.cram_top_next_valid = 0;
	vdp.cram_top_next_armed = 1;
	vdp.cram_top_capture_active = 0;
}


static int vdp_sms_or_native_gg_console(void)
{
	/* Use the cycle-aware Mode 4 path for SMS/SMS2 and native Game Gear.
	 * CONSOLE_GGMS is an SMS compatibility frontend mode, so it should not
	 * opt into GG-specific timing or add another VDP-special case. */
	return (sms.console == CONSOLE_SMS) ||
	       (sms.console == CONSOLE_SMS2) ||
	       (sms.console == CONSOLE_GG);
}

int vdp_timed_render_active(void)
{
	return vdp_sms_or_native_gg_console();
}

static void vdp_maybe_update_hscroll_on_reg8_write(uint8_t data)
{
	int32_t cycles_per_line;
	int32_t dot;

	if (!vdp_timed_render_active())
		return;

	cycles_per_line = system_cycles_per_line();
	if (cycles_per_line <= 0)
		return;

	dot = z80_get_elapsed_cycles() % cycles_per_line;
	/* The Z80 core reports port writes when the OUT instruction retires.
	 * Around the reg-8 latch boundary this is later than the VDP
	 * sample point.  Writes up to the first duplicated HCounter $F3 slot feed the
	 * undrawn scanline; writes at the second $F3 slot have missed it. */
	if ((dot <= 13) && (dot < vdp_render_event_cycle()))
		vdp.hscroll = data;
}

void vdp_prepare_scanline(int32_t line, int skip_render)
{
	vdp.line = line;
	sms_vdp_write_log_count = 0;
	sms_vdp_reg_write_log_count = 0;
	sms_vdp_line_render_skip = (uint8_t)(skip_render ? 1 : 0);
	sms_vdp_line_rendered = (uint8_t)(skip_render ? 1 : 0);
}

void vdp_render_scanline_now(void)
{
	if (!vdp_timed_render_active())
		return;
	if (sms_vdp_line_rendered || sms_vdp_line_render_skip)
		return;
	render_line(vdp.line);
	sms_vdp_line_rendered = 1;
}

void vdp_render_scanline_if_due(void)
{
	int32_t cycles_per_line;
	int32_t cyc;
	int32_t line;
	int32_t dot;

	if (!vdp_timed_render_active())
		return;
	if (sms_vdp_line_rendered || sms_vdp_line_render_skip)
		return;

	cycles_per_line = system_cycles_per_line();
	if (cycles_per_line <= 0)
		return;

	cyc = z80_get_elapsed_cycles();
	line = (cyc / cycles_per_line) % vdp.lpf;
	dot = cyc % cycles_per_line;
	if ((line == vdp.line) && (dot >= vdp_render_event_cycle()))
	{
		render_line(vdp.line);
		sms_vdp_line_rendered = 1;
	}
}

static uint8_t vdp_sprite_mode_from_regs(const vdp_t *ctx)
{
	return (uint8_t)((ctx->reg[1] & 0x03) | (ctx->reg[0] & 0x08));
}

void vdp_latch_sprite_mode(void)
{
	vdp.sprite_mode_latch = vdp_sprite_mode_from_regs(&vdp);
}

static int vdp_event_reached(int32_t event_line, int32_t event_cycle)
{
    int32_t cycles_per_line = system_cycles_per_line();
    int32_t cyc;
    int32_t line;
    int32_t dot;

    if (cycles_per_line <= 0)
        return 0;

    cyc = z80_get_elapsed_cycles();
    line = (cyc / cycles_per_line) % vdp.lpf;
    dot = cyc % cycles_per_line;

    if (line == event_line)
        return dot >= event_cycle;

    /* Sprite status requests are generated for the current frame and reset at
     * frame start, so a later scanline number means the event has passed. */
    return line > event_line;
}

void vdp_request_sprite_collision(int32_t line, int32_t x)
{
    int32_t offset;
    int32_t cycle;

    if ((vdp.status & 0x20) || vdp.spr_col_pending)
        return;

    if (x < 0) x = 0;
    if (x > 255) x = 255;

    offset = vdp_gamegear_timing_active() ? 22 : 10;
    cycle = offset + ((x * system_cycles_per_line()) / 256);
    if (cycle >= system_cycles_per_line())
        cycle = system_cycles_per_line() - 1;

    vdp.spr_col_line = line;
    vdp.spr_col_cycle = cycle;
    vdp.spr_col_pending = 1;
    vdp.spr_col = (line << 16) | cycle;
}

void vdp_request_sprite_overflow(int32_t line)
{
    int32_t cycle;

    if ((vdp.status & 0x40) || vdp.spr_ovr_pending)
        return;

    cycle = vdp_gamegear_timing_active() ? 27 : 15;
    vdp.spr_ovr_line = line;
    vdp.spr_ovr_cycle = cycle;
    vdp.spr_ovr_pending = 1;
}

static void vdp_update_sprite_status_for_now(void)
{
    if (vdp.spr_col_pending && vdp_event_reached(vdp.spr_col_line, vdp.spr_col_cycle))
    {
        vdp.status |= 0x20;
        vdp.spr_col_pending = 0;
    }

    if (vdp.spr_ovr_pending && vdp_event_reached(vdp.spr_ovr_line, vdp.spr_ovr_cycle))
    {
        vdp.status |= 0x40;
        vdp.spr_ovr_pending = 0;
    }
}

void vdp_update_status_end_of_scanline(void)
{
    vdp_update_sprite_status_for_now();
}

static int vdp_sprite_mode_write_is_before_latch(void)
{
	int32_t cycles_per_line = system_cycles_per_line();
	int32_t dot;

	if (cycles_per_line <= 0)
		return 1;

	/* The SMS/GG VDP samples sprite size/shift shortly before the next
	 * scanline.  Writes after that point must not retroactively alter the
	 * already-scanned sprite line.  10 master pixels correspond to about 15
	 * Z80 cycles in the 228-cycle SMS line model, matching the timing used by
	 * the PicoDrive fix this is based on. */
	dot = z80_get_elapsed_cycles() % cycles_per_line;
	return dot < SMS_VDP_SPRITE_MODE_LATCH_CYCLE;
}

static void vdp_latch_sprite_mode_if_visible_to_next_line(void)
{
	if (vdp_sprite_mode_write_is_before_latch())
		vdp_latch_sprite_mode();
}

#define SYSTEME_VRAM_BANK_SIZE 0x4000
#define SYSTEME_VRAM_BANK_COUNT 4

static uint8_t *systeme_vram_bank_ptr(int chip, int bank)
{
	return systeme_vram_banks + (((chip & 1) << 1) | (bank & 1)) * SYSTEME_VRAM_BANK_SIZE;
}

static int systeme_vdp_runtime_alloc(void)
{
	if (!vdp2_ptr)
	{
		vdp2_ptr = (vdp_t *)calloc(1, sizeof(vdp_t));
		if (!vdp2_ptr)
			return 0;
	}

	if (!systeme_vram_banks)
	{
		systeme_vram_banks = (uint8_t *)calloc(SYSTEME_VRAM_BANK_COUNT, SYSTEME_VRAM_BANK_SIZE);
		if (!systeme_vram_banks)
		{
			free(vdp2_ptr);
			vdp2_ptr = NULL;
			return 0;
		}
	}

	return 1;
}

static void systeme_vdp_runtime_free(void)
{
	if (systeme_vram_banks)
	{
		free(systeme_vram_banks);
		systeme_vram_banks = NULL;
	}
	if (vdp2_ptr)
	{
		free(vdp2_ptr);
		vdp2_ptr = NULL;
	}
}

static vdp_t *systeme_vdp_context(int chip)
{
	return chip ? vdp2_ptr : &vdp;
}

static void systeme_vdp_irq_refresh(void)
{
	int asserted = 0;

	if ((vdp.vint_pending && (vdp.reg[1] & 0x20)) ||
	    (vdp.hint_pending && (vdp.reg[0] & 0x10)) ||
	    (vdp2.vint_pending && (vdp2.reg[1] & 0x20)) ||
	    (vdp2.hint_pending && (vdp2.reg[0] & 0x10)))
		asserted = 1;

	z80_set_irq_line(vdp.irq, asserted ? ASSERT_LINE : CLEAR_LINE);
}

static void systeme_vdp_update_mode(vdp_t *ctx)
{
	int32_t m1 = (ctx->reg[1] >> 4) & 1;
	int32_t m3 = (ctx->reg[1] >> 3) & 1;
	int32_t m2 = (ctx->reg[0] >> 1) & 1;
	int32_t m4 = (ctx->reg[0] >> 2) & 1;
	ctx->mode = (m4 << 3 | m3 << 2 | m2 << 1 | m1 << 0);

	if (sms.console >= CONSOLE_SMS2)
	{
		switch (ctx->mode)
		{
			case 0x0B:
				ctx->height = 224;
				ctx->extended = 1;
				ctx->ntab = ((ctx->reg[2] << 10) & 0x3000) | 0x0700;
				break;
			case 0x0E:
				ctx->height = 240;
				ctx->extended = 2;
				ctx->ntab = ((ctx->reg[2] << 10) & 0x3000) | 0x0700;
				break;
			default:
				ctx->height = 192;
				ctx->extended = 0;
				ctx->ntab = (ctx->reg[2] << 10) & 0x3800;
				if ((ctx->mode & 0x0B) == 0x09) ctx->mode = 1;
				break;
		}
	}
	else
	{
		ctx->height = 192;
		ctx->extended = 0;
		ctx->ntab = (ctx->reg[2] << 10) & 0x3800;
		if ((ctx->mode & 0x09) == 0x09) ctx->mode = 1;
	}

	ctx->pn = (ctx->reg[2] << 10) & 0x3C00;
}

static void systeme_vdp_select_visible_bank(int chip, uint8_t bank)
{
	vdp_t *ctx = systeme_vdp_context(chip);
	uint8_t *src;

	if (!ctx || !systeme_vram_banks)
		return;

	bank &= 1;
	if (systeme_active_vram_bank[chip] == bank)
		return;

	src = systeme_vram_bank_ptr(chip, bank);
	systeme_active_vram_bank[chip] = bank;
	memcpy(ctx->vram, src, SYSTEME_VRAM_BANK_SIZE);
	render_invalidate_bg_cache();
}

void systeme_vdp_bank_w(uint8_t data)
{
	systeme_vdp_select_visible_bank(0, (data >> 7) & 1);
	systeme_vdp_select_visible_bank(1, (data >> 6) & 1);
}


/* Initialize VDP emulation */
void vdp_init(void)
{
  /* display area */
	if ((sms.console == CONSOLE_GG) && (!option.extra_gg))
	{
		bitmap.viewport.w = 160;
		bitmap.viewport.x = 48;
	}
	else
	{
		bitmap.viewport.w = 256;
		bitmap.viewport.x = 0;
	}

	/* number of scanlines */
	vdp.lpf = sms.display ? 313 : 262;

	/* reset viewport */
	viewport_check();
	bitmap.viewport.changed = 1;
}

void vdp_shutdown(void)
{
	systeme_vdp_runtime_free();
}

  
/* Reset VDP emulation */
void vdp_reset(void)
{
	/* reset VDP structure */
	memset(&vdp, 0, sizeof(vdp_t));
	if (sms.console != CONSOLE_SYSTEME)
		systeme_vdp_runtime_free();

	/* number of scanlines */
	vdp.lpf = sms.display ? 313 : 262;

	/* VDP registers default values (usually set by BIOS) */
	/* Tested on Megadrive and it does not initiliaze the VDP registers. */
	if ((sms.console != CONSOLE_SYSTEME) && IS_SMS && (bios.enabled != 3))
	{
		vdp.reg[0]  = 0x36; 
		vdp.reg[1]  = 0xE0;
		vdp.reg[2]  = 0xFF;
		vdp.reg[3]  = 0xFF;
		vdp.reg[4]  = 0xFF;
		vdp.reg[5]  = 0xFF;
		vdp.reg[6]  = 0xFB;
		vdp.reg[10] = 0xFF;
	}

	/* VDP interrupt */
	if (sms.console == CONSOLE_COLECO)
		vdp.irq = INPUT_LINE_NMI;
	else
		vdp.irq = INPUT_LINE_IRQ0;

	/* reset VDP viewport */
	viewport_check();

	/* reset VDP internals */
	vdp.ct    = (vdp.reg[3] <<  6) & 0x3FC0;
	vdp.pg    = (vdp.reg[4] << 11) & 0x3800;
	vdp.satb  = (vdp.reg[5] << 7) & 0x3F00;
	vdp.sa    = (vdp.reg[5] <<  7) & 0x3F80;
	vdp.sg    = (vdp.reg[6] << 11) & 0x3800;
	vdp.bd    = (vdp.reg[7] & 0x0F);
	vdp.hscroll = vdp.reg[8];
	vdp.hscroll_top = vdp.reg[8];
	vdp.hscroll_top_next = vdp.reg[8];
	vdp.hscroll_top_next_valid = 0;
	vdp.hscroll_top_next_armed = 1;
	memcpy(vdp.cram_top, vdp.cram, sizeof(vdp.cram_top));
	memcpy(vdp.cram_top_next, vdp.cram, sizeof(vdp.cram_top_next));
	memcpy(vdp.cram_line_latch, vdp.cram, sizeof(vdp.cram_line_latch));
	vdp.cram_top_valid = 0;
	vdp.cram_top_next_valid = 0;
	vdp.cram_top_next_armed = 1;
	vdp.cram_top_capture_active = 0;
	vdp.sprite_mode_latch = vdp_sprite_mode_from_regs(&vdp);
	vdp.sprite_mode_draw = vdp.sprite_mode_latch;

	bitmap.viewport.changed = 1;

	if (sms.console == CONSOLE_SYSTEME)
	{
		if (!systeme_vdp_runtime_alloc())
		{
			fprintf(stderr, "System E VDP allocation failed\n");
			abort();
		}
		memcpy(&vdp2, &vdp, sizeof(vdp_t));
		memset(systeme_vram_banks, 0, SYSTEME_VRAM_BANK_COUNT * SYSTEME_VRAM_BANK_SIZE);
		systeme_active_vram_bank[0] = 0;
		systeme_active_vram_bank[1] = 0;
		memcpy(systeme_vram_bank_ptr(0, 0), vdp.vram, SYSTEME_VRAM_BANK_SIZE);
		memcpy(systeme_vram_bank_ptr(1, 0), vdp2.vram, SYSTEME_VRAM_BANK_SIZE);
		render_invalidate_bg_cache();
	}
}


void viewport_check(void)
{
	int32_t i;
	int32_t m1 = (vdp.reg[1] >> 4) & 1;
	int32_t m3 = (vdp.reg[1] >> 3) & 1;
	int32_t m2 = (vdp.reg[0] >> 1) & 1;
	int32_t m4 = (vdp.reg[0] >> 2) & 1;
	vdp.mode = (m4 << 3 | m3 << 2 | m2 << 1 | m1 << 0);
	
	/* Check for extended modes */
	if (sms.console >= CONSOLE_SMS2)
	{
		switch (vdp.mode)
		{
		  case 0x0B:  /* Mode 4 extended (224 lines) */
			vdp.height = 224;
			vdp.extended = 1;
			vdp.ntab = ((vdp.reg[2] << 10) & 0x3000) | 0x0700;
			break;

		  case 0x0E:  /* Mode 4 extended (240 lines) */
			vdp.height = 240;
			vdp.extended = 2;
			vdp.ntab = ((vdp.reg[2] << 10) & 0x3000) | 0x0700;
			break;

		  default:  /* Mode 4 (192 lines) */
			vdp.height = 192;
			vdp.extended = 0;
			vdp.ntab = (vdp.reg[2] << 10) & 0x3800;

			/* invalid text mode (Mode 4) */
			if ((vdp.mode & 0x0B) == 0x09) vdp.mode = 1;
			break;
		}
	}
	else
	{
		/* always use Mode 4 (192 lines) */
		vdp.height = 192;
		vdp.extended = 0;
		vdp.ntab = (vdp.reg[2] << 10) & 0x3800;
		/* invalid text mode (Mode 4) */
		if ((vdp.mode & 0x09) == 0x09) vdp.mode = 1;
	}

	/* update display area */
	if ((sms.console != CONSOLE_GG) || option.extra_gg)
	{
		if(bitmap.viewport.h != vdp.height)
		{
			bitmap.viewport.oh = bitmap.viewport.h;
			bitmap.viewport.h = vdp.height;
			bitmap.viewport.changed = 1;
		}
	}
	else
	{
		/* GG display area is fixed */
		bitmap.viewport.h = 144;
	}

	/* update border area */
	bitmap.viewport.y = 0;

	/* check if this is switching in/out of tms */
	if (IS_SMS || IS_GG)
	{
		/* Restore palette */
		for(i = 0; i < PALETTE_SIZE; i++)
		{
			palette_sync(i);
		}
	}

	vdp.pn = (vdp.reg[2] << 10) & 0x3C00;

	if (vdp.mode & 8)
	{
		render_bg  = render_bg_sms;
		render_obj = render_obj_sms;
	}
	else
	{
		render_bg  = render_bg_tms;
		render_obj = render_obj_tms;
	}
}


static void vdp_reg_w(uint8_t r, uint8_t d)
{
	uint8_t old_sprite_mode = vdp_sprite_mode_from_regs(&vdp);
	uint8_t old_reg1 = vdp.reg[1];

	/* Store register data.  The renderer can later reconstruct the register
	 * value visible at each background fetch slot by walking this per-line log
	 * backwards from the final value. */
	vdp_record_reg_write(r, vdp.reg[r]);
	vdp.reg[r] = d;
	if ((r == 1) && vdp_sms_top_latch_console() && vdp.cram_top_capture_active &&
	    !(old_reg1 & 0x40) && (d & 0x40))
	{
		vdp.cram_top_next_valid = 1;
		vdp.cram_top_next_armed = 0;
		vdp.cram_top_capture_active = 0;
	}
	if ((r == 1) && vdp_sms_top_latch_capture_window())
	{
		uint8_t old_display = (uint8_t)(old_reg1 & 0x40);
		uint8_t new_display = (uint8_t)(d & 0x40);
		if (!old_display && new_display && vdp.hscroll_top_next_valid)
			vdp.hscroll_top_next_armed |= 0x02;
		else if (old_display && !new_display && vdp.hscroll_top_next_valid &&
		         (vdp.hscroll_top_next_armed & 0x02))
			vdp.hscroll_top_next_armed |= 0x40;
	}
	if (r == 0x08)
	{
		if (vdp_sms_top_latch_capture_window() &&
		    (vdp.hscroll_top_next_armed & 0x01) && !(vdp.reg[1] & 0x40))
		{
			vdp.hscroll_top_next = d;
			vdp.hscroll_top_next_valid = 1;
			vdp.hscroll_top_next_armed &= (uint8_t)~0x01;
		}
		vdp_maybe_update_hscroll_on_reg8_write(d);
	}
	if ((r == 0 || r == 1) && vdp_sms_or_native_gg_console() && (vdp.reg[1] & 0x40) && (vdp.line < vdp.height))
	{
		uint8_t new_sprite_mode = vdp_sprite_mode_from_regs(&vdp);
		uint8_t changed_sprite_mode = (uint8_t)((old_sprite_mode ^ new_sprite_mode) & 0x0B);
		/*
		 * Gearsystem's Madou timing fix is not limited to the 2X zoom bit.
		 * Madou also changes the 8x16 sprite-size bit during active display
		 * (for example reg1 0xE0 <-> 0xE2 around the character scene).
		 *
		 * Keep the old compatibility behaviour for games that change only the
		 * 8x16/shift bits early in the line (Drift 2 depends on that output),
		 * but switch to late rendering when such a write occurs after the real
		 * render event.  Zoom changes remain force-timed because the original
		 * bug class was explicitly 2X sprite zoom.
		 */
		if ((changed_sprite_mode & 0x01) ||
		    (changed_sprite_mode && ((z80_get_elapsed_cycles() % system_cycles_per_line()) >= vdp_render_event_cycle())))
			vdp.timed_render = 1;
	}
	switch(r)
	{
	case 0x00: /* Mode Control No. 1 */
		if(vdp.hint_pending)
		{
			if(d & 0x10) z80_set_irq_line(0, ASSERT_LINE);
			else z80_set_irq_line(0, CLEAR_LINE);
		}
		viewport_check();
		vdp_latch_sprite_mode_if_visible_to_next_line();
	break;
    case 0x01: /* Mode Control No. 2 */
		if(vdp.vint_pending)
		{
			#ifdef SORDM5_EMU
			if (sms.console == CONSOLE_SORDM5)
			{
				if(d & 0x20) sordm5_ctc_vdp_interrupt();
			}
			else
			#endif
			{
				if(d & 0x20) z80_set_irq_line(vdp.irq, ASSERT_LINE);
				else z80_set_irq_line(vdp.irq, CLEAR_LINE);
			}
		}
		viewport_check();
		vdp_latch_sprite_mode_if_visible_to_next_line();
	break;

    case 0x02: /* Name Table A Base Address */
		viewport_check();
	break;

    case 0x03:
		vdp.ct = (d <<  6) & 0x3FC0;
	break;

    case 0x04:
		vdp.pg = (d << 11) & 0x3800;
	break;

    case 0x05: /* Sprite Attribute Table Base Address */
		vdp.satb = (d << 7) & 0x3F00;
		vdp.sa = (d <<  7) & 0x3F80;
	break;

    case 0x06:
		vdp.sg = (d << 11) & 0x3800;
	break;

    case 0x07:
		vdp.bd = (d & 0x0F);
	break;
  }
}


static void systeme_vdp_reg_w(int chip, uint8_t r, uint8_t d)
{
	vdp_t *ctx = systeme_vdp_context(chip);

	ctx->reg[r] = d;
	switch(r)
	{
		case 0x00:
			if(ctx->hint_pending)
			{
				if(d & 0x10) z80_set_irq_line(vdp.irq, ASSERT_LINE);
				else systeme_vdp_irq_refresh();
			}
			if (chip == 0) viewport_check();
			else systeme_vdp_update_mode(ctx);
		break;

		case 0x01:
			if(ctx->vint_pending)
			{
				if(d & 0x20) z80_set_irq_line(vdp.irq, ASSERT_LINE);
				else systeme_vdp_irq_refresh();
			}
			if (chip == 0) viewport_check();
			else systeme_vdp_update_mode(ctx);
		break;

		case 0x02:
			if (chip == 0) viewport_check();
			else systeme_vdp_update_mode(ctx);
		break;

		case 0x03:
			ctx->ct = (d <<  6) & 0x3FC0;
		break;

		case 0x04:
			ctx->pg = (d << 11) & 0x3800;
		break;

		case 0x05:
			ctx->satb = (d << 7) & 0x3F00;
			ctx->sa = (d <<  7) & 0x3F80;
		break;

		case 0x06:
			ctx->sg = (d << 11) & 0x3800;
		break;

		case 0x07:
			ctx->bd = (d & 0x0F);
		break;
	}
}

void systeme_vdp_direct_write(uint8_t bank_select, uint16_t address, uint8_t data)
{
	int chip, bank;
	uint16_t index = address & 0x3FFF;
	uint8_t *bank_ptr;

	if (!systeme_vram_banks)
		return;

	/* Port F7 bits 5-7 select one of eight write windows.  This follows
	 * MAME's System E bank table: odd entries write VDP1/back VRAM and
	 * even entries write VDP2/front VRAM.  The selected write bank is the
	 * opposite polarity of the visible-bank bits latched by bits 7 and 6. */
	if (bank_select & 1)
	{
		chip = 0;
		bank = (bank_select & 4) ? 0 : 1;
	}
	else
	{
		chip = 1;
		bank = (bank_select & 2) ? 0 : 1;
	}

	bank_ptr = systeme_vram_bank_ptr(chip, bank);
	if (data == bank_ptr[index])
		return;

	if (((z80_get_elapsed_cycles() + 1) / system_cycles_per_line()) > vdp.line)
		render_line((vdp.line + 1) % vdp.lpf);

	bank_ptr[index] = data;
	if (systeme_active_vram_bank[chip] == bank)
	{
		vdp_t *ctx = systeme_vdp_context(chip);
		if (ctx)
		{
			ctx->vram[index] = data;
			render_mark_bg_dirty_chip(chip, index);
		}
	}
}

void systeme_vdp_write(int chip, int32_t offset, uint8_t data)
{
	vdp_t *ctx = systeme_vdp_context(chip);
	int32_t index;

	MULTIREXZ80_TRACE_VDP_WRITE(chip ? "systeme-vdp2" : "systeme-vdp1", offset, data);

	if (((z80_get_elapsed_cycles() + 1) / system_cycles_per_line()) > vdp.line)
		render_line((vdp.line + 1) % vdp.lpf);

	switch(offset & 1)
	{
		case 0:
			ctx->pending = 0;
			switch(ctx->code)
			{
				case 0:
				case 1:
				case 2:
					index = (ctx->addr & 0x3FFF);
					if(data != ctx->vram[index])
					{
						ctx->vram[index] = data;
						if (systeme_vram_banks) systeme_vram_bank_ptr(chip, systeme_active_vram_bank[chip])[index] = data;
						render_mark_bg_dirty_chip(chip, ctx->addr);
					}
					ctx->buffer = data;
				break;

				case 3:
					index = (ctx->addr & 0x1F);
					if(data != ctx->cram[index])
					{
						ctx->cram[index] = data;
						palette_sync_chip(chip, index);
					}
					ctx->buffer = data;
				break;
			}
			ctx->addr = (ctx->addr + 1) & 0x3FFF;
		return;

		case 1:
			if(ctx->pending == 0)
			{
				ctx->addr = (ctx->addr & 0x3F00) | (data & 0xFF);
				ctx->latch = data;
				ctx->pending = 1;
			}
			else
			{
				ctx->pending = 0;
				ctx->code = (data >> 6) & 3;
				ctx->addr = (data << 8 | ctx->latch) & 0x3FFF;

				if(ctx->code == 0)
				{
					ctx->buffer = ctx->vram[ctx->addr & 0x3FFF];
					ctx->addr = (ctx->addr + 1) & 0x3FFF;
				}

				if(ctx->code == 2)
				{
					uint8_t r = (data & 0x0F);
					uint8_t d = ctx->latch;
					systeme_vdp_reg_w(chip, r, d);
				}
			}
		return;
	}
}

static void systeme_vdp_update_vint_flag_for_now(vdp_t *ctx)
{
	int32_t cyc = z80_get_elapsed_cycles();
	int32_t line = (cyc / system_cycles_per_line()) % ctx->lpf;
	int32_t dot = cyc % system_cycles_per_line();
	int32_t flag_cycle = 25;

	if ((line == ctx->height) && (dot >= flag_cycle) && !ctx->vint_flag_raised)
	{
		ctx->status |= 0x80;
		ctx->vint_pending = 1;
		ctx->vint_flag_raised = 1;
		if (ctx->reg[0x01] & 0x20)
			z80_set_irq_line(vdp.irq, ASSERT_LINE);
	}
}

uint8_t systeme_vdp_read(int chip, int32_t offset)
{
	vdp_t *ctx = systeme_vdp_context(chip);
	uint8_t temp;

	switch(offset & 1)
	{
		case 0:
			ctx->pending = 0;
			temp = ctx->buffer;
			ctx->buffer = ctx->vram[ctx->addr & 0x3FFF];
			ctx->addr = (ctx->addr + 1) & 0x3FFF;
			return temp;

		case 1:
			systeme_vdp_update_vint_flag_for_now(ctx);
			temp = ctx->status | 0x1f;
			ctx->status = 0;
			ctx->pending = 0;
			ctx->vint_pending = 0;
			ctx->hint_pending = 0;
			systeme_vdp_irq_refresh();
			return temp;
	}

	return 0;
}

uint8_t systeme_vdp_counter_r(int32_t offset)
{
	return vdp_counter_r(offset);
}

void systeme_vdp_frame_start(void)
{
	vdp2.vscroll = vdp2.reg[9];
	vdp2.left = vdp2.reg[0x0A];
	vdp2.spr_col = 0xff00;
}

void systeme_vdp_set_line(int32_t line)
{
	vdp2.line = line;
}


void vdp_write(int32_t offset, uint8_t data)
{
	MULTIREXZ80_TRACE_VDP_WRITE("sms", offset, data);
	int32_t index;
	vdp_render_scanline_if_due();
	if (!vdp_timed_render_active() && (((z80_get_elapsed_cycles() + 1) / system_cycles_per_line()) > vdp.line))
	{
		/* Legacy line-start renderer guard: render next line before updating register.
		 * Timed SMS/GG rendering must not pre-render the next line before its
		 * late render event, or late sprite-mode writes can still leak through. */
		render_line((vdp.line+1)%vdp.lpf);
	}

	switch(offset & 1)
	{
    case 0: /* Data port */

      vdp.pending = 0;

      switch(vdp.code)
      {
        case 0: /* VRAM write */
        case 1: /* VRAM write */
        case 2: /* VRAM write */
          index = (vdp.addr & 0x3FFF);
          if(data != vdp.vram[index])
          {
            vdp_record_vram_write((uint16_t)index, vdp.vram[index]);
            vdp.vram[index] = data;
            MARK_BG_DIRTY(vdp.addr);
            vdp_invalidate_sprite_status_pipeline((uint16_t)index);
          }
          vdp.buffer = data;
          break;
    
        case 3: /* CRAM write */
          index = (vdp.addr & 0x1F);
          vdp_top_cram_capture_write((uint8_t)index, data);
          if(data != vdp.cram[index])
          {
            vdp.cram[index] = data;
            palette_sync(index);
          }
          vdp.buffer = data;
          break;
      }
      vdp.addr = (vdp.addr + 1) & 0x3FFF;
      return;

    case 1: /* Control port */
      if(vdp.pending == 0)
      {
        vdp.addr = (vdp.addr & 0x3F00) | (data & 0xFF);
        vdp.latch = data;
        vdp.pending = 1;
      }
      else
      {
        vdp.pending = 0;
        vdp.code = (data >> 6) & 3;
        vdp.addr = (data << 8 | vdp.latch) & 0x3FFF;

        if(vdp.code == 0)
        {
          vdp.buffer = vdp.vram[vdp.addr & 0x3FFF];
          vdp.addr = (vdp.addr + 1) & 0x3FFF;
        }
    
        if(vdp.code == 2)
        {
          uint8_t r = (data & 0x0F);
          uint8_t d = vdp.latch;
          vdp_reg_w(r, d);
        }
      }
      return;
  }
}

static int vdp_vcounter_line_for_now(void)
{
    int32_t cycles_per_line = system_cycles_per_line();
    int32_t cyc = z80_get_elapsed_cycles();
    int32_t line = (cyc / cycles_per_line) % vdp.lpf;
    int32_t dot = cyc % cycles_per_line;
    int32_t vcount_cycle = 15;

    if (cycles_per_line <= 0)
        return vdp.line;

    /* The V counter increments part-way through the scanline, not at the
     * beginning.  Gearsystem initializes it to the final frame line and then
     * increments at TIMING_VCOUNT, so reads before the event still see the
     * previous line and reads after it see the current line.  The previous
     * SMS path was one line ahead, which is visible in VDPTEST and can break
     * games that poll VCounter for raster timing. */
    if (vdp_gamegear_timing_active())
        vcount_cycle = 28;

    if (dot < vcount_cycle)
        line = (line + vdp.lpf - 1) % vdp.lpf;

    return line;
}

static int vdp_vint_flag_event_now(void)
{
    int32_t cycles_per_line = system_cycles_per_line();
    int32_t cyc = z80_get_elapsed_cycles();
    int32_t line = (cyc / cycles_per_line) % vdp.lpf;
    int32_t dot = cyc % cycles_per_line;
    int32_t flag_cycle = vdp_gamegear_timing_active() ? 27 : 15;

    return (line == ((int32_t)vdp.height + 1)) && (dot >= flag_cycle);
}

static void vdp_update_vint_flag_for_now(void)
{
    if (vdp_vint_flag_event_now() && !vdp.vint_flag_raised)
    {
        vdp.status |= 0x80;
        vdp.vint_pending = 1;
        vdp.vint_flag_raised = 1;
        if (vdp.reg[0x01] & 0x20)
        {
#ifdef SORDM5_EMU
            if (sms.console == CONSOLE_SORDM5)
                sordm5_ctc_vdp_interrupt();
            else
#endif
                z80_set_irq_line(vdp.irq, ASSERT_LINE);
        }
    }
}

uint8_t vdp_read(int32_t offset)
{
	uint8_t temp;

	switch(offset & 1)
	{
		case 0: /* CPU <-> VDP data buffer */
		  vdp.pending = 0;
		  temp = vdp.buffer;
		  vdp.buffer = vdp.vram[vdp.addr & 0x3FFF];
		  vdp.addr = (vdp.addr + 1) & 0x3FFF;
		return temp;
		case 1: /* Status flags */
		{
			vdp_render_scanline_if_due();
			vdp_update_vint_flag_for_now();
			vdp_update_sprite_status_for_now();

			/* low 5 bits return non-zero data (fixes PGA Tour Golf course map introduction) */
			temp = vdp.status | 0x1f;
			if (temp & 0x80)
				vdp.vint_flag_ack_seen = 1;

			/* Clear visible flags.  The SMS VDP status port is also read by the
			 * standard IM 1 IRQ vector solely to acknowledge frame/line IRQs.
			 * Keep already-latched sprite collision/overflow bits alive through
			 * that IRQ acknowledge read so the external status poll that timed
			 * the sprite event can still observe it. */
			if ((Z80.pc.w.l >= 0x0038) && (Z80.pc.w.l <= 0x0040))
				vdp.status &= 0x60;
			else
				vdp.status = 0;
			vdp.pending = 0;
			vdp.vint_pending = 0;
			vdp.hint_pending = 0;
#ifdef SORDM5_EMU
			if (sms.console != CONSOLE_SORDM5)
#endif
				z80_set_irq_line(vdp.irq, CLEAR_LINE);
			return temp;
		}
	}

	/* Just to please the compiler */
	return 0;
}

uint8_t vdp_counter_r(int32_t offset)
{
	switch(offset & 1)
	{
		case 0: /* V Counter */
			return vc_table[sms.display][vdp.extended][vdp_vcounter_line_for_now()];
		case 1: /* H Counter -- return previously latched values or ZERO */
			return sms.hlatch;
	}

	/* Just to please the compiler */
	return 0;
}


/*--------------------------------------------------------------------------*/
/* Game Gear VDP handlers                           */
/*--------------------------------------------------------------------------*/

void gg_vdp_write(int32_t offset, uint8_t data)
{
	MULTIREXZ80_TRACE_VDP_WRITE("gg", offset, data);
	int32_t index;

	vdp_render_scanline_if_due();
	if (!vdp_timed_render_active() && (((z80_get_elapsed_cycles() + 1) / system_cycles_per_line()) > vdp.line))
	{
		/* Legacy line-start renderer guard: render next line before updating register.
		 * Timed SMS/GG rendering must not pre-render the next line before its
		 * late render event, or late sprite-mode writes can still leak through. */
		render_line((vdp.line+1)%vdp.lpf);
	}

	switch(offset & 1)
	{
		case 0: /* Data port */
		vdp.pending = 0;
		switch(vdp.code)
		{
			case 0: /* VRAM write */
			case 1: /* VRAM write */
			case 2: /* VRAM write */
				index = (vdp.addr & 0x3FFF);
				if(data != vdp.vram[index])
				{
					vdp_record_vram_write((uint16_t)index, vdp.vram[index]);
					vdp.vram[index] = data;
					MARK_BG_DIRTY(vdp.addr);
					vdp_invalidate_sprite_status_pipeline((uint16_t)index);
				}
				vdp.buffer = data;
			break;
			case 3: /* CRAM write */
				if(vdp.addr & 1)
				{
					vdp.cram_latch = (vdp.cram_latch & 0x00FF) | ((data & 0xFF) << 8);
					vdp.cram[(vdp.addr & 0x3E) | (0)] = (vdp.cram_latch >> 0) & 0xFF;
					vdp.cram[(vdp.addr & 0x3E) | (1)] = (vdp.cram_latch >> 8) & 0xFF;
					palette_sync((vdp.addr >> 1) & 0x1F);
				}
				else
				{
					vdp.cram_latch = (vdp.cram_latch & 0xFF00) | ((data & 0xFF) << 0);
				}
				vdp.buffer = data;
			break;
		}
		vdp.addr = (vdp.addr + 1) & 0x3FFF;
		return;

		case 1: /* Control port */
		if(vdp.pending == 0)
		{
			vdp.addr = (vdp.addr & 0x3F00) | (data & 0xFF);
			vdp.latch = data;
			vdp.pending = 1;
		}
		else
		{
			vdp.pending = 0;
			vdp.code = (data >> 6) & 3;
			vdp.addr = (data << 8 | vdp.latch) & 0x3FFF;
			if(vdp.code == 0)
			{
				vdp.buffer = vdp.vram[vdp.addr & 0x3FFF];
				vdp.addr = (vdp.addr + 1) & 0x3FFF;
			}
			if(vdp.code == 2)
			{
				uint8_t r = (data & 0x0F);
				uint8_t d = vdp.latch;
				vdp_reg_w(r, d);
			}
		}
		return;
	}
}

/*--------------------------------------------------------------------------*/
/* MegaDrive / Genesis VDP handlers                     */
/*--------------------------------------------------------------------------*/

void md_vdp_write(int32_t offset, uint8_t data)
{
	MULTIREXZ80_TRACE_VDP_WRITE("md", offset, data);
	int32_t index;
	switch(offset & 1)
	{
		case 0: /* Data port */
		vdp.pending = 0;
		switch(vdp.code)
		{
			case 0: /* VRAM write */
			case 1: /* VRAM write */
			index = (vdp.addr & 0x3FFF);
			if(data != vdp.vram[index])
			{
				vdp.vram[index] = data;
				MARK_BG_DIRTY(vdp.addr);
			}
			break;
			case 2: /* CRAM write */
			case 3: /* CRAM write */
				index = (vdp.addr & 0x1F);
				if(data != vdp.cram[index])
				{
					vdp.cram[index] = data;
					palette_sync(index);
				}
			break;
		}
		vdp.addr = (vdp.addr + 1) & 0x3FFF;
		return;

		case 1: /* Control port */
		if(vdp.pending == 0)
		{
			vdp.latch = data;
			vdp.pending = 1;
		}
		else
		{
			vdp.pending = 0;
			vdp.code = (data >> 6) & 3;
			vdp.addr = (data << 8 | vdp.latch) & 0x3FFF;

			if(vdp.code == 0)
			{
				vdp.buffer = vdp.vram[vdp.addr & 0x3FFF];
				vdp.addr = (vdp.addr + 1) & 0x3FFF;
			}
    
			if(vdp.code == 2)
			{
				uint8_t r = (data & 0x0F);
				uint8_t d = vdp.latch;
				vdp_reg_w(r, d);
			}
		}
	return;
  }
}

/*--------------------------------------------------------------------------*/
/* TMS9918 VDP handlers                           */
/*--------------------------------------------------------------------------*/

void tms_write(int32_t offset, uint8_t data)
{
	MULTIREXZ80_TRACE_VDP_WRITE("tms9918", offset, data);
	int32_t index;
	switch(offset & 1)
	{
		case 0: /* Data port */
			vdp.pending = 0;

			switch(vdp.code)
			{
				case 0: /* VRAM write */
				case 1: /* VRAM write */
				case 2: /* VRAM write */
				case 3: /* VRAM write */
				index = (vdp.addr & 0x3FFF);
				if(data != vdp.vram[index])
				{
					vdp_record_vram_write((uint16_t)index, vdp.vram[index]);
					vdp.vram[index] = data;
					MARK_BG_DIRTY(vdp.addr);
				}
				break;
			}
			vdp.addr = (vdp.addr + 1) & 0x3FFF;
		return;

		case 1: /* Control port */
		if(vdp.pending == 0)
		{
			vdp.latch = data;
			vdp.pending = 1;
		}
		else
		{
			vdp.pending = 0;
			vdp.code = (data >> 6) & 3;
			vdp.addr = (data << 8 | vdp.latch) & 0x3FFF;
			if(vdp.code == 0)
			{
				vdp.buffer = vdp.vram[vdp.addr & 0x3FFF];
				vdp.addr = (vdp.addr + 1) & 0x3FFF;
			}
    
			if(vdp.code == 2)
			{
				uint8_t r = (data & 0x07);
				uint8_t d = vdp.latch;
				vdp_reg_w(r, d);
			}
		}
		return;
	}
}
