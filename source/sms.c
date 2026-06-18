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
 *   Sega Master System console emulation
 *
 ******************************************************************************/
/*
 * See git commit history for more information.
 * - Gameblabla
 * July 16th 2019 : Add 4PAK support.
 * March 13th 2019 : Reverted previous changes (partially) due to regressions.
 * March 7th 2019 : Removing CrabZ80's core.
 * Feb 19th 2019 : Minor fix to sms_irq function.
 * June 6th 2019 : Japanese BIOS does not clear BIOS, fix that.
*/

#include "shared.h"

/* SMS context */
sms_t sms;

/* BIOS/CART ROM */
bios_t bios;
slot_t slot;

/* Colecovision support */
t_coleco coleco;

uint8_t dummy_write[0x400];
uint8_t dummy_read[0x400];

static uint8_t systeme_ram[0x4000];
static uint8_t systeme_bank;
static uint8_t systeme_vram_write_bank;

static void systeme_map_banked_rom(uint8_t bank)
{
    uint_fast8_t i;
    uint32_t base = 0x10000u + ((uint32_t)(bank & 0x0f) << 14);

    for (i = 0x20; i <= 0x2F; i++)
    {
        uint32_t offset = base + ((uint32_t)(i & 0x0F) << 10);
        cpu_readmap[i] = (cart.loaded && offset < cart.size) ? &cart.rom[offset] : dummy_read;
        cpu_writemap[i] = dummy_write;
    }
}

void systeme_bank_w(uint8_t data)
{
    systeme_bank = data;
    systeme_vram_write_bank = data >> 5;
    systeme_map_banked_rom(data & 0x0F);
    systeme_vdp_bank_w(data);
}

static void systeme_map_memory(int clear_ram)
{
    uint_fast8_t i;

    if (clear_ram)
        memset(systeme_ram, 0, sizeof(systeme_ram));

    for (i = 0x00; i <= 0x1F; i++)
    {
        uint32_t offset = (uint32_t)i << 10;
        cpu_readmap[i] = (cart.loaded && offset < cart.size) ? &cart.rom[offset] : dummy_read;
        cpu_writemap[i] = dummy_write;
    }

    systeme_bank_w(0);

    for (i = 0x30; i <= 0x3F; i++)
    {
        cpu_readmap[i] = &systeme_ram[(i & 0x0F) << 10];
        cpu_writemap[i] = &systeme_ram[(i & 0x0F) << 10];
    }
}

static void writemem_mapper_systeme(uint16_t offset, uint8_t data)
{
    MULTIREXZ80_TRACE_MEM_WRITE(offset, data);

    if (offset >= 0x8000 && offset <= 0xBFFF)
    {
        /* System E maps this address range as banked ROM for reads and as
         * direct VDP VRAM for writes.  Port F7 selects which VDP and which
         * 16 KiB VRAM bank receives these direct writes. */
        systeme_vdp_direct_write(systeme_vram_write_bank, offset & 0x3FFF, data);
        return;
    }

    if (offset >= 0xC000)
    {
        cpu_writemap[offset >> 10][offset & 0x03FF] = data;
        return;
    }
}

static void writemem_mapper_none(uint16_t offset, uint8_t data)
{
	MULTIREXZ80_TRACE_MEM_WRITE(offset, data);
	cpu_writemap[offset >> 10][offset & 0x03FF] = data;
}

static void writemem_mapper_sega(uint16_t offset, uint8_t data)
{
	if(offset >= 0xFFFC)
	{
		mapper_16k_w(offset & 3, data);
	}
	MULTIREXZ80_TRACE_MEM_WRITE(offset, data);
	cpu_writemap[offset >> 10][offset & 0x03FF] = data;
}

static void writemem_mapper_codies(uint16_t offset, uint8_t data)
{
	if (offset == 0x0000)
	{
		mapper_16k_w(1,data);
		return;
	}
	if (offset == 0x4000)
	{
		mapper_16k_w(2,data);
		return;
	}
	if (offset == 0x8000)
	{
		mapper_16k_w(3,data);
		return;
	}
	MULTIREXZ80_TRACE_MEM_WRITE(offset, data);
	cpu_writemap[offset >> 10][offset & 0x03FF] = data;
}

static void writemem_mapper_korea_msx(uint16_t offset, uint8_t data)
{
	if (offset <= 0x0003)
	{
		mapper_8k_w(offset,data);
		return;
	}
	MULTIREXZ80_TRACE_MEM_WRITE(offset, data);
	cpu_writemap[offset >> 10][offset & 0x03FF] = data;
}

