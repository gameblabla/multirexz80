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

/*
 * Sega System 1 support for MultiRexZ80.
 *
 * Hardware behavior is derived from MAME's Sega System 1 driver
 * (src/mame/sega/system1.cpp and system1_v.cpp).  MAME source license:
 * BSD-3-Clause.  Original copyright-holders: Jarek Parchanski, Nicola
 * Salmoria and Mirko Buffoni.  This is a small
 * independent C implementation for this emulator: it reuses the existing Z80
 * core and implements the System 1 address map, inputs, Block Gal ROM layout,
 * and a compact System 1 renderer.
 */

#include "shared.h"

#define SYSTEM1_MAIN_FIXED_SIZE 0x8000u
#define SYSTEM1_MAIN_ROM_SIZE   0x20000u
#define SYSTEM1_RAM_SIZE        0x1000u
#define SYSTEM1_SPRITERAM_SIZE  0x0800u
#define SYSTEM1_PALRAM_SIZE     0x0800u
#define SYSTEM1_VRAM_SIZE       0x4000u
#define SYSTEM1_TILE_ROM_SIZE   0x18000u
#define SYSTEM1_SPRITE_ROM_SIZE 0x20000u
#define SYSTEM1_SOUND_ROM_SIZE  0x10000u
#define SYSTEM1_SOUND_RAM_SIZE   0x0800u

#define SYSTEM1_CPU_MAIN  0
#define SYSTEM1_CPU_SOUND 1
#define SYSTEM1_CPU_COUNT 2
#define SYSTEM1_PROM_SIZE       0x0100u
#define SYSTEM1_MIXCOL_SIZE     0x0040u
#define SYSTEM1_SPRCOL_SIZE     0x0400u

typedef struct
{
    Z80_Regs regs;
    int32_t cycles;
    uint8_t *readmap[64];
    uint8_t *writemap[64];
} system1_z80_context_t;

typedef struct
{
    uint8_t *ram;
    uint8_t *spriteram;
    uint8_t *paletteram;
    uint8_t *videoram;
    uint8_t *opcode_rom;
    uint8_t *data_readmap[64];
    uint8_t *tile_rom;
    uint32_t tile_rom_loaded;
    uint8_t *tile_row_cache;
    uint32_t tile_cache_count;
    uint8_t tile_cache_dirty;
    uint8_t *sprite_rom;
    uint8_t *sound_rom;
    uint8_t *sound_ram;
    uint32_t sprite_rom_loaded;
    uint8_t *prom;
    uint8_t *color_prom;
    uint32_t pen_cache[0x800];
    uint8_t pen_cache_dirty;
    uint16_t *sprite_line;
    uint8_t *mix_collision;
    uint8_t *sprite_collision;
    uint8_t mix_collision_summary;
    uint8_t sprite_collision_summary;
    uint16_t collision_partial_line;
    uint8_t bank;
    uint8_t video_mode;
    uint8_t vram_bank;
    uint8_t video_type;
    uint8_t rowscroll;
    uint8_t bank_mode;
    uint8_t tilemap_pages;
    uint8_t sound_latch;
    uint8_t sound_nmi_asserted;
    uint8_t sound_irq_asserted;
    system1_z80_context_t cpu[SYSTEM1_CPU_COUNT];
    uint8_t current_cpu;
    uint16_t main_cycles_per_line;
    uint8_t pio_a;
    uint8_t pio_b;
    uint8_t pio_ctrl_a;
    uint8_t pio_ctrl_b;
    uint8_t dip_a;
    uint8_t dip_b;
    uint8_t game;
    uint8_t uses_dial;
    uint8_t button_swap; /* MAME: some games swap BUTTON1/BUTTON2 bit positions */
    uint8_t color_prom_present;
    uint8_t rotate;
} system1_state_t;

static system1_state_t s1;

static void system1_update_collisions_now(void);
static uint8_t system1_collision_r_value(const uint8_t *ram, uint16_t offset, uint8_t summary);

static uint8_t *xcalloc(size_t n, size_t s)
{
    uint8_t *p = (uint8_t *)calloc(n, s);
    return p;
}

int system1_alloc(void)
{
    if (s1.ram) return 1;
    s1.ram = xcalloc(SYSTEM1_RAM_SIZE, 1);
    s1.spriteram = xcalloc(SYSTEM1_SPRITERAM_SIZE, 1);
    s1.paletteram = xcalloc(SYSTEM1_PALRAM_SIZE, 1);
    s1.videoram = xcalloc(SYSTEM1_VRAM_SIZE, 1);
    s1.opcode_rom = xcalloc(SYSTEM1_MAIN_ROM_SIZE, 1);
    s1.tile_rom = xcalloc(SYSTEM1_TILE_ROM_SIZE, 1);
    s1.tile_row_cache = xcalloc((SYSTEM1_TILE_ROM_SIZE / 24u) * 8u * 8u, 1);
    s1.sprite_rom = xcalloc(SYSTEM1_SPRITE_ROM_SIZE, 1);
    s1.sound_rom = xcalloc(SYSTEM1_SOUND_ROM_SIZE, 1);
    s1.sound_ram = xcalloc(SYSTEM1_SOUND_RAM_SIZE, 1);
    s1.prom = xcalloc(SYSTEM1_PROM_SIZE, 1);
    s1.color_prom = xcalloc(0x300, 1);
    s1.sprite_line = (uint16_t *)calloc((size_t)SYSTEM1_RAW_WIDTH * SYSTEM1_VISIBLE_HEIGHT, sizeof(uint16_t));
    s1.mix_collision = xcalloc(SYSTEM1_MIXCOL_SIZE, 1);
    s1.sprite_collision = xcalloc(SYSTEM1_SPRCOL_SIZE, 1);
    if (!s1.ram || !s1.spriteram || !s1.paletteram || !s1.videoram ||
        !s1.opcode_rom || !s1.tile_rom || !s1.tile_row_cache || !s1.sprite_rom || !s1.sound_rom || !s1.sound_ram || !s1.prom || !s1.color_prom ||
        !s1.sprite_line || !s1.mix_collision || !s1.sprite_collision)
    {
        system1_free();
        return 0;
    }
    memset(s1.opcode_rom, 0xff, SYSTEM1_MAIN_ROM_SIZE);
    memset(s1.tile_rom, 0xff, SYSTEM1_TILE_ROM_SIZE);
    s1.tile_rom_loaded = 0;
    s1.tile_cache_count = 0;
    s1.tile_cache_dirty = 1;
    s1.pen_cache_dirty = 1;
    memset(s1.sprite_rom, 0xff, SYSTEM1_SPRITE_ROM_SIZE);
    s1.sprite_rom_loaded = 0;
    memset(s1.sound_rom, 0xff, SYSTEM1_SOUND_ROM_SIZE);
    memset(s1.prom, 0xff, SYSTEM1_PROM_SIZE);
    memset(s1.color_prom, 0xff, 0x300);
    s1.dip_a = 0xff;
    s1.dip_b = 0xff;
    s1.tilemap_pages = 2;
    return 1;
}

void system1_free(void)
{
    free(s1.ram); free(s1.spriteram); free(s1.paletteram); free(s1.videoram);
    free(s1.opcode_rom); free(s1.tile_rom); free(s1.tile_row_cache); free(s1.sprite_rom); free(s1.sound_rom); free(s1.sound_ram); free(s1.prom); free(s1.color_prom);
    free(s1.sprite_line); free(s1.mix_collision); free(s1.sprite_collision);
    memset(&s1, 0, sizeof(s1));
}

void system1_clear_roms(void)
{
    if (!system1_alloc()) return;
    memset(s1.tile_rom, 0xff, SYSTEM1_TILE_ROM_SIZE);
    s1.tile_rom_loaded = 0;
    s1.tile_cache_count = 0;
    s1.tile_cache_dirty = 1;
    s1.pen_cache_dirty = 1;
    memset(s1.sprite_rom, 0xff, SYSTEM1_SPRITE_ROM_SIZE);
    s1.sprite_rom_loaded = 0;
    memset(s1.sound_rom, 0xff, SYSTEM1_SOUND_ROM_SIZE);
    memset(s1.prom, 0xff, SYSTEM1_PROM_SIZE);
    memset(s1.color_prom, 0xff, 0x300);
    s1.color_prom_present = 0;
}

int system1_set_region(int region, uint32_t offset, const uint8_t *data, uint32_t size)
{
    uint8_t *dst = NULL;
    uint32_t limit = 0;
    if (!system1_alloc() || !data) return 0;
    switch (region)
    {
        case SYSTEM1_REGION_TILES:   dst = s1.tile_rom;   limit = SYSTEM1_TILE_ROM_SIZE; break;
        case SYSTEM1_REGION_SPRITES: dst = s1.sprite_rom; limit = SYSTEM1_SPRITE_ROM_SIZE; break;
        case SYSTEM1_REGION_SOUND:   dst = s1.sound_rom;  limit = SYSTEM1_SOUND_ROM_SIZE; break;
        case SYSTEM1_REGION_PROM:    dst = s1.prom;       limit = SYSTEM1_PROM_SIZE; break;
        case SYSTEM1_REGION_OPCODES: dst = s1.opcode_rom; limit = SYSTEM1_MAIN_ROM_SIZE; break;
        case SYSTEM1_REGION_COLOR:   dst = s1.color_prom; limit = 0x300; s1.color_prom_present = 1; s1.pen_cache_dirty = 1; break;
        default: return 0;
    }
    if (offset > limit || size > limit - offset) return 0;
    memcpy(dst + offset, data, size);
    if (region == SYSTEM1_REGION_TILES)
    {
        if (offset + size > s1.tile_rom_loaded)
            s1.tile_rom_loaded = offset + size;
        s1.tile_cache_dirty = 1;
    }
    if (region == SYSTEM1_REGION_SPRITES && offset + size > s1.sprite_rom_loaded)
        s1.sprite_rom_loaded = offset + size;
    return 1;
}

enum { SYSTEM1_BANK_FIXED = 0, SYSTEM1_BANK_44 = 1, SYSTEM1_BANK_0C = 2 };

static void system1_set_game_common(uint8_t game, uint8_t video_type, uint8_t rowscroll, uint8_t bank_mode, uint8_t pages, uint8_t rotate, uint8_t dip_a, uint8_t dip_b, uint8_t uses_dial)
{
    s1.game = game;
    s1.video_type = video_type;
    s1.rowscroll = rowscroll;
    s1.bank_mode = bank_mode;
    s1.tilemap_pages = pages;
    s1.rotate = rotate;
    s1.dip_a = dip_a;
    s1.dip_b = dip_b;
    s1.uses_dial = uses_dial;
    s1.button_swap = 0;
    s1.main_cycles_per_line = SYSTEM1_CYCLES_PER_LINE;
}

