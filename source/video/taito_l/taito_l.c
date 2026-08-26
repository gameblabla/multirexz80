/*
 * MultiRexZ80
 *
 * Default project license: GPL-2.0-or-later.  File-specific notices below
 * are retained and take precedence for imported or derived components,
 * including MAME-derived code and other third-party modules.
 *
 * Taito L-System arcade hardware support for MultiRexZ80.
 *
 * This is an independent compact C implementation of the single-CPU
 * Taito L-System games (Plotting, Puzznic, Palamedes, Cachat/Tube-It,
 * American Horseshoes, Play Girls / Play Girls 2 / LA Girl, Cuby Bop),
 * built around the custom TC0090LVC SoC.  Hardware maps, ROM layout,
 * the TC0090LVC register model, tile/sprite formats and the palette
 * equation are derived from MAME's BSD-3-Clause Taito L driver
 * (src/mame/taito/taito_l.cpp) by Olivier Galibert and the TC0090LVC
 * device (src/devices/machine/tc009xlvc.cpp/.h) by Angelo Salese.
 */

#include "shared.h"
#include "taito_l.h"
#include "ym2203.h"

/* ---- Region sizes (large enough for every game in the database) ------ */
#define TAITOL_MAIN_SIZE     0x100000u   /* fhawk uses 0x100000 */
#define TAITOL_GFX_SIZE      0x180000u   /* champwr uses 0x180000 */
#define TAITOL_AUDIO_SIZE     0x80000u
#define TAITOL_SLAVE_SIZE    0x20000u
#define TAITOL_ADPCM_SIZE    0x20000u
#define TAITOL_MCU_SIZE      0x1000u
#define TAITOL_YM2610_SIZE   0x100000u

/* VRAM / palette / bitmap RAM sizes (TC0090LVC address space). */
#define TAITOL_VRAM_SIZE     0x10000u    /* 64 KB tile/sprite VRAM */
#define TAITOL_BITMAP_SIZE   0x20000u    /* 128 KB bitmap RAM */
#define TAITOL_PALETTE_SIZE  0x200u      /* 256 entries x 2 bytes */
#define TAITOL_WORKRAM_SIZE  0x2000u     /* 0x8000-0x9fff */
#define TAITOL_VREGS_SIZE    0x100u

/* MAME gfx_layout character increments are in bits: an 8x8 tile occupies
 * 256 bits (32 bytes), and a 16x16 tile occupies 1024 bits (128 bytes). */
#define TAITOL_GFX8_MAX     (TAITOL_GFX_SIZE / 32u)
#define TAITOL_GFX16_MAX    (TAITOL_GFX_SIZE / 128u)
#define TAITOL_VRAM_TILES   (TAITOL_VRAM_SIZE / 32u)   /* RAM-based 8x8 tiles */

#define TAITOL_MAIN_ROM_MASK 0x7fffu   /* visible fixed+banked ROM window  */
#define TAITOL_ROM_BANK_SHIFT 13

#define TAITOL_PAL_ENTRIES 256

#ifdef MULTIREXZ80_RENDER_32BPP
typedef uint32_t taitol_pixel_t;
#else
typedef uint16_t taitol_pixel_t;
#endif

/* YM2203 clock = 13.33056 MHz / 4 = 3.33264 MHz (1-CPU games). */
#define TAITOL_YM2203_CLOCK 3332640u

typedef struct
{
    /* ROM regions */
    uint8_t *main_rom;
    uint8_t *gfx_rom;
    uint8_t *audio_rom;   /* not driven (multi-CPU) */
    uint8_t *slave_rom;   /* not driven (multi-CPU) */
    uint8_t *adpcm_rom;   /* not driven (multi-CPU) */
    uint8_t *mcu_rom;     /* not driven (puzznic MCU) */
    uint8_t *ym2610_rom;  /* not driven (raimais) */
    uint32_t gfx_region_size;

    /* TC0090LVC RAM */
    uint8_t *vram;        /* 64 KB tile/sprite VRAM (vram_space 0x10000-0x1ffff) */
    uint8_t *bitmap_ram;  /* 128 KB bitmap RAM (vram_space 0x40000-0x5ffff) */
    uint8_t *palette_ram; /* 256x2 palette (vram_space 0x80000-0x801ff) */
    uint8_t *work_ram;    /* 0x8000-0x9fff external RAM */
    uint8_t *vregs;       /* 0x100 video registers */
    uint8_t *sprram_buf;  /* 0x400 double-buffered sprite RAM */

    /* Pre-decoded gfx for fast rendering */
    uint8_t *gfx8_dec;    /* 8x8 ROM tiles, 1 byte per pixel */
    uint8_t *gfx16_dec;  /* 16x16 ROM tiles, 1 byte per pixel */
    uint8_t *vram_dec;   /* 8x8 VRAM tiles, 1 byte per pixel (RAM-based chars) */
    uint8_t *priority_bitmap;
    taitol_pixel_t *frame_buffer;
    uint32_t *palette;    /* 256-entry RGB palette */
    int      gfx_decoded;

    /* TC0090LVC registers */
    uint8_t  irq_vector[3];
    uint8_t  irq_enable;
    uint8_t  ram_bank[4];
    uint8_t  rom_bank;
    uint8_t  last_irq_level;
    uint8_t  irq_active;

    /* i8255 PPI (palamed/cachat/plgirls). Ports C/A/B mirror MAME. */
    uint8_t  ppi_port_a;
    uint8_t  ppi_port_b;
    uint8_t  ppi_port_c;
    uint8_t  ppi_ctrl;

    /* Per-game config */
    uint8_t  game_variant;
    uint8_t  map_variant;    /* TAITOL_MAP_* */
    uint8_t  rotate;         /* 0 = none, 1 = ROT270 */
    uint8_t  active;
    uint8_t  dswa;
    uint8_t  dswb;

    /* Sound */
    ym2203_state *ym;
    int16_t *ym_left;
    int16_t *ym_right;
    uint32_t ym_capacity;
} taitol_state;

static taitol_state tl;

/* ---- Helpers ---------------------------------------------------------- */

static void *tl_xcalloc(size_t n, size_t sz)
{
    void *p = calloc(n, sz);
    return p;
}