static void writemem_mapper_korea(uint16_t offset, uint8_t data)
{
	if (offset == 0xA000)
	{
		mapper_16k_w(3,data);
		return;
	}
	MULTIREXZ80_TRACE_MEM_WRITE(offset, data);
	cpu_writemap[offset >> 10][offset & 0x03FF] = data;
}

/*
	Mostly similar to Codemasters's writemap function except for 8000-0xBFFF range.
	Write xx to 3FFE: map bank (xx)&maxbankmask to memory slot in 0000-3FFF region
	Write yy to 7FFF: map bank (yy)&maxbankmask to memory slot in 4000-7FFF region
	Write zz to BFFF: map bank ((xx&0x30)+(zz))&maxbankmask to memory slot in 8000-BFFF region
*/
static void writemem_mapper_4pak(uint16_t offset, uint8_t data)
{
	if (offset == 0x3FFE)
	{
		mapper_16k_w(1,data);
		return;
	}
	if (offset == 0x7FFF)
	{
		mapper_16k_w(2,data);
		return;
	}
	/* https://github.com/OpenEmu/CrabEmu-Core/blob/a25c7277cd14f9e32647e1b9e1a45621fb051f13/consoles/sms/mapper-4PAA.c#91 
	 * I could not figure this bit out but it looks like xx was just in fact paging register 1. */
	if (offset == 0xBFFF)
	{
		mapper_16k_w(3, (slot.fcr[1] & 0x30) + data);
		return;
	}
	MULTIREXZ80_TRACE_MEM_WRITE(offset, data);
	cpu_writemap[offset >> 10][offset & 0x03FF] = data;
}



/*
 * Wonder Kid Game Gear prototype mapper.
 *
 * Hardware notes supplied with the prototype dump, with Ben Sittler credited
 * for reverse-engineering help: it is close to the Codemasters mapper but only
 * has one bank register.  Writes to $8000 select the 16 KiB ROM bank visible at
 * $8000-$BFFF.  The boot state is all-zero, so $0000-$3FFF, $4000-$7FFF and
 * $8000-$BFFF all initially expose bank 0.
 */
static void mapper_wonderkid_bank_w(uint8_t data)
{
	uint_fast8_t i;
	uint16_t page = data % slot.pages;

	slot.fcr[3] = data;
	for(i = 0x20; i <= 0x2F; i++)
	{
		cpu_readmap[i] = &slot.rom[(page << 14) | ((i & 0x0F) << 10)];
		cpu_writemap[i] = dummy_write;
	}
}

static void writemem_mapper_wonderkid(uint16_t offset, uint8_t data)
{
	if (offset == 0x8000)
	{
		mapper_wonderkid_bank_w(data);
		return;
	}

	MULTIREXZ80_TRACE_MEM_WRITE(offset, data);
	cpu_writemap[offset >> 10][offset & 0x03FF] = data;
}


/*
 * ColecoVision MegaCart mapper.
 *
 * This follows MAME's colecovision_megacart_cartridge_device behavior:
 * $8000-$BFFF reads from the last 16 KiB bank, $C000-$FFFF reads from the
 * active 16 KiB bank, and any read from the final 64 bytes of the cartridge
 * address space selects the active bank using the low address bits.  The bank
 * count is required to be a power of two for the address-mask behavior.
 */
static void coleco_megacart_map_active_bank(uint16_t bank)
{
    uint_fast8_t i;
    uint32_t bankcount = slot.coleco_megacart_bankcount ? slot.coleco_megacart_bankcount : (cart.size >> 14);

    if (bankcount == 0)
        bankcount = 1;

    /* MegaCart bank counts are constrained to powers of two by loader
     * detection, matching the low-address-bit mask behavior used by the
     * hardware/MAME device. */
    bank &= (uint16_t)(bankcount - 1);
    slot.coleco_megacart_activebank = bank;

    /* $C000-$FFFF: active 16 KiB bank.  Keeping cpu_readmap[] current makes
     * Z80 opcode fetches a direct pointer lookup with no mapper branch in the
     * hot path. */
    for (i = 0x30; i < 0x40; i++)
    {
        cpu_readmap[i] = &cart.rom[((uint32_t)bank << 14) | ((i & 0x0F) << 10)];
        cpu_writemap[i] = dummy_write;
    }
}

