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

#ifndef MULTIREXZ80_INSPECT_H_
#define MULTIREXZ80_INSPECT_H_

#include <stdint.h>
#include <stdio.h>

#ifdef MULTIREXZ80_HEADLESS
extern int multirexz80_inspect_trace_active;
void multirexz80_inspect_set_trace(FILE *fp);
void multirexz80_inspect_set_frame(uint64_t frame);
void multirexz80_inspect_cpu_frame(uint64_t frame);
void multirexz80_inspect_vdp_write(const char *chip, uint16_t port, uint8_t data,
                               int32_t line, uint16_t addr, uint8_t code);
void multirexz80_inspect_psg_write(uint16_t port, uint8_t data);
void multirexz80_inspect_ym_write(uint16_t port, uint8_t data);
void multirexz80_inspect_mem_write(uint16_t addr, uint8_t data);

#define MULTIREXZ80_TRACE_SET_TRACE(fp) multirexz80_inspect_set_trace(fp)
#define MULTIREXZ80_TRACE_SET_FRAME(f) multirexz80_inspect_set_frame(f)
#define MULTIREXZ80_TRACE_CPU_FRAME(f) do { if (multirexz80_inspect_trace_active) multirexz80_inspect_cpu_frame(f); } while (0)
#define MULTIREXZ80_TRACE_VDP_WRITE(chip, port, data) do { if (multirexz80_inspect_trace_active) multirexz80_inspect_vdp_write((chip), (uint16_t)(port), (uint8_t)(data), vdp.line, vdp.addr, vdp.code); } while (0)
#define MULTIREXZ80_TRACE_PSG_WRITE(port, data) do { if (multirexz80_inspect_trace_active) multirexz80_inspect_psg_write((uint16_t)(port), (uint8_t)(data)); } while (0)
#define MULTIREXZ80_TRACE_YM_WRITE(port, data) do { if (multirexz80_inspect_trace_active) multirexz80_inspect_ym_write((uint16_t)(port), (uint8_t)(data)); } while (0)
#define MULTIREXZ80_TRACE_MEM_WRITE(addr, data) do { if (multirexz80_inspect_trace_active) multirexz80_inspect_mem_write((uint16_t)(addr), (uint8_t)(data)); } while (0)
#else
#define MULTIREXZ80_TRACE_SET_TRACE(fp) ((void)0)
#define MULTIREXZ80_TRACE_SET_FRAME(f) ((void)0)
#define MULTIREXZ80_TRACE_CPU_FRAME(f) ((void)0)
#define MULTIREXZ80_TRACE_VDP_WRITE(chip, port, data) ((void)0)
#define MULTIREXZ80_TRACE_PSG_WRITE(port, data) ((void)0)
#define MULTIREXZ80_TRACE_YM_WRITE(port, data) ((void)0)
#define MULTIREXZ80_TRACE_MEM_WRITE(addr, data) ((void)0)
#endif

#endif /* MULTIREXZ80_INSPECT_H_ */
