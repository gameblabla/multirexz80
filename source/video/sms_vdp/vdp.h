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
#ifndef VDP_H_
#define VDP_H_

/*
    vdp1

    mode 4 when m4 set and m1 reset

    vdp2

    mode 4 when m4 set and m2,m1 != 1,0


*/

/* VDP context */
typedef struct
{
    uint8_t vram[0x4000];
    uint8_t cram[0x40]; 
    uint8_t cram_top[0x40];
    uint8_t cram_top_next[0x40];
    uint8_t cram_line_latch[0x40];
    uint8_t reg[0x10];
    uint8_t vscroll;
    uint8_t hscroll;
    uint8_t hscroll_top;
    uint8_t hscroll_top_next;
    uint8_t hscroll_top_next_valid;
    uint8_t hscroll_top_next_armed;
    uint8_t cram_top_valid;
    uint8_t cram_top_next_valid;
    uint8_t cram_top_next_armed;
    uint8_t cram_top_capture_active;
    uint8_t status;
    uint8_t latch;
    uint8_t pending;
    uint8_t code;
    uint8_t buffer;
    uint8_t height;
    uint8_t extended;
    uint8_t irq;
    uint8_t vint_pending;
    uint8_t hint_pending;
    uint8_t spr_ovr;
    uint8_t bd;
    uint8_t sprite_mode_latch;
    uint8_t sprite_mode_draw;
    uint16_t lpf;
    uint16_t cram_latch;
    uint16_t addr;
    int32_t pn, ct, pg, sa, sg;
    int32_t ntab;
    int32_t satb;
    int32_t line;
    int32_t left;
    int32_t spr_col;
    int32_t spr_col_line;
    int32_t spr_col_cycle;
    int32_t spr_ovr_line;
    int32_t spr_ovr_cycle;
    int32_t mode;
    uint8_t spr_col_pending;
    uint8_t spr_ovr_pending;
    uint8_t timed_render;
} vdp_t;

/* Global data */
extern vdp_t vdp;
extern vdp_t *vdp2_ptr;
#define vdp2 (*vdp2_ptr)
extern uint8_t hc_256[228];

/* Function prototypes */
extern void vdp_init(void);
extern void vdp_shutdown(void);
extern void vdp_reset(void);
extern void viewport_check(void);
extern void vdp_latch_sprite_mode(void);
extern int vdp_timed_render_active(void);
extern int vdp_gamegear_timing_active(void);
extern int vdp_render_event_cycle(void);
extern int vdp_hint_event_cycle(void);
extern int vdp_xscroll_event_cycle(void);
extern void vdp_latch_hscroll(void);
extern void vdp_frame_scroll_latch_start(void);
extern uint8_t vdp_vram_byte_for_bg_fetch(uint16_t address, int32_t fetch_cycle);
extern uint8_t vdp_reg_byte_for_bg_fetch(uint8_t reg, int32_t fetch_cycle);
extern void vdp_prepare_scanline(int32_t line, int skip_render);
extern void vdp_render_scanline_if_due(void);
extern void vdp_render_scanline_now(void);
extern void vdp_request_sprite_collision(int32_t line, int32_t x);
extern void vdp_request_sprite_overflow(int32_t line);
extern void vdp_update_status_end_of_scanline(void);
extern uint8_t vdp_counter_r(int32_t offset);
extern uint8_t vdp_read(int32_t offset);
extern void vdp_write(int32_t offset, uint8_t data);
extern void vdp_vram_direct_write(uint16_t address, uint8_t data);
extern void systeme_vdp_bank_w(uint8_t data);
extern void systeme_vdp_direct_write(uint8_t bank_select, uint16_t address, uint8_t data);
extern uint8_t systeme_vdp_read(int chip, int32_t offset);
extern void systeme_vdp_write(int chip, int32_t offset, uint8_t data);
extern uint8_t systeme_vdp_counter_r(int32_t offset);
extern void systeme_vdp_frame_start(void);
extern void systeme_vdp_set_line(int32_t line);
extern void gg_vdp_write(int32_t offset, uint8_t data);
extern void md_vdp_write(int32_t offset, uint8_t data);
extern void tms_write(int32_t offset, uint8_t data);

#endif /* _VDP_H_ */