static void coleco_megacart_reset(void)
{
    uint_fast8_t i;
    uint32_t bank;
    uint32_t banks = cart.size >> 14;

    if (banks < 2)
        banks = 2;

    slot.coleco_megacart_bankcount = (uint16_t)banks;

    /* $8000-$BFFF: fixed final 16 KiB bank. */
    bank = (slot.coleco_megacart_bankcount - 1) << 14;
    for (i = 0x20; i < 0x30; i++)
    {
        cpu_readmap[i] = &cart.rom[bank | ((i & 0x0F) << 10)];
        cpu_writemap[i] = dummy_write;
    }

    coleco_megacart_map_active_bank(0);
}

static uint8_t coleco_megacart_read(uint16_t address)
{
    /* Reads from $FFC0-$FFFF select the active bank.  Data reads go through
     * this function; after the bank is selected, both data reads and opcode
     * fetches use the updated direct page table. */
    if (address >= 0xFFC0)
    {
        uint32_t bankcount = slot.coleco_megacart_bankcount ? slot.coleco_megacart_bankcount : (cart.size >> 14);
        if (bankcount > 1)
            coleco_megacart_map_active_bank((uint16_t)(address & (bankcount - 1)));
    }

    return cpu_readmap[address >> 10][address & 0x03FF];
}

static uint8_t mapper_93c46_read(uint16_t address)
{
    if ((address & 0xF000) == 0x8000)
    {
        if (address == 0x8000) return eeprom93c46_read();
        if (address >= 0x8008 && address < 0x8088) return eeprom93c46_direct_read(address);
    }
    return cpu_readmap[address >> 10][address & 0x03FF];
}

static void writemem_mapper_93c46(uint16_t offset, uint8_t data)
{
    MULTIREXZ80_TRACE_MEM_WRITE(offset, data);

    if (offset == 0x8000)
    {
        eeprom93c46_write(data);
        return;
    }
    if (offset >= 0x8008 && offset < 0x8088)
    {
        eeprom93c46_direct_write(offset, data);
        return;
    }
    if (offset == 0xFFFC)
    {
        eeprom93c46_control_write(data);
        return;
    }
    if (offset > 0xFFFC)
    {
        mapper_16k_w(offset & 3, data);
        return;
    }

    cpu_writemap[offset >> 10][offset & 0x03FF] = data;
}

uint8_t sms_readmem16(uint16_t address)
{
    if (slot.mapper == MAPPER_SYSTEM1)
        return system1_readmem(address);
    if (slot.mapper == MAPPER_SNKPSYCHOS)
        return snk_psychos_readmem(address);
    if (slot.mapper == MAPPER_TAITOL)
        return taitol_readmem(address);
    if (slot.mapper == MAPPER_93C46)
        return mapper_93c46_read(address);
    if (slot.mapper == MAPPER_COLECO_MEGACART && address >= 0x8000)
        return coleco_megacart_read(address);
    return cpu_readmap[address >> 10][address & 0x03FF];
}


void mapper_reset(void)
{
	if (slot.mapper != MAPPER_SNKPSYCHOS && slot.mapper != MAPPER_TAITOL)
		z80_select_default_context();
	z80_data_operand_fetch = 0;
	switch(slot.mapper)
	{
		case MAPPER_NONE:
			cpu_writemem16 = writemem_mapper_none;
		break;
		case MAPPER_CODIES:
			cpu_writemem16 = writemem_mapper_codies;
		break;
		case MAPPER_KOREA:
			cpu_writemem16 = writemem_mapper_korea;
		break;
		case MAPPER_KOREA_MSX:
			cpu_writemem16 = writemem_mapper_korea_msx;
		break;
		case MAPPER_4PAK:
			cpu_writemem16 = writemem_mapper_4pak;
		break;
		case MAPPER_93C46:
			cpu_writemem16 = writemem_mapper_93c46;
		break;
		case MAPPER_WONDERKID:
			cpu_writemem16 = writemem_mapper_wonderkid;
		break;
		case MAPPER_COLECO_MEGACART:
			cpu_writemem16 = writemem_mapper_none;
		break;
		case MAPPER_SYSTEME:
			cpu_writemem16 = writemem_mapper_systeme;
		break;
		case MAPPER_SYSTEM1:
			cpu_writemem16 = system1_writemem;
		break;
		case MAPPER_SNKPSYCHOS:
			cpu_writemem16 = snk_psychos_writemem;
		break;
		case MAPPER_TAITOL:
			cpu_writemem16 = taitol_writemem;
		break;
		default:
			cpu_writemem16 = writemem_mapper_sega;
		break;
	}
}

