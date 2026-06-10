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
/*
 * See git commit history for more information.
 * - Gameblabla
 * March 13th 2019 : Minor fixes as part of the CrabZ80's revert. (mostly whitepacing but the TMS code was also broken to some extent)
 * March 7th 2019 : Some whitepacing and changing variables to c99 datatypes.
 * Feb 19th 2019 : Minor whitepacing fix.
 * August 12th 2018 : Minor fixes. (mostly changing variables to c99 datatypes and whitepacing)
*/

#include "shared.h"

bitmap_t bitmap;
cart_t cart;
input_t input;

extern int32_t z80_cycle_count;

int32_t system_cycles_per_line(void)
{
	if (sms.console == CONSOLE_SYSTEME) return SYSTEME_CYCLES_PER_LINE;
	if (sms.console == CONSOLE_SYSTEM1) return SYSTEM1_CYCLES_PER_LINE;
	if (sms.console == CONSOLE_SNKPSYCHOS) return SNK_PSYCHOS_CYCLES_PER_LINE;
	return CYCLES_PER_LINE;
}

int32_t system_hcounter_index(void)
{
	int32_t cycles_per_line = system_cycles_per_line();
	int32_t dot = z80_get_elapsed_cycles() % cycles_per_line;

	if (sms.console == CONSOLE_SYSTEME)
		dot = (dot * CYCLES_PER_LINE) / SYSTEME_CYCLES_PER_LINE;
	else if (sms.console == CONSOLE_SYSTEM1)
		dot = (dot * CYCLES_PER_LINE) / SYSTEM1_CYCLES_PER_LINE;
	else if (sms.console == CONSOLE_SNKPSYCHOS)
		dot = (dot * CYCLES_PER_LINE) / SNK_PSYCHOS_CYCLES_PER_LINE;

	if (dot < 0) dot = 0;
	if (dot >= CYCLES_PER_LINE) dot = CYCLES_PER_LINE - 1;
	return dot;
}

static void lightgun_update_dpad_cursor(void)
{
	int32_t port;
	int32_t max_y = (vdp.height > 0) ? (vdp.height - 1) : 191;
	int32_t speed = option.lightgun_dpad_speed > 0 ? option.lightgun_dpad_speed : 3;

	for (port = 0; port < 2; port++)
	{
		if (sms.device[port] != DEVICE_LIGHTGUN) continue;

		int32_t x = input.analog[port][0];
		int32_t y = input.analog[port][1];
		int32_t step = (input.pad[port] & INPUT_BUTTON2) ? speed * 2 : speed;

		if (input.pad[port] & INPUT_LEFT)  x -= step;
		if (input.pad[port] & INPUT_RIGHT) x += step;
		if (input.pad[port] & INPUT_UP)    y -= step;
		if (input.pad[port] & INPUT_DOWN)  y += step;

		if (x < 0) x = 0;
		if (x > 255) x = 255;
		if (y < 0) y = 0;
		if (y > max_y) y = max_y;
		input.analog[port][0] = x;
		input.analog[port][1] = y;
	}
}


static void sms_process_horizontal_interrupt(int32_t iline, int32_t cycles_per_line)
{
	if (sms.console < CONSOLE_SMS)
		return;

	if (vdp.line > iline)
		return;

	if (--vdp.left < 0)
	{
		vdp.left = vdp.reg[0x0A];
		vdp.hint_pending = 1;
		if (vdp.reg[0x00] & 0x10)
		{
			/* IRQ line is latched between instructions, on instruction last cycle.
			 * Preserve the legacy exact-line-start quirk for the compatibility path;
			 * timed SMS/GG HINTs occur after the line has started. */
			if (!(z80_get_elapsed_cycles() % cycles_per_line))
				z80_execute(1);
			z80_set_irq_line(0, ASSERT_LINE);
		}
	}

	if (sms.console == CONSOLE_SYSTEME && --vdp2.left < 0)
	{
		vdp2.left = vdp2.reg[0x0A];
		vdp2.hint_pending = 1;
		if (vdp2.reg[0x00] & 0x10)
		{
			if (!(z80_get_elapsed_cycles() % cycles_per_line))
				z80_execute(1);
			z80_set_irq_line(0, ASSERT_LINE);
		}
	}
}