static int taitol_ensure_alloc(void)
{
    if (tl.main_rom) return 1;
    tl.main_rom    = tl_xcalloc(TAITOL_MAIN_SIZE, 1);
    tl.gfx_rom     = tl_xcalloc(TAITOL_GFX_SIZE, 1);
    tl.audio_rom   = tl_xcalloc(TAITOL_AUDIO_SIZE, 1);
    tl.slave_rom   = tl_xcalloc(TAITOL_SLAVE_SIZE, 1);
    tl.adpcm_rom   = tl_xcalloc(TAITOL_ADPCM_SIZE, 1);
    tl.mcu_rom     = tl_xcalloc(TAITOL_MCU_SIZE, 1);
    tl.ym2610_rom  = tl_xcalloc(TAITOL_YM2610_SIZE, 1);
    tl.vram        = tl_xcalloc(TAITOL_VRAM_SIZE, 1);
    tl.bitmap_ram  = tl_xcalloc(TAITOL_BITMAP_SIZE, 1);
    tl.palette_ram = tl_xcalloc(TAITOL_PALETTE_SIZE, 1);
    tl.work_ram    = tl_xcalloc(TAITOL_WORKRAM_SIZE, 1);
    tl.vregs       = tl_xcalloc(TAITOL_VREGS_SIZE, 1);
    tl.sprram_buf  = tl_xcalloc(0x400, 1);
    tl.gfx8_dec    = tl_xcalloc((size_t)TAITOL_GFX8_MAX * 64, 1);
    tl.gfx16_dec   = tl_xcalloc((size_t)TAITOL_GFX16_MAX * 256, 1);
    tl.vram_dec    = tl_xcalloc((size_t)TAITOL_VRAM_TILES * 64, 1);
    tl.priority_bitmap = tl_xcalloc(TAITOL_VISIBLE_WIDTH * TAITOL_VISIBLE_HEIGHT, 1);
    tl.frame_buffer = tl_xcalloc(TAITOL_VISIBLE_WIDTH * TAITOL_VISIBLE_HEIGHT,
                                 sizeof(*tl.frame_buffer));
    tl.palette     = (uint32_t *)calloc(TAITOL_PAL_ENTRIES, sizeof(uint32_t));
    if (!tl.main_rom || !tl.gfx_rom || !tl.vram || !tl.bitmap_ram ||
        !tl.palette_ram || !tl.work_ram || !tl.vregs || !tl.sprram_buf ||
        !tl.gfx8_dec || !tl.gfx16_dec || !tl.vram_dec ||
        !tl.priority_bitmap || !tl.frame_buffer || !tl.palette)
    {
        taitol_free();
        return 0;
    }
    taitol_clear_roms();
    return 1;
}

/* ---- Allocation / region API ------------------------------------------ */

int taitol_alloc(void)
{
    if (tl.main_rom) return 1;
    return taitol_ensure_alloc();
}

void taitol_free(void)
{
    if (tl.ym) { ym2203_destroy(tl.ym); tl.ym = NULL; }
    free(tl.ym_left); tl.ym_left = NULL;
    free(tl.ym_right); tl.ym_right = NULL;
    tl.ym_capacity = 0;
    free(tl.main_rom); free(tl.gfx_rom); free(tl.audio_rom); free(tl.slave_rom);
    free(tl.adpcm_rom); free(tl.mcu_rom); free(tl.ym2610_rom);
    free(tl.vram); free(tl.bitmap_ram); free(tl.palette_ram); free(tl.work_ram);
    free(tl.vregs); free(tl.sprram_buf);
    free(tl.gfx8_dec); free(tl.gfx16_dec); free(tl.vram_dec);
    free(tl.priority_bitmap);
    free(tl.frame_buffer);
    free(tl.palette);
    memset(&tl, 0, sizeof(tl));
}

void taitol_clear_roms(void)
{
    if (!tl.main_rom && !taitol_ensure_alloc()) return;
    memset(tl.main_rom, 0xff, TAITOL_MAIN_SIZE);
    memset(tl.gfx_rom, 0xff, TAITOL_GFX_SIZE);
    memset(tl.audio_rom, 0xff, TAITOL_AUDIO_SIZE);
    memset(tl.slave_rom, 0xff, TAITOL_SLAVE_SIZE);
    memset(tl.adpcm_rom, 0xff, TAITOL_ADPCM_SIZE);
    memset(tl.mcu_rom, 0xff, TAITOL_MCU_SIZE);
    memset(tl.ym2610_rom, 0xff, TAITOL_YM2610_SIZE);
    tl.gfx_decoded = 0;
    tl.gfx_region_size = 0;
}

int taitol_set_region(int region, uint32_t offset, const uint8_t *data, uint32_t size)
{
    uint8_t *dst = NULL;
    uint32_t limit = 0;
    if (!data || !taitol_ensure_alloc()) return 0;
    switch (region)
    {
        case TAITOL_REGION_MAIN:    dst = tl.main_rom;   limit = TAITOL_MAIN_SIZE;   break;
        case TAITOL_REGION_GFX:     dst = tl.gfx_rom;    limit = TAITOL_GFX_SIZE;    break;
        case TAITOL_REGION_AUDIO:   dst = tl.audio_rom;  limit = TAITOL_AUDIO_SIZE;  break;
        case TAITOL_REGION_SLAVE:   dst = tl.slave_rom;  limit = TAITOL_SLAVE_SIZE;  break;
        case TAITOL_REGION_ADPCM:   dst = tl.adpcm_rom;  limit = TAITOL_ADPCM_SIZE;  break;
        case TAITOL_REGION_MCU:     dst = tl.mcu_rom;    limit = TAITOL_MCU_SIZE;    break;
        case TAITOL_REGION_YM2610:  dst = tl.ym2610_rom; limit = TAITOL_YM2610_SIZE; break;
        default: return 0;
    }
    if (offset > limit || size > limit - offset) return 0;
    memcpy(dst + offset, data, size);
    if (region == TAITOL_REGION_GFX)
    {
        tl.gfx_decoded = 0;
        if (offset + size > tl.gfx_region_size)
            tl.gfx_region_size = offset + size;
    }
    return 1;
}

uint8_t *taitol_main_rom_ptr(void)
{
    return tl.main_rom;
}

uint8_t *taitol_gfx_rom_ptr(void)
{
    return tl.gfx_rom;
}

void taitol_invalidate_gfx(void)
{
    tl.gfx_decoded = 0;
}

void taitol_note_gfx_region_size(uint32_t size)
{
    if (size > tl.gfx_region_size && size <= TAITOL_GFX_SIZE)
        tl.gfx_region_size = size;
    tl.gfx_decoded = 0;
}

/* ---- Per-game configuration ------------------------------------------- */