void mapper_restore_state(void)
{
    uint_fast8_t i;

    slot.rom = cart.rom;
    slot.pages = cart.pages;
    slot.mapper = cart.mapper;
    slot.fcr = &cart.fcr[0];

    mapper_reset();

#if MULTIREXZ80_ENABLE_COLECO
    if (sms.console == CONSOLE_COLECO)
    {
        /* Restore the fixed ColecoVision address map, then re-apply the
         * active MegaCart bank saved in the v2 state extension. */
        for (i = 0x00; i < 0x08; i++)
        {
            cpu_readmap[i]  = &coleco.rom[i << 10];
            cpu_writemap[i] = dummy_write;
        }
        for (i = 0x08; i < 0x18; i++)
        {
            cpu_readmap[i]  = dummy_read;
            cpu_writemap[i] = dummy_write;
        }
        for (i = 0x18; i < 0x20; i++)
        {
            cpu_readmap[i]  = &sms.wram[0];
            cpu_writemap[i] = &sms.wram[0];
        }

        if (slot.mapper == MAPPER_COLECO_MEGACART)
        {
            uint16_t saved_bank = slot.coleco_megacart_activebank;
            coleco_megacart_reset();
            coleco_megacart_map_active_bank(saved_bank);
        }
        else
        {
            for (i = 0x20; i < 0x40; i++)
            {
                cpu_readmap[i]  = &cart.rom[(i & 0x1F) << 10];
                cpu_writemap[i] = dummy_write;
            }
        }
        return;
    }
#endif

#if MULTIREXZ80_ENABLE_SORDM5
    if (sms.console == CONSOLE_SORDM5)
    {
        for (i = 0x00; i < 0x40; i++)
        {
            cpu_readmap[i]  = dummy_read;
            cpu_writemap[i] = dummy_write;
        }
        for (i = 0x00; i < 0x08; i++)
        {
            cpu_readmap[i]  = &coleco.rom[i << 10];
            cpu_writemap[i] = dummy_write;
        }
        for (i = 0x08; i < 0x1C; i++)
        {
            uint32_t cart_offset = (uint32_t)(i - 0x08) << 10;
            cpu_readmap[i]  = (cart.loaded && (cart_offset < cart.size)) ? &cart.rom[cart_offset] : dummy_read;
            cpu_writemap[i] = dummy_write;
        }
        for (i = 0x1C; i < 0x20; i++)
        {
            cpu_readmap[i]  = &sms.wram[(i - 0x1C) << 10];
            cpu_writemap[i] = &sms.wram[(i - 0x1C) << 10];
        }
        return;
    }
#endif

    if (sms.console == CONSOLE_SYSTEME)
    {
        systeme_map_memory(0);
        return;
    }

    if (sms.console == CONSOLE_SYSTEM1)
    {
        system1_memory_map(0);
        return;
    }

    if (sms.console == CONSOLE_SNKPSYCHOS)
    {
        snk_psychos_memory_map(0);
        return;
    }

    if (sms.console == CONSOLE_TAITOL)
    {
        taitol_memory_map(0);
        return;
    }

    if ((sms.console != CONSOLE_SG1000) && (sms.console != CONSOLE_SF7000) && (sms.console != CONSOLE_SC3000))
    {
        cpu_readmap[0] = &slot.rom[0];
        if (slot.mapper != MAPPER_KOREA_MSX)
        {
            mapper_16k_w(0, slot.fcr[0]);
            mapper_16k_w(1, slot.fcr[1]);
            mapper_16k_w(2, slot.fcr[2]);
            mapper_16k_w(3, slot.fcr[3]);
        }
        else
        {
            mapper_8k_w(0, slot.fcr[0]);
            mapper_8k_w(1, slot.fcr[1]);
            mapper_8k_w(2, slot.fcr[2]);
            mapper_8k_w(3, slot.fcr[3]);
        }
    }
}