static void system1_set_game_common_swapped(uint8_t game, uint8_t video_type, uint8_t rowscroll, uint8_t bank_mode, uint8_t pages, uint8_t rotate, uint8_t dip_a, uint8_t dip_b)
{
    system1_set_game_common(game, video_type, rowscroll, bank_mode, pages, rotate, dip_a, dip_b, 0);
    s1.button_swap = 1;
}

void system1_set_game_blockgal(void)
{
    /* Decrypted blockgalb in MAME is a System 2 map using the Choplifter DIP
     * order: SWA has game options and SWB has coinage.  Block Gal is ROT90
     * and uses an 8-bit dial plus a fire button on the SYSTEM port. */
    system1_set_game_common(1, 2, 0, SYSTEM1_BANK_FIXED, 8, SYSTEM1_ROTATE_CW, 0xd6, 0xff, 1);
}

void system1_set_game_blockgal_mc8123(void)
{
    /* Encrypted parent blockgal is a System 1 PIO/MC8123 board in MAME,
     * not the System 2 bootleg map.  SWA is read through the Block Gal
     * custom PIO map at ports 0x0d/0x0f; SWB remains available at 0x10-0x13. */
    system1_set_game_common(4, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_CW, 0xff, 0xd6, 1);
}

void system1_set_game_choplifter(void)
{
    /* System 2 rowscroll game.  SWA contains game options, SWB coinage. */
    system1_set_game_common(2, 2, 1, SYSTEM1_BANK_0C, 8, SYSTEM1_ROTATE_NONE, 0x3c, 0xff, 0);
}

void system1_set_game_flicky(void)
{
    /* Flicky 128K parent is a System 1 PIO board using Sega 315-5051
     * encrypted opcodes.  The 128K set uses two tilemap pages and the
     * standard 256x224 non-scrolling visible area. */
    system1_set_game_common(5, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_NONE, 0xff, 0xff, 0);
}

void system1_set_game_brain(void)
{
    /* PIO System 1 with bank 44 layout. */
    system1_set_game_common(3, 1, 0, SYSTEM1_BANK_44, 2, SYSTEM1_ROTATE_NONE, 0xff, 0xff, 0);
}

void system1_set_game_teddybb(void)
{
    /* MAME swaps BUTTON1/BUTTON2 vs the generic System 1 mapping. */
    system1_set_game_common_swapped(6, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_NONE, 0xff, 0xff);
}

void system1_set_game_wboy(void)
{
    /* MAME swaps BUTTON1/BUTTON2 vs the generic System 1 mapping. */
    system1_set_game_common_swapped(7, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_NONE, 0xff, 0xff);
}

void system1_set_game_wbml(void)
{
    /* Wonder Boy: Monster Land is a System 2 MC-8123 board in MAME
     * (sys2x): System 2 video without rowscroll, bank-0c external program
     * banks, and standard horizontal active output.  SWA contains gameplay
     * options, SWB coinage. */
    system1_set_game_common(10, 2, 0, SYSTEM1_BANK_0C, 8, SYSTEM1_ROTATE_NONE, 0xfe, 0xff, 0);
    /* MC-8123 hardware stretches opcode fetches (/M1 wait states).
     * MAME models this with adjust_cycles(); approximate the net beam/CPU
     * phase here without doing per-fetch runtime decryption.  WBML relies on
     * this timing for its early VRAM/palette initialization. */
    s1.main_cycles_per_line = 242;
}

void system1_set_game_gardia(void)
{
    /* MAME's Gardia defaults SWB to 0x7c: upright, demo sounds on,
     * three lives, first bonus table, easy difficulty, and the manual's
     * "always on" SWB:8 value low. */
    system1_set_game_common(8, 1, 0, SYSTEM1_BANK_44, 2, SYSTEM1_ROTATE_CCW, 0xff, 0x7c, 0);
}

void system1_set_game_ufosensi(void)
{
    system1_set_game_common(9, 2, 1, SYSTEM1_BANK_0C, 8, SYSTEM1_ROTATE_NONE, 0xff, 0xff, 0);
}


void system1_set_game_starjack(void)
{
    /* System 1 PPI scrolling board, ROT270 in MAME. */
    system1_set_game_common(11, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_CCW, 0xff, 0xff, 0);
}

void system1_set_game_upndown(void)
{
    /* System 1 PPI scrolling board, encrypted 315-5030/5098, ROT270. */
    system1_set_game_common(12, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_CCW, 0xff, 0xff, 0);
}

void system1_set_game_swat(void)
{
    /* System 1 PPI board, encrypted 315-5048, ROT270. */
    system1_set_game_common(13, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_CCW, 0xff, 0xff, 0);
}

void system1_set_game_wmatch(void)
{
    /* System 1 PPI scrolling board, encrypted 315-5064, ROT270. */
    system1_set_game_common(14, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_CCW, 0xff, 0xff, 0);
}

void system1_set_game_spatter(void)
{
    /* System 1 PIO scrolling board, encrypted 315-5096. */
    system1_set_game_common(15, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_NONE, 0xff, 0xff, 0);
}

void system1_set_game_pitfall2(void)
{
    /* System 1 PIO board, encrypted 315-5093. */
    system1_set_game_common(16, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_NONE, 0xff, 0xff, 0);
}

void system1_set_game_seganinj(void)
{
    /* System 1 PIO board, encrypted 315-5102. */
    system1_set_game_common(17, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_NONE, 0xff, 0xff, 0);
}

void system1_set_game_imsorry(void)
{
    /* System 1 PIO board, encrypted 315-5110. */
    system1_set_game_common(18, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_NONE, 0xff, 0xff, 0);
}

void system1_set_game_myhero(void)
{
    /* System 1 PIO board, unencrypted parent.
     * MAME swaps BUTTON1/BUTTON2 vs the generic System 1 mapping. */
    system1_set_game_common_swapped(19, 1, 0, SYSTEM1_BANK_FIXED, 2, SYSTEM1_ROTATE_NONE, 0xff, 0xff);
}

void system1_set_game_nob(void)
{
    /* Noboranka uses a non-standard System 1 map and an 8751.  The loader
     * still uses the normal active renderer; protection is approximated in
     * the port/memory layer below. */
    system1_set_game_common(20, 1, 0, SYSTEM1_BANK_44, 2, SYSTEM1_ROTATE_CCW, 0xff, 0xff, 0);
}

int system1_uses_dial(void)
{
    return s1.uses_dial != 0;
}

static void system1_update_dial_from_dpad(void)
{
    int port;
    if (!system1_uses_dial()) return;

    for (port = 0; port < 2; port++)
    {
        int32_t x = input.analog[port][0];
        int32_t step = (input.pad[port] & INPUT_BUTTON2) ? 8 : 4;

        /* MAME marks Block Gal's dial PORT_REVERSE.  Keyboard/d-pad
         * emulation therefore increments on LEFT and decrements on RIGHT. */
        if (input.pad[port] & INPUT_LEFT)  x += step;
        if (input.pad[port] & INPUT_RIGHT) x -= step;

        /* Block Gal uses an 8-bit dial.  Let it wrap, matching the
         * hardware-style free-spinning control better than clamping. */
        input.analog[port][0] = x & 0xff;
    }
}


static void system1_load_cpu(int which)
{
    s1.current_cpu = (uint8_t)which;
    z80_select_context(&s1.cpu[which].regs, &s1.cpu[which].cycles);
    if (which == SYSTEM1_CPU_MAIN)
        z80_select_memory_maps(cpu_readmap, cpu_writemap);
    else
        z80_select_memory_maps(s1.cpu[which].readmap, s1.cpu[which].writemap);
    z80_data_operand_fetch = (which == SYSTEM1_CPU_MAIN) ? 1 : 0;
}

static void system1_set_sound_nmi(int assert_line)
{
    int state = assert_line ? ASSERT_LINE : CLEAR_LINE;
    s1.sound_nmi_asserted = assert_line ? 1 : 0;
    if (s1.current_cpu == SYSTEM1_CPU_SOUND)
    {
        z80_set_irq_line(INPUT_LINE_NMI, state);
    }
    else
    {
        if (state == ASSERT_LINE && s1.cpu[SYSTEM1_CPU_SOUND].regs.nmi_state == CLEAR_LINE)
            s1.cpu[SYSTEM1_CPU_SOUND].regs.nmi_pending = 1;
        s1.cpu[SYSTEM1_CPU_SOUND].regs.nmi_state = state;
    }
}

static void system1_set_sound_irq(int assert_line)
{
    s1.sound_irq_asserted = assert_line ? 1 : 0;
    if (s1.current_cpu == SYSTEM1_CPU_SOUND)
        z80_set_irq_line(INPUT_LINE_IRQ0, assert_line ? ASSERT_LINE : CLEAR_LINE);
    else
        s1.cpu[SYSTEM1_CPU_SOUND].regs.irq_state = assert_line ? ASSERT_LINE : CLEAR_LINE;
}

static int32_t system1_irq_callback(int32_t param)
{
    (void)param;
    if (s1.current_cpu == SYSTEM1_CPU_SOUND)
        system1_set_sound_irq(0);
    return 0xff;
}

static void system1_map_sound_cpu(void)
{
    uint_fast8_t i;
    system1_z80_context_t *c = &s1.cpu[SYSTEM1_CPU_SOUND];
    for (i = 0; i < 64; i++)
    {
        c->readmap[i] = dummy_read;
        c->writemap[i] = dummy_write;
    }
    for (i = 0; i < 0x20; i++)
        c->readmap[i] = s1.sound_rom + ((uint32_t)i << 10);
    for (i = 0x20; i < 0x28; i++)
    {
        uint32_t off = ((uint32_t)(i & 1) << 10);
        c->readmap[i] = s1.sound_ram + off;
        c->writemap[i] = s1.sound_ram + off;
    }
}

static void system1_reset_one_cpu(int which)
{
    system1_load_cpu(which);
    z80_reset();
    z80_get_context()->irq_callback = system1_irq_callback;
    z80_set_irq_line(INPUT_LINE_IRQ0, CLEAR_LINE);
    z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);
    s1.cpu[which].cycles = 0;
}

