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

#include "shared.h"
#include "inspect.h"

#ifdef MULTIREXZ80_HEADLESS
int multirexz80_inspect_trace_active;
static FILE *trace_fp;
static uint64_t trace_frame;

void multirexz80_inspect_set_trace(FILE *fp)
{
    trace_fp = fp;
    multirexz80_inspect_trace_active = (fp != NULL);
    if (trace_fp)
    {
        fprintf(trace_fp, "type,frame,line,cycles,pc,sp,detail,a,b,c,d,e,h,l,ix,iy,dev_port,dev_addr,data,code\n");
        fflush(trace_fp);
    }
}

void multirexz80_inspect_set_frame(uint64_t frame)
{
    trace_frame = frame;
}

static void trace_cpu_common(const char *type, uint64_t frame, const char *detail,
                             uint16_t dev_port, uint16_t dev_addr, uint8_t data, uint8_t code)
{
    if (!trace_fp) return;
    fprintf(trace_fp,
            "%s,%llu,%d,%d,%04X,%04X,%s,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%04X,%04X,%04X,%04X,%02X,%02X\n",
            type,
            (unsigned long long)frame,
            vdp.line,
            z80_get_elapsed_cycles(),
            Z80.pc.w.l,
            Z80.sp.w.l,
            detail ? detail : "",
            Z80.af.b.h,
            Z80.bc.b.h,
            Z80.bc.b.l,
            Z80.de.b.h,
            Z80.de.b.l,
            Z80.hl.b.h,
            Z80.hl.b.l,
            Z80.ix.w.l,
            Z80.iy.w.l,
            dev_port,
            dev_addr,
            data,
            code);
}

void multirexz80_inspect_cpu_frame(uint64_t frame)
{
    trace_cpu_common("cpu_frame", frame, "end", 0, 0, 0, 0);
}

void multirexz80_inspect_vdp_write(const char *chip, uint16_t port, uint8_t data,
                               int32_t line, uint16_t addr, uint8_t code)
{
    if (!trace_fp) return;
    fprintf(trace_fp,
            "vdp_write,%llu,%d,%d,%04X,%04X,%s,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%04X,%04X,%04X,%04X,%02X,%02X\n",
            (unsigned long long)trace_frame,
            line,
            z80_get_elapsed_cycles(),
            Z80.pc.w.l,
            Z80.sp.w.l,
            chip ? chip : "vdp",
            Z80.af.b.h,
            Z80.bc.b.h,
            Z80.bc.b.l,
            Z80.de.b.h,
            Z80.de.b.l,
            Z80.hl.b.h,
            Z80.hl.b.l,
            Z80.ix.w.l,
            Z80.iy.w.l,
            port,
            addr,
            data,
            code);
}

void multirexz80_inspect_psg_write(uint16_t port, uint8_t data)
{
    trace_cpu_common("psg_write", trace_frame, "sn76489", port, 0, data, 0);
}

void multirexz80_inspect_ym_write(uint16_t port, uint8_t data)
{
    trace_cpu_common("ym_write", trace_frame, "ym2413", port, 0, data, 0);
}

void multirexz80_inspect_mem_write(uint16_t addr, uint8_t data)
{
    trace_cpu_common("mem_write", trace_frame, "z80", 0, addr, data, 0);
}
#endif