void sms_init(void)
{
	CPUZ80_Init();
	eeprom93c46_init();
	/* Default: open bus */
	data_bus_pullup   = 0x00;
	data_bus_pulldown = 0x00;
	
	/* Initialize port handlers */
	switch(sms.console)
	{
#if MULTIREXZ80_ENABLE_COLECO
		case CONSOLE_COLECO:
			cpu_writeport16 = coleco_port_w;
			cpu_readport16 = coleco_port_r;
			data_bus_pullup = 0xFF;
		break;
#endif
		#if MULTIREXZ80_ENABLE_SORDM5
		case CONSOLE_SORDM5:
			cpu_writeport16 = sordm5_port_w;
			cpu_readport16 = sordm5_port_r;
			data_bus_pullup = 0xFF;
		break;
		#endif
		case CONSOLE_SG1000:
		case CONSOLE_SC3000:
		case CONSOLE_SF7000:
			cpu_writeport16 = tms_port_w;
			cpu_readport16 = tms_port_r;
			data_bus_pullup = 0xFF;
		break;

		case CONSOLE_SMS:
			cpu_writeport16 = sms_port_w;
			cpu_readport16 = sms_port_r;
		break;

		case CONSOLE_SMS2:
			cpu_writeport16 = sms_port_w;
			cpu_readport16 = sms_port_r;
			data_bus_pullup = 0xFF;
		break;

		case CONSOLE_SYSTEME:
			cpu_writeport16 = systeme_port_w;
			cpu_readport16 = systeme_port_r;
			data_bus_pullup = 0xFF;
		break;

		case CONSOLE_SYSTEM1:
			cpu_writeport16 = system1_port_w;
			cpu_readport16 = system1_port_r;
			data_bus_pullup = 0xFF;
		break;

		case CONSOLE_SNKPSYCHOS:
			cpu_writeport16 = snk_psychos_port_w;
			cpu_readport16 = snk_psychos_port_r;
			data_bus_pullup = 0xFF;
		break;

		case CONSOLE_TAITOL:
			cpu_writeport16 = taitol_port_w;
			cpu_readport16 = taitol_port_r;
			data_bus_pullup = 0xFF;
		break;

		case CONSOLE_GG:
			cpu_writeport16 = gg_port_w;
			cpu_readport16 = gg_port_r;
			data_bus_pullup = 0xFF;
		break;

		case CONSOLE_GGMS:
			cpu_writeport16 = ggms_port_w;
			cpu_readport16 = ggms_port_r;
			data_bus_pullup = 0xFF;
		break;

		case CONSOLE_GEN:
		case CONSOLE_MD:
			cpu_writeport16 = md_port_w;
			cpu_readport16 = md_port_r;
		break;

		case CONSOLE_GENPBC:
		case CONSOLE_MDPBC:
			cpu_writeport16 = md_port_w;
			cpu_readport16 = md_port_r;
			data_bus_pullup = 0xFF;
		break;
	}
}

void sms_shutdown(void)
{
	system1_free();
	snk_psychos_free();
	taitol_free();
}