static void map_1k(uint8_t page, uint8_t *data_base, uint8_t *opcode_base, uint8_t *write_base)
{
    s1.data_readmap[page] = data_base ? data_base : dummy_read;
    cpu_readmap[page] = opcode_base ? opcode_base : s1.data_readmap[page];
    cpu_writemap[page] = write_base ? write_base : dummy_write;
}

void system1_bank_w(uint8_t data)
{
    uint32_t available;
    uint32_t bank_count;
    uint32_t base;
    uint_fast8_t i;

    s1.bank = data & 0x03;
    if (s1.bank_mode == SYSTEM1_BANK_FIXED)
    {
        /* Plain System 1 boards leave 0x8000-0xbfff fixed to the ROM at 0x8000;
         * plain System 2 boards with external banks start at 0x10000 and keep bank 0
         * selected unless a game explicitly uses the 0x0c bank latch. */
        if (s1.video_type == 2 && cart.size > 0x10000u)
            base = 0x10000u;
        else
            base = (cart.size > SYSTEM1_MAIN_FIXED_SIZE) ? SYSTEM1_MAIN_FIXED_SIZE : 0;
    }
    else if (s1.bank_mode == SYSTEM1_BANK_0C)
    {
        available = (cart.size > 0x10000u) ? (cart.size - 0x10000u) : ((cart.size > SYSTEM1_MAIN_FIXED_SIZE) ? (cart.size - SYSTEM1_MAIN_FIXED_SIZE) : 0);
        bank_count = available / 0x4000u;
        if (!bank_count) bank_count = 1;
        base = ((cart.size > 0x10000u) ? 0x10000u : SYSTEM1_MAIN_FIXED_SIZE) + ((uint32_t)(s1.bank % bank_count) << 14);
    }
    else
    {
        /* MAME configures the bank window from 0x10000 whenever external
         * bank ROMs exist.  Bank-44 games such as Gardia and Brain select
         * those entries with video-mode bits 6 and 2; starting at 0x8000
         * maps empty space for parent Gardia and corrupts the attract/title
         * tilemap writes. */
        uint32_t bank_base = (cart.size > 0x10000u) ? 0x10000u : SYSTEM1_MAIN_FIXED_SIZE;
        available = (cart.size > bank_base) ? (cart.size - bank_base) : 0;
        bank_count = available / 0x4000u;
        if (!bank_count) bank_count = 1;
        base = bank_base + ((uint32_t)(s1.bank % bank_count) << 14);
    }
    if (base >= cart.size) base = (cart.size > SYSTEM1_MAIN_FIXED_SIZE) ? SYSTEM1_MAIN_FIXED_SIZE : 0;

    for (i = 0x20; i <= 0x2f; i++)
    {
        uint32_t off = base + ((uint32_t)(i & 0x0f) << 10);
        map_1k(i, (cart.rom && off < cart.size) ? cart.rom + off : dummy_read, (s1.opcode_rom && off < SYSTEM1_MAIN_ROM_SIZE) ? s1.opcode_rom + off : NULL, dummy_write);
    }
}

void system1_memory_map(int clear_ram)
{
    uint_fast8_t i;
    if (!system1_alloc()) return;
    z80_data_operand_fetch = 1;
    if (clear_ram)
    {
        memset(s1.ram, 0, SYSTEM1_RAM_SIZE);
        memset(s1.spriteram, 0, SYSTEM1_SPRITERAM_SIZE);
        memset(s1.paletteram, 0, SYSTEM1_PALRAM_SIZE);
        memset(s1.videoram, 0, SYSTEM1_VRAM_SIZE);
        memset(s1.sound_ram, 0, SYSTEM1_SOUND_RAM_SIZE);
        memset(s1.mix_collision, 0, SYSTEM1_MIXCOL_SIZE);
        memset(s1.sprite_collision, 0, SYSTEM1_SPRCOL_SIZE);
        s1.mix_collision_summary = 0;
        s1.sprite_collision_summary = 0;
        s1.collision_partial_line = 0;
        s1.bank = s1.video_mode = s1.vram_bank = s1.sound_latch = 0;
        s1.sound_nmi_asserted = s1.sound_irq_asserted = 0;
    }

    for (i = 0x00; i <= 0x1f; i++)
    {
        uint32_t off = (uint32_t)i << 10;
        map_1k(i, (cart.rom && off < cart.size) ? cart.rom + off : dummy_read, (s1.opcode_rom && off < SYSTEM1_MAIN_ROM_SIZE) ? s1.opcode_rom + off : NULL, dummy_write);
    }
    system1_bank_w(0);
    system1_map_sound_cpu();
    for (i = 0x30; i <= 0x33; i++) map_1k(i, s1.ram + ((i & 0x03) << 10), NULL, s1.ram + ((i & 0x03) << 10));
    for (i = 0x34; i <= 0x35; i++) map_1k(i, s1.spriteram + ((i & 0x01) << 10), NULL, s1.spriteram + ((i & 0x01) << 10));
    for (i = 0x36; i <= 0x37; i++) map_1k(i, s1.paletteram + ((i & 0x01) << 10), NULL, s1.paletteram + ((i & 0x01) << 10));
    for (i = 0x38; i <= 0x3b; i++) map_1k(i, dummy_read, NULL, dummy_write);
    for (i = 0x3c; i <= 0x3f; i++) map_1k(i, dummy_read, NULL, dummy_write);
}

uint8_t system1_readmem(uint16_t address)
{
    if (s1.current_cpu == SYSTEM1_CPU_SOUND)
    {
        if (address < 0x8000)
            return s1.sound_rom[address & (SYSTEM1_SOUND_ROM_SIZE - 1)];
        if (address < 0xa000)
            return s1.sound_ram[address & (SYSTEM1_SOUND_RAM_SIZE - 1)];
        if (address >= 0xe000)
        {
            /* Reading the latch acknowledges the PIO/PPI sound handshake. */
            system1_set_sound_nmi(0);
            return s1.sound_latch;
        }
        return 0xff;
    }

    if (s1.game == 20)
    {
        if (address == 0x0001)
        {
            Z80_Regs *ctx = z80_get_context();
            /* Noboranka has a small M1/reset-vector dependent protection
             * quirk: the low byte of the initial JP operand is seen as 80h
             * only during reset fetch.  The Z80 core reads opcode operands
             * through the data map for encrypted-opcode support, so model
             * the MAME nob_start_r() behavior here rather than patching ROM. */
            if (ctx && ctx->pc.w.l <= 0x0003)
                return 0x80;
        }
        if (address < 0xc000)
            return s1.data_readmap[address >> 10][address & 0x03ff];
        if (address < 0xc400) { system1_update_collisions_now(); return system1_collision_r_value(s1.mix_collision, (uint16_t)((address - 0xc000) & (SYSTEM1_MIXCOL_SIZE - 1)), s1.mix_collision_summary); }
        if (address < 0xc800)
            return 0xff;
        if (address < 0xcc00) { system1_update_collisions_now(); return system1_collision_r_value(s1.sprite_collision, (uint16_t)((address - 0xc800) & (SYSTEM1_SPRCOL_SIZE - 1)), s1.sprite_collision_summary); }
        if (address < 0xd000)
            return 0xff;
        if (address < 0xd800)
            return s1.spriteram[address & 0x07ff];
        if (address < 0xe000)
            return s1.paletteram[address & 0x07ff];
        if (address < 0xf000)
        {
            uint32_t bank = (uint32_t)((s1.vram_bank >> 1) % ((s1.tilemap_pages > 1) ? (s1.tilemap_pages / 2) : 1));
            return s1.videoram[(bank << 12) | (address & 0x0fff)];
        }
        return s1.ram[address & 0x0fff];
    }

    if (address < 0xe000)
        return s1.data_readmap[address >> 10][address & 0x03ff];
    if (address < 0xf000)
    {
        uint32_t bank = (uint32_t)((s1.vram_bank >> 1) % ((s1.tilemap_pages > 1) ? (s1.tilemap_pages / 2) : 1));
        return s1.videoram[(bank << 12) | (address & 0x0fff)];
    }
    if (address < 0xf400) { system1_update_collisions_now(); return system1_collision_r_value(s1.mix_collision, (uint16_t)((address - 0xf000) & (SYSTEM1_MIXCOL_SIZE - 1)), s1.mix_collision_summary); }
    if (address < 0xf800)
        return 0xff;
    if (address < 0xfc00) { system1_update_collisions_now(); return system1_collision_r_value(s1.sprite_collision, (uint16_t)((address - 0xf800) & (SYSTEM1_SPRCOL_SIZE - 1)), s1.sprite_collision_summary); }
    return 0xff;
}

void system1_writemem(uint16_t address, uint8_t data)
{
    MULTIREXZ80_TRACE_MEM_WRITE(address, data);
    if (s1.current_cpu == SYSTEM1_CPU_SOUND)
    {
        if (address >= 0x8000 && address < 0xa000)
        {
            s1.sound_ram[address & (SYSTEM1_SOUND_RAM_SIZE - 1)] = data;
            return;
        }
        if (address >= 0xa000 && address < 0xc000)
        {
            psg_write_chip(0, data);
            return;
        }
        if (address >= 0xc000 && address < 0xe000)
        {
            psg_write_chip(1, data);
            return;
        }
        return;
    }

    if (s1.game == 20)
    {
        if (address < 0xc000) return;
        if (address < 0xc400) { system1_update_collisions_now(); s1.mix_collision[(address - 0xc000) & (SYSTEM1_MIXCOL_SIZE - 1)] = 0; return; }
        if (address < 0xc800) { system1_update_collisions_now(); s1.mix_collision_summary = 0; return; }
        if (address < 0xcc00) { system1_update_collisions_now(); s1.sprite_collision[(address - 0xc800) & (SYSTEM1_SPRCOL_SIZE - 1)] = 0; return; }
        if (address < 0xd000) { system1_update_collisions_now(); s1.sprite_collision_summary = 0; return; }
        if (address < 0xd800) { s1.spriteram[address & 0x07ff] = data; return; }
        if (address < 0xe000)
        {
            uint32_t poff = (uint32_t)(address & 0x07ff);
            if (s1.paletteram[poff] != data) { s1.paletteram[poff] = data; s1.pen_cache_dirty = 1; }
            return;
        }
        if (address < 0xf000) { uint32_t bank = (uint32_t)((s1.vram_bank >> 1) % ((s1.tilemap_pages > 1) ? (s1.tilemap_pages / 2) : 1)); s1.videoram[(bank << 12) | (address & 0x0fff)] = data; return; }
        s1.ram[address & 0x0fff] = data;
        return;
    }

    if (address < 0xc000) return;
    if (address < 0xd000) { s1.ram[address & 0x0fff] = data; return; }
    if (address < 0xd800) { s1.spriteram[address & 0x07ff] = data; return; }
    if (address < 0xe000)
    {
        uint32_t poff = (uint32_t)(address & 0x07ff);
        if (s1.paletteram[poff] != data)
        {
            s1.paletteram[poff] = data;
            s1.pen_cache_dirty = 1;
        }
        return;
    }
    if (address < 0xf000) { uint32_t bank = (uint32_t)((s1.vram_bank >> 1) % ((s1.tilemap_pages > 1) ? (s1.tilemap_pages / 2) : 1)); s1.videoram[(bank << 12) | (address & 0x0fff)] = data; return; }
    if (address < 0xf400) { system1_update_collisions_now(); s1.mix_collision[(address - 0xf000) & (SYSTEM1_MIXCOL_SIZE - 1)] = 0; return; }
    if (address < 0xf800) { system1_update_collisions_now(); s1.mix_collision_summary = 0; return; }
    if (address < 0xfc00) { system1_update_collisions_now(); s1.sprite_collision[(address - 0xf800) & (SYSTEM1_SPRCOL_SIZE - 1)] = 0; return; }
    system1_update_collisions_now(); s1.sprite_collision_summary = 0;
}