void taitol_set_game_variant(int variant)
{
    static const struct {
        uint8_t map;
        uint8_t rotate;
        uint8_t dswa;
        uint8_t dswb;
    } defaults[TAITOL_GAME_COUNT] = {
        /* PLOTTING */    { TAITOL_MAP_PLOTTING, 0, 0x00, 0x00 },
        /* PLOTTINGA */   { TAITOL_MAP_PLOTTING, 0, 0x00, 0x00 },
        /* PLOTTINGB */   { TAITOL_MAP_PLOTTING, 0, 0x00, 0x00 },
        /* PLOTTINGU */   { TAITOL_MAP_PLOTTING, 0, 0x00, 0x00 },
        /* FLIPULL */     { TAITOL_MAP_PLOTTING, 0, 0x00, 0x00 },
        /* PUZZNIC */     { TAITOL_MAP_PUZZNIC,  0, 0x00, 0x00 },
        /* PUZZNICU */    { TAITOL_MAP_PUZZNIC,  0, 0x00, 0x00 },
        /* PUZZNICJ */    { TAITOL_MAP_PUZZNIC,  0, 0x00, 0x00 },
        /* PUZZNICI */    { TAITOL_MAP_PUZZNICI, 0, 0x00, 0x00 },
        /* PUZZNICB */    { TAITOL_MAP_PUZZNICI, 0, 0x00, 0x00 },
        /* PUZZNICBA */   { TAITOL_MAP_PUZZNICI, 0, 0x00, 0x00 },
        /* HORSHOES */    { TAITOL_MAP_HORSHOES, 1, 0x00, 0x00 },
        /* PALAMED */     { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* PALAMEDJ */    { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* CACHAT */      { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* TUBEIT */      { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* CUBYBOP */     { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* PLGIRLS */     { TAITOL_MAP_PALAMED,  1, 0xff, 0xff },
        /* LAGIRL */      { TAITOL_MAP_PALAMED,  1, 0xff, 0xff },
        /* PLGIRLS2 */    { TAITOL_MAP_PALAMED,  1, 0xff, 0xff },
        /* PLGIRLS2B */   { TAITOL_MAP_PALAMED,  1, 0xff, 0xff },
        /* multi-CPU games below: not driven, kept for ROM DB completeness */
        /* RAIMAIS */     { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* RAIMAISJ */    { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* RAIMAISJO */   { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* FHAWK */        { TAITOL_MAP_PALAMED,  1, 0x00, 0x00 },
        /* FHAWKJ */       { TAITOL_MAP_PALAMED,  1, 0x00, 0x00 },
        /* CHAMPWR */      { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* CHAMPWRU */     { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* CHAMPWRJ */     { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* KURIKINT */     { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* KURIKINTW */    { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* KURIKINTU */     { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* KURIKINTJ */    { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* KURIKINTA */    { TAITOL_MAP_PALAMED,  0, 0x00, 0x00 },
        /* EVILSTON */     { TAITOL_MAP_PALAMED,  1, 0x00, 0x00 },
    };
    if (!taitol_ensure_alloc()) return;
    tl.active = 1;
    tl.game_variant = (uint8_t)variant;
    if (variant >= 0 && variant < TAITOL_GAME_COUNT)
    {
        tl.map_variant = defaults[variant].map;
        tl.rotate      = defaults[variant].rotate;
        tl.dswa        = defaults[variant].dswa;
        tl.dswb        = defaults[variant].dswb;
    }
}

/* ---- Palette decoding (xBGRBBBBGGGGRRRR_bit0) ------------------------- */

static inline uint8_t pal5bit(uint8_t n) { return (uint8_t)((n << 3) | (n >> 2)); }

static void taitol_update_palette(void)
{
    int i;
    for (i = 0; i < TAITOL_PAL_ENTRIES; i++)
    {
        uint16_t raw = (uint16_t)(tl.palette_ram[i * 2] | (tl.palette_ram[i * 2 + 1] << 8));
        uint8_t r5 = (uint8_t)(((raw << 1) & 0x1e) | ((raw >> 12) & 0x01));
        uint8_t g5 = (uint8_t)(((raw >> 3) & 0x1e) | ((raw >> 13) & 0x01));
        uint8_t b5 = (uint8_t)(((raw >> 7) & 0x1e) | ((raw >> 14) & 0x01));
        uint8_t r = pal5bit(r5);
        uint8_t g = pal5bit(g5);
        uint8_t b = pal5bit(b5);
        tl.palette[i] = MAKE_PIXEL(r, g, b);
    }
    bitmap.pal.update = 1;
}

/* ---- Gfx decoding (MAME layout_8x8 / layout_16x16) --------------------- */

static const int gfx_plane_offset[4] = { 8, 12, 0, 4 };
static const int gfx_x_offset_8[8]  = { 3, 2, 1, 0, 19, 18, 17, 16 };
static const int gfx_y_offset_8[8]  = { 0, 32, 64, 96, 128, 160, 192, 224 };
static const int gfx_x_offset_16[16] =
    { 3,2,1,0, 19,18,17,16, 259,258,257,256, 275,274,273,272 };
static const int gfx_y_offset_16[16] =
    { 0,32,64,96,128,160,192,224, 512,544,576,608,640,672,704,736 };

static int taitol_gfx_pixel(const uint8_t *rom, uint32_t size, int is16,
                            uint32_t tile, int x, int y)
{
    const int *xo = is16 ? gfx_x_offset_16 : gfx_x_offset_8;
    const int *yo = is16 ? gfx_y_offset_16 : gfx_y_offset_8;
    int inc = is16 ? 1024 : 256;
    uint32_t base = tile * (uint32_t)inc;
    int pixel = 0, p;
    for (p = 0; p < 4; p++)
    {
        uint32_t bit_index = base + (uint32_t)yo[y] + (uint32_t)xo[x] + (uint32_t)gfx_plane_offset[p];
        uint32_t byte_index = bit_index >> 3;
        /* MAME uses MSB-first bit ordering: 0x80 >> (bitoffset % 8) */
        int bit = 7 - (int)(bit_index & 7);
        if (byte_index < size)
            /* MAME assigns plane 0 to the most-significant output bit. */
            pixel |= ((rom[byte_index] >> bit) & 1) << (3 - p);
    }
    return pixel;
}

static void taitol_decode_gfx(void)
{
    uint32_t t, x, y;
    uint32_t gfx_size = tl.gfx_region_size ? tl.gfx_region_size : TAITOL_GFX_SIZE;
    uint32_t gfx8_count = gfx_size / 32u;
    uint32_t gfx16_count = gfx_size / 128u;
    if (tl.gfx_decoded) return;
    for (t = 0; t < gfx8_count; t++)
    {
        uint8_t *dst = tl.gfx8_dec + t * 64;
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                dst[y * 8 + x] = (uint8_t)taitol_gfx_pixel(tl.gfx_rom, gfx_size, 0, t, x, y);
    }
    for (t = 0; t < gfx16_count; t++)
    {
        uint8_t *dst = tl.gfx16_dec + t * 256;
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                dst[y * 16 + x] = (uint8_t)taitol_gfx_pixel(tl.gfx_rom, gfx_size, 1, t, x, y);
    }
    tl.gfx_decoded = 1;
}

/* Decode the RAM-based 8x8 tiles from VRAM (TX layer graphics).  Called
 * every frame because VRAM tiles change during gameplay. */
static void taitol_decode_vram_tiles(void)
{
    uint32_t t, x, y;
    for (t = 0; t < TAITOL_VRAM_TILES; t++)
    {
        uint8_t *dst = tl.vram_dec + t * 64;
        /* The VRAM tiles use the same 8x8 layout but the source is the
         * VRAM region at offset 0 (the gfx(2) RAM device). */
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
            {
                int pixel = 0, p;
                uint32_t base = t * 256u;
                for (p = 0; p < 4; p++)
                {
                    uint32_t bit_index = base + (uint32_t)gfx_y_offset_8[y] +
                                         (uint32_t)gfx_x_offset_8[x] +
                                         (uint32_t)gfx_plane_offset[p];
                    uint32_t byte_index = bit_index >> 3;
                    /* MAME uses MSB-first bit ordering */
                    int bit = 7 - (int)(bit_index & 7);
                    if (byte_index < TAITOL_VRAM_SIZE)
                        pixel |= ((tl.vram[byte_index] >> bit) & 1) << (3 - p);
                }
                dst[y * 8 + x] = (uint8_t)pixel;
            }
    }
}

/* ---- TC0090LVC register helpers --------------------------------------- */

static int  tl_screen_enable(void) { return (tl.vregs[4] >> 5) & 1; }
static int  tl_global_flip(void)   { return (tl.vregs[4] >> 4) & 1; }
static int  tl_bg0_pri(void)       { return (tl.vregs[4] >> 3) & 1; }
static int  tl_bitmap_mode(void)   { return (tl.vregs[4] & 0x7) == 7; }
static uint8_t tl_tilebank(int slot){ return tl.vregs[slot & 3]; }

/* vram_addr: 20-bit VRAM address from the 4KB banking.  The Z80 address
 * bits 12-13 select one of the four ram_bank registers, which supply the
 * upper bits of the 20-bit address. */
static inline uint32_t tl_vram_addr(uint16_t offset)
{
    int bank = (offset >> 12) & 3;
    return ((uint32_t)tl.ram_bank[bank] << 12) | (offset & 0x0fff);
}

/* Read/write the VRAM space (tiles, sprite RAM, bitmap RAM, palette).
 * Palette is mirrored every 0x200 bytes within 0x80000-0x80fff (MAME .mirror(0x00e00)). */
static uint8_t tl_vram_space_read(uint32_t addr)
{
    if (addr >= 0x10000u && addr < 0x20000u)
        return tl.vram[addr - 0x10000u];
    if (addr >= 0x40000u && addr < 0x60000u)
        return tl.bitmap_ram[addr - 0x40000u];
    if (addr >= 0x80000u && addr < 0x81000u)
        return tl.palette_ram[(addr - 0x80000u) & 0x1ffu];
    return 0xff;
}

static void tl_vram_space_write(uint32_t addr, uint8_t data)
{
    if (addr >= 0x14000u && addr < 0x20000u)
    {
        uint32_t off = addr - 0x10000u;
        tl.vram[off] = data;
    }
    else if (addr >= 0x40000u && addr < 0x60000u)
    {
        tl.bitmap_ram[addr - 0x40000u] = data;
    }
    else if (addr >= 0x80000u && addr < 0x81000u)
    {
        uint32_t off = (addr - 0x80000u) & 0x1ffu;
        tl.palette_ram[off] = data;
    }
}

/* ---- Memory map (cpu_readmap / cpu_writemap) -------------------------- */

/* Update cpu_readmap entries for the banked ROM window (0x6000-0x7fff).
 * Called on init and whenever the ROM bank register (0xff08) changes. */
void taitol_update_rom_bank(void)
{
    uint32_t bank_base = ((uint32_t)tl.rom_bank << TAITOL_ROM_BANK_SHIFT) & (TAITOL_MAIN_SIZE - 1);
    int i;
    for (i = 0; i < 8; i++)
        cpu_readmap[24 + i] = tl.main_rom + bank_base + ((uint32_t)i << 10);
}

void taitol_memory_map(int clear_ram)
{
    int i;
    if (!taitol_ensure_alloc()) return;
    if (clear_ram)
    {
        memset(tl.work_ram, 0, TAITOL_WORKRAM_SIZE);
        memset(tl.vram, 0, TAITOL_VRAM_SIZE);
        memset(tl.bitmap_ram, 0, TAITOL_BITMAP_SIZE);
        memset(tl.palette_ram, 0, TAITOL_PALETTE_SIZE);
        memset(tl.vregs, 0, TAITOL_VREGS_SIZE);
        memset(tl.sprram_buf, 0, 0x400);
        tl.irq_vector[0] = tl.irq_vector[1] = tl.irq_vector[2] = 0;
        tl.irq_enable = 0;
        tl.rom_bank = 0;
        tl.ram_bank[0] = tl.ram_bank[1] = tl.ram_bank[2] = tl.ram_bank[3] = 0x80;
        tl.last_irq_level = 0;
        tl.irq_active = 0;
        tl.ppi_port_a = tl.ppi_port_b = tl.ppi_port_c = 0xff;
        tl.ppi_ctrl = 0;
    }
    /* Point the Z80 read/write maps at our handlers; the actual dispatch is
     * done in taitol_readmem / taitol_writemem because of the banking.
     * However, cpu_readop() (opcode fetch) uses cpu_readmap directly, so
     * we must wire the ROM area into cpu_readmap for the CPU to execute. */
    for (i = 0; i < 64; i++)
    {
        cpu_readmap[i] = dummy_read;
        cpu_writemap[i] = dummy_write;
    }
    /* Fixed ROM: 0x0000-0x5fff (24 × 1KB pages) */
    for (i = 0; i < 24; i++)
        cpu_readmap[i] = tl.main_rom + ((uint32_t)i << 10);
    /* Banked ROM: 0x6000-0x7fff (8 × 1KB pages) */
    taitol_update_rom_bank();
    z80_select_default_context();
    tl.gfx_decoded = 0;
}

uint8_t taitol_readmem(uint16_t address)
{
    /* 0x0000-0x5fff fixed ROM */
    if (address < 0x6000)
        return tl.main_rom[address & (TAITOL_MAIN_SIZE - 1)];
    /* 0x6000-0x7fff banked ROM */
    if (address < 0x8000)
    {
        uint32_t a = ((uint32_t)tl.rom_bank << TAITOL_ROM_BANK_SHIFT) | (address & 0x1fff);
        return tl.main_rom[a & (TAITOL_MAIN_SIZE - 1)];
    }
    /* 0x8000-0x9fff work RAM */
    if (address < 0xa000)
        return tl.work_ram[address & 0x1fff];
    /* 0xa000-0xa003 YM2203 (palamed map) */
    if (address >= 0xa000 && address <= 0xa003 && tl.map_variant == TAITOL_MAP_PALAMED)
        return ym2203_read(tl.ym, address & 3);
    /* 0xa800-0xa803 i8255 PPI (palamed map) */
    if (address >= 0xa800 && address <= 0xa803 && tl.map_variant == TAITOL_MAP_PALAMED)
    {
        switch (address & 3)
        {
            case 0: return tl.ppi_port_a;  /* PA (inputs handled at write time) */
            case 1: return tl.ppi_port_b;
            case 2: return tl.ppi_port_c;
            case 3: return tl.ppi_ctrl;
        }
    }
    /* 0xb000 / 0xb001 watchdog/control (read as 0xff) */
    if (address == 0xb001 && tl.map_variant == TAITOL_MAP_PALAMED)
        return 0xff;
    /* 0xc000-0xfdff banked VRAM */
    if (address >= 0xc000 && address < 0xfe00)
        return tl_vram_space_read(tl_vram_addr(address));
    /* 0xfe00-0xfeff video registers */
    if (address < 0xff00)
        return tl.vregs[address & 0xff];
    /* 0xff00-0xff02 IRQ vector */
    if (address < 0xff03)
        return tl.irq_vector[address & 3];
    /* 0xff03 IRQ enable */
    if (address == 0xff03)
        return tl.irq_enable;
    /* 0xff04-0xff07 RAM bank */
    if (address >= 0xff04 && address < 0xff08)
        return tl.ram_bank[address & 3];
    /* 0xff08 ROM bank */
    if (address == 0xff08)
        return tl.rom_bank;
    return 0xff;
}

void taitol_writemem(uint16_t address, uint8_t data)
{
    MULTIREXZ80_TRACE_MEM_WRITE(address, data);
    if (address < 0x8000) return;  /* ROM */
    if (address < 0xa000) { tl.work_ram[address & 0x1fff] = data; return; }
    /* YM2203 */
    if (address >= 0xa000 && address <= 0xa003 && tl.map_variant == TAITOL_MAP_PALAMED)
    {
        ym2203_write(tl.ym, address & 3, data);
        return;
    }
    /* i8255 PPI */
    if (address >= 0xa800 && address <= 0xa803 && tl.map_variant == TAITOL_MAP_PALAMED)
    {
        switch (address & 3)
        {
            case 0: tl.ppi_port_a = data; break;
            case 1: tl.ppi_port_b = data; break;
            case 2: tl.ppi_port_c = data; break;
            case 3: tl.ppi_ctrl = data; break;
        }
        return;
    }
    /* 0xb000 nop (control register) */
    if (address == 0xb000 && tl.map_variant == TAITOL_MAP_PALAMED) return;
    /* 0xc000-0xfdff banked VRAM */
    if (address >= 0xc000 && address < 0xfe00)
    {
        tl_vram_space_write(tl_vram_addr(address), data);
        return;
    }
    /* 0xfe00-0xfeff video registers */
    if (address < 0xff00)
    {
        if ((address & 0xfc) == 0)
        {
            /* tilebank changed; in MAME this marks all BG tiles dirty.  We
             * decode on demand so no explicit dirty tracking is needed. */
        }
        tl.vregs[address & 0xff] = data;
        return;
    }
    /* 0xff00-0xff02 IRQ vector */
    if (address < 0xff03) { tl.irq_vector[address & 3] = data; return; }
    /* 0xff03 IRQ enable */
    if (address == 0xff03)
    {
        tl.irq_enable = data;
        if ((tl.irq_enable & (1 << tl.last_irq_level)) == 0)
        {
            tl.irq_active = 0;
            z80_set_irq_line(0, 0);  /* CLEAR_LINE */
        }
        return;
    }
    /* 0xff04-0xff07 RAM bank */
    if (address >= 0xff04 && address < 0xff08) { tl.ram_bank[address & 3] = data; return; }
    /* 0xff08 ROM bank */
    if (address == 0xff08) { tl.rom_bank = data; taitol_update_rom_bank(); return; }
}

uint8_t taitol_port_r(uint16_t port)
{
    (void)port;
    return 0xff;
}

void taitol_port_w(uint16_t port, uint8_t data)
{
    (void)port; (void)data;
}

/* ---- Input handling (i8255 PPI + DIPs via YM2203 ports) -------------- */

static uint8_t tl_in0(void)
{
    /* plgirls/plgirls2 IN0: active-low.
     * bit0 SERVICE1, bit1 TILT, bit2 COIN1, bit3 COIN2,
     * bit4 START1, bit5 START2, bit6 P1 BUTTON1, bit7 P1 BUTTON2 */
    uint8_t r = 0xff;
    if (input.arcade & INPUT_ARCADE_SERVICE) r &= (uint8_t)~0x01;
    if (input.arcade & INPUT_ARCADE_TEST)    r &= (uint8_t)~0x02; /* tilt/test */
    if (input.arcade & INPUT_ARCADE_COIN1)  r &= (uint8_t)~0x04;
    if (input.arcade & INPUT_ARCADE_COIN2)  r &= (uint8_t)~0x08;
    if (input.arcade & INPUT_ARCADE_START1) r &= (uint8_t)~0x10;
    if (input.arcade & INPUT_ARCADE_START2) r &= (uint8_t)~0x20;
    if (input.pad[0] & INPUT_BUTTON1) r &= (uint8_t)~0x40;
    if (input.pad[0] & INPUT_BUTTON2) r &= (uint8_t)~0x80;
    return r;
}

static uint8_t tl_in1(void)
{
    /* plgirls/plgirls2 IN1 (player joysticks), active-low. */
    uint8_t r = 0xff;
    if (input.pad[0] & INPUT_UP)    r &= (uint8_t)~0x01;
    if (input.pad[0] & INPUT_DOWN)  r &= (uint8_t)~0x02;
    if (input.pad[0] & INPUT_LEFT)  r &= (uint8_t)~0x04;
    if (input.pad[0] & INPUT_RIGHT) r &= (uint8_t)~0x08;
    if (input.pad[1] & INPUT_UP)    r &= (uint8_t)~0x10;
    if (input.pad[1] & INPUT_DOWN)  r &= (uint8_t)~0x20;
    if (input.pad[1] & INPUT_LEFT)  r &= (uint8_t)~0x40;
    if (input.pad[1] & INPUT_RIGHT) r &= (uint8_t)~0x80;
    return r;
}

static uint8_t tl_in2(void)
{
    /* plgirls/plgirls2 IN2 (P2 buttons), active-low. */
    uint8_t r = 0xff;
    if (input.pad[1] & INPUT_BUTTON1) r &= (uint8_t)~0x01;
    if (input.pad[1] & INPUT_BUTTON2) r &= (uint8_t)~0x02;
    return r;
}

/* Update the i8255 PPI input ports from the host input state.  MAME wires
 * PA=IN0, PB=IN1, PC=IN2 for the cachat/plgirls configuration. */
static void taitol_update_inputs(void)
{
    tl.ppi_port_a = tl_in0();
    tl.ppi_port_b = tl_in1();
    tl.ppi_port_c = tl_in2();
}

/* ---- Reset ------------------------------------------------------------ */

void taitol_reset(void)
{
    taitol_memory_map(1);
    taitol_decode_gfx();
    if (tl.ym) ym2203_reset(tl.ym);
    taitol_update_palette();
    z80_reset();
    vdp.height = TAITOL_VISIBLE_HEIGHT;
    vdp.lpf = TAITOL_LINES_PER_FRAME;
    vdp.line = 0;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = tl.rotate ? TAITOL_VISIBLE_HEIGHT : TAITOL_VISIBLE_WIDTH;
    bitmap.viewport.h = tl.rotate ? TAITOL_VISIBLE_WIDTH : TAITOL_VISIBLE_HEIGHT;
    bitmap.viewport.changed = 1;
}

/* ---- Video rendering -------------------------------------------------- */

/* Draw a 16x16 sprite tile.  In MAME, sprites use gfx(1) which is the
 * 16x16 layout.  The sprite code from sprite RAM is used directly as
 * the 16x16 tile index (the <<= 2 / >>= 2 in MAME is only for the
 * tile callback and nets to no change when no callback modifies it). */
static void taitol_draw_sprite(taitol_pixel_t *dst, int pitch, uint32_t code,
                               int palette, int flipx, int flipy,
                               int x, int y, int clip_minx, int clip_maxx,
                               int clip_miny, int clip_maxy,
                               const uint8_t *priority)
{
    const uint8_t *src;
    uint32_t gfx16_count = (tl.gfx_region_size ? tl.gfx_region_size : TAITOL_GFX_SIZE) / 128u;
    int sx, sy, dx, dy;
    if (code >= gfx16_count) code %= gfx16_count;
    src = tl.gfx16_dec + code * 256;
    for (sy = 0; sy < 16; sy++)
    {
        dy = y + sy;
        if (dy < clip_miny || dy > clip_maxy) continue;
        for (sx = 0; sx < 16; sx++)
        {
            uint8_t pen;
            dx = x + sx;
            if (dx < clip_minx || dx > clip_maxx) continue;
            pen = src[(flipy ? 15 - sy : sy) * 16 + (flipx ? 15 - sx : sx)];
            if (pen == 0) continue;
            /* MAME's prio_transpen mask is 0xaa for sprite palette groups
             * 8-15.  BG0 writes priority value 1 when bg0_pri is clear,
             * so those sprite pixels are hidden by an opaque BG0 pixel. */
            if ((palette & 0x08) && priority && priority[dy * TAITOL_VISIBLE_WIDTH + dx])
                continue;
            dst[dy * pitch + dx] = tl.palette[(palette & 0x0f) * 16 + pen];
        }
    }
}

/* Render one BG tilemap layer into the destination bitmap.
 * Uses a pixel-based approach matching MAME's tilemap drawing:
 * For each screen pixel (sx, sy) in the visible area:
 *   tilemap_x = (sx - xoffs - scrollx) % 512
 *   tilemap_y = (sy + VISIBLE_Y - scrolly) % 256
 *   tile = tilemap[tilemap_y/8 * 64 + tilemap_x/8]
 *   pen = gfx_decode(tile, tilemap_x%8, tilemap_y%8) */
static void taitol_draw_bg_layer(taitol_pixel_t *dst, int pitch, int layer,
                                 int scrollx, int scrolly, int xoffs,
                                 int opaque, int flip,
                                 int clip_minx, int clip_maxx,
                                 int clip_miny, int clip_maxy,
                                 uint8_t *priority)
{
    int base = (layer == 0) ? 0x8000 : 0x9000;
    int sx, sy;
    /* MAME's effective non-flipped tilemap position is xoffs - (-dx),
     * i.e. xoffs + dx (and likewise dy vertically).  Convert each screen
     * coordinate back to the corresponding source coordinate. */

    for (sy = clip_miny; sy <= clip_maxy; sy++)
    {
        int screen_y = sy + TAITOL_VISIBLE_Y;
        int tm_y = flip ? (255 - screen_y - scrolly) : (screen_y - scrolly);
        if (tm_y < 0) tm_y += 256;
        tm_y %= 256;
        int tile_row = tm_y / 8;
        int pix_y = tm_y % 8;
        int row_off = base + (tile_row * 64) * 2;
        taitol_pixel_t *dst_line = dst + sy * pitch;

        for (sx = clip_minx; sx <= clip_maxx; sx++)
        {
            int tm_x = flip ? (319 - sx - xoffs - scrollx) : (sx - scrollx - xoffs);
            if (tm_x < 0) tm_x += 512;
            tm_x %= 512;
            int tile_col = tm_x / 8;
            int pix_x = tm_x % 8;
            int taddr = row_off + tile_col * 2;
            uint8_t code_lo = tl.vram[taddr];
            uint8_t attr = tl.vram[taddr + 1];
            uint32_t code = (uint32_t)code_lo | (((uint32_t)(attr & 0x03)) << 8);
            code |= (uint32_t)tl_tilebank((attr >> 2) & 3) << 10;
            int pal = (attr >> 4) & 0x0f;

            uint32_t gfx8_count = (tl.gfx_region_size ? tl.gfx_region_size : TAITOL_GFX_SIZE) / 32u;
            if (code >= gfx8_count) code %= gfx8_count;
            {
                uint8_t pen = tl.gfx8_dec[code * 64 + pix_y * 8 + pix_x];
                if (!opaque && pen == 0) continue;
                dst_line[sx] = tl.palette[(pal & 0x0f) * 16 + pen];
                if (priority) priority[sy * TAITOL_VISIBLE_WIDTH + sx] = 1;
            }
        }
    }
}

static void taitol_render(void)
{
    taitol_pixel_t *dst = tl.frame_buffer;
    int pitch = TAITOL_VISIBLE_WIDTH;
    int minx = 0, maxx = (int)TAITOL_VISIBLE_WIDTH - 1;
    int miny = 0, maxy = (int)TAITOL_VISIBLE_HEIGHT - 1;
    int x, y;
    int flip = tl_global_flip();
    /* Scroll values: MAME reads from &m_vram[0xb000] (live VRAM).
     * Scroll registers are at offsets 0x3f4-0x3f6 (BG0) and 0x3fc-0x3fe (BG1)
     * within the 0xb000 sprite/scroll block. */
    int bg0_dx = (int)(tl.vram[0xb3f4] | (tl.vram[0xb3f5] << 8));
    int bg0_dy = (int)tl.vram[0xb3f6];
    int bg1_dx = (int)(tl.vram[0xb3fc] | (tl.vram[0xb3fd] << 8));
    int bg1_dy = (int)tl.vram[0xb3fe];

    if (flip)
    {
        /* MAME: dx = ((dx & 0xfffc) | ((dx - 3) & 0x0003)) ^ 0xf; */
        bg0_dx = ((bg0_dx & 0xfffc) | ((bg0_dx - 3) & 0x0003)) ^ 0xf;
        bg1_dx = ((bg1_dx & 0xfffc) | ((bg1_dx - 3) & 0x0003)) ^ 0xf;
    }

    taitol_decode_gfx();
    taitol_decode_vram_tiles();
    taitol_update_palette();

    if (!bitmap.data || !dst)
        return;

    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = tl.rotate ? TAITOL_VISIBLE_HEIGHT : TAITOL_VISIBLE_WIDTH;
    bitmap.viewport.h = tl.rotate ? TAITOL_VISIBLE_WIDTH : TAITOL_VISIBLE_HEIGHT;
    bitmap.viewport.changed = 1;

    if (!tl_screen_enable())
    {
        for (y = 0; y < TAITOL_VISIBLE_HEIGHT; y++)
            for (x = 0; x < TAITOL_VISIBLE_WIDTH; x++)
                dst[y * pitch + x] = 0;
        goto present;
    }

    if (tl_bitmap_mode())
    {
        /* 8bpp bitmap mode - MAME: res_y = global_flip ? 256-y : y */
        for (y = 0; y < TAITOL_VISIBLE_HEIGHT; y++)
        {
            int screen_y = y + TAITOL_VISIBLE_Y;
            int res_y = flip ? (256 - screen_y) : screen_y;
            uint32_t count = (uint32_t)(res_y & 0xff) * 512u;
            for (x = 0; x < TAITOL_VISIBLE_WIDTH; x++)
            {
                int res_x = flip ? (TAITOL_VISIBLE_WIDTH - x) : x;
                dst[y * pitch + x] = tl.palette[tl.bitmap_ram[count + (res_x & 0x1ff)] & 0xff];
            }
        }
        goto present;
    }

    /* Clear with pen 0. */
    for (y = 0; y < TAITOL_VISIBLE_HEIGHT; y++)
        for (x = 0; x < TAITOL_VISIBLE_WIDTH; x++)
            dst[y * pitch + x] = tl.palette[0];
    memset(tl.priority_bitmap, 0, TAITOL_VISIBLE_WIDTH * TAITOL_VISIBLE_HEIGHT);

    /* BG1 (opaque) - MAME: m_bg_tilemap[1]->draw(OPAQUE)
     * scrolldx(38, -21), scrolldy(0, 0) */
    taitol_draw_bg_layer(dst, pitch, 1, bg1_dx, bg1_dy,
                         flip ? -21 : 38, 1, flip, minx, maxx, miny, maxy,
                         NULL);

    /* BG0 (priority-dependent) - MAME: m_bg_tilemap[0]->draw(0, bg0_pri() ? 0 : 1)
     * scrolldx(28, -11), scrolldy(0, 0) */
    taitol_draw_bg_layer(dst, pitch, 0, bg0_dx, bg0_dy,
                         flip ? -11 : 28, 0, flip, minx, maxx, miny, maxy,
                         tl_bg0_pri() ? NULL : tl.priority_bitmap);

    /* Sprites. MAME reads from m_sprram_buffer (double-buffered at VBlank). */
    {
        int i;
        for (i = 0; i < 0x3e7; i += 8)
        {
            uint32_t code = (uint32_t)tl.sprram_buf[i] | ((uint32_t)tl.sprram_buf[i + 1] << 8);
            int col = tl.sprram_buf[i + 2] & 0x0f;
            int fx = tl.sprram_buf[i + 3] & 0x1;
            int fy = (tl.sprram_buf[i + 3] >> 1) & 0x1;
            /* MAME: x and y are screen coordinates. cliprect.max_x = 319, max_y = 239 */
            int sx = tl.sprram_buf[i + 4] | ((tl.sprram_buf[i + 5] & 1) << 8);
            int sy = tl.sprram_buf[i + 6];
            if (sx >= 319) sx -= 512;
            if (sy >= 239) sy -= 256;
            if (flip)
            {
                sx = 304 - sx;
                sy = 240 - sy;
                fx = !fx;
                fy = !fy;
            }
            /* Convert to bitmap coordinates (visible area starts at y=16) */
            sy -= TAITOL_VISIBLE_Y;
            taitol_draw_sprite(dst, pitch, code, col, fx, fy,
                              sx, sy, minx, maxx, miny, maxy, tl.priority_bitmap);
        }
    }

    /* TX (text) layer - MAME: scrolldx(-8, -8), scrolldy(0, 0), scroll(0, 0)
     * Tilemap is 64x32 tiles.  Screen position: col*8 + (-8), row*8 + 0.
     * Visible area: y=16..239 → bitmap_y = row*8 - 16. */
    {
        int row, col;
        for (row = 0; row < 32; row++)
        {
            int bitmap_y = row * 8 - TAITOL_VISIBLE_Y;
            if (bitmap_y + 8 <= miny || bitmap_y > maxy) continue;
            for (col = 0; col < 64; col++)
            {
                int taddr = 0xa000 + (row * 64 + col) * 2;
                uint8_t code_lo = tl.vram[taddr];
                uint8_t attr = tl.vram[taddr + 1];
                uint32_t code = (uint32_t)code_lo | (((uint32_t)(attr & 0x07)) << 8);
                int pal = (attr >> 4) & 0x0f;
                const uint8_t *src;
                int bitmap_x = col * 8 - 8;
                int x_, y_;
                if (bitmap_x + 8 <= minx || bitmap_x > maxx) continue;
                if (code >= TAITOL_VRAM_TILES) code %= TAITOL_VRAM_TILES;
                src = tl.vram_dec + code * 64;
                for (y_ = 0; y_ < 8; y_++)
                {
                    int py = bitmap_y + y_;
                    if (py < miny || py > maxy) continue;
                    for (x_ = 0; x_ < 8; x_++)
                    {
                        int px = bitmap_x + x_;
                        uint8_t pen;
                        if (px < minx || px > maxx) continue;
                        pen = src[y_ * 8 + x_];
                        if (pen == 0) continue;
                        dst[py * pitch + px] = tl.palette[(pal & 0x0f) * 16 + pen];
                    }
                }
            }
        }
    }

present:
    /* Apply the cabinet orientation in the hardware-independent renderer so
     * SDL 1.2, SDL3, libretro, headless and other frontends all see the same
     * correctly oriented active viewport.  MAME ROT270 maps a source pixel
     * (x, y) to (y, width - 1 - x). */
    if (tl.rotate)
    {
        int out_w = TAITOL_VISIBLE_HEIGHT;
        int out_h = TAITOL_VISIBLE_WIDTH;
        if (out_w > (int)bitmap.width) out_w = (int)bitmap.width;
        if (out_h > (int)bitmap.height) out_h = (int)bitmap.height;
        for (y = 0; y < out_h; y++)
        {
            taitol_pixel_t *out = (taitol_pixel_t *)(void *)(bitmap.data + (size_t)y * bitmap.pitch);
            for (x = 0; x < out_w; x++)
                out[x] = dst[x * pitch + (TAITOL_VISIBLE_WIDTH - 1 - y)];
        }
    }
    else
    {
        int out_w = TAITOL_VISIBLE_WIDTH;
        int out_h = TAITOL_VISIBLE_HEIGHT;
        if (out_w > (int)bitmap.width) out_w = (int)bitmap.width;
        if (out_h > (int)bitmap.height) out_h = (int)bitmap.height;
        for (y = 0; y < out_h; y++)
            memcpy(bitmap.data + (size_t)y * bitmap.pitch, dst + y * pitch,
                   (size_t)out_w * sizeof(*dst));
    }
}

/* Return whether the current game uses ROT270 orientation. */
int taitol_needs_rotation(void)
{
    return tl.rotate;
}

/* ---- Sound ------------------------------------------------------------ */

static void taitol_sound_ensure(void)
{
    uint32_t rate = snd.sample_rate ? (uint32_t)snd.sample_rate : 44100u;
    if (!tl.ym)
    {
        tl.ym = ym2203_create(TAITOL_YM2203_CLOCK, rate);
        return;
    }
    if (rate != tl.ym_capacity)
    {
        /* ym2203_create baked the rate in; recreate if it changed. */
        ym2203_destroy(tl.ym);
        tl.ym = ym2203_create(TAITOL_YM2203_CLOCK, rate);
    }
    if ((uint32_t)rate > tl.ym_capacity)
    {
        int16_t *l = (int16_t *)realloc(tl.ym_left,  rate * sizeof(int16_t));
        int16_t *r = (int16_t *)realloc(tl.ym_right, rate * sizeof(int16_t));
        if (l) tl.ym_left = l;
        if (r) tl.ym_right = r;
        tl.ym_capacity = rate;
    }
}

void taitol_sound_reset(void)
{
    taitol_sound_ensure();
    if (tl.ym) ym2203_reset(tl.ym);
}

void taitol_sound_update(int16_t **buffer, int32_t length)
{
    if (!buffer || !buffer[0] || !buffer[1] || length <= 0) return;
    taitol_sound_ensure();
    if (!tl.ym)
    {
        memset(buffer[0], 0, (size_t)length * sizeof(int16_t));
        memset(buffer[1], 0, (size_t)length * sizeof(int16_t));
        return;
    }
    if ((uint32_t)length > tl.ym_capacity)
    {
        int16_t *l = (int16_t *)realloc(tl.ym_left,  length * sizeof(int16_t));
        int16_t *r = (int16_t *)realloc(tl.ym_right, length * sizeof(int16_t));
        if (l) tl.ym_left = l;
        if (r) tl.ym_right = r;
        tl.ym_capacity = (uint32_t)length;
    }
    if (!tl.ym_left || !tl.ym_right)
    {
        memset(buffer[0], 0, (size_t)length * sizeof(int16_t));
        memset(buffer[1], 0, (size_t)length * sizeof(int16_t));
        return;
    }
    ym2203_update(tl.ym, tl.ym_left, tl.ym_right, length);
    {
        int32_t i;
        int32_t level = option.soundlevel ? option.soundlevel : 1;
        for (i = 0; i < length; i++)
        {
            buffer[0][i] = (int16_t)(((int32_t)tl.ym_left[i] * level) >> 1);
            buffer[1][i] = (int16_t)(((int32_t)tl.ym_right[i] * level) >> 1);
        }
    }
}

int taitol_audio_mixer_gain_num(int headroom_db)
{
    (void)headroom_db;
    return 384;
}

/* ---- Frame ----------------------------------------------------------- */

int32_t taitol_irq_callback(int32_t param)
{
    (void)param;
    tl.irq_active = 0;
    z80_set_irq_line(0, 0);
    return (int32_t)tl.irq_vector[tl.last_irq_level];
}

static void taitol_check_irq(int scanline)
{
    /* Kludge: interrupts only fire when the Z80 is in IM2 (matches MAME). */
    if (Z80.im != 2) return;
    if (scanline == 120 && (tl.irq_enable & 1))
    {
        tl.last_irq_level = 0;
        tl.irq_active = 1;
        z80_set_irq_line(0, 1);  /* ASSERT_LINE */
    }
    else if (scanline == 0 && (tl.irq_enable & 2))
    {
        tl.last_irq_level = 1;
        tl.irq_active = 1;
        z80_set_irq_line(0, 1);
    }
    else if (scanline == 240 && (tl.irq_enable & 4))
    {
        tl.last_irq_level = 2;
        tl.irq_active = 1;
        z80_set_irq_line(0, 1);
    }
}


void taitol_frame(uint32_t skip_render)
{
    int line;
    const int32_t cycles_per_line = TAITOL_CYCLES_PER_LINE;

    if (input.system & INPUT_RESET)
        taitol_reset();

    sms.paused = 0;
    vdp.height = TAITOL_VISIBLE_HEIGHT;
    vdp.lpf = TAITOL_LINES_PER_FRAME;
    taitol_update_inputs();

    for (line = 0; line < TAITOL_LINES_PER_FRAME; line++)
    {
        vdp.line = line;
        taitol_check_irq(line);
        z80_execute(cycles_per_line);
        if (line == 240)
        {
            /* VBlank: copy sprite RAM to the double buffer (screen_eof). */
            memcpy(tl.sprram_buf, &tl.vram[0xb000], 0x400);
            if (!skip_render) taitol_render();
        }
        MULTIREXZ80_sound_update(line);
    }
    z80_reset_cycle_count();
}

/* ---- State save / load ---------------------------------------------- */

uint32_t taitol_state_size(void)
{
    return (uint32_t)(sizeof(taitol_state) + TAITOL_VRAM_SIZE +
                      TAITOL_BITMAP_SIZE + TAITOL_PALETTE_SIZE +
                      TAITOL_WORKRAM_SIZE + TAITOL_VREGS_SIZE + 0x400);
}

int taitol_save_state(FILE *fd)
{
    uint8_t header[32];
    uint32_t ym_sz;
    if (!fd || !tl.active) return 0;
    memset(header, 0, sizeof(header));
    header[0] = 'T'; header[1] = 'L'; header[2] = 1;
    header[3] = tl.game_variant;
    header[4] = tl.map_variant;
    header[5] = tl.rom_bank;
    header[6] = tl.irq_enable;
    header[7] = tl.last_irq_level;
    header[8] = tl.irq_vector[0];
    header[9] = tl.irq_vector[1];
    header[10] = tl.irq_vector[2];
    header[11] = tl.ram_bank[0];
    header[12] = tl.ram_bank[1];
    header[13] = tl.ram_bank[2];
    header[14] = tl.ram_bank[3];
    header[15] = tl.ppi_ctrl;
    if (fwrite(header, 1, sizeof(header), fd) != sizeof(header)) return 0;
    if (fwrite(tl.vram, 1, TAITOL_VRAM_SIZE, fd) != TAITOL_VRAM_SIZE) return 0;
    if (fwrite(tl.bitmap_ram, 1, TAITOL_BITMAP_SIZE, fd) != TAITOL_BITMAP_SIZE) return 0;
    if (fwrite(tl.palette_ram, 1, TAITOL_PALETTE_SIZE, fd) != TAITOL_PALETTE_SIZE) return 0;
    if (fwrite(tl.work_ram, 1, TAITOL_WORKRAM_SIZE, fd) != TAITOL_WORKRAM_SIZE) return 0;
    if (fwrite(tl.vregs, 1, TAITOL_VREGS_SIZE, fd) != TAITOL_VREGS_SIZE) return 0;
    if (fwrite(tl.sprram_buf, 1, 0x400, fd) != 0x400) return 0;
    ym_sz = tl.ym ? ym2203_state_size(tl.ym) : 0;
    if (fwrite(&ym_sz, 1, sizeof(ym_sz), fd) != sizeof(ym_sz)) return 0;
    if (ym_sz)
    {
        uint8_t *buf = (uint8_t *)malloc(ym_sz);
        if (!buf) return 0;
        if (!ym2203_save_state(tl.ym, buf, ym_sz)) { free(buf); return 0; }
        if (fwrite(buf, 1, ym_sz, fd) != ym_sz) { free(buf); return 0; }
        free(buf);
    }
    return 1;
}

int taitol_load_state(FILE *fd, uint32_t size)
{
    uint8_t header[32];
    uint32_t ym_sz;
    if (!fd || size < sizeof(header)) return 0;
    if (fread(header, 1, sizeof(header), fd) != sizeof(header)) return 0;
    if (header[0] != 'T' || header[1] != 'L') return 0;
    tl.game_variant = header[3];
    tl.map_variant = header[4];
    tl.rom_bank = header[5];
    tl.irq_enable = header[6];
    tl.last_irq_level = header[7];
    tl.irq_vector[0] = header[8];
    tl.irq_vector[1] = header[9];
    tl.irq_vector[2] = header[10];
    tl.ram_bank[0] = header[11];
    tl.ram_bank[1] = header[12];
    tl.ram_bank[2] = header[13];
    tl.ram_bank[3] = header[14];
    tl.ppi_ctrl = header[15];
    if (fread(tl.vram, 1, TAITOL_VRAM_SIZE, fd) != TAITOL_VRAM_SIZE) return 0;
    if (fread(tl.bitmap_ram, 1, TAITOL_BITMAP_SIZE, fd) != TAITOL_BITMAP_SIZE) return 0;
    if (fread(tl.palette_ram, 1, TAITOL_PALETTE_SIZE, fd) != TAITOL_PALETTE_SIZE) return 0;
    if (fread(tl.work_ram, 1, TAITOL_WORKRAM_SIZE, fd) != TAITOL_WORKRAM_SIZE) return 0;
    if (fread(tl.vregs, 1, TAITOL_VREGS_SIZE, fd) != TAITOL_VREGS_SIZE) return 0;
    if (fread(tl.sprram_buf, 1, 0x400, fd) != 0x400) return 0;
    if (fread(&ym_sz, 1, sizeof(ym_sz), fd) != sizeof(ym_sz)) return 0;
    if (ym_sz)
    {
        uint8_t *buf = (uint8_t *)malloc(ym_sz);
        if (!buf) return 0;
        if (fread(buf, 1, ym_sz, fd) != ym_sz) { free(buf); return 0; }
        if (tl.ym) ym2203_load_state(tl.ym, buf, ym_sz);
        free(buf);
    }
    tl.gfx_decoded = 0;
    taitol_decode_gfx();
    taitol_update_palette();
    return 1;
}