void sms_reset(void)
{
  uint_fast8_t i;

  /* reset Z80 state */
  CPUZ80_Reset();

  /* clear SMS context */
  memset(dummy_write,data_bus_pullup, sizeof(dummy_write));
  memset(dummy_read,data_bus_pullup, sizeof(dummy_read));
  /* The joys of uninitiliazed memory ! The Japanese and Korean BIOS do not properly clear the memory and some games like Alibaba and 40 Thieves relies on this.
   * Without said fix, Alibaba will flicker (or it seems that it constantly resets ?). See the thread about this behaviour here :
   * https://www.smspower.org/forums/13333-UninitializedMemoryPatterns
   * 
   * Megadrive : To be confirmed. It seems that on a flashcart at least RAM gets cleared
   * but to be tested properly, this should be tested on a rewriteable cartridge.
   * */
  if (sms.territory == TERRITORY_DOMESTIC && sms.console == CONSOLE_SMS)
  {
	  memset(sms.wram, 0xF0, sizeof(sms.wram));
  }
  else
  {
	  memset(sms.wram, 0, sizeof(sms.wram));
  }

  sms.paused    = 0x00;
  sms.save      = 0x00;
  sms.fm_detect = 0x00;
  sms.ioctrl    = 0xFF;
  sms.hlatch    = 0x00;
  sms.memctrl   = 0xAB;

  /* enable Cartridge ROM by default*/
  slot.rom      = cart.rom;
  slot.pages    = cart.pages;
  slot.mapper   = cart.mapper;
  slot.fcr      = &cart.fcr[0];

  if (cart.mapper == MAPPER_93C46)
  {
    eeprom93c46_reset();
  }

  /* reset Memory Mapping */
  switch(sms.console)
  {
#if MULTIREXZ80_ENABLE_COLECO
    case CONSOLE_COLECO:
    {
      /* $0000-$1FFF mapped to internal ROM (8K) */
      for(i = 0x00; i < 0x08; i++)
      {
        cpu_readmap[i]  = &coleco.rom[i << 10];
        cpu_writemap[i] = dummy_write;
      }

      /* $2000-$5FFF mapped to expansion */
      for(i = 0x08; i < 0x18; i++)
      {
        cpu_readmap[i]  = dummy_read;
        cpu_writemap[i] = dummy_write;
      }

      /* $6000-$7FFF mapped to RAM (1K mirrored) */
      for(i = 0x18; i < 0x20; i++)
      {
        cpu_readmap[i]  = &sms.wram[0];
        cpu_writemap[i] = &sms.wram[0];
      }

      if (cart.mapper == MAPPER_COLECO_MEGACART)
      {
        coleco_megacart_reset();
      }
      else
      {
        /* $8000-$FFFF mapped to Cartridge ROM (max. 32K) */
        for(i = 0x20; i < 0x40; i++)
        {
          cpu_readmap[i]  = &cart.rom[(i&0x1F) << 10];
          cpu_writemap[i] = dummy_write;
        }
      }

      /* reset I/O */
      coleco.keypad[0] = 0xf0;
      coleco.keypad[1] = 0xf0;
      coleco.pio_mode  = 0x00;

      break;
    }
#endif

	#if MULTIREXZ80_ENABLE_SORDM5
    case CONSOLE_SORDM5:
    {
      /*
       * Sord M5 base map:
       *   $0000-$1FFF : 8 KB monitor BIOS
       *   $2000-$6FFF : cartridge ROM area
       *   $7000-$7FFF : 4 KB internal RAM
       * Everything else is left as open bus for the stock machine.
       */
      for(i = 0x00; i < 0x40; i++)
      {
        cpu_readmap[i]  = dummy_read;
        cpu_writemap[i] = dummy_write;
      }

      sordm5_ctc_reset();

      /* $0000-$1FFF mapped to internal ROM (8 KB) */
      for(i = 0x00; i < 0x08; i++)
      {
        cpu_readmap[i]  = &coleco.rom[i << 10];
        cpu_writemap[i] = dummy_write;
      }

      /* $2000-$6FFF mapped to cartridge ROM. */
      for(i = 0x08; i < 0x1C; i++)
      {
        uint32_t cart_offset = (uint32_t)(i - 0x08) << 10;
        cpu_readmap[i]  = (cart.loaded && (cart_offset < cart.size)) ? &cart.rom[cart_offset] : dummy_read;
        cpu_writemap[i] = dummy_write;
      }

      /* $7000-$7FFF mapped to internal RAM (4 KB) */
      for(i = 0x1C; i < 0x20; i++)
      {
        cpu_readmap[i]  = &sms.wram[(i - 0x1C) << 10];
        cpu_writemap[i] = &sms.wram[(i - 0x1C) << 10];
      }

      #ifndef MULTIREXZ80_HEADLESS
      printf("Sord M5 mode\n");
      #endif
      break;
    }
    #endif
    case CONSOLE_SYSTEME:
    {
      systeme_map_memory(1);
      break;
    }

    case CONSOLE_SYSTEM1:
    {
      system1_reset();
      break;
    }

    case CONSOLE_SNKPSYCHOS:
    {
      snk_psychos_reset();
      break;
    }

    case CONSOLE_TAITOL:
    {
      taitol_reset();
      break;
    }

    case CONSOLE_SC3000:
    case CONSOLE_SF7000:
    {
      /* $0000-$7FFF mapped to cartridge ROM (max. 32K) */
      for(i = 0x00; i < 0x20; i++)
      {
        cpu_readmap[i]  = &cart.rom[i << 10];
        cpu_writemap[i] = dummy_write;
      }

      /* $8000-$BFFF mapped to external RAM (lower 16K) */
      for(i = 0x20; i < 0x30; i++)
      {
        cpu_readmap[i]  = &cart.sram[(i & 0x0F) << 10];
        cpu_writemap[i] = &cart.sram[(i & 0x0F) << 10];
      }

      /* $C000-$FFFF mapped to internal RAM (2K) or external RAM (upper 16K) */
      for(i = 0x30; i < 0x40; i++)
      {
        cpu_readmap[i]  = &cart.sram[0x4000 + ((i & 0x0F) << 10)];
        cpu_writemap[i] = &cart.sram[0x4000 + ((i & 0x0F) << 10)];
      }

      break;
    }

    default:
    {
      /* SMS BIOS support */
      if (IS_SMS)
      {
        if (bios.enabled == 3)
        {
          /* reset BIOS paging */
          bios.fcr[0] = 0;
          bios.fcr[1] = 0;
          bios.fcr[2] = 1;
          bios.fcr[3] = 2;

          /* enable BIOS ROM */
          slot.rom    = bios.rom;
          slot.pages  = bios.pages;
          slot.mapper = MAPPER_SEGA;
          slot.fcr    = &bios.fcr[0];
          sms.memctrl = 0xE0;
        }
        else
        {
          /* save Memory Control register value in RAM */
          sms.wram[0] = sms.memctrl;
        }
      }

      /* default cartridge ROM mapping at $0000-$BFFF (first 32k mirrored) */
      for(i = 0x00; i <= 0x2F; i++)
      {
        cpu_readmap[i]  = &slot.rom[(i & 0x1F) << 10];
        cpu_writemap[i] = dummy_write;
      }

      /* enable internal RAM at $C000-$FFFF (8k mirrored) */
      for(i = 0x30; i <= 0x3F; i++)
      {
          cpu_readmap[i] = &sms.wram[(i & 0x07) << 10];
          cpu_writemap[i] = &sms.wram[(i & 0x07) << 10];
      }

      /* reset cartridge paging registers */
      switch(cart.mapper)
      {
        case MAPPER_NONE:
        case MAPPER_SEGA:
        case MAPPER_93C46:
          cart.fcr[0] = 0;
          cart.fcr[1] = 0;
          cart.fcr[2] = 1;
          cart.fcr[3] = 2;
          break;

        case MAPPER_WONDERKID:
          /* This prototype's mapper powers up with every bank register at 0. */
          cart.fcr[0] = 0;
          cart.fcr[1] = 0;
          cart.fcr[2] = 0;
          cart.fcr[3] = 0;
          break;

        default:
          cart.fcr[0] = 0;
          cart.fcr[1] = 0;
          cart.fcr[2] = 1;
          cart.fcr[3] = 0;
          break;
      }

      /* reset memory map */
      if (slot.mapper != MAPPER_KOREA_MSX)
      {
        mapper_16k_w(0,slot.fcr[0]);
        mapper_16k_w(1,slot.fcr[1]);
        mapper_16k_w(2,slot.fcr[2]);
        mapper_16k_w(3,slot.fcr[3]);
      }
      else
      {
        mapper_8k_w(0,slot.fcr[0]);
        mapper_8k_w(1,slot.fcr[1]);
        mapper_8k_w(2,slot.fcr[2]);
        mapper_8k_w(3,slot.fcr[3]);
      }
      break;
    }
  }

  /* reset SLOT mapper */
  mapper_reset();
}