static uint8_t system1_player_r(int port)
{
    uint8_t r = 0xff;
    uint8_t p = input.pad[port & 1];
    if (s1.uses_dial)
        return (uint8_t)input.analog[port & 1][0];
    if (s1.button_swap)
    {
        /* MAME: teddybb/wboy/myhero map BUTTON1→0x02, BUTTON2→0x04 */
        if (p & INPUT_BUTTON1) r &= (uint8_t)~0x02;
        if (p & INPUT_BUTTON2) r &= (uint8_t)~0x04;
    }
    else
    {
        if (p & INPUT_BUTTON2) r &= (uint8_t)~0x02;
        if (p & INPUT_BUTTON1) r &= (uint8_t)~0x04;
    }
    if (p & INPUT_DOWN)    r &= (uint8_t)~0x10;
    if (p & INPUT_UP)      r &= (uint8_t)~0x20;
    if (p & INPUT_RIGHT)   r &= (uint8_t)~0x40;
    if (p & INPUT_LEFT)    r &= (uint8_t)~0x80;
    return r;
}

static uint8_t system1_system_r(void)
{
    uint8_t r = 0xff;
    if (input.arcade & INPUT_ARCADE_COIN1)   r &= (uint8_t)~0x01;
    if (input.arcade & INPUT_ARCADE_COIN2)   r &= (uint8_t)~0x02;
    if (input.arcade & INPUT_ARCADE_TEST)    r &= (uint8_t)~0x04;
    if (input.arcade & INPUT_ARCADE_SERVICE) r &= (uint8_t)~0x08;
    if (input.arcade & INPUT_ARCADE_START1)  r &= (uint8_t)~0x10;
    if (input.arcade & INPUT_ARCADE_START2)  r &= (uint8_t)~0x20;
    if (s1.uses_dial)
    {
        if (input.pad[0] & INPUT_BUTTON1) r &= (uint8_t)~0x40;
        if (input.pad[1] & INPUT_BUTTON1) r &= (uint8_t)~0x80;
    }
    return r;
}

uint8_t system1_port_r(uint16_t port)
{
    uint8_t p = (uint8_t)(port & 0x1f);
    if (p <= 0x03) return system1_player_r(0);
    if (p <= 0x07) return system1_player_r(1);
    if (p <= 0x0b) return system1_system_r();
    if (s1.game == 4)
    {
        if (p == 0x0c || p == 0x0e) return 0xff;
        if (p == 0x0d || p == 0x0f) return s1.dip_a;
        if (p >= 0x10 && p <= 0x13) return s1.dip_b;
    }
    else
    {
        if (p == 0x0c || p == 0x0e) return s1.dip_a;
        if (p == 0x0d || p == 0x0f) return s1.dip_b;
        if (p >= 0x10 && p <= 0x13) return s1.dip_b;
    }
    /* PPI System 1/2 boards read back the 8255 output latches.
     * Choplifter initializes video mode with IN 15h / OR 0Ch / OUT 15h;
     * returning open-bus 0xff here incorrectly sets the video blanking bit
     * and leaves the game permanently black.  MAME marks port B bit 6 as an
     * input on the PPI, so expose it high while returning the latched output
     * bits for the rest of the port. */
    if (p == 0x14) return s1.pio_a;
    if (p == 0x15) return (uint8_t)((s1.pio_b & ~0x40u) | 0x40u);
    if (p == 0x16) return (uint8_t)((s1.vram_bank & ~0xc0u) | 0xc0u);
    if (p == 0x17) return 0xff;

    if (p == 0x18) return s1.pio_a;
    if (p == 0x19) return s1.pio_b;
    if (p == 0x1a) return 0xff;
    if (p == 0x1b) return 0xff;
    return 0xff;
}

static void system1_videomode_w(uint8_t data)
{
    s1.video_mode = data;
    if (s1.bank_mode == SYSTEM1_BANK_44)
        system1_bank_w((uint8_t)(((data & 0x40) >> 5) | ((data & 0x04) >> 2)));
    else if (s1.bank_mode == SYSTEM1_BANK_0C)
        system1_bank_w((data & 0x0c) >> 2);
    else
        system1_bank_w(0);
}

void system1_port_w(uint16_t port, uint8_t data)
{
    uint8_t p = (uint8_t)(port & 0x1f);
    switch (p)
    {
        case 0x14: /* PPI-compatible sound latch, used by older sets. */
        case 0x18: /* Z80 PIO port A data. */
            s1.pio_a = data;
            s1.sound_latch = data;
            /* PIO System 1 boards pulse ARDY into the sound CPU NMI.
             * PPI boards assert the NMI from the port-C write below;
             * asserting here as well makes split bootlegs robust and the
             * sound CPU acknowledges it when it reads the latch. */
            system1_set_sound_nmi(1);
            break;
        case 0x15: /* PPI-compatible video mode. */
        case 0x19: /* Z80 PIO port B data. */
            s1.pio_b = data;
            system1_videomode_w(data);
            break;
        case 0x16: /* PPI-compatible sound control / VRAM bank. */
            s1.vram_bank = data;
            system1_set_sound_nmi((data & 0x80) ? 0 : 1);
            break;
        case 0x1a:
            s1.pio_ctrl_a = data;
            break;
        case 0x1b:
            s1.pio_ctrl_b = data;
            break;
        default:
            break;
    }
}

static uint32_t system1_rgb(uint8_t packed)
{
    uint8_t r = (uint8_t)((packed & 0x07) * 255 / 7);
    uint8_t g = (uint8_t)(((packed >> 3) & 0x07) * 255 / 7);
    uint8_t b = (uint8_t)(((packed >> 6) & 0x03) * 255 / 3);
    return MAKE_PIXEL(r, g, b);
}

static uint32_t system1_pen_uncached(uint16_t index)
{
    uint8_t packed = s1.paletteram[index & 0x07ff];
    if (s1.color_prom_present)
    {
        uint8_t rv = s1.color_prom[packed + 0x000];
        uint8_t gv = s1.color_prom[packed + 0x100];
        uint8_t bv = s1.color_prom[packed + 0x200];
        uint8_t r = (uint8_t)(0x0e * ((rv >> 0) & 1) + 0x1f * ((rv >> 1) & 1) + 0x43 * ((rv >> 2) & 1) + 0x8f * ((rv >> 3) & 1));
        uint8_t g = (uint8_t)(0x0e * ((gv >> 0) & 1) + 0x1f * ((gv >> 1) & 1) + 0x43 * ((gv >> 2) & 1) + 0x8f * ((gv >> 3) & 1));
        uint8_t b = (uint8_t)(0x0e * ((bv >> 0) & 1) + 0x1f * ((bv >> 1) & 1) + 0x43 * ((bv >> 2) & 1) + 0x8f * ((bv >> 3) & 1));
        return MAKE_PIXEL(r, g, b);
    }
    return system1_rgb(packed);
}

static void system1_rebuild_pen_cache(void)
{
    uint32_t i;
    for (i = 0; i < 0x800u; i++)
        s1.pen_cache[i] = system1_pen_uncached((uint16_t)i);
    s1.pen_cache_dirty = 0;
}

static inline uint32_t system1_pen(uint16_t index)
{
    return s1.pen_cache[index & 0x07ff];
}
static uint8_t system1_collision_r_value(const uint8_t *ram, uint16_t offset, uint8_t summary)
{
    return (uint8_t)((ram[offset] ? 0x01u : 0x00u) | 0x7eu | (summary ? 0x80u : 0x00u));
}

static inline void system1_set_mix_collision(uint8_t index)
{
    s1.mix_collision[index & (SYSTEM1_MIXCOL_SIZE - 1)] = 1;
    s1.mix_collision_summary = 1;
}

static inline void system1_set_sprite_collision(uint16_t index)
{
    s1.sprite_collision[index & (SYSTEM1_SPRCOL_SIZE - 1)] = 1;
    s1.sprite_collision_summary = 1;
}


static void system1_rebuild_tile_cache(void)
{
    uint32_t tile_count = (s1.tile_rom_loaded ? s1.tile_rom_loaded : SYSTEM1_TILE_ROM_SIZE) / 24u;
    uint32_t max_tiles = SYSTEM1_TILE_ROM_SIZE / 24u;
    uint32_t plane_stride;
    uint32_t code, rownum;

    if (!tile_count) tile_count = 1;
    if (tile_count > max_tiles) tile_count = max_tiles;
    plane_stride = tile_count * 8u;

    for (code = 0; code < tile_count; code++)
    {
        for (rownum = 0; rownum < 8; rownum++)
        {
            uint32_t row = code * 8u + rownum;
            uint8_t p0 = s1.tile_rom[row + 0u * plane_stride];
            uint8_t p1 = s1.tile_rom[row + 1u * plane_stride];
            uint8_t p2 = s1.tile_rom[row + 2u * plane_stride];
            uint8_t *dst = s1.tile_row_cache + ((code * 8u + rownum) * 8u);
            int x;
            for (x = 0; x < 8; x++)
            {
                uint8_t mask = (uint8_t)(0x80u >> x);
                dst[x] = (uint8_t)(((p0 & mask) ? 4 : 0) | ((p1 & mask) ? 2 : 0) | ((p2 & mask) ? 1 : 0));
            }
        }
    }

    s1.tile_cache_count = tile_count;
    s1.tile_cache_dirty = 0;
}