/* Run the virtual console emulation for one frame */
void system_frame(uint32_t skip_render)
{
	int32_t iline = 0, line_z80 = 0;
	const int32_t cycles_per_line = system_cycles_per_line();

	if (sms.console == CONSOLE_SYSTEM1)
	{
		system1_frame(skip_render);
		return;
	}

	if (sms.console == CONSOLE_SNKPSYCHOS)
	{
		snk_psychos_frame(skip_render);
		return;
	}

	/* Debounce pause key.  Arcade drivers use dedicated coin/service/start bits;
	 * do not assert the SMS pause/NMI path while an arcade game is running. */
	if((sms.console != CONSOLE_SYSTEME) && (sms.console != CONSOLE_SYSTEM1) && (sms.console != CONSOLE_SNKPSYCHOS) && (input.system & INPUT_PAUSE))
	{
		if(!sms.paused)
		{
			sms.paused = 1;
			CPUIRQ_Pause();
		}
	}
	else
	{
		sms.paused = 0;
	}

	/* Reset TMS Text offset counter */
	text_counter = 0;

	/* Light Phaser d-pad fallback for ports without mouse/touch input. */
	lightgun_update_dpad_cursor();

	/* 3D glasses faking */
	if (sms.glasses_3d) skip_render = sms.wram[0x1ffb];

	vdp_frame_scroll_latch_start();
	vdp.vint_flag_raised = 0;
	if (sms.display != DISPLAY_PAL || !(vdp.status & 0x80))
		vdp.vint_flag_ack_seen = 0;
	if (vdp2_ptr)
	{
		vdp2.vint_flag_raised = 0;
		if (sms.display != DISPLAY_PAL || !(vdp2.status & 0x80))
			vdp2.vint_flag_ack_seen = 0;
	}

	/* VDP register 9 is latched during VBLANK */
	vdp.vscroll = vdp.reg[9];
	if (sms.console != CONSOLE_SYSTEME)
		vdp_latch_sprite_mode();

	/* Reload Horizontal Interrupt counter */
	vdp.left = vdp.reg[0x0A];

	/* Reset sprite status request timing for the new frame.  Visible status bits
	 * themselves are still cleared by VDP status reads. */
	vdp.spr_col = 0xff00;
	vdp.spr_col_pending = 0;
	vdp.spr_ovr_pending = 0;
	if (sms.console == CONSOLE_SYSTEME) systeme_vdp_frame_start();

	/* Line processing */
	for(vdp.line = 0; vdp.line < vdp.lpf; vdp.line++)
	{
		int32_t line_start = line_z80;
		int32_t render_target = line_start + vdp_render_event_cycle();
		int32_t line_end;
		int timed_render = vdp_timed_render_active();

		if (sms.console == CONSOLE_SYSTEME) systeme_vdp_set_line(vdp.line);
		if ((sms.display == DISPLAY_PAL) && vdp.vint_flag_ack_seen && (vdp.line == 160))
		{
			vdp.status &= 0x7F;
			vdp.vint_pending = 0;
			vdp.vint_flag_ack_seen = 0;
		}
		iline = vdp.height;

		/* Standard path renders at line start for compatibility.  When a
		 * program has been observed changing SMS/GG sprite mode mid-active
		 * display, switch to the late render event used by the real VDP. */
		if (timed_render)
			vdp_prepare_scanline(vdp.line, skip_render);
		else if(!skip_render)
			render_line(vdp.line);

		/* Horizontal Interrupt */
		if (!timed_render)
			sms_process_horizontal_interrupt(iline, cycles_per_line);

		/* Run Z80 CPU */
		line_end = line_start + cycles_per_line;
		line_z80 = line_end;
		if (timed_render)
		{
			int32_t xscroll_target = line_start + vdp_xscroll_event_cycle();
			int32_t hint_target = line_start + vdp_hint_event_cycle();
			int32_t vint_irq_target = line_start + (vdp_gamegear_timing_active() ? 27 : 14);
			int32_t vint_flag_target = line_start + (vdp_gamegear_timing_active() ? 27 : 15);
			if (xscroll_target > line_end)
				xscroll_target = line_end;
			if (hint_target > line_end)
				hint_target = line_end;
			if (render_target > line_end)
				render_target = line_end;
			if (vint_irq_target > line_end)
				vint_irq_target = line_end;
			if (vint_flag_target > line_end)
				vint_flag_target = line_end;

			if (xscroll_target > z80_cycle_count)
				z80_execute(xscroll_target - z80_cycle_count);
			vdp_latch_hscroll();

			if (vdp.line == (iline + 1))
			{
				if (vint_irq_target > z80_cycle_count)
					z80_execute(vint_irq_target - z80_cycle_count);
				vdp.vint_pending = 1;
				if (vdp.reg[0x01] & 0x20)
					z80_set_irq_line(vdp.irq, ASSERT_LINE);

				if (vint_flag_target > z80_cycle_count)
					z80_execute(vint_flag_target - z80_cycle_count);
				vdp.status |= 0x80;
				vdp.vint_flag_raised = 1;
				vdp.vint_pending = 1;
			}

			if (hint_target > z80_cycle_count)
				z80_execute(hint_target - z80_cycle_count);
			sms_process_horizontal_interrupt(iline, cycles_per_line);

			if (render_target > z80_cycle_count)
				z80_execute(render_target - z80_cycle_count);
			vdp_render_scanline_now();
			if (line_end > z80_cycle_count)
				z80_execute(line_end - z80_cycle_count);
		}
		else
		{
			z80_execute(line_z80 - z80_cycle_count);
		}
#ifdef SORDM5_EMU
		if (sms.console == CONSOLE_SORDM5)
			sordm5_ctc_tick(cycles_per_line);
#endif
		
		/* Vertical Interrupt.  Timed SMS/GG mode asserts this at its hardware
		 * cycle inside the following line; legacy/System E keep the old path. */
		if(!timed_render && vdp.line == iline)
		{
			vdp.status |= 0x80;
			vdp.vint_flag_raised = 1;
			vdp.vint_pending = 1;
			if (sms.console == CONSOLE_SYSTEME)
			{
				vdp2.status |= 0x80;
				vdp2.vint_flag_raised = 1;
				vdp2.vint_pending = 1;
			}
			if(vdp.reg[0x01] & 0x20)
			{
				#ifdef SORDM5_EMU
				if (sms.console == CONSOLE_SORDM5)
					sordm5_ctc_vdp_interrupt();
				else
				#endif
					z80_set_irq_line(vdp.irq, ASSERT_LINE);
			}
			if((sms.console == CONSOLE_SYSTEME) && (vdp2.reg[0x01] & 0x20))
				z80_set_irq_line(vdp.irq, ASSERT_LINE);
		}

		/* Commit sprite collision/overflow requests whose hardware cycle has
		 * passed even when the program does not read the VDP status port on
		 * that exact scanline. The visible flags persist until status read. */
		vdp_update_status_end_of_scanline();

		/* Run sound chips */
		MULTIREXZ80_sound_update(vdp.line);
	}

	/* Adjust Z80 cycle count for next frame */
	z80_cycle_count -= line_z80;
}