void mapper_8k_w(uint16_t address, uint8_t data)
{
	uint_fast8_t i;

	/* cartridge ROM page (8k) index */
	uint16_t page = data % (slot.pages << 1);
  
	/* Save frame control register data */
	slot.fcr[address] = data;

	switch (address & 3)
	{
		case 0: /* cartridge ROM bank (16k) at $8000-$9FFF */
		for(i = 0x20; i <= 0x27; i++)
		{
			cpu_readmap[i] = &slot.rom[(page << 13) | ((i & 0x07) << 10)];
		}
		break;
		case 1: /* cartridge ROM bank (16k) at $A000-$BFFF */
		for(i = 0x28; i <= 0x2F; i++)
		{
			cpu_readmap[i] = &slot.rom[(page << 13) | ((i & 0x07) << 10)];
		}
		break;
		case 2: /* cartridge ROM bank (16k) at $4000-$5FFF */
		for(i = 0x10; i <= 0x17; i++)
		{
			cpu_readmap[i] = &slot.rom[(page << 13) | ((i & 0x07) << 10)];
		}
		break;
		case 3: /* cartridge ROM bank (16k) at $6000-$7FFF */
		for(i = 0x18; i <= 0x1F; i++)
		{
			cpu_readmap[i] = &slot.rom[(page << 13) | ((i & 0x07) << 10)];
		}
		break;
	}
}
    