static inline void tilemap_row_from_offset(uint32_t off, uint32_t y, uint16_t *base, const uint8_t **rowpix)
{
    uint16_t tiledata = (uint16_t)((uint16_t)s1.videoram[off] | (uint16_t)((uint16_t)s1.videoram[off + 1u] << 8));
    uint16_t code = (uint16_t)(((tiledata >> 4) & 0x0800) | (tiledata & 0x07ff));
    uint32_t tile_count = s1.tile_cache_count ? s1.tile_cache_count : 1u;

    if ((uint32_t)code >= tile_count)
        code = (uint16_t)((uint32_t)code % tile_count);

    *rowpix = s1.tile_row_cache + (((uint32_t)code * 8u + (y & 7u)) * 8u);
    /* MAME tilemap pixmaps retain the tile color bits even when the
     * pixel pen is transparent.  The mixer checks transparency with
     * (pix & 7) == 0, but if the PROM still selects that layer the
     * color bits participate in the palette lookup. */
    *base = (uint16_t)(((tiledata >> 5) & 0xffu) << 3);
}

static inline uint16_t tilemap_pixel_from_row(uint16_t base, const uint8_t *rowpix, uint32_t x)
{
    return (uint16_t)(base | rowpix[x & 7u]);
}

static void draw_sprites_range(int min_y, int max_y, int update_collision)
{
    int spr;
    int clear_y;
    if (min_y < 0) min_y = 0;
    if (max_y >= SYSTEM1_VISIBLE_HEIGHT) max_y = SYSTEM1_VISIBLE_HEIGHT - 1;
    if (max_y < min_y) return;
    for (clear_y = min_y; clear_y <= max_y; clear_y++)
        memset(s1.sprite_line + (size_t)clear_y * SYSTEM1_RAW_WIDTH, 0, (size_t)SYSTEM1_RAW_WIDTH * sizeof(uint16_t));
    for (spr = 0; spr < 32; spr++)
    {
        const uint8_t *sd = &s1.spriteram[spr * 0x10];
        uint16_t srcaddr = (uint16_t)((uint16_t)sd[6] | (uint16_t)((uint16_t)sd[7] << 8));
        uint16_t stride = (uint16_t)((uint16_t)sd[4] | (uint16_t)((uint16_t)sd[5] << 8));
        uint8_t bank = (uint8_t)(((sd[3] & 0x80) >> 7) | ((sd[3] & 0x40) >> 5) | ((sd[3] & 0x20) >> 3));
        int xstart = ((int)((uint16_t)sd[2] | ((uint16_t)sd[3] << 8)) & 0x1ff);
        int top = (int)sd[0] + 1;
        int bottom = (int)sd[1] + 1;
        int y;
        uint32_t gfxbanks = (s1.sprite_rom_loaded ? s1.sprite_rom_loaded : SYSTEM1_SPRITE_ROM_SIZE) / 0x8000u;
        if (sd[0] == 0xff) break;
        if (!gfxbanks) gfxbanks = 1;
        bank %= gfxbanks;
        for (y = top; y < bottom; y++)
        {
            uint16_t cur;
            int addrdelta;
            int x;
            if (y < min_y || y > max_y)
            {
                srcaddr = (uint16_t)(srcaddr + stride);
                continue;
            }
            srcaddr = (uint16_t)(srcaddr + stride);
            addrdelta = (srcaddr & 0x8000) ? -1 : 1;
            cur = srcaddr;
            for (x = xstart + ((s1.video_type == 2) ? 14 : 0); x < 512 + 32; x += 4, cur = (uint16_t)(cur + addrdelta))
            {
                uint8_t data = s1.sprite_rom[(uint32_t)bank * 0x8000u + (cur & 0x7fff)];
                uint8_t c1 = (cur & 0x8000) ? (data & 0x0f) : (data >> 4);
                uint8_t c2 = (cur & 0x8000) ? (data >> 4) : (data & 0x0f);
                if (c1 == 0x0f) break;
                if (c1)
                {
                    int i;
                    for (i = 0; i < 2; i++)
                    {
                        int effx = x + i;
                        if (effx >= 0 && effx < SYSTEM1_RAW_WIDTH)
                        {
                            uint16_t *dstpix = &s1.sprite_line[(size_t)y * SYSTEM1_RAW_WIDTH + (uint32_t)effx];
                            if (update_collision && ((*dstpix & 0x0f) != 0))
                                system1_set_sprite_collision((uint16_t)((((*dstpix >> 4) & 0x1f) + 32 * spr)));
                            *dstpix = (uint16_t)((spr << 4) | c1);
                        }
                    }
                }
                if (c2 == 0x0f) break;
                if (c2)
                {
                    int i;
                    for (i = 0; i < 2; i++)
                    {
                        int effx = x + 2 + i;
                        if (effx >= 0 && effx < SYSTEM1_RAW_WIDTH)
                        {
                            uint16_t *dstpix = &s1.sprite_line[(size_t)y * SYSTEM1_RAW_WIDTH + (uint32_t)effx];
                            if (update_collision && ((*dstpix & 0x0f) != 0))
                                system1_set_sprite_collision((uint16_t)((((*dstpix >> 4) & 0x1f) + 32 * spr)));
                            *dstpix = (uint16_t)((spr << 4) | c2);
                        }
                    }
                }
            }
        }
    }
}

static void system1_update_collisions_to_line(int target_line)
{
    int y, x;
    uint32_t pages;
    uint32_t page_mask;
    int pages_power_of_two;
    int fgpage;
    uint32_t fgpage_norm;

    if (target_line < 0) return;
    if (target_line > SYSTEM1_VISIBLE_HEIGHT) target_line = SYSTEM1_VISIBLE_HEIGHT;
    if (target_line <= (int)s1.collision_partial_line) return;

    if (s1.tile_cache_dirty || !s1.tile_cache_count)
        system1_rebuild_tile_cache();

    draw_sprites_range((int)s1.collision_partial_line, target_line - 1, 1);

    pages = s1.tilemap_pages ? s1.tilemap_pages : 2u;
    page_mask = pages - 1u;
    pages_power_of_two = ((pages & page_mask) == 0);
    fgpage = (s1.video_type == 2) ? 0 : 1;
    fgpage_norm = pages_power_of_two ? ((uint32_t)fgpage & page_mask) : ((uint32_t)fgpage % pages);

    for (y = (int)s1.collision_partial_line; y < target_line; y++)
    {
        int screen_y = y;
        int bgyscroll, xscroll;
        uint32_t fgy = (uint32_t)screen_y & 0xffu;
        uint32_t fg_row_base = (fgpage_norm << 11) | (((fgy >> 3) & 0x1fu) << 6);
        uint32_t fg_pix_y = fgy & 7u;
        uint32_t bg_pix_y, bg_ty_base;
        uint32_t fg_last_col = 0xffffffffu;
        uint32_t bg_last_key = 0xffffffffu;
        uint16_t fg_base = 0, bg_base = 0;
        const uint8_t *fg_rowpix = NULL, *bg_rowpix = NULL;
        const uint16_t *sprite_row = s1.sprite_line + (size_t)y * SYSTEM1_RAW_WIDTH;

        if (s1.video_type == 2)
        {
            bgyscroll = s1.videoram[0x07ba];
            if (s1.rowscroll)
            {
                uint32_t rowoffs = 0x07c0u + ((((uint32_t)screen_y >> 3) & 0x1f) * 2u);
                xscroll = (((int)((uint16_t)s1.videoram[rowoffs] | ((uint16_t)s1.videoram[rowoffs + 1] << 8))) & 0x1ff) - 512 + 10;
            }
            else
            {
                xscroll = (((int)((uint16_t)s1.videoram[0x07c0] | ((uint16_t)s1.videoram[0x07c1] << 8))) & 0x1ff) - 512 + 10;
            }
        }
        else
        {
            bgyscroll = s1.videoram[0x0fbd];
            xscroll = (int)(int16_t)(((uint16_t)s1.videoram[0x0ffc] | ((uint16_t)s1.videoram[0x0ffd] << 8)) + 28);
        }

        {
            uint32_t bgy = (uint32_t)((screen_y + bgyscroll) & 0x1ff);
            bg_pix_y = bgy & 7u;
            bg_ty_base = ((bgy >> 3) & 0x1fu) << 6;
        }

        for (x = 0; x < SYSTEM1_RAW_WIDTH; x += 2)
        {
            int bgpage = 0;
            int bgx;
            int out_x = x >> 1;
            uint32_t bgpage_norm;
            uint32_t bg_key;
            uint32_t fgx = (uint32_t)out_x & 0xffu;
            uint32_t fg_col = (fgx >> 3) & 0x1fu;
            uint16_t bg, fg, sp;
            uint8_t lookup_index, lookup_value;

            if (fg_col != fg_last_col)
            {
                uint32_t fg_off = fg_row_base | (fg_col << 1);
                tilemap_row_from_offset(fg_off, fg_pix_y, &fg_base, &fg_rowpix);
                fg_last_col = fg_col;
            }

            bgx = ((x - xscroll) / 2) & 0x1ff;
            if (s1.video_type == 2)
            {
                int quad = ((((screen_y + bgyscroll) & 0x1ff) >> 8) * 2) + (bgx >> 8);
                bgpage = s1.videoram[0x0740 + (quad * 2)] & 7;
            }
            bgpage_norm = pages_power_of_two ? ((uint32_t)bgpage & page_mask) : ((uint32_t)bgpage % pages);
            bg_key = (bgpage_norm << 5) | (((uint32_t)bgx >> 3) & 0x1fu);
            if (bg_key != bg_last_key)
            {
                uint32_t bg_off = (bgpage_norm << 11) | bg_ty_base | ((((uint32_t)bgx >> 3) & 0x1fu) << 1);
                tilemap_row_from_offset(bg_off, bg_pix_y, &bg_base, &bg_rowpix);
                bg_last_key = bg_key;
            }

            bg = tilemap_pixel_from_row(bg_base, bg_rowpix, (uint32_t)bgx);
            fg = tilemap_pixel_from_row(fg_base, fg_rowpix, fgx);
            sp = sprite_row[(uint32_t)x];
            lookup_index = (uint8_t)((((sp & 0x0f) == 0) << 0) |
                           (((fg & 0x07) == 0) << 1) |
                           (((fg >> 9) & 0x03) << 2) |
                           (((bg & 0x07) == 0) << 4) |
                           (((bg >> 9) & 0x03) << 5));
            lookup_value = s1.prom[lookup_index];
            if (!(lookup_value & 4))
                system1_set_mix_collision((uint8_t)(((lookup_value & 8) << 2) | ((sp >> 4) & 0x1f)));
        }
    }
    s1.collision_partial_line = (uint16_t)target_line;
}