void system_init(void)
{
	sms_init();
	pio_init();
	vdp_init();
	render_init();
	MULTIREXZ80_sound_init();
}

void system_shutdown(void)
{
	sms_shutdown();
	pio_shutdown();
	vdp_shutdown();
	render_shutdown();
	MULTIREXZ80_sound_shutdown();
	free_rom();
}

void system_reset(void)
{
	/* ColecoVision hardware inserts one WAIT state on each Z80 M1
	 * opcode-fetch cycle.  Timing-sensitive TMS9918 effects, including
	 * vm_multicolor's active-display R4 changes, rely on this slower
	 * instruction cadence. */
	z80_set_m1_wait_cycles((sms.console == CONSOLE_COLECO) ? 1 : 0);
	sms_reset();
	pio_reset();
	vdp_reset();
	render_reset();
	if (sms.console == CONSOLE_SYSTEM1) system1_reset();
	if (sms.console == CONSOLE_SNKPSYCHOS) snk_psychos_reset();
	MULTIREXZ80_sound_reset();
	system_manage_sram(cart.sram, SLOT_CART, SRAM_LOAD);
	if (cart.mapper == MAPPER_93C46) eeprom93c46_load_from_sram(cart.sram);
}


void system_poweron(void)
{
	system_init();
	system_reset();
}

void system_poweroff(void)
{
	if (cart.mapper == MAPPER_93C46)
	{
		eeprom93c46_save_to_sram(cart.sram);
		sms.save = 1;
	}
	system_manage_sram(cart.sram, SLOT_CART, SRAM_SAVE);
}