void mapper_16k_w(uint16_t address, uint8_t data)
{
	extern uint8_t gaiden_hack;
	uint_fast8_t i;

	/* cartridge ROM page (16k) index */
	uint16_t page = data % slot.pages;
  
	/* page index increment (SEGA mapper) */
	
	/* 
		About the "gaiden_hack" bit :
		I initially thought that it was a hack.
		But it turns out the translation hack actually expects this :
		https://github.com/libretro/multirexz80-gx/issues/25
		
		TODO : Probably reimplement this ?
	*/
	if (slot.fcr[0] & 0x03 && gaiden_hack != 1)
	{
		page = (page + ((4 - (slot.fcr[0] & 0x03)) << 3)) % slot.pages;
	}
	
	/* save frame control register data */
	slot.fcr[address] = data;

	switch (address)
	{
		case 0: /* control register (SEGA mapper) */
		if(data & 0x08)
		{
			/* external RAM (upper or lower 16K) mapped at $8000-$BFFF */
			int32_t offset = (data & 0x04) ? 0x4000 : 0x0000;
			for(i = 0x20; i <= 0x2F; i++)
			{
				cpu_readmap[i] = cpu_writemap[i] = &cart.sram[offset + ((i & 0x0F) << 10)];
			}
			sms.save = 1;
		}
		else
		{
			page = slot.fcr[3] % slot.pages;
			/* page index increment (SEGA mapper) */
			if (slot.fcr[0] & 0x03)
			{
				page = (page + ((4 - (slot.fcr[0] & 0x03)) << 3)) % slot.pages;
			}

			/* cartridge ROM mapped at $8000-$BFFF */
			for(i = 0x20; i <= 0x2F; i++)
			{
				cpu_readmap[i] = &slot.rom[(page << 14) | ((i & 0x0F) << 10)];
				cpu_writemap[i] = dummy_write;
			}
		}
		
		if(data & 0x10)
		{
			/* external RAM (lower 16K) mapped at $C000-$FFFF */
			for(i = 0x30; i <= 0x3F; i++)
			{
			  cpu_writemap[i] = cpu_readmap[i]  = &cart.sram[(i & 0x0F) << 10];
			}
			sms.save = 1;
		}
		else
		{
			/* internal RAM (8K mirrorred) mapped at $C000-$FFFF */
			for(i = 0x30; i <= 0x3F; i++)
			{
			  cpu_writemap[i] = cpu_readmap[i] = &sms.wram[(i & 0x07) << 10];
			}
		}
		break;
		case 1: /* cartridge ROM bank (16k) at $0000-$3FFF */
		/* first 1k is not fixed (CODEMASTER and 4PAK mapper) */
		if (slot.mapper == MAPPER_CODIES || slot.mapper == MAPPER_4PAK)
		{
			cpu_readmap[0] = &slot.rom[(page << 14)];
		}

		for(i = 0x01; i <= 0x0F; i++)
		{
			cpu_readmap[i] = &slot.rom[(page << 14) | ((i & 0x0F) << 10)];
		}
		break;
		case 2: /* cartridge ROM bank (16k) at $4000-$7FFF */
		for(i = 0x10; i <= 0x1F; i++)
		{
			cpu_readmap[i] = &slot.rom[(page << 14) | ((i & 0x0F) << 10)];
		}

		/* Ernie Elf's Golf external RAM switch */
		if (slot.mapper == MAPPER_CODIES)
		{
			if (data & 0x80)
			{
				/* external RAM (8k) mapped at $A000-$BFFF */
				for(i = 0x28; i <= 0x2F; i++)
				{
					cpu_writemap[i] = cpu_readmap[i]  = &cart.sram[(i & 0x0F) << 10];
				}
				sms.save = 1;
			}
			else
			{
				/* cartridge ROM mapped at $A000-$BFFF */
				for(i = 0x28; i <= 0x2F; i++)
				{
					cpu_readmap[i] = &slot.rom[((slot.fcr[3] % slot.pages) << 14) | ((i & 0x0F) << 10)];
					cpu_writemap[i] = dummy_write;
				}
			}
		}
		break;
		case 3: /* cartridge ROM bank (16k) at $8000-$BFFF */
		/* check that external RAM (16k) is not mapped at $8000-$BFFF (SEGA mapper) */
		if ((slot.fcr[0] & 0x08)) break;

		/* first 8k */
		for(i = 0x20; i <= 0x27; i++)
		{
			cpu_readmap[i] = &slot.rom[(page << 14) | ((i & 0x0F) << 10)];
		}

		/* check that external RAM (8k) is not mapped at $A000-$BFFF (CODEMASTER mapper) */
		if ((slot.mapper == MAPPER_CODIES) && (slot.fcr[2] & 0x80)) break;

		/* last 8k */
		for(i = 0x28; i <= 0x2F; i++)
		{
			cpu_readmap[i] = &slot.rom[(page << 14) | ((i & 0x0F) << 10)];
		}
		break;
	}
}

int32_t sms_irq_callback(int32_t param)
{
	#if MULTIREXZ80_ENABLE_SORDM5
	if (sms.console == CONSOLE_SORDM5)
		return sordm5_ctc_irq_callback();
	#endif
	#if MULTIREXZ80_ENABLE_ARCADE
	if (sms.console == CONSOLE_TAITOL)
		return taitol_irq_callback(param);
	#endif
	return 0xFF;
}