static void system1_update_collisions_now(void)
{
    int target = (vdp.line < SYSTEM1_VISIBLE_HEIGHT) ? vdp.line : SYSTEM1_VISIBLE_HEIGHT;
    system1_update_collisions_to_line(target);
}

static void system1_clear_viewport(void)
{
    uint32_t y;
    uint32_t clear_w;
    uint32_t clear_h;

    if (!bitmap.data || !bitmap.pitch || bitmap.viewport.w <= 0 || bitmap.viewport.h <= 0)
        return;
    clear_w = (uint32_t)bitmap.viewport.w;
    clear_h = (uint32_t)bitmap.viewport.h;
    if (clear_w > bitmap.width) clear_w = bitmap.width;
    if (clear_h > bitmap.height) clear_h = bitmap.height;

#ifdef MULTIREXZ80_RENDER_32BPP
    for (y = 0; y < clear_h; y++)
        memset(bitmap.data + (size_t)y * bitmap.pitch, 0, (size_t)clear_w * sizeof(uint32_t));
#else
    for (y = 0; y < clear_h; y++)
        memset(bitmap.data + (size_t)y * bitmap.pitch, 0, (size_t)clear_w * sizeof(uint16_t));
#endif
}


static void system1_render_fast_none(void)
{
    int y, x;
    uint32_t pages = s1.tilemap_pages ? s1.tilemap_pages : 2u;
    uint32_t page_mask = pages - 1u;
    int pages_power_of_two = ((pages & page_mask) == 0);
    int fgpage = (s1.video_type == 2) ? 0 : 1;
    uint32_t fgpage_norm = pages_power_of_two ? ((uint32_t)fgpage & page_mask) : ((uint32_t)fgpage % pages);

    for (y = 0; y < SYSTEM1_VISIBLE_HEIGHT; y++)
    {
        int screen_y = y;
        int bgyscroll, xscroll;
        uint32_t fgy = (uint32_t)screen_y & 0xffu;
        uint32_t fg_row_base = (fgpage_norm << 11) | (((fgy >> 3) & 0x1fu) << 6);
        uint32_t fg_pix_y = fgy & 7u;
        uint32_t bg_pix_y, bg_ty_base;
        uint32_t fg_last_col = 0xffffffffu;
        uint32_t bg_last_key = 0xffffffffu;
        uint16_t fg_base = 0, bg_base = 0;
        const uint8_t *fg_rowpix = NULL, *bg_rowpix = NULL;
        const uint16_t *sprite_row = s1.sprite_line + (size_t)y * SYSTEM1_RAW_WIDTH;
#ifdef MULTIREXZ80_RENDER_32BPP
        uint32_t *dst = (uint32_t *)(void *)(bitmap.data + (size_t)y * bitmap.pitch);
#else
        uint16_t *dst = (uint16_t *)(void *)(bitmap.data + (size_t)y * bitmap.pitch);
#endif

        if (s1.video_type == 2)
        {
            bgyscroll = s1.videoram[0x07ba];
            if (s1.rowscroll)
            {
                uint32_t rowoffs = 0x07c0u + ((((uint32_t)screen_y >> 3) & 0x1f) * 2u);
                xscroll = (((int)((uint16_t)s1.videoram[rowoffs] | ((uint16_t)s1.videoram[rowoffs + 1] << 8))) & 0x1ff) - 512 + 10;
            }
            else
            {
                xscroll = (((int)((uint16_t)s1.videoram[0x07c0] | ((uint16_t)s1.videoram[0x07c1] << 8))) & 0x1ff) - 512 + 10;
            }
        }
        else
        {
            bgyscroll = s1.videoram[0x0fbd];
            xscroll = (int)(int16_t)(((uint16_t)s1.videoram[0x0ffc] | ((uint16_t)s1.videoram[0x0ffd] << 8)) + 28);
        }

        {
            uint32_t bgy = (uint32_t)((screen_y + bgyscroll) & 0x1ff);
            bg_pix_y = bgy & 7u;
            bg_ty_base = ((bgy >> 3) & 0x1fu) << 6;
        }

        for (x = 0; x < SYSTEM1_RAW_WIDTH; x += 2)
        {
            int bgpage = 0;
            int bgx;
            int out_x = x >> 1;
            uint32_t bgpage_norm;
            uint32_t bg_key;
            uint32_t fgx = (uint32_t)out_x & 0xffu;
            uint32_t fg_col = (fgx >> 3) & 0x1fu;
            uint16_t bg, fg, sp, pen;
            uint8_t lookup_index, lookup_value;

            if (fg_col != fg_last_col)
            {
                uint32_t fg_off = fg_row_base | (fg_col << 1);
                tilemap_row_from_offset(fg_off, fg_pix_y, &fg_base, &fg_rowpix);
                fg_last_col = fg_col;
            }

            bgx = ((x - xscroll) / 2) & 0x1ff;
            if (s1.video_type == 2)
            {
                int quad = ((((screen_y + bgyscroll) & 0x1ff) >> 8) * 2) + (bgx >> 8);
                bgpage = s1.videoram[0x0740 + (quad * 2)] & 7;
            }
            bgpage_norm = pages_power_of_two ? ((uint32_t)bgpage & page_mask) : ((uint32_t)bgpage % pages);
            bg_key = (bgpage_norm << 5) | (((uint32_t)bgx >> 3) & 0x1fu);
            if (bg_key != bg_last_key)
            {
                uint32_t bg_off = (bgpage_norm << 11) | bg_ty_base | ((((uint32_t)bgx >> 3) & 0x1fu) << 1);
                tilemap_row_from_offset(bg_off, bg_pix_y, &bg_base, &bg_rowpix);
                bg_last_key = bg_key;
            }

            bg = tilemap_pixel_from_row(bg_base, bg_rowpix, (uint32_t)bgx);
            fg = tilemap_pixel_from_row(fg_base, fg_rowpix, fgx);
            sp = sprite_row[(uint32_t)x];
            lookup_index = (uint8_t)((((sp & 0x0f) == 0) << 0) |
                           (((fg & 0x07) == 0) << 1) |
                           (((fg >> 9) & 0x03) << 2) |
                           (((bg & 0x07) == 0) << 4) |
                           (((bg >> 9) & 0x03) << 5));
            lookup_value = s1.prom[lookup_index];
            switch (lookup_value & 3)
            {
                default:
                case 0: pen = (uint16_t)(0x000 | (sp & 0x1ff)); break;
                case 1: pen = (uint16_t)(0x200 | (fg & 0x1ff)); break;
                case 2:
                case 3: pen = (uint16_t)(0x400 | (bg & 0x1ff)); break;
            }
#ifdef MULTIREXZ80_RENDER_32BPP
            dst[out_x] = system1_pen(pen);
#else
            dst[out_x] = (uint16_t)system1_pen(pen);
#endif
        }
    }
}
static void system1_render_fast_cw(void)
{
    int y, x;
    uint32_t pages = s1.tilemap_pages ? s1.tilemap_pages : 2u;
    uint32_t page_mask = pages - 1u;
    int pages_power_of_two = ((pages & page_mask) == 0);
    int fgpage = (s1.video_type == 2) ? 0 : 1;
    uint32_t fgpage_norm = pages_power_of_two ? ((uint32_t)fgpage & page_mask) : ((uint32_t)fgpage % pages);

    for (y = 0; y < SYSTEM1_VISIBLE_HEIGHT; y++)
    {
        int screen_y = y;
        int bgyscroll, xscroll;
        uint32_t fgy = (uint32_t)screen_y & 0xffu;
        uint32_t fg_row_base = (fgpage_norm << 11) | (((fgy >> 3) & 0x1fu) << 6);
        uint32_t fg_pix_y = fgy & 7u;
        uint32_t bg_pix_y, bg_ty_base;
        uint32_t fg_last_col = 0xffffffffu;
        uint32_t bg_last_key = 0xffffffffu;
        uint16_t fg_base = 0, bg_base = 0;
        const uint8_t *fg_rowpix = NULL, *bg_rowpix = NULL;
        const uint16_t *sprite_row = s1.sprite_line + (size_t)y * SYSTEM1_RAW_WIDTH;
        if (s1.video_type == 2)
        {
            bgyscroll = s1.videoram[0x07ba];
            if (s1.rowscroll)
            {
                uint32_t rowoffs = 0x07c0u + ((((uint32_t)screen_y >> 3) & 0x1f) * 2u);
                xscroll = (((int)((uint16_t)s1.videoram[rowoffs] | ((uint16_t)s1.videoram[rowoffs + 1] << 8))) & 0x1ff) - 512 + 10;
            }
            else
            {
                xscroll = (((int)((uint16_t)s1.videoram[0x07c0] | ((uint16_t)s1.videoram[0x07c1] << 8))) & 0x1ff) - 512 + 10;
            }
        }
        else
        {
            bgyscroll = s1.videoram[0x0fbd];
            xscroll = (int)(int16_t)(((uint16_t)s1.videoram[0x0ffc] | ((uint16_t)s1.videoram[0x0ffd] << 8)) + 28);
        }

        {
            uint32_t bgy = (uint32_t)((screen_y + bgyscroll) & 0x1ff);
            bg_pix_y = bgy & 7u;
            bg_ty_base = ((bgy >> 3) & 0x1fu) << 6;
        }

        for (x = 0; x < SYSTEM1_RAW_WIDTH; x += 2)
        {
            int bgpage = 0;
            int bgx;
            int out_x = x >> 1;
            uint32_t bgpage_norm;
            uint32_t bg_key;
            uint32_t fgx = (uint32_t)out_x & 0xffu;
            uint32_t fg_col = (fgx >> 3) & 0x1fu;
            uint16_t bg, fg, sp, pen;
            uint8_t lookup_index, lookup_value;

            if (fg_col != fg_last_col)
            {
                uint32_t fg_off = fg_row_base | (fg_col << 1);
                tilemap_row_from_offset(fg_off, fg_pix_y, &fg_base, &fg_rowpix);
                fg_last_col = fg_col;
            }

            bgx = ((x - xscroll) / 2) & 0x1ff;
            if (s1.video_type == 2)
            {
                int quad = ((((screen_y + bgyscroll) & 0x1ff) >> 8) * 2) + (bgx >> 8);
                bgpage = s1.videoram[0x0740 + (quad * 2)] & 7;
            }
            bgpage_norm = pages_power_of_two ? ((uint32_t)bgpage & page_mask) : ((uint32_t)bgpage % pages);
            bg_key = (bgpage_norm << 5) | (((uint32_t)bgx >> 3) & 0x1fu);
            if (bg_key != bg_last_key)
            {
                uint32_t bg_off = (bgpage_norm << 11) | bg_ty_base | ((((uint32_t)bgx >> 3) & 0x1fu) << 1);
                tilemap_row_from_offset(bg_off, bg_pix_y, &bg_base, &bg_rowpix);
                bg_last_key = bg_key;
            }

            bg = tilemap_pixel_from_row(bg_base, bg_rowpix, (uint32_t)bgx);
            fg = tilemap_pixel_from_row(fg_base, fg_rowpix, fgx);
            sp = sprite_row[(uint32_t)x];
            lookup_index = (uint8_t)((((sp & 0x0f) == 0) << 0) |
                           (((fg & 0x07) == 0) << 1) |
                           (((fg >> 9) & 0x03) << 2) |
                           (((bg & 0x07) == 0) << 4) |
                           (((bg >> 9) & 0x03) << 5));
            lookup_value = s1.prom[lookup_index];
            switch (lookup_value & 3)
            {
                default:
                case 0: pen = (uint16_t)(0x000 | (sp & 0x1ff)); break;
                case 1: pen = (uint16_t)(0x200 | (fg & 0x1ff)); break;
                case 2:
                case 3: pen = (uint16_t)(0x400 | (bg & 0x1ff)); break;
            }
#ifdef MULTIREXZ80_RENDER_32BPP
            ((uint32_t *)(void *)(bitmap.data + (size_t)out_x * bitmap.pitch))[SYSTEM1_VISIBLE_HEIGHT - 1 - y] = system1_pen(pen);
#else
            ((uint16_t *)(void *)(bitmap.data + (size_t)out_x * bitmap.pitch))[SYSTEM1_VISIBLE_HEIGHT - 1 - y] = (uint16_t)system1_pen(pen);
#endif
        }
    }
}

