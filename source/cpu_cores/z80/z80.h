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

#ifndef Z80_H_
#define Z80_H_

#include "cpuintrf.h"

#define Z80_Regs z80_t
#define Z80_Context &Z80

#define true 1
#define false 0

#define Z80_INPUT_LINE_WAIT 4
#define Z80_INPUT_LINE_BUSRQ 5

enum
{
  Z80_PC, Z80_SP,
  Z80_A, Z80_B, Z80_C, Z80_D, Z80_E, Z80_H, Z80_L,
  Z80_AF, Z80_BC, Z80_DE, Z80_HL,
  Z80_IX, Z80_IY,  Z80_AF2, Z80_BC2, Z80_DE2, Z80_HL2,
  Z80_R, Z80_I, Z80_IM, Z80_IFF1, Z80_IFF2, Z80_HALT,
  Z80_DC0, Z80_DC1, Z80_DC2, Z80_DC3, Z80_WZ
};

enum {
  Z80_TABLE_op,
  Z80_TABLE_cb,
  Z80_TABLE_ed,
  Z80_TABLE_xy,
  Z80_TABLE_xycb,
  Z80_TABLE_ex  /* cycles counts for taken jr/jp/call and interrupt latency (rst opcodes) */
};

/****************************************************************************/
/* The Z80 registers. HALT is set to 1 when the CPU is halted, the refresh  */
/* register is calculated as follows: refresh=(Z80.r&127)|(Z80.r2&128)      */
/****************************************************************************/
typedef struct
{
	PAIR  pc,sp,af,bc,de,hl,ix,iy,wz;
	PAIR  af2,bc2,de2,hl2;
	PAIR prvpc;
	uint8_t	r,r2,iff1,iff2,halt,im,i;
	uint8_t	after_ei;      /* are we in the EI shadow? */
	uint8_t	after_ldair; /* same, but for LD A,I or LD A,R */
  
	uint8_t *cc_op;
	uint8_t *cc_cb;
	uint8_t *cc_ed;
	uint8_t *cc_xy;
	uint8_t *cc_xycb;
	uint8_t *cc_ex;
	
	uint16_t ea;
	
	int32_t	nmi_state;      /* nmi line state */
	int32_t nmi_pending;    /* nmi pending */
	int32_t irq_state;      /* irq line state */
	int32_t (*irq_callback)(int32_t);
	int32_t icount;
	int32_t wait_state;         // wait line state
	int32_t busrq_state;        // bus request line state
}  Z80_Regs;


extern int32_t z80_cycle_count;
extern Z80_Regs Z80;

void z80_init(int32_t (*irqcallback)(int32_t));
void z80_reset (void);
void z80_exit (void);
int32_t z80_execute(int32_t cycles);
void z80_set_irq_line(int32_t inputnum, int32_t state);
void z80_reset_cycle_count(void);
void z80_set_m1_wait_cycles(int32_t cycles);
int32_t z80_get_elapsed_cycles(void);

/* Optional multi-instance activation used by multi-Z80 arcade drivers.
 * The default SMS/GG path continues to use the exported global Z80 context
 * and cpu_readmap[] arrays.  These functions only redirect the interpreter's
 * internal hot paths; they do not share mutable CPU state. */
void z80_select_context(Z80_Regs *regs, int32_t *cycle_counter);
void z80_select_default_context(void);
Z80_Regs *z80_get_context(void);
void z80_select_opcode_map(uint8_t **readmap);
void z80_select_memory_maps(uint8_t **readmap, uint8_t **writemap);
void z80_select_default_opcode_map(void);

extern void (*cpu_writemem16)(uint16_t address, uint8_t data);
extern void (*cpu_writeport16)(uint16_t port, uint8_t data);
extern uint8_t (*cpu_readport16)(uint16_t port);

#endif