static void system1_render_fast_ccw(void)
{
    int y, x;
    uint32_t pages = s1.tilemap_pages ? s1.tilemap_pages : 2u;
    uint32_t page_mask = pages - 1u;
    int pages_power_of_two = ((pages & page_mask) == 0);
    int fgpage = (s1.video_type == 2) ? 0 : 1;
    uint32_t fgpage_norm = pages_power_of_two ? ((uint32_t)fgpage & page_mask) : ((uint32_t)fgpage % pages);

    for (y = 0; y < SYSTEM1_VISIBLE_HEIGHT; y++)
    {
        int screen_y = y;
        int bgyscroll, xscroll;
        uint32_t fgy = (uint32_t)screen_y & 0xffu;
        uint32_t fg_row_base = (fgpage_norm << 11) | (((fgy >> 3) & 0x1fu) << 6);
        uint32_t fg_pix_y = fgy & 7u;
        uint32_t bg_pix_y, bg_ty_base;
        uint32_t fg_last_col = 0xffffffffu;
        uint32_t bg_last_key = 0xffffffffu;
        uint16_t fg_base = 0, bg_base = 0;
        const uint8_t *fg_rowpix = NULL, *bg_rowpix = NULL;
        const uint16_t *sprite_row = s1.sprite_line + (size_t)y * SYSTEM1_RAW_WIDTH;
        if (s1.video_type == 2)
        {
            bgyscroll = s1.videoram[0x07ba];
            if (s1.rowscroll)
            {
                uint32_t rowoffs = 0x07c0u + ((((uint32_t)screen_y >> 3) & 0x1f) * 2u);
                xscroll = (((int)((uint16_t)s1.videoram[rowoffs] | ((uint16_t)s1.videoram[rowoffs + 1] << 8))) & 0x1ff) - 512 + 10;
            }
            else
            {
                xscroll = (((int)((uint16_t)s1.videoram[0x07c0] | ((uint16_t)s1.videoram[0x07c1] << 8))) & 0x1ff) - 512 + 10;
            }
        }
        else
        {
            bgyscroll = s1.videoram[0x0fbd];
            xscroll = (int)(int16_t)(((uint16_t)s1.videoram[0x0ffc] | ((uint16_t)s1.videoram[0x0ffd] << 8)) + 28);
        }

        {
            uint32_t bgy = (uint32_t)((screen_y + bgyscroll) & 0x1ff);
            bg_pix_y = bgy & 7u;
            bg_ty_base = ((bgy >> 3) & 0x1fu) << 6;
        }

        for (x = 0; x < SYSTEM1_RAW_WIDTH; x += 2)
        {
            int bgpage = 0;
            int bgx;
            int out_x = x >> 1;
            uint32_t bgpage_norm;
            uint32_t bg_key;
            uint32_t fgx = (uint32_t)out_x & 0xffu;
            uint32_t fg_col = (fgx >> 3) & 0x1fu;
            uint16_t bg, fg, sp, pen;
            uint8_t lookup_index, lookup_value;

            if (fg_col != fg_last_col)
            {
                uint32_t fg_off = fg_row_base | (fg_col << 1);
                tilemap_row_from_offset(fg_off, fg_pix_y, &fg_base, &fg_rowpix);
                fg_last_col = fg_col;
            }

            bgx = ((x - xscroll) / 2) & 0x1ff;
            if (s1.video_type == 2)
            {
                int quad = ((((screen_y + bgyscroll) & 0x1ff) >> 8) * 2) + (bgx >> 8);
                bgpage = s1.videoram[0x0740 + (quad * 2)] & 7;
            }
            bgpage_norm = pages_power_of_two ? ((uint32_t)bgpage & page_mask) : ((uint32_t)bgpage % pages);
            bg_key = (bgpage_norm << 5) | (((uint32_t)bgx >> 3) & 0x1fu);
            if (bg_key != bg_last_key)
            {
                uint32_t bg_off = (bgpage_norm << 11) | bg_ty_base | ((((uint32_t)bgx >> 3) & 0x1fu) << 1);
                tilemap_row_from_offset(bg_off, bg_pix_y, &bg_base, &bg_rowpix);
                bg_last_key = bg_key;
            }

            bg = tilemap_pixel_from_row(bg_base, bg_rowpix, (uint32_t)bgx);
            fg = tilemap_pixel_from_row(fg_base, fg_rowpix, fgx);
            sp = sprite_row[(uint32_t)x];
            lookup_index = (uint8_t)((((sp & 0x0f) == 0) << 0) |
                           (((fg & 0x07) == 0) << 1) |
                           (((fg >> 9) & 0x03) << 2) |
                           (((bg & 0x07) == 0) << 4) |
                           (((bg >> 9) & 0x03) << 5));
            lookup_value = s1.prom[lookup_index];
            switch (lookup_value & 3)
            {
                default:
                case 0: pen = (uint16_t)(0x000 | (sp & 0x1ff)); break;
                case 1: pen = (uint16_t)(0x200 | (fg & 0x1ff)); break;
                case 2:
                case 3: pen = (uint16_t)(0x400 | (bg & 0x1ff)); break;
            }
#ifdef MULTIREXZ80_RENDER_32BPP
            ((uint32_t *)(void *)(bitmap.data + (size_t)(SYSTEM1_VISIBLE_WIDTH - 1 - out_x) * bitmap.pitch))[y] = system1_pen(pen);
#else
            ((uint16_t *)(void *)(bitmap.data + (size_t)(SYSTEM1_VISIBLE_WIDTH - 1 - out_x) * bitmap.pitch))[y] = (uint16_t)system1_pen(pen);
#endif
        }
    }
}

static void system1_render(void)
{
    int y, x;
    int fast_bounds;
    if (!bitmap.data) return;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = (s1.rotate != SYSTEM1_ROTATE_NONE) ? SYSTEM1_VISIBLE_HEIGHT : SYSTEM1_VISIBLE_WIDTH;
    bitmap.viewport.h = (s1.rotate != SYSTEM1_ROTATE_NONE) ? SYSTEM1_VISIBLE_WIDTH : SYSTEM1_VISIBLE_HEIGHT;
    bitmap.viewport.changed = 1;

    if (s1.video_mode & 0x10)
    {
        system1_clear_viewport();
        return;
    }

    fast_bounds = ((int32_t)bitmap.width >= bitmap.viewport.w && (int32_t)bitmap.height >= bitmap.viewport.h);

    if (s1.tile_cache_dirty || !s1.tile_cache_count)
        system1_rebuild_tile_cache();
    if (s1.pen_cache_dirty)
        system1_rebuild_pen_cache();

    draw_sprites_range(0, SYSTEM1_VISIBLE_HEIGHT - 1, 0);
    if (fast_bounds)
    {
        if (s1.rotate == SYSTEM1_ROTATE_NONE)
        {
            system1_render_fast_none();
            return;
        }
        if (s1.rotate == SYSTEM1_ROTATE_CW)
        {
            system1_render_fast_cw();
            return;
        }
        if (s1.rotate == SYSTEM1_ROTATE_CCW)
        {
            system1_render_fast_ccw();
            return;
        }
    }
    {
        uint32_t pages = s1.tilemap_pages ? s1.tilemap_pages : 2u;
        uint32_t page_mask = pages - 1u;
        int pages_power_of_two = ((pages & page_mask) == 0);

        for (y = 0; y < SYSTEM1_VISIBLE_HEIGHT; y++)
        {
            int screen_y = y;
            int bgyscroll, xscroll;
            int fgpage = (s1.video_type == 2) ? 0 : 1;
            uint32_t fgpage_norm = pages_power_of_two ? ((uint32_t)fgpage & page_mask) : ((uint32_t)fgpage % pages);
            uint32_t fgy = (uint32_t)screen_y & 0xffu;
            uint32_t fg_row_base = (fgpage_norm << 11) | (((fgy >> 3) & 0x1fu) << 6);
            uint32_t fg_pix_y = fgy & 7u;
            uint32_t bg_pix_y, bg_ty_base;

            if (s1.video_type == 2)
            {
                bgyscroll = s1.videoram[0x07ba];
                if (s1.rowscroll)
                {
                    uint32_t rowoffs = 0x07c0u + ((((uint32_t)screen_y >> 3) & 0x1f) * 2u);
                    xscroll = (((int)((uint16_t)s1.videoram[rowoffs] | ((uint16_t)s1.videoram[rowoffs + 1] << 8))) & 0x1ff) - 512 + 10;
                }
                else
                {
                    xscroll = (((int)((uint16_t)s1.videoram[0x07c0] | ((uint16_t)s1.videoram[0x07c1] << 8))) & 0x1ff) - 512 + 10;
                }
            }
            else
            {
                bgyscroll = s1.videoram[0x0fbd];
                xscroll = (int)(int16_t)(((uint16_t)s1.videoram[0x0ffc] | ((uint16_t)s1.videoram[0x0ffd] << 8)) + 28);
            }

            {
                uint32_t bgy = (uint32_t)((screen_y + bgyscroll) & 0x1ff);
                bg_pix_y = bgy & 7u;
                bg_ty_base = ((bgy >> 3) & 0x1fu) << 6;
            }

            {
                uint32_t fg_last_col = 0xffffffffu;
                uint32_t bg_last_key = 0xffffffffu;
                uint16_t fg_base = 0, bg_base = 0;
                const uint8_t *fg_rowpix = NULL, *bg_rowpix = NULL;
                const uint16_t *sprite_row = s1.sprite_line + (size_t)y * SYSTEM1_RAW_WIDTH;

            for (x = 0; x < SYSTEM1_RAW_WIDTH; x += 2)
            {
                int bgpage = 0;
                int bgx;
                int out_x = x >> 1;
                uint32_t bgpage_norm;
                uint32_t bg_key;
                uint32_t fgx = (uint32_t)out_x & 0xffu;
                uint32_t fg_col = (fgx >> 3) & 0x1fu;
                uint16_t bg, fg, sp, pen;
                uint8_t lookup_index, lookup_value;

                if (fg_col != fg_last_col)
                {
                    uint32_t fg_off = fg_row_base | (fg_col << 1);
                    tilemap_row_from_offset(fg_off, fg_pix_y, &fg_base, &fg_rowpix);
                    fg_last_col = fg_col;
                }

                bgx = ((x - xscroll) / 2) & 0x1ff;
                if (s1.video_type == 2)
                {
                    int quad = ((((screen_y + bgyscroll) & 0x1ff) >> 8) * 2) + (bgx >> 8);
                    bgpage = s1.videoram[0x0740 + (quad * 2)] & 7;
                }
                bgpage_norm = pages_power_of_two ? ((uint32_t)bgpage & page_mask) : ((uint32_t)bgpage % pages);
                bg_key = (bgpage_norm << 5) | (((uint32_t)bgx >> 3) & 0x1fu);
                if (bg_key != bg_last_key)
                {
                    uint32_t bg_off = (bgpage_norm << 11) | bg_ty_base | ((((uint32_t)bgx >> 3) & 0x1fu) << 1);
                    tilemap_row_from_offset(bg_off, bg_pix_y, &bg_base, &bg_rowpix);
                    bg_last_key = bg_key;
                }

                bg = tilemap_pixel_from_row(bg_base, bg_rowpix, (uint32_t)bgx);
                fg = tilemap_pixel_from_row(fg_base, fg_rowpix, fgx);
                sp = sprite_row[(uint32_t)x];
                lookup_index = (uint8_t)((((sp & 0x0f) == 0) << 0) |
                               (((fg & 0x07) == 0) << 1) |
                               (((fg >> 9) & 0x03) << 2) |
                               (((bg & 0x07) == 0) << 4) |
                               (((bg >> 9) & 0x03) << 5));
                lookup_value = s1.prom[lookup_index];
                switch (lookup_value & 3)
                {
                    default:
                    case 0: pen = (uint16_t)(0x000 | (sp & 0x1ff)); break;
                    case 1: pen = (uint16_t)(0x200 | (fg & 0x1ff)); break;
                    case 2:
                    case 3: pen = (uint16_t)(0x400 | (bg & 0x1ff)); break;
                }
                {
                    uint32_t out = system1_pen(pen);
#ifdef MULTIREXZ80_RENDER_32BPP
                    if (fast_bounds)
                    {
                        if (s1.rotate == SYSTEM1_ROTATE_NONE)
                            ((uint32_t *)(void *)(bitmap.data + (size_t)y * bitmap.pitch))[out_x] = out;
                        else if (s1.rotate == SYSTEM1_ROTATE_CW)
                            ((uint32_t *)(void *)(bitmap.data + (size_t)out_x * bitmap.pitch))[SYSTEM1_VISIBLE_HEIGHT - 1 - y] = out;
                        else
                            ((uint32_t *)(void *)(bitmap.data + (size_t)(SYSTEM1_VISIBLE_WIDTH - 1 - out_x) * bitmap.pitch))[y] = out;
                    }
                    else
                    {
                        int dx = out_x;
                        int dy = y;
                        if (s1.rotate == SYSTEM1_ROTATE_CW)
                        {
                            dx = SYSTEM1_VISIBLE_HEIGHT - 1 - y;
                            dy = out_x;
                        }
                        else if (s1.rotate == SYSTEM1_ROTATE_CCW)
                        {
                            dx = y;
                            dy = SYSTEM1_VISIBLE_WIDTH - 1 - out_x;
                        }
                        if (dx >= 0 && dy >= 0 && dx < (int)bitmap.width && dy < (int)bitmap.height)
                            ((uint32_t *)(void *)(bitmap.data + (size_t)dy * bitmap.pitch))[dx] = out;
                    }
#else
                    if (fast_bounds)
                    {
                        if (s1.rotate == SYSTEM1_ROTATE_NONE)
                            ((uint16_t *)(void *)(bitmap.data + (size_t)y * bitmap.pitch))[out_x] = (uint16_t)out;
                        else if (s1.rotate == SYSTEM1_ROTATE_CW)
                            ((uint16_t *)(void *)(bitmap.data + (size_t)out_x * bitmap.pitch))[SYSTEM1_VISIBLE_HEIGHT - 1 - y] = (uint16_t)out;
                        else
                            ((uint16_t *)(void *)(bitmap.data + (size_t)(SYSTEM1_VISIBLE_WIDTH - 1 - out_x) * bitmap.pitch))[y] = (uint16_t)out;
                    }
                    else
                    {
                        int dx = out_x;
                        int dy = y;
                        if (s1.rotate == SYSTEM1_ROTATE_CW)
                        {
                            dx = SYSTEM1_VISIBLE_HEIGHT - 1 - y;
                            dy = out_x;
                        }
                        else if (s1.rotate == SYSTEM1_ROTATE_CCW)
                        {
                            dx = y;
                            dy = SYSTEM1_VISIBLE_WIDTH - 1 - out_x;
                        }
                        if (dx >= 0 && dy >= 0 && dx < (int)bitmap.width && dy < (int)bitmap.height)
                            ((uint16_t *)(void *)(bitmap.data + (size_t)dy * bitmap.pitch))[dx] = (uint16_t)out;
                    }
#endif
                }
            }
            }
        }
    }
}

void system1_reset(void)
{
    int i;
    system1_memory_map(1);
    for (i = 0; i < SYSTEM1_CPU_COUNT; i++)
        system1_reset_one_cpu(i);
    system1_load_cpu(SYSTEM1_CPU_MAIN);
    vdp.height = SYSTEM1_VISIBLE_HEIGHT;
    vdp.lpf = SYSTEM1_LINES_PER_FRAME;
    vdp.line = 0;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = (s1.rotate != SYSTEM1_ROTATE_NONE) ? SYSTEM1_VISIBLE_HEIGHT : SYSTEM1_VISIBLE_WIDTH;
    bitmap.viewport.h = (s1.rotate != SYSTEM1_ROTATE_NONE) ? SYSTEM1_VISIBLE_WIDTH : SYSTEM1_VISIBLE_HEIGHT;
    bitmap.viewport.changed = 1;
}

void system1_frame(uint32_t skip_render)
{
    int line;
    int32_t line_z80 = 0;
    const int32_t cycles_per_line = s1.main_cycles_per_line ? (int32_t)s1.main_cycles_per_line : SYSTEM1_CYCLES_PER_LINE;

    if (input.system & INPUT_RESET)
    {
        system1_reset();
    }
    sms.paused = 0;
    system1_update_dial_from_dpad();
    vdp.height = SYSTEM1_VISIBLE_HEIGHT;
    vdp.lpf = SYSTEM1_LINES_PER_FRAME;
    s1.collision_partial_line = 0;

    for (line = 0; line < SYSTEM1_LINES_PER_FRAME; line++)
    {
        vdp.line = line;

        /*
         * MAME renders System 1/2 as the beam reaches each scanline, with
         * update_partial() calls before palette/VRAM/collision side effects.
         * Rendering only after the whole emulated frame lets the vblank IRQ
         * handler's palette updates leak backwards into the just-finished
         * picture; Block Gal's color-cycled title logo makes this immediately
         * visible as incorrect letter colors.  Snapshot the visible field at
         * the start of vblank, before asserting the vblank IRQ.
         */
        if (line == SYSTEM1_VISIBLE_HEIGHT)
        {
            system1_update_collisions_to_line(SYSTEM1_VISIBLE_HEIGHT);
            if (!skip_render)
                system1_render();
        }

        line_z80 += cycles_per_line;
        system1_load_cpu(SYSTEM1_CPU_MAIN);
        if (line == SYSTEM1_VISIBLE_HEIGHT)
            z80_set_irq_line(0, ASSERT_LINE);
        z80_execute(line_z80 - s1.cpu[SYSTEM1_CPU_MAIN].cycles);
        if (line == SYSTEM1_VISIBLE_HEIGHT)
            z80_set_irq_line(0, CLEAR_LINE);

        if (((line - 32) & 63) == 0)
            system1_set_sound_irq(1);
        system1_load_cpu(SYSTEM1_CPU_SOUND);
        z80_set_irq_line(INPUT_LINE_NMI, s1.sound_nmi_asserted ? ASSERT_LINE : CLEAR_LINE);
        z80_set_irq_line(INPUT_LINE_IRQ0, s1.sound_irq_asserted ? ASSERT_LINE : CLEAR_LINE);
        z80_execute(line_z80 - s1.cpu[SYSTEM1_CPU_SOUND].cycles);

        MULTIREXZ80_sound_update(line);
    }
    s1.cpu[SYSTEM1_CPU_MAIN].cycles = 0;
    s1.cpu[SYSTEM1_CPU_SOUND].cycles = 0;
    system1_load_cpu(SYSTEM1_CPU_MAIN);
}
