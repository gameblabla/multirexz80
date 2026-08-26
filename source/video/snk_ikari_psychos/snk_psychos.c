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
 * SNK Psycho Soldier hardware support for MultiRexZ80.
 *
 * This is an independent compact C implementation for the SNK triple-Z80 boards
 * used by Psycho Soldier, Athena and Ikari Warriors.  Hardware maps, ROM layout, input ports and video
 * ordering are based on MAME's BSD-3-Clause SNK driver
 * (src/mame/snk/snk.cpp and snk_v.cpp), credited there to Ernesto Corvi,
 * Tim Lindquist, Carlos A. Lozano, Bryan McPhail, Jarek Parchanski,
 * Nicola Salmoria, Tomasz Slanina, Phil Stroffolino, Acho A. Tang and
 * Victor Trucco, with thanks to Marco Cassili.
 */

#include "shared.h"

#define SNK_CPU_MAIN  0
#define SNK_CPU_SUB   1
#define SNK_CPU_AUDIO 2
#define SNK_CPU_COUNT 3

#define SNK_ROM_SIZE       0x10000u
#define SNK_AUDIO_ROM_SIZE 0x10000u
#define SNK_PROM_SIZE      0x1400u
#define SNK_TX_SIZE        0x8000u
#define SNK_BG_SIZE        0x50000u
#define SNK_SP16_SIZE      0x40000u
#define SNK_PSYCHOS_SP16_SIZE 0x20000u
#define SNK_CHOPPER_SP16_SIZE 0x20000u
#define SNK_SP32_SIZE      0x80000u
#define SNK_YM2_SIZE       0x40000u

#define SNK_TX_ELEMENTS     (SNK_TX_SIZE / 32u)
#define SNK_BG_ELEMENTS     (SNK_BG_SIZE / 128u)
#define SNK_SP16_ELEMENTS   ((SNK_SP16_SIZE >> 2) / 32u)
#define SNK_PSYCHOS_SP16_ELEMENTS ((SNK_PSYCHOS_SP16_SIZE >> 2) / 32u)
#define SNK_CHOPPER_SP16_ELEMENTS ((SNK_CHOPPER_SP16_SIZE >> 2) / 32u)
#define SNK_SP32_ELEMENTS   ((SNK_SP32_SIZE >> 2) / 128u)
#define SNK_SP16_3B_ELEMENTS ((SNK_SP16_SIZE / 3u) / 32u)
#define SNK_SP32_3B_ELEMENTS ((SNK_SP32_SIZE / 3u) / 128u)

#define SNK_TNK3_TX_SIZE        0x4000u
#define SNK_TNK3_BG_SIZE        0x8000u
#define SNK_TNK3_SP16_SIZE      0x18000u
#define SNK_TNK3_TX_ELEMENTS    (SNK_TNK3_TX_SIZE / 32u)
#define SNK_TNK3_BG_ELEMENTS    (SNK_TNK3_BG_SIZE / 32u)
#define SNK_TNK3_SP16_ELEMENTS  ((SNK_TNK3_SP16_SIZE / 3u) / 32u)

#define SNK_IKARI_SP16_SIZE      0x18000u
#define SNK_IKARI_SP32_SIZE      0x30000u
#define SNK_IKARI_SP16_ELEMENTS  ((SNK_IKARI_SP16_SIZE / 3u) / 32u)
#define SNK_IKARI_SP32_ELEMENTS  ((SNK_IKARI_SP32_SIZE / 3u) / 128u)

#define SNK_TX_DECODED_SIZE   (SNK_TX_ELEMENTS * 64u)
#define SNK_BG_DECODED_SIZE   (SNK_BG_ELEMENTS * 256u)
#define SNK_SP16_DECODED_SIZE (SNK_SP16_3B_ELEMENTS * 256u)
#define SNK_SP32_DECODED_SIZE (SNK_SP32_3B_ELEMENTS * 1024u)

#define SNK_SHARE_SIZE     0x0800u
#define SNK_BGVRAM_SIZE    0x2000u
#define SNK_SPRRAM_SIZE    0x1800u
#define SNK_TXVRAM_SIZE    0x0800u
#define SNK_AUDIORAM_SIZE  0x1000u

/* MAME gfxdecode base indices for gfx_gwar. */
#define SNK_PAL_TX_BASE    0x000u
#define SNK_PAL_SP16_BASE  0x100u
#define SNK_PAL_SP32_BASE  0x200u
#define SNK_PAL_BG_BASE    0x300u
#define SNK_TNK3_PAL_SP_BASE 0x000u
#define SNK_TNK3_PAL_BG_BASE 0x080u
#define SNK_TNK3_PAL_TX_BASE 0x180u

#define SNK_ROT_NONE       0
#define SNK_ROT_CW         1
#define SNK_ROT_CCW        2

#define SNK_SOUND_YM1_IRQ   0x01u
#define SNK_SOUND_YM2_IRQ   0x02u
#define SNK_SOUND_BUSY      0x04u
#define SNK_SOUND_CMD_IRQ   0x08u

#define SNK_OPL_CLOCK       4000000u
#define SNK_OPL_NATIVE_RATE (SNK_OPL_CLOCK / 72u)

typedef struct
{
    Z80_Regs regs;
    int32_t cycles;
    uint8_t *readmap[64];
    uint8_t *writemap[64];
} snk_z80_context_t;

typedef struct
{
    uint8_t *main_rom;
    uint8_t *sub_rom;
    uint8_t *audio_rom;
    uint8_t *proms;
    uint8_t *tx_rom;
    uint8_t *bg_rom;
    uint8_t *sp16_rom;
    uint8_t *sp32_rom;
    uint8_t *ym2_rom;

    uint8_t *sharedram;
    uint8_t *bg_vram;
    uint8_t *spriteram;
    uint8_t *tx_vram;
    uint8_t *audio_ram;
    uint32_t *palette;
    uint32_t *framebuf;
    uint32_t *framebuf_shadow;
    /* RGB output is not enough for SNK sprite shadows: MAME's 3bpp
     * shadow pen maps the destination palette index to index | 0x200. */
    uint16_t *frameidx;
    uint16_t *frameidx_shadow;
    uint8_t *tx_dec;
    uint8_t *bg_dec;
    uint8_t *sp16_dec;
    uint8_t *sp32_dec;
    uint8_t gfx_decoded;

    snk_z80_context_t cpu[SNK_CPU_COUNT];
    uint8_t current_cpu;
    uint8_t active;
    uint8_t athena_main_ramtest_done;

    uint8_t sound_latch;
    uint8_t sound_status;
    uint8_t sound_irq_pending;
    uint8_t ym1_addr;
    uint8_t ym2_addr;
    opl_chip_t *ym[2];
    uint32_t ym_sample_rate;
    uint32_t ym_output_rate;
    uint32_t ym_resample_step;
    uint32_t ym_resample_phase;
    uint32_t ym_resample_count;
    int16_t *ym_resample_tmp[2];
    uint32_t ym_resample_capacity;

    uint16_t bg_scrollx;
    uint16_t bg_scrolly;
    uint16_t sp16_scrollx;
    uint16_t sp16_scrolly;
    uint16_t sp32_scrollx;
    uint16_t sp32_scrolly;
    uint8_t sprite_split;
    uint8_t video_attr;
    uint16_t tx_tile_offset;
    uint16_t tx_palette_offset;
    uint16_t bg_palette_offset;
    uint16_t hf_posx;
    uint16_t hf_posy;

    uint8_t dsw1;
    uint8_t dsw2;
    uint8_t rotary_repeat[2];
    uint8_t rotary_last_pos[2];
    uint8_t rotary_gwar_cp_count[2];

    uint8_t game_type;
    uint8_t rotate;
    uint8_t sprite_bpp;
    uint16_t screen_w;
    uint16_t screen_h;
    uint16_t crop_x;
    uint16_t crop_y;
    uint16_t crop_w;
    uint16_t crop_h;
    uint8_t tx_cols;
    uint8_t tx_rows;
    uint8_t tx_scroll_y;
    int16_t bg_scrolldx;
    int16_t bg_scrolldy;
    uint16_t pal_tx_base;
    uint16_t pal_bg_base;
    uint16_t pal_sp16_base;
    uint16_t pal_sp32_base;
} snk_psychos_state_t;

static snk_psychos_state_t snk;

static void snk_set_irq_saved(int which, int state);
static int32_t snk_irq_callback(int32_t param);

static int snk_is_ikari_video_hw(void)
{
    return snk.game_type == SNK_GAME_VICTROAD || snk.game_type == SNK_GAME_IKARI;
}

static int snk_uses_two_ym3526(void)
{
    return snk.game_type == SNK_GAME_ATHENA || snk.game_type == SNK_GAME_IKARI;
}

int snk_psychos_audio_mixer_gain_num(int headroom_db)
{
    /* The imported OPL core produces Psycho Soldier at roughly 18 dB below a
     * useful SDL playback level even after applying the PCB's 2.0 routes.
     * Normalize that game here, before the optional headroom and limiter, so
     * it is audible without raising every other emulated system. */
    int base;
    switch (snk.game_type)
    {
        case SNK_GAME_TDFEVER:
            base = 512;
            break;
        case SNK_GAME_CHOPPER:
            /* Preserve the board-specific balance calibrated for Chopper. */
            base = 384;
            break;
        case SNK_GAME_PSYCHOS:
            base = 8192;
            break;
        default:
            base = 1024;
            break;
    }
    if (headroom_db <= 0) return base;
    if (headroom_db <= 3) return (base * 724 + 512) / 1024;
    if (headroom_db <= 6) return (base * 512 + 512) / 1024;
    return (base * 362 + 512) / 1024;
}

static uint8_t *snk_xcalloc(size_t n, size_t s)
{
    return (uint8_t *)calloc(n, s);
}

int snk_psychos_alloc(void)
{
    if (snk.main_rom) return 1;
    snk.main_rom = snk_xcalloc(SNK_ROM_SIZE, 1);
    snk.sub_rom = snk_xcalloc(SNK_ROM_SIZE, 1);
    snk.audio_rom = snk_xcalloc(SNK_AUDIO_ROM_SIZE, 1);
    snk.proms = snk_xcalloc(SNK_PROM_SIZE, 1);
    snk.tx_rom = snk_xcalloc(SNK_TX_SIZE, 1);
    snk.bg_rom = snk_xcalloc(SNK_BG_SIZE, 1);
    snk.sp16_rom = snk_xcalloc(SNK_SP16_SIZE, 1);
    snk.sp32_rom = snk_xcalloc(SNK_SP32_SIZE, 1);
    snk.ym2_rom = snk_xcalloc(SNK_YM2_SIZE, 1);
    snk.sharedram = snk_xcalloc(SNK_SHARE_SIZE, 1);
    snk.bg_vram = snk_xcalloc(SNK_BGVRAM_SIZE, 1);
    snk.spriteram = snk_xcalloc(SNK_SPRRAM_SIZE, 1);
    snk.tx_vram = snk_xcalloc(SNK_TXVRAM_SIZE, 1);
    snk.audio_ram = snk_xcalloc(SNK_AUDIORAM_SIZE, 1);
    snk.palette = (uint32_t *)calloc(0x400, sizeof(uint32_t));
    snk.framebuf_shadow = (uint32_t *)calloc((size_t)SNK_PSYCHOS_FRAME_WIDTH * SNK_PSYCHOS_FRAME_HEIGHT, sizeof(uint32_t));
    snk.frameidx_shadow = (uint16_t *)calloc((size_t)SNK_PSYCHOS_FRAME_WIDTH * SNK_PSYCHOS_FRAME_HEIGHT, sizeof(uint16_t));
    snk.framebuf = snk.framebuf_shadow;
    snk.frameidx = snk.frameidx_shadow;
    snk.tx_dec = snk_xcalloc(SNK_TX_DECODED_SIZE, 1);
    snk.bg_dec = snk_xcalloc(SNK_BG_DECODED_SIZE, 1);
    snk.sp16_dec = snk_xcalloc(SNK_SP16_DECODED_SIZE, 1);
    snk.sp32_dec = snk_xcalloc(SNK_SP32_DECODED_SIZE, 1);

    if (!snk.main_rom || !snk.sub_rom || !snk.audio_rom || !snk.proms ||
        !snk.tx_rom || !snk.bg_rom || !snk.sp16_rom || !snk.sp32_rom ||
        !snk.ym2_rom || !snk.sharedram || !snk.bg_vram || !snk.spriteram ||
        !snk.tx_vram || !snk.audio_ram || !snk.palette || !snk.framebuf_shadow ||
        !snk.frameidx_shadow || !snk.tx_dec || !snk.bg_dec || !snk.sp16_dec || !snk.sp32_dec)
    {
        snk_psychos_free();
        return 0;
    }
    snk_psychos_clear_roms();
    return 1;
}

static void snk_psychos_sound_destroy_chips(void);

void snk_psychos_free(void)
{
    snk_psychos_sound_destroy_chips();
    free(snk.main_rom); free(snk.sub_rom); free(snk.audio_rom); free(snk.proms);
    free(snk.tx_rom); free(snk.bg_rom); free(snk.sp16_rom); free(snk.sp32_rom); free(snk.ym2_rom);
    free(snk.sharedram); free(snk.bg_vram); free(snk.spriteram); free(snk.tx_vram); free(snk.audio_ram);
    free(snk.palette);
    free(snk.framebuf_shadow);
    free(snk.frameidx_shadow);
    snk.framebuf = NULL;
    snk.framebuf_shadow = NULL;
    snk.frameidx = NULL;
    snk.frameidx_shadow = NULL;
    free(snk.tx_dec);
    free(snk.bg_dec);
    free(snk.sp16_dec);
    free(snk.sp32_dec);
    memset(&snk, 0, sizeof(snk));
    z80_select_default_context();
}

void snk_psychos_clear_roms(void)
{
    if (!snk.main_rom && !snk_psychos_alloc()) return;
    memset(snk.main_rom, 0xff, SNK_ROM_SIZE);
    memset(snk.sub_rom, 0xff, SNK_ROM_SIZE);
    memset(snk.audio_rom, 0xff, SNK_AUDIO_ROM_SIZE);
    memset(snk.proms, 0xff, SNK_PROM_SIZE);
    memset(snk.tx_rom, 0xff, SNK_TX_SIZE);
    memset(snk.bg_rom, 0xff, SNK_BG_SIZE);
    memset(snk.sp16_rom, 0xff, SNK_SP16_SIZE);
    memset(snk.sp32_rom, 0xff, SNK_SP32_SIZE);
    memset(snk.ym2_rom, 0xff, SNK_YM2_SIZE);
}

int snk_psychos_set_region(int region, uint32_t offset, const uint8_t *data, uint32_t size)
{
    uint8_t *dst = NULL;
    uint32_t limit = 0;
    if (!data || !snk_psychos_alloc()) return 0;
    switch (region)
    {
        case SNK_REGION_MAIN:  dst = snk.main_rom;  limit = SNK_ROM_SIZE; break;
        case SNK_REGION_SUB:   dst = snk.sub_rom;   limit = SNK_ROM_SIZE; break;
        case SNK_REGION_AUDIO: dst = snk.audio_rom; limit = SNK_AUDIO_ROM_SIZE; break;
        case SNK_REGION_PROM:  dst = snk.proms;     limit = SNK_PROM_SIZE; break;
        case SNK_REGION_TX:    dst = snk.tx_rom;    limit = SNK_TX_SIZE; break;
        case SNK_REGION_BG:    dst = snk.bg_rom;    limit = SNK_BG_SIZE; break;
        case SNK_REGION_SP16:  dst = snk.sp16_rom;  limit = SNK_SP16_SIZE; break;
        case SNK_REGION_SP32:  dst = snk.sp32_rom;  limit = SNK_SP32_SIZE; break;
        case SNK_REGION_YM2:   dst = snk.ym2_rom;   limit = SNK_YM2_SIZE; break;
        default: return 0;
    }
    if (offset > limit || size > limit - offset) return 0;
    memcpy(dst + offset, data, size);
    if (region == SNK_REGION_TX || region == SNK_REGION_BG ||
        region == SNK_REGION_SP16 || region == SNK_REGION_SP32)
        snk.gfx_decoded = 0;
    return 1;
}

void snk_psychos_set_game_variant(int variant)
{
    if (!snk_psychos_alloc()) return;
    snk.active = 1;
    snk.game_type = (uint8_t)variant;
    snk.rotate = SNK_ROT_NONE;
    snk.sprite_bpp = 4;
    snk.screen_w = 400;
    snk.screen_h = 224;
    snk.crop_x = 0;
    snk.crop_y = 0;
    snk.crop_w = 400;
    snk.crop_h = 224;
    snk.tx_cols = 50;
    snk.tx_rows = 32;
    snk.tx_scroll_y = 0;
    snk.bg_scrolldx = 16;
    snk.bg_scrolldy = 0;
    snk.pal_tx_base = SNK_PAL_TX_BASE;
    snk.pal_bg_base = SNK_PAL_BG_BASE;
    snk.pal_sp16_base = SNK_PAL_SP16_BASE;
    snk.pal_sp32_base = SNK_PAL_SP32_BASE;
    snk.dsw1 = 0x3b;
    snk.dsw2 = 0xbf;
    input.rotary_pos[0] = 0;
    input.rotary_pos[1] = 0;
    input.rotary_aim[0] = 0;
    input.rotary_aim[1] = 0;
    snk.rotary_repeat[0] = 0;
    snk.rotary_repeat[1] = 0;
    snk.rotary_last_pos[0] = 0;
    snk.rotary_last_pos[1] = 0;
    snk.rotary_gwar_cp_count[0] = 0;
    snk.rotary_gwar_cp_count[1] = 0;

    switch (variant)
    {
        case SNK_GAME_VICTROAD:
        case SNK_GAME_IKARI:
            snk.rotate = SNK_ROT_CCW;
            snk.sprite_bpp = 3;
            snk.screen_w = 288;
            snk.screen_h = 224;
            snk.crop_x = 0;
            snk.crop_y = 8;
            snk.crop_w = 288;
            snk.crop_h = 216;
            snk.tx_cols = 36;
            snk.tx_rows = 28;
            snk.tx_scroll_y = 8;
            snk.bg_scrolldx = 15;
            snk.bg_scrolldy = 8;
            snk.pal_tx_base = 0x180;
            snk.pal_bg_base = 0x100;
            snk.pal_sp16_base = 0x000;
            snk.pal_sp32_base = 0x080;
            if (variant == SNK_GAME_IKARI)
            {
                /* Ikari Warriors JAMMA defaults from MAME input ports.
                 * Bonus-life fake-port bits are folded into DSW1 bit 2 and
                 * DSW2 bits 4-5, matching MAME's default 50k/100k/100k+. */
                snk.dsw1 = 0x3f;
                snk.dsw2 = 0x7b;
            }
            else
            {
                snk.dsw1 = 0xbf;
                snk.dsw2 = 0xff;
            }
            break;
        case SNK_GAME_GWAR:
        case SNK_GAME_CHOPPER:
            snk.rotate = SNK_ROT_CCW;
            snk.crop_x = 0;
            snk.crop_w = 400;
            snk.dsw1 = 0xff;
            snk.dsw2 = 0xff;
            break;
        case SNK_GAME_TDFEVER:
            snk.rotate = SNK_ROT_CW;
            snk.crop_x = 8;
            snk.crop_w = 384;
            snk.pal_bg_base = 0x000;
            snk.pal_sp16_base = 0x100;
            snk.pal_sp32_base = 0x100;
            snk.dsw1 = 0xff;
            snk.dsw2 = 0xff;
            break;
        case SNK_GAME_ATHENA:
            /* Athena uses the earlier TNK III video path: horizontal 36x28
             * display, 8x8 background tiles, 3bpp 16x16 sprites and the
             * non-linear TNK3 PROM palette. */
            snk.rotate = SNK_ROT_NONE;
            snk.sprite_bpp = 3;
            snk.screen_w = 288;
            snk.screen_h = 224;
            snk.crop_x = 0;
            snk.crop_y = 8;
            snk.crop_w = 288;
            snk.crop_h = 216;
            snk.tx_cols = 36;
            snk.tx_rows = 28;
            snk.tx_scroll_y = 8;
            snk.bg_scrolldx = 15;
            snk.bg_scrolldy = 8;
            snk.pal_tx_base = SNK_TNK3_PAL_TX_BASE;
            snk.pal_bg_base = SNK_TNK3_PAL_BG_BASE;
            snk.pal_sp16_base = SNK_TNK3_PAL_SP_BASE;
            snk.pal_sp32_base = SNK_TNK3_PAL_SP_BASE;
            snk.dsw1 = 0x3d;
            snk.dsw2 = 0xfb;
            break;
        case SNK_GAME_PSYCHOS:
        default:
            /* MAME default DIP values for psychos, including the fake bonus-life port
             * folded into the custom DIP bits. */
            snk.dsw1 = 0x3b;
            snk.dsw2 = 0xbf;
            break;
    }
}

void snk_psychos_set_game(void)
{
    snk_psychos_set_game_variant(SNK_GAME_PSYCHOS);
}

static uint8_t snk_rgb444_prom(uint8_t v)
{
    /* MAME's palette_device::RGB_444_PROMS path uses the standard SNK
     * bipolar PROM resistor weights, not linear nibble expansion.  Linear
     * v*17 gave recognisable sprite shapes but visibly wrong sprite and
     * text colours compared with Psycho Soldier hardware/MAME. */
    return (uint8_t)(0x0e * ((v >> 0) & 1) +
                     0x1f * ((v >> 1) & 1) +
                     0x43 * ((v >> 2) & 1) +
                     0x8f * ((v >> 3) & 1));
}

static void snk_build_palette(void)
{
    uint32_t i;
    for (i = 0; i < 0x400; i++)
    {
        uint8_t r, g, b;
        if (snk.game_type == SNK_GAME_ATHENA)
        {
            /* TNK3/Athena PROM bit wiring from MAME snk_state::tnk3_palette. */
            r = (uint8_t)(0x0e * ((snk.proms[i + 0x800] >> 3) & 1) +
                          0x1f * ((snk.proms[i + 0x000] >> 1) & 1) +
                          0x43 * ((snk.proms[i + 0x000] >> 2) & 1) +
                          0x8f * ((snk.proms[i + 0x000] >> 3) & 1));
            g = (uint8_t)(0x0e * ((snk.proms[i + 0x800] >> 2) & 1) +
                          0x1f * ((snk.proms[i + 0x400] >> 2) & 1) +
                          0x43 * ((snk.proms[i + 0x400] >> 3) & 1) +
                          0x8f * ((snk.proms[i + 0x000] >> 0) & 1));
            b = (uint8_t)(0x0e * ((snk.proms[i + 0x800] >> 0) & 1) +
                          0x1f * ((snk.proms[i + 0x800] >> 1) & 1) +
                          0x43 * ((snk.proms[i + 0x400] >> 0) & 1) +
                          0x8f * ((snk.proms[i + 0x400] >> 1) & 1));
        }
        else
        {
            r = snk_rgb444_prom(snk.proms[i + 0x000]);
            g = snk_rgb444_prom(snk.proms[i + 0x400]);
            b = snk_rgb444_prom(snk.proms[i + 0x800]);
        }
        snk.palette[i] = MAKE_PIXEL(r, g, b);
    }
}

static void snk_sound_update_irq(void)
{
    int state = (snk.sound_status & (SNK_SOUND_YM1_IRQ | SNK_SOUND_YM2_IRQ | SNK_SOUND_CMD_IRQ)) ? ASSERT_LINE : CLEAR_LINE;
    snk_set_irq_saved(SNK_CPU_AUDIO, state);
}

static void snk_psychos_sound_destroy_chips(void)
{
    OPL_Destroy(snk.ym[0]);
    OPL_Destroy(snk.ym[1]);
    free(snk.ym_resample_tmp[0]);
    free(snk.ym_resample_tmp[1]);
    snk.ym[0] = NULL;
    snk.ym[1] = NULL;
    snk.ym_resample_tmp[0] = NULL;
    snk.ym_resample_tmp[1] = NULL;
    snk.ym_resample_capacity = 0;
    snk.ym_sample_rate = 0;
    snk.ym_output_rate = 0;
    snk.ym_resample_step = 0;
    snk.ym_resample_phase = 0;
    snk.ym_resample_count = 0;
}

static void snk_ym_irq_callback(void *opaque, int state)
{
    uint8_t mask = ((uintptr_t)opaque == 0) ? SNK_SOUND_YM1_IRQ : SNK_SOUND_YM2_IRQ;
    if (state)
    {
        snk.sound_status |= mask;
        snk_sound_update_irq();
    }
}

static void snk_psychos_sound_ensure_chips(void)
{
    uint32_t out_rate = snd.sample_rate ? (uint32_t)snd.sample_rate : 44100u;
    uint32_t chip_rate = SNK_OPL_NATIVE_RATE;
    if (snk.ym[0] && snk.ym[1] && snk.ym_sample_rate == chip_rate && snk.ym_output_rate == out_rate)
        return;

    snk_psychos_sound_destroy_chips();
    snk.ym_sample_rate = chip_rate;
    snk.ym_output_rate = out_rate;
    snk.ym_resample_step = (uint32_t)(((uint64_t)chip_rate << 16) / out_rate);
    snk.ym[0] = OPL_Create(OPL_CHIP_YM3526, SNK_OPL_CLOCK, chip_rate);
    snk.ym[1] = OPL_Create(snk_uses_two_ym3526() ? OPL_CHIP_YM3526 : OPL_CHIP_Y8950, SNK_OPL_CLOCK, chip_rate);
    if (snk.ym[0])
        OPL_SetIRQHandler(snk.ym[0], snk_ym_irq_callback, (void *)(uintptr_t)0);
    if (snk.ym[1])
    {
        OPL_SetIRQHandler(snk.ym[1], snk_ym_irq_callback, (void *)(uintptr_t)1);
        if (!snk_uses_two_ym3526())
            OPL_SetADPCMROM(snk.ym[1], snk.ym2_rom, SNK_YM2_SIZE);
    }
}

static void snk_sound_command_w(uint8_t data)
{
    snk.sound_latch = data;
    snk.sound_status |= (SNK_SOUND_BUSY | SNK_SOUND_CMD_IRQ);
    snk_sound_update_irq();
}

static void snk_sound_status_w(uint8_t data)
{
    if (~data & 0x10) snk.sound_status &= (uint8_t)~SNK_SOUND_YM1_IRQ;
    if (~data & 0x20) snk.sound_status &= (uint8_t)~SNK_SOUND_YM2_IRQ;
    if (~data & 0x40) snk.sound_status &= (uint8_t)~SNK_SOUND_BUSY;
    if (~data & 0x80) snk.sound_status &= (uint8_t)~SNK_SOUND_CMD_IRQ;
    snk_sound_update_irq();
}

static uint8_t snk_ym_status(int chip)
{
    snk_psychos_sound_ensure_chips();
    if (chip < 0 || chip > 1 || !snk.ym[chip]) return 0xff;
    return OPL_ReadStatus(snk.ym[chip]);
}

static void snk_ym_write(int chip, uint8_t offset, uint8_t data)
{
    uint8_t *addr;
    uint8_t reg;

    if (chip < 0 || chip > 1) return;
    snk_psychos_sound_ensure_chips();
    addr = (chip == 0) ? &snk.ym1_addr : &snk.ym2_addr;

    if (!(offset & 1))
    {
        *addr = data;
        MULTIREXZ80_TRACE_YM_WRITE((uint16_t)(chip ? 0xf000 : 0xe800), data);
    }
    else
    {
        reg = *addr;
        MULTIREXZ80_TRACE_YM_WRITE((uint16_t)((chip ? 0xf400 : 0xec00) | reg), data);
    }

    if (snk.ym[chip])
        OPL_Write(snk.ym[chip], offset & 1, data);
}

void snk_psychos_sound_reset(void)
{
    int i;
    snk_psychos_sound_ensure_chips();
    for (i = 0; i < 2; i++)
        if (snk.ym[i]) OPL_Reset(snk.ym[i]);
    if (snk.ym[1] && !snk_uses_two_ym3526())
        OPL_SetADPCMROM(snk.ym[1], snk.ym2_rom, SNK_YM2_SIZE);
    snk.ym1_addr = snk.ym2_addr = 0;
    snk.ym_resample_phase = 0;
    snk.ym_resample_count = 0;
    snk.sound_status &= (uint8_t)~(SNK_SOUND_YM1_IRQ | SNK_SOUND_YM2_IRQ);
    snk_sound_update_irq();
}

static int snk_psychos_sound_ensure_resample_tmp(uint32_t samples)
{
    int i;
    if (samples <= snk.ym_resample_capacity)
        return 1;

    for (i = 0; i < 2; i++)
    {
        int16_t *p = (int16_t *)realloc(snk.ym_resample_tmp[i], (size_t)samples * sizeof(int16_t));
        if (!p)
            return 0;
        snk.ym_resample_tmp[i] = p;
    }
    snk.ym_resample_capacity = samples;
    return 1;
}

void snk_psychos_sound_update(int16_t **buffer, int32_t length)
{
    uint32_t needed_index;
    uint32_t needed_count;
    uint32_t generate_count;
    uint32_t final_phase;
    uint32_t drop;
    int32_t i;
    if (!buffer || !buffer[0] || !buffer[1] || length <= 0) return;
    snk_psychos_sound_ensure_chips();

    if (!snk.ym[0] || !snk.ym[1])
    {
        memset(buffer[0], 0, (size_t)length * sizeof(int16_t));
        memset(buffer[1], 0, (size_t)length * sizeof(int16_t));
        return;
    }

    needed_index = (snk.ym_resample_phase + (uint32_t)((uint64_t)(length - 1) * snk.ym_resample_step)) >> 16;
    needed_count = needed_index + 2u;

    if (!snk_psychos_sound_ensure_resample_tmp(needed_count))
    {
        memset(buffer[0], 0, (size_t)length * sizeof(int16_t));
        memset(buffer[1], 0, (size_t)length * sizeof(int16_t));
        return;
    }

    if (snk.ym_resample_count < needed_count)
    {
        generate_count = needed_count - snk.ym_resample_count;
        OPL_UpdateMono(snk.ym[0], snk.ym_resample_tmp[0] + snk.ym_resample_count, (int32_t)generate_count);
        OPL_UpdateMono(snk.ym[1], snk.ym_resample_tmp[1] + snk.ym_resample_count, (int32_t)generate_count);
        snk.ym_resample_count = needed_count;
    }

    for (i = 0; i < length; i++)
    {
        uint32_t pos = snk.ym_resample_phase + (uint32_t)((uint64_t)i * snk.ym_resample_step);
        uint32_t idx = pos >> 16;
        uint32_t frac = pos & 0xffffu;
        int32_t a0 = snk.ym_resample_tmp[0][idx];
        int32_t b0 = snk.ym_resample_tmp[0][idx + 1u];
        int32_t a1 = snk.ym_resample_tmp[1][idx];
        int32_t b1 = snk.ym_resample_tmp[1][idx + 1u];
        buffer[0][i] = (int16_t)(a0 + (int32_t)(((int64_t)(b0 - a0) * frac) >> 16));
        buffer[1][i] = (int16_t)(a1 + (int32_t)(((int64_t)(b1 - a1) * frac) >> 16));
    }

    final_phase = snk.ym_resample_phase + (uint32_t)((uint64_t)length * snk.ym_resample_step);
    drop = final_phase >> 16;
    if (drop > 0)
    {
        if (drop >= snk.ym_resample_count)
        {
            snk.ym_resample_count = 0;
        }
        else
        {
            snk.ym_resample_count -= drop;
            memmove(snk.ym_resample_tmp[0], snk.ym_resample_tmp[0] + drop, (size_t)snk.ym_resample_count * sizeof(int16_t));
            memmove(snk.ym_resample_tmp[1], snk.ym_resample_tmp[1] + drop, (size_t)snk.ym_resample_count * sizeof(int16_t));
        }
    }
    snk.ym_resample_phase = final_phase & 0xffffu;
}

static void snk_map_cpu(int which)
{
    uint8_t *rom = (which == SNK_CPU_SUB) ? snk.sub_rom : (which == SNK_CPU_AUDIO ? snk.audio_rom : snk.main_rom);
    uint_fast8_t i;
    snk_z80_context_t *c = &snk.cpu[which];
    for (i = 0; i < 64; i++)
    {
        c->readmap[i] = dummy_read;
        c->writemap[i] = dummy_write;
    }
    for (i = 0; i < 48; i++)
        c->readmap[i] = rom + ((uint32_t)i << 10);

    if (which == SNK_CPU_AUDIO)
    {
        for (i = 0x30; i < 0x34; i++)
        {
            c->readmap[i] = snk.audio_ram + ((i & 3) << 10);
            c->writemap[i] = snk.audio_ram + ((i & 3) << 10);
        }
    }
    else if (snk.game_type == SNK_GAME_ATHENA)
    {
        if (which == SNK_CPU_MAIN)
        {
            for (i = 0x34; i < 0x36; i++)
                c->readmap[i] = c->writemap[i] = snk.spriteram + ((i - 0x34) << 10);
            for (i = 0x36; i < 0x3e; i++)
                c->readmap[i] = c->writemap[i] = snk.bg_vram + ((i - 0x36) << 10);
        }
        else
        {
            for (i = 0x32; i < 0x34; i++)
                c->readmap[i] = c->writemap[i] = snk.spriteram + ((i - 0x32) << 10);
            for (i = 0x34; i < 0x3c; i++)
                c->readmap[i] = c->writemap[i] = snk.bg_vram + ((i - 0x34) << 10);
            for (i = 0x3c; i < 0x3e; i++)
                c->readmap[i] = c->writemap[i] = snk.sharedram + ((i - 0x3c) << 10);
        }
        for (i = 0x3e; i < 0x40; i++)
            c->readmap[i] = c->writemap[i] = snk.tx_vram + ((i & 1) << 10);
    }
    else if (snk_is_ikari_video_hw())
    {
        /* Ikari/Victory Road mirror background RAM at D000-DFFF. */
        for (i = 0x34; i < 0x38; i++)
            c->readmap[i] = c->writemap[i] = snk.bg_vram + ((i & 1) << 10);
        for (i = 0x38; i < 0x3e; i++)
            c->readmap[i] = c->writemap[i] = snk.spriteram + ((i - 0x38) << 10);
        for (i = 0x3e; i < 0x40; i++)
            c->readmap[i] = c->writemap[i] = snk.tx_vram + ((i & 1) << 10);
    }
    else
    {
        for (i = 0x34; i < 0x36; i++)
            c->readmap[i] = c->writemap[i] = snk.bg_vram + ((i & 1) << 10);
        for (i = 0x36; i < 0x38; i++)
            c->readmap[i] = c->writemap[i] = snk.sharedram + ((i & 1) << 10);
        for (i = 0x38; i < 0x3e; i++)
            c->readmap[i] = c->writemap[i] = snk.spriteram + ((i - 0x38) << 10);
        for (i = 0x3e; i < 0x40; i++)
            c->readmap[i] = c->writemap[i] = snk.tx_vram + ((i & 1) << 10);
    }
}

static void snk_load_cpu(int which)
{
    snk.current_cpu = (uint8_t)which;
    z80_select_context(&snk.cpu[which].regs, &snk.cpu[which].cycles);
    z80_select_memory_maps(snk.cpu[which].readmap, snk.cpu[which].writemap);
    snk.cpu[which].regs.irq_callback = snk_irq_callback;
    z80_data_operand_fetch = 0;
}

static void snk_save_cpu(int which)
{
    (void)which;
}

static void snk_pulse_nmi_saved(int which)
{
    if (which == snk.current_cpu)
    {
        z80_set_irq_line(INPUT_LINE_NMI, ASSERT_LINE);
        z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);
    }
    else
    {
        snk.cpu[which].regs.nmi_pending = 1;
        snk.cpu[which].regs.nmi_state = CLEAR_LINE;
    }
}

static void snk_set_irq_saved(int which, int state)
{
    if (which == snk.current_cpu)
        z80_set_irq_line(INPUT_LINE_IRQ0, state);
    else
        snk.cpu[which].regs.irq_state = state;
}

static int32_t snk_irq_callback(int32_t param)
{
    (void)param;
    z80_set_irq_line(INPUT_LINE_IRQ0, CLEAR_LINE);
    return 0xff;
}


#define SNK_PSYCHOS_STATE_MAGIC   0x53504b53u /* "SKPS" as a native-endian guard. */
#define SNK_PSYCHOS_STATE_VERSION 2u
#define SNK_PSYCHOS_STATE_MIN_VERSION 1u

typedef struct
{
    uint32_t pc, sp, af, bc, de, hl, ix, iy, wz;
    uint32_t af2, bc2, de2, hl2;
    uint32_t prvpc;
    uint8_t r, r2, iff1, iff2, halt, im, i;
    uint8_t after_ei, after_ldair;
    uint16_t ea;
    int32_t nmi_state;
    int32_t nmi_pending;
    int32_t irq_state;
    int32_t icount;
    int32_t wait_state;
    int32_t busrq_state;
    int32_t cycles;
} snk_z80_state_file_t;


#define SNK_PSYCHOS_SOUND_STATE_MAGIC   0x32444e53u /* "SND2" native-endian */
#define SNK_PSYCHOS_SOUND_STATE_VERSION 1u

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t ym_size[2];
} snk_psychos_sound_state_file_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint8_t current_cpu;
    uint8_t active;
    uint8_t athena_main_ramtest_done;
    uint8_t sound_latch;
    uint8_t sound_status;
    uint8_t sound_irq_pending;
    uint8_t ym1_addr;
    uint8_t ym2_addr;
    uint16_t bg_scrollx;
    uint16_t bg_scrolly;
    uint16_t sp16_scrollx;
    uint16_t sp16_scrolly;
    uint16_t sp32_scrollx;
    uint16_t sp32_scrolly;
    uint8_t sprite_split;
    uint8_t video_attr;
    uint16_t tx_tile_offset;
    uint16_t tx_palette_offset;
    uint16_t bg_palette_offset;
    uint16_t hf_posx;
    uint16_t hf_posy;
    uint8_t dsw1;
    uint8_t dsw2;
    uint8_t game_type;
    uint8_t rotate;
    uint8_t sprite_bpp;
    uint16_t screen_w;
    uint16_t screen_h;
    uint16_t crop_x;
    uint16_t crop_y;
    uint16_t crop_w;
    uint16_t crop_h;
    uint8_t tx_cols;
    uint8_t tx_rows;
    uint8_t tx_scroll_y;
    int16_t bg_scrolldx;
    int16_t bg_scrolldy;
    uint16_t pal_tx_base;
    uint16_t pal_bg_base;
    uint16_t pal_sp16_base;
    uint16_t pal_sp32_base;
} snk_psychos_state_file_t;

static void snk_pack_z80_state(snk_z80_state_file_t *dst, const snk_z80_context_t *src)
{
    const Z80_Regs *r = &src->regs;
    memset(dst, 0, sizeof(*dst));
    dst->pc = r->pc.d; dst->sp = r->sp.d; dst->af = r->af.d; dst->bc = r->bc.d;
    dst->de = r->de.d; dst->hl = r->hl.d; dst->ix = r->ix.d; dst->iy = r->iy.d;
    dst->wz = r->wz.d; dst->af2 = r->af2.d; dst->bc2 = r->bc2.d; dst->de2 = r->de2.d;
    dst->hl2 = r->hl2.d; dst->prvpc = r->prvpc.d;
    dst->r = r->r; dst->r2 = r->r2; dst->iff1 = r->iff1; dst->iff2 = r->iff2;
    dst->halt = r->halt; dst->im = r->im; dst->i = r->i;
    dst->after_ei = r->after_ei; dst->after_ldair = r->after_ldair;
    dst->ea = r->ea;
    dst->nmi_state = r->nmi_state; dst->nmi_pending = r->nmi_pending; dst->irq_state = r->irq_state;
    dst->icount = r->icount; dst->wait_state = r->wait_state; dst->busrq_state = r->busrq_state;
    dst->cycles = src->cycles;
}

static void snk_unpack_z80_state(snk_z80_context_t *dst, const snk_z80_state_file_t *src)
{
    Z80_Regs *r = &dst->regs;
    r->pc.d = src->pc; r->sp.d = src->sp; r->af.d = src->af; r->bc.d = src->bc;
    r->de.d = src->de; r->hl.d = src->hl; r->ix.d = src->ix; r->iy.d = src->iy;
    r->wz.d = src->wz; r->af2.d = src->af2; r->bc2.d = src->bc2; r->de2.d = src->de2;
    r->hl2.d = src->hl2; r->prvpc.d = src->prvpc;
    r->r = src->r; r->r2 = src->r2; r->iff1 = src->iff1; r->iff2 = src->iff2;
    r->halt = src->halt; r->im = src->im; r->i = src->i;
    r->after_ei = src->after_ei; r->after_ldair = src->after_ldair;
    r->ea = src->ea;
    r->nmi_state = src->nmi_state; r->nmi_pending = src->nmi_pending; r->irq_state = src->irq_state;
    r->icount = src->icount; r->wait_state = src->wait_state; r->busrq_state = src->busrq_state;
    r->irq_callback = snk_irq_callback;
    dst->cycles = src->cycles;
}

static uint32_t snk_psychos_ram_state_size(void)
{
    return SNK_SHARE_SIZE + SNK_BGVRAM_SIZE + SNK_SPRRAM_SIZE + SNK_TXVRAM_SIZE + SNK_AUDIORAM_SIZE;
}

static uint32_t snk_psychos_base_state_size(void)
{
    return (uint32_t)(sizeof(snk_psychos_state_file_t) +
                      sizeof(snk_z80_state_file_t) * SNK_CPU_COUNT +
                      snk_psychos_ram_state_size());
}

static uint32_t snk_psychos_sound_state_size(uint32_t *ym0_size, uint32_t *ym1_size)
{
    uint32_t y0, y1;
    snk_psychos_sound_ensure_chips();
    y0 = OPL_GetStateSize(snk.ym[0]);
    y1 = OPL_GetStateSize(snk.ym[1]);
    if (ym0_size) *ym0_size = y0;
    if (ym1_size) *ym1_size = y1;
    return (uint32_t)(sizeof(snk_psychos_sound_state_file_t) + y0 + y1);
}

uint32_t snk_psychos_state_size(void)
{
    uint32_t y0, y1;
    return snk_psychos_base_state_size() + snk_psychos_sound_state_size(&y0, &y1);
}

static int snk_write_block(FILE *fd, const void *data, uint32_t size)
{
    return size == 0 || fwrite(data, 1, size, fd) == size;
}

static int snk_read_block(FILE *fd, void *data, uint32_t size)
{
    return size == 0 || fread(data, 1, size, fd) == size;
}

static int snk_write_opl_state(FILE *fd, opl_chip_t *chip, uint32_t size)
{
    uint8_t *buf;
    int ok;
    if (!size) return 1;
    buf = (uint8_t *)malloc(size);
    if (!buf) return 0;
    ok = OPL_SaveState(chip, buf, size) && snk_write_block(fd, buf, size);
    free(buf);
    return ok;
}

static int snk_read_opl_state(FILE *fd, opl_chip_t *chip, uint32_t size)
{
    uint8_t *buf;
    int ok;
    if (!size) return 1;
    buf = (uint8_t *)malloc(size);
    if (!buf) return 0;
    ok = snk_read_block(fd, buf, size) && OPL_LoadState(chip, buf, size);
    free(buf);
    return ok;
}

int snk_psychos_save_state(FILE *fd)
{
    snk_psychos_state_file_t st;
    snk_psychos_sound_state_file_t sndst;
    snk_z80_state_file_t cpust[SNK_CPU_COUNT];
    uint32_t ym0_size = 0, ym1_size = 0;
    int i;

    if (!fd || !snk.active)
        return 0;

    memset(&st, 0, sizeof(st));
    st.magic = SNK_PSYCHOS_STATE_MAGIC;
    st.version = SNK_PSYCHOS_STATE_VERSION;
    st.size = snk_psychos_base_state_size() + snk_psychos_sound_state_size(&ym0_size, &ym1_size);
    st.current_cpu = snk.current_cpu;
    st.active = snk.active;
    st.athena_main_ramtest_done = snk.athena_main_ramtest_done;
    st.sound_latch = snk.sound_latch;
    st.sound_status = snk.sound_status;
    st.sound_irq_pending = snk.sound_irq_pending;
    st.ym1_addr = snk.ym1_addr;
    st.ym2_addr = snk.ym2_addr;
    st.bg_scrollx = snk.bg_scrollx;
    st.bg_scrolly = snk.bg_scrolly;
    st.sp16_scrollx = snk.sp16_scrollx;
    st.sp16_scrolly = snk.sp16_scrolly;
    st.sp32_scrollx = snk.sp32_scrollx;
    st.sp32_scrolly = snk.sp32_scrolly;
    st.sprite_split = snk.sprite_split;
    st.video_attr = snk.video_attr;
    st.tx_tile_offset = snk.tx_tile_offset;
    st.tx_palette_offset = snk.tx_palette_offset;
    st.bg_palette_offset = snk.bg_palette_offset;
    st.hf_posx = snk.hf_posx;
    st.hf_posy = snk.hf_posy;
    st.dsw1 = snk.dsw1;
    st.dsw2 = snk.dsw2;
    st.game_type = snk.game_type;
    st.rotate = snk.rotate;
    st.sprite_bpp = snk.sprite_bpp;
    st.screen_w = snk.screen_w;
    st.screen_h = snk.screen_h;
    st.crop_x = snk.crop_x;
    st.crop_y = snk.crop_y;
    st.crop_w = snk.crop_w;
    st.crop_h = snk.crop_h;
    st.tx_cols = snk.tx_cols;
    st.tx_rows = snk.tx_rows;
    st.tx_scroll_y = snk.tx_scroll_y;
    st.bg_scrolldx = snk.bg_scrolldx;
    st.bg_scrolldy = snk.bg_scrolldy;
    st.pal_tx_base = snk.pal_tx_base;
    st.pal_bg_base = snk.pal_bg_base;
    st.pal_sp16_base = snk.pal_sp16_base;
    st.pal_sp32_base = snk.pal_sp32_base;

    for (i = 0; i < SNK_CPU_COUNT; i++)
        snk_pack_z80_state(&cpust[i], &snk.cpu[i]);

    memset(&sndst, 0, sizeof(sndst));
    sndst.magic = SNK_PSYCHOS_SOUND_STATE_MAGIC;
    sndst.version = SNK_PSYCHOS_SOUND_STATE_VERSION;
    sndst.ym_size[0] = ym0_size;
    sndst.ym_size[1] = ym1_size;
    sndst.size = (uint32_t)(sizeof(sndst) + ym0_size + ym1_size);

    return snk_write_block(fd, &st, (uint32_t)sizeof(st)) &&
           snk_write_block(fd, cpust, (uint32_t)sizeof(cpust)) &&
           snk_write_block(fd, snk.sharedram, SNK_SHARE_SIZE) &&
           snk_write_block(fd, snk.bg_vram, SNK_BGVRAM_SIZE) &&
           snk_write_block(fd, snk.spriteram, SNK_SPRRAM_SIZE) &&
           snk_write_block(fd, snk.tx_vram, SNK_TXVRAM_SIZE) &&
           snk_write_block(fd, snk.audio_ram, SNK_AUDIORAM_SIZE) &&
           snk_write_block(fd, &sndst, (uint32_t)sizeof(sndst)) &&
           snk_write_opl_state(fd, snk.ym[0], ym0_size) &&
           snk_write_opl_state(fd, snk.ym[1], ym1_size);
}

int snk_psychos_load_state(FILE *fd, uint32_t size)
{
    snk_psychos_state_file_t st;
    snk_psychos_sound_state_file_t sndst;
    snk_z80_state_file_t cpust[SNK_CPU_COUNT];
    uint32_t consumed;
    uint32_t remaining;
    int have_sound_state = 0;
    int i;

    if (!fd || size < snk_psychos_base_state_size())
        return 0;
    if (!snk_read_block(fd, &st, (uint32_t)sizeof(st)))
        return 0;
    if (st.magic != SNK_PSYCHOS_STATE_MAGIC ||
        st.version < SNK_PSYCHOS_STATE_MIN_VERSION || st.version > SNK_PSYCHOS_STATE_VERSION ||
        st.size > size || st.size < snk_psychos_base_state_size())
        return 0;
    if (!snk_psychos_alloc())
        return 0;

    /* Do not silently apply a state from a different SNK board variant. */
    if (snk.active && snk.game_type != st.game_type)
        return 0;

    snk.current_cpu = (st.current_cpu < SNK_CPU_COUNT) ? st.current_cpu : SNK_CPU_MAIN;
    snk.active = st.active;
    snk.athena_main_ramtest_done = st.athena_main_ramtest_done;
    snk.sound_latch = st.sound_latch;
    snk.sound_status = st.sound_status;
    snk.sound_irq_pending = st.sound_irq_pending;
    snk.ym1_addr = st.ym1_addr;
    snk.ym2_addr = st.ym2_addr;
    snk.bg_scrollx = st.bg_scrollx;
    snk.bg_scrolly = st.bg_scrolly;
    snk.sp16_scrollx = st.sp16_scrollx;
    snk.sp16_scrolly = st.sp16_scrolly;
    snk.sp32_scrollx = st.sp32_scrollx;
    snk.sp32_scrolly = st.sp32_scrolly;
    snk.sprite_split = st.sprite_split;
    snk.video_attr = st.video_attr;
    snk.tx_tile_offset = st.tx_tile_offset;
    snk.tx_palette_offset = st.tx_palette_offset;
    snk.bg_palette_offset = st.bg_palette_offset;
    snk.hf_posx = st.hf_posx;
    snk.hf_posy = st.hf_posy;
    snk.dsw1 = st.dsw1;
    snk.dsw2 = st.dsw2;
    snk.game_type = st.game_type;
    snk.rotate = st.rotate;
    snk.sprite_bpp = st.sprite_bpp;
    snk.screen_w = st.screen_w;
    snk.screen_h = st.screen_h;
    snk.crop_x = st.crop_x;
    snk.crop_y = st.crop_y;
    snk.crop_w = st.crop_w;
    snk.crop_h = st.crop_h;
    snk.tx_cols = st.tx_cols;
    snk.tx_rows = st.tx_rows;
    snk.tx_scroll_y = st.tx_scroll_y;
    snk.bg_scrolldx = st.bg_scrolldx;
    snk.bg_scrolldy = st.bg_scrolldy;
    snk.pal_tx_base = st.pal_tx_base;
    snk.pal_bg_base = st.pal_bg_base;
    snk.pal_sp16_base = st.pal_sp16_base;
    snk.pal_sp32_base = st.pal_sp32_base;

    if (!snk_read_block(fd, cpust, (uint32_t)sizeof(cpust)) ||
        !snk_read_block(fd, snk.sharedram, SNK_SHARE_SIZE) ||
        !snk_read_block(fd, snk.bg_vram, SNK_BGVRAM_SIZE) ||
        !snk_read_block(fd, snk.spriteram, SNK_SPRRAM_SIZE) ||
        !snk_read_block(fd, snk.tx_vram, SNK_TXVRAM_SIZE) ||
        !snk_read_block(fd, snk.audio_ram, SNK_AUDIORAM_SIZE))
        return 0;

    consumed = snk_psychos_base_state_size();
    remaining = (st.size >= consumed) ? (st.size - consumed) : 0;

    snk_build_palette();
    snk_psychos_sound_reset();
    snk.sound_latch = st.sound_latch;
    snk.sound_status = st.sound_status;
    snk.sound_irq_pending = st.sound_irq_pending;
    snk.ym1_addr = st.ym1_addr;
    snk.ym2_addr = st.ym2_addr;

    if (st.version >= 2 && remaining >= sizeof(sndst))
    {
        if (!snk_read_block(fd, &sndst, (uint32_t)sizeof(sndst)))
            return 0;
        remaining -= (uint32_t)sizeof(sndst);
        if (sndst.magic == SNK_PSYCHOS_SOUND_STATE_MAGIC &&
            sndst.version == SNK_PSYCHOS_SOUND_STATE_VERSION &&
            sndst.size >= sizeof(sndst) && sndst.size <= remaining + sizeof(sndst) &&
            sndst.ym_size[0] + sndst.ym_size[1] <= remaining)
        {
            have_sound_state = 1;
            if (!snk_read_opl_state(fd, snk.ym[0], sndst.ym_size[0]) ||
                !snk_read_opl_state(fd, snk.ym[1], sndst.ym_size[1]))
                return 0;
        }
    }

    if (have_sound_state)
    {
        snk.sound_latch = st.sound_latch;
        snk.sound_status = st.sound_status;
        snk.sound_irq_pending = st.sound_irq_pending;
        snk.ym1_addr = st.ym1_addr;
        snk.ym2_addr = st.ym2_addr;
    }

    snk_psychos_memory_map(0);
    for (i = 0; i < SNK_CPU_COUNT; i++)
    {
        snk_unpack_z80_state(&snk.cpu[i], &cpust[i]);
        snk_map_cpu(i);
    }
    snk_load_cpu(snk.current_cpu);
    snk_sound_update_irq();

    vdp.height = (snk.rotate == SNK_ROT_NONE) ? snk.crop_h : snk.crop_w;
    vdp.lpf = SNK_PSYCHOS_LINES_PER_FRAME;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = (snk.rotate == SNK_ROT_NONE) ? snk.crop_w : snk.crop_h;
    bitmap.viewport.h = (snk.rotate == SNK_ROT_NONE) ? snk.crop_h : snk.crop_w;
    bitmap.viewport.changed = 1;
    return 1;
}

void snk_psychos_memory_map(int clear_ram)
{
    int i;
    if (!snk_psychos_alloc()) return;
    if (clear_ram)
    {
        memset(snk.sharedram, 0, SNK_SHARE_SIZE);
        memset(snk.bg_vram, 0, SNK_BGVRAM_SIZE);
        memset(snk.spriteram, 0, SNK_SPRRAM_SIZE);
        memset(snk.tx_vram, 0, SNK_TXVRAM_SIZE);
        memset(snk.audio_ram, 0, SNK_AUDIORAM_SIZE);
        snk.sound_latch = 0;
        snk.sound_status = 0;
        snk.sound_irq_pending = 0;
        snk.bg_scrollx = snk.bg_scrolly = 0;
        snk.sp16_scrollx = snk.sp16_scrolly = 0;
        snk.sp32_scrollx = snk.sp32_scrolly = 0;
        snk.sprite_split = 0;
        snk.video_attr = 0;
        snk.tx_tile_offset = 0;
        snk.tx_palette_offset = 0;
        snk.bg_palette_offset = 0;
        snk.hf_posx = snk.hf_posy = 0;
    }
    for (i = 0; i < SNK_CPU_COUNT; i++) snk_map_cpu(i);
    snk_load_cpu(SNK_CPU_MAIN);
}

static uint8_t snk_in0(void)
{
    /* MAME IN0 for SNK games:
     * bit0: sound_busy (CUSTOM, active-high)
     * bit1: SERVICE1 (service coin) - Ikari/Athena/Chopper; UNKNOWN for Psychos
     * bit2: TILT for Victroad/Psychos/Chopper; UNKNOWN for Ikari/Athena
     * bit3: SERVICE (menu) for Chopper; UNKNOWN for others
     * bit4: COIN2, bit5: COIN1, bit6: START2, bit7: START1 */
    uint8_t r = (snk.sound_status & 0x04) ? 0x01 : 0x00;
    r |= 0xfe;
    if (input.arcade & INPUT_ARCADE_SERVICE) r &= (uint8_t)~0x02;
    if (input.arcade & INPUT_ARCADE_TEST)    r &= (uint8_t)~0x04; /* tilt */
    if (input.arcade & INPUT_ARCADE_COIN2)  r &= (uint8_t)~0x10;
    if (input.arcade & INPUT_ARCADE_COIN1)  r &= (uint8_t)~0x20;
    if (input.arcade & INPUT_ARCADE_START2) r &= (uint8_t)~0x40;
    if (input.arcade & INPUT_ARCADE_START1) r &= (uint8_t)~0x80;
    return r;
}

static uint8_t snk_in1(void)
{
    uint8_t r = 0xff;
    uint8_t p2 = input.pad[1];
    uint8_t p1 = input.pad[0];
    if (p2 & INPUT_UP)    r &= (uint8_t)~0x01;
    if (p2 & INPUT_DOWN)  r &= (uint8_t)~0x02;
    if (p2 & INPUT_LEFT)  r &= (uint8_t)~0x04;
    if (p2 & INPUT_RIGHT) r &= (uint8_t)~0x08;
    if (p1 & INPUT_UP)    r &= (uint8_t)~0x10;
    if (p1 & INPUT_DOWN)  r &= (uint8_t)~0x20;
    if (p1 & INPUT_LEFT)  r &= (uint8_t)~0x40;
    if (p1 & INPUT_RIGHT) r &= (uint8_t)~0x80;
    return r;
}

static uint8_t snk_in2(void)
{
    uint8_t r = 0xff;
    if (input.pad[0] & INPUT_BUTTON1) r &= (uint8_t)~0x01;
    if (input.pad[0] & INPUT_BUTTON2) r &= (uint8_t)~0x02;
    if (input.pad[1] & INPUT_BUTTON1) r &= (uint8_t)~0x08;
    if (input.pad[1] & INPUT_BUTTON2) r &= (uint8_t)~0x10;
    return r;
}

static uint8_t snk_athena_player_port(int player)
{
    uint8_t r = 0xff;
    uint8_t p = input.pad[player & 1];
    if (p & INPUT_UP)      r &= (uint8_t)~0x01;
    if (p & INPUT_DOWN)    r &= (uint8_t)~0x02;
    if (p & INPUT_LEFT)    r &= (uint8_t)~0x04;
    if (p & INPUT_RIGHT)   r &= (uint8_t)~0x08;
    if (p & INPUT_BUTTON1) r &= (uint8_t)~0x10;
    if (p & INPUT_BUTTON2) r &= (uint8_t)~0x20;
    return r;
}



static uint8_t snk_ls30_pos_from_aim(uint8_t aim)
{
    int up = (aim & INPUT_AIM_UP) != 0;
    int down = (aim & INPUT_AIM_DOWN) != 0;
    int left = (aim & INPUT_AIM_LEFT) != 0;
    int right = (aim & INPUT_AIM_RIGHT) != 0;

    if (up && down) up = down = 0;
    if (left && right) left = right = 0;

    /* MAME models the original SNK LS-30 as a 12-position rotary control in
     * the high nibble, not as the bootleg joystick-hack bit pattern.  Keep an
     * internal 12-step compass with 0 = up and cardinal points every 3 steps. */
    if (up && right) return 1;
    if (right && down) return 4;
    if (down && left) return 7;
    if (left && up) return 10;
    if (up) return 0;
    if (right) return 3;
    if (down) return 6;
    if (left) return 9;
    return 0xff;
}

static void snk_ls30_step_player(int player, int dir)
{
    player &= 1;
    input.rotary_pos[player] = (uint8_t)((input.rotary_pos[player] + (dir > 0 ? 1 : 11)) % 12);
}

static void snk_update_ls30_player(int player)
{
    uint8_t aim, pos, step_mask;
    player &= 1;

    aim = (uint8_t)(input.rotary_aim[player] & (INPUT_AIM_UP | INPUT_AIM_DOWN | INPUT_AIM_LEFT | INPUT_AIM_RIGHT));
    pos = snk_ls30_pos_from_aim(aim);
    if (pos != 0xff)
    {
        input.rotary_pos[player] = pos;
        snk.rotary_repeat[player] = 0;
        return;
    }

    step_mask = (uint8_t)(input.pad[player] & (INPUT_ROTATE_LEFT | INPUT_ROTATE_RIGHT));
    if ((step_mask & INPUT_ROTATE_LEFT) && !(step_mask & INPUT_ROTATE_RIGHT))
    {
        if (snk.rotary_repeat[player] == 0) snk_ls30_step_player(player, -1);
        snk.rotary_repeat[player] = (uint8_t)((snk.rotary_repeat[player] + 1) & 3);
    }
    else if ((step_mask & INPUT_ROTATE_RIGHT) && !(step_mask & INPUT_ROTATE_LEFT))
    {
        if (snk.rotary_repeat[player] == 0) snk_ls30_step_player(player, 1);
        snk.rotary_repeat[player] = (uint8_t)((snk.rotary_repeat[player] + 1) & 3);
    }
    else
    {
        snk.rotary_repeat[player] = 0;
    }
}

static void snk_update_ls30_inputs(void)
{
    if (snk.game_type == SNK_GAME_IKARI || snk.game_type == SNK_GAME_VICTROAD || snk.game_type == SNK_GAME_GWAR)
    {
        snk_update_ls30_player(0);
        snk_update_ls30_player(1);
    }
}

static uint8_t snk_ls30_read_pos(int player)
{
    uint8_t pos;
    player &= 1;
    pos = (uint8_t)(input.rotary_pos[player] % 12);

    /* MAME's GWAR protection notes that the original mechanical 12-way rotary
     * can momentarily report 0xf when crossing between positions 5 and 6.  Do
     * the same occasionally so this is still a hardware-style model rather than
     * a perfect TTL counter. */
    if (snk.game_type == SNK_GAME_GWAR &&
        ((snk.rotary_last_pos[player] == 5 && pos == 6) || (snk.rotary_last_pos[player] == 6 && pos == 5)))
    {
        uint8_t ret = pos;
        if (snk.rotary_gwar_cp_count[player] == 0) ret = 0x0f;
        snk.rotary_gwar_cp_count[player] = (uint8_t)((snk.rotary_gwar_cp_count[player] + 1) & 7);
        snk.rotary_last_pos[player] = pos;
        return ret;
    }
    snk.rotary_last_pos[player] = pos;
    return pos;
}

static uint8_t snk_ikari_player_port(int player)
{
    uint8_t r = 0x0f;
    uint8_t p = input.pad[player & 1];
    player &= 1;

    if (p & INPUT_UP)    r &= (uint8_t)~0x01;
    if (p & INPUT_DOWN)  r &= (uint8_t)~0x02;
    if (p & INPUT_LEFT)  r &= (uint8_t)~0x04;
    if (p & INPUT_RIGHT) r &= (uint8_t)~0x08;

    return (uint8_t)(r | (uint8_t)(snk_ls30_read_pos(player) << 4));
}

static int snk_hardflags_check(int num)
{
    const uint8_t *sr = &snk.spriteram[0x800 + 4 * (num & 0x3f)];
    int x = sr[2] + ((sr[3] & 0x80) << 1);
    int y = sr[0] + ((sr[3] & 0x10) << 4);
    int dx = (x - snk.hf_posx) & 0x1ff;
    int dy = (y - snk.hf_posy) & 0x1ff;
    return (dx > 0x20 && dx <= 0x1e0 && dy > 0x20 && dy <= 0x1e0) ? 0 : 1;
}

static uint8_t snk_hardflags_check8(int num)
{
    uint8_t r = 0;
    int i;
    for (i = 0; i < 8; i++)
        r |= (uint8_t)(snk_hardflags_check(num * 8 + i) << i);
    return r;
}

static uint8_t snk_hardflags7_r(void)
{
    int a = snk_hardflags_check(6 * 8 + 0);
    int b = snk_hardflags_check(6 * 8 + 1);
    return (uint8_t)((a << 0) | (b << 1) | (a << 4) | (b << 5));
}

uint8_t snk_psychos_readmem(uint16_t address)
{
    if (snk.current_cpu == SNK_CPU_AUDIO)
    {
        if (address < 0xc000) return snk.audio_rom[address];
        if (address < 0xd000) return snk.audio_ram[address & 0x0fff];
        if (address == 0xe000) return snk.sound_latch;
        if (address == 0xe800) return snk_ym_status(0);
        if (address == 0xf000) return snk_ym_status(1);
        if (address == 0xf800) return snk.sound_status;
        return 0xff;
    }

    if (address < 0xc000)
        return (snk.current_cpu == SNK_CPU_SUB) ? snk.sub_rom[address] : snk.main_rom[address];

    if (snk.game_type == SNK_GAME_ATHENA)
    {
        if (snk.current_cpu == SNK_CPU_MAIN)
        {
            switch (address)
            {
                case 0xc000: return snk_in0();
                case 0xc100: return snk_athena_player_port(0);
                case 0xc200: return snk_athena_player_port(1);
                case 0xc300: return 0xff;
                case 0xc500: return snk.dsw1;
                case 0xc600: return snk.dsw2;
                case 0xc700: snk_pulse_nmi_saved(SNK_CPU_SUB); return 0xff;
                default: break;
            }
            if (address >= 0xd000 && address < 0xd800) return snk.spriteram[address - 0xd000];
            if (address >= 0xd800 && address < 0xf800) return snk.bg_vram[address - 0xd800];
            if (address >= 0xf800) return snk.tx_vram[address & 0x07ff];
        }
        else if (snk.current_cpu == SNK_CPU_SUB)
        {
            if (address == 0xc000)
            {
                snk_pulse_nmi_saved(SNK_CPU_MAIN);
                return 0xff;
            }
            if (address >= 0xc800 && address < 0xd000) return snk.spriteram[address - 0xc800];
            if (address >= 0xd000 && address < 0xf000) return snk.bg_vram[address - 0xd000];
            if (address >= 0xf000 && address < 0xf800) return snk.sharedram[address - 0xf000];
            if (address >= 0xf800) return snk.tx_vram[address & 0x07ff];
        }
        return 0xff;
    }

    if (snk.current_cpu == SNK_CPU_MAIN)
    {
        if (snk_is_ikari_video_hw())
        {
            if (address >= 0xce00 && address <= 0xcea0 && ((address & 0x1f) == 0))
                return snk_hardflags_check8((address - 0xce00) >> 5);
            if (address == 0xcee0)
                return snk_hardflags7_r();
        }
        if (snk.game_type == SNK_GAME_IKARI || snk.game_type == SNK_GAME_VICTROAD || snk.game_type == SNK_GAME_GWAR)
        {
            switch (address)
            {
                case 0xc000: return snk_in0();
                case 0xc100: return snk_ikari_player_port(0);
                case 0xc200: return snk_ikari_player_port(1);
                case 0xc300: return snk_in2();
                case 0xc500: return snk.dsw1;
                case 0xc600: return snk.dsw2;
                case 0xc700: snk_pulse_nmi_saved(SNK_CPU_SUB); return 0xff;
                default: break;
            }
        }
        else if (snk.game_type == SNK_GAME_TDFEVER)
        {
            switch (address)
            {
                case 0xc000: return snk_in0();
                case 0xc080: return snk_in1();
                case 0xc100: return snk_in2();
                case 0xc180: return 0xff;
                case 0xc200: return 0xff;
                case 0xc280: return 0xff;
                case 0xc300: return 0xff;
                case 0xc380: return 0xff;
                case 0xc400: return 0xff;
                case 0xc480: return 0xff;
                case 0xc580: return snk.dsw1;
                case 0xc600: return snk.dsw2;
                case 0xc700: snk_pulse_nmi_saved(SNK_CPU_SUB); return 0xff;
                default: break;
            }
        }
        else
        {
            switch (address)
            {
                case 0xc000: return snk_in0();
                case 0xc100: return snk_in1();
                case 0xc200: return snk_in2();
                case 0xc300: return 0xff;
                case 0xc500: return snk.dsw1;
                case 0xc600: return snk.dsw2;
                case 0xc700: snk_pulse_nmi_saved(SNK_CPU_SUB); return 0xff;
                default: break;
            }
        }
    }
    else if (snk.current_cpu == SNK_CPU_SUB)
    {
        if (address == 0xc700 ||
            ((snk.game_type == SNK_GAME_GWAR || snk.game_type == SNK_GAME_CHOPPER || snk_is_ikari_video_hw() || snk.game_type == SNK_GAME_TDFEVER) && address == 0xc000))
        {
            snk_pulse_nmi_saved(SNK_CPU_MAIN);
            return 0xff;
        }
    }

    if (snk_is_ikari_video_hw())
    {
        if (address >= 0xd000 && address < 0xe000) return snk.bg_vram[address & 0x07ff];
        if (address >= 0xe000 && address < 0xf800) return snk.spriteram[address - 0xe000];
        if (address >= 0xf800) return snk.tx_vram[address & 0x07ff];
    }
    else
    {
        if (address >= 0xd000 && address < 0xd800) return snk.bg_vram[address & 0x07ff];
        if (address >= 0xd800 && address < 0xe000) return snk.sharedram[address & 0x07ff];
        if (address >= 0xe000 && address < 0xf800) return snk.spriteram[address - 0xe000];
        if (address >= 0xf800) return snk.tx_vram[address & 0x07ff];
    }
    return 0xff;
}

void snk_psychos_writemem(uint16_t address, uint8_t data)
{
    MULTIREXZ80_TRACE_MEM_WRITE(address, data);

    if (snk.current_cpu == SNK_CPU_AUDIO)
    {
        if (address >= 0xc000 && address < 0xd000) { snk.audio_ram[address & 0x0fff] = data; return; }
        if (address == 0xe800) { snk_ym_write(0, 0, data); return; }
        if (address == 0xec00) { snk_ym_write(0, 1, data); return; }
        if (address == 0xf000) { snk_ym_write(1, 0, data); return; }
        if (address == 0xf400) { snk_ym_write(1, 1, data); return; }
        if (address == 0xf800) { snk_sound_status_w(data); return; }
        return;
    }

    if (address < 0xc000) return;

    if (snk.game_type == SNK_GAME_ATHENA)
    {
        if (snk.current_cpu == SNK_CPU_MAIN)
        {
            switch (address)
            {
                case 0xc300: return; /* coin counters */
                case 0xc400: snk_sound_command_w(data); return;
                case 0xc700: z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE); return;
                case 0xc800:
                    snk.video_attr = data;
                    snk.tx_tile_offset = (uint16_t)(data & 0x40) << 2;
                    snk.bg_scrolly = (snk.bg_scrolly & 0x00ff) | ((uint16_t)(data & 0x10) << 4);
                    snk.sp16_scrolly = (snk.sp16_scrolly & 0x00ff) | ((uint16_t)(data & 0x08) << 5);
                    snk.bg_scrollx = (snk.bg_scrollx & 0x00ff) | ((uint16_t)(data & 0x02) << 7);
                    snk.sp16_scrollx = (snk.sp16_scrollx & 0x00ff) | ((uint16_t)(data & 0x01) << 8);
                    return;
                case 0xc900: snk.sp16_scrolly = (snk.sp16_scrolly & 0xff00) | data; return;
                case 0xca00: snk.sp16_scrollx = (snk.sp16_scrollx & 0xff00) | data; return;
                case 0xcb00: snk.bg_scrolly = (snk.bg_scrolly & 0xff00) | data; return;
                case 0xcc00: snk.bg_scrollx = (snk.bg_scrollx & 0xff00) | data; return;
                case 0xcf00: return;
                default: break;
            }
            if (address >= 0xd000 && address < 0xd800) { snk.spriteram[address - 0xd000] = data; return; }
            if (address >= 0xd800 && address < 0xf800) { snk.bg_vram[address - 0xd800] = data; return; }
            if (address >= 0xf800) { snk.tx_vram[address & 0x07ff] = data; return; }
        }
        else if (snk.current_cpu == SNK_CPU_SUB)
        {
            if (address == 0xc000)
            {
                z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);
                return;
            }
            if (address >= 0xc800 && address < 0xd000) { snk.spriteram[address - 0xc800] = data; return; }
            if (address >= 0xd000 && address < 0xf000) { snk.bg_vram[address - 0xd000] = data; return; }
            if (address >= 0xf000 && address < 0xf800) { snk.sharedram[address - 0xf000] = data; return; }
            if (address >= 0xf800)
            {
                /* Athena's boot ROM lets CPU B perform its own RAM test first,
                 * then CPU A verifies the full D000-FFFF range.  In this compact
                 * frame scheduler CPU B can reach its text-update path before CPU
                 * A has completed that second test, which corrupts CPU A's check
                 * at F800-FFFF and produces a false F819H RAM ERROR.  MAME's
                 * tighter scheduler does not expose that race.  Allow CPU B's
                 * NMI self-test writes (PC < 0200) but defer later text VRAM
                 * writes until CPU A has passed its RAM test. */
                uint16_t pc = z80_get_context()->pc.w.l;
                if (!snk.athena_main_ramtest_done && pc >= 0x0200) return;
                snk.tx_vram[address & 0x07ff] = data; return;
            }
        }
        return;
    }

    if (snk_is_ikari_video_hw())
    {
        switch (address)
        {
            case 0xc800: snk.bg_scrolly = (snk.bg_scrolly & 0xff00) | data; return;
            case 0xc880: snk.bg_scrollx = (snk.bg_scrollx & 0xff00) | data; return;
            case 0xc900:
                snk.bg_scrollx = (snk.bg_scrollx & 0x00ff) | ((uint16_t)(data & 0x02) << 7);
                snk.bg_scrolly = (snk.bg_scrolly & 0x00ff) | ((uint16_t)(data & 0x01) << 8);
                return;
            case 0xc980:
                snk.tx_palette_offset = (uint16_t)(data & 0x01) << 4;
                snk.tx_tile_offset = (uint16_t)(data & 0x10) << 4;
                return;
            case 0xca00: snk.sp16_scrolly = (snk.sp16_scrolly & 0xff00) | data; return;
            case 0xca80: snk.sp16_scrollx = (snk.sp16_scrollx & 0xff00) | data; return;
            case 0xcb00: snk.sp32_scrolly = (snk.sp32_scrolly & 0xff00) | data; return;
            case 0xcb80: snk.sp32_scrollx = (snk.sp32_scrollx & 0xff00) | data; return;
            case 0xcc00: snk.hf_posy = (snk.hf_posy & 0xff00) | data; return;
            case 0xcc80: snk.hf_posx = (snk.hf_posx & 0xff00) | data; return;
            case 0xcd00:
                snk.sp32_scrollx = (snk.sp32_scrollx & 0x00ff) | ((uint16_t)(data & 0x20) << 3);
                snk.sp16_scrollx = (snk.sp16_scrollx & 0x00ff) | ((uint16_t)(data & 0x10) << 4);
                snk.sp32_scrolly = (snk.sp32_scrolly & 0x00ff) | ((uint16_t)(data & 0x08) << 5);
                snk.sp16_scrolly = (snk.sp16_scrolly & 0x00ff) | ((uint16_t)(data & 0x04) << 6);
                return;
            case 0xcd80:
                snk.hf_posx = (snk.hf_posx & 0x00ff) | ((uint16_t)(data & 0x80) << 1);
                snk.hf_posy = (snk.hf_posy & 0x00ff) | ((uint16_t)(data & 0x40) << 2);
                return;
            default: break;
        }
    }
    else
    {
        switch (address)
        {
            case 0xc800: snk.bg_scrolly = (snk.bg_scrolly & 0xff00) | data; return;
            case 0xc840: snk.bg_scrollx = (snk.bg_scrollx & 0xff00) | data; return;
            case 0xc880:
                snk.video_attr = data;
                /* GWAR-class hardware, including Psycho Soldier, drives the
                 * sprite scroll MSBs from the video-attributes latch.  MAME's
                 * bermudat_state::gwar_videoattrs_w applies these bits for
                 * Psycho Soldier as well; leaving PSYCHOS out wraps the 16x16
                 * and 32x32 sprite coordinate spaces at 256 pixels, producing
                 * misplaced/incorrect-looking objects such as the title Y and
                 * in-game energy orbs. */
                if (snk.game_type == SNK_GAME_PSYCHOS || snk.game_type == SNK_GAME_GWAR || snk.game_type == SNK_GAME_CHOPPER)
                {
                    snk.sp32_scrollx = (snk.sp32_scrollx & 0x00ff) | ((uint16_t)(data & 0x80) << 1);
                    snk.sp16_scrollx = (snk.sp16_scrollx & 0x00ff) | ((uint16_t)(data & 0x40) << 2);
                    snk.sp32_scrolly = (snk.sp32_scrolly & 0x00ff) | ((uint16_t)(data & 0x20) << 3);
                    snk.sp16_scrolly = (snk.sp16_scrolly & 0x00ff) | ((uint16_t)(data & 0x10) << 4);
                }
                snk.bg_scrollx = (snk.bg_scrollx & 0x00ff) | ((uint16_t)(data & 0x02) << 7);
                snk.bg_scrolly = (snk.bg_scrolly & 0x00ff) | ((uint16_t)(data & 0x01) << 8);
                return;
            case 0xc8c0:
                snk.tx_palette_offset = (uint16_t)(data & 0x0f) << 4;
                snk.tx_tile_offset = (uint16_t)(data & 0x30) << 4;
                snk.bg_palette_offset = (snk.game_type == SNK_GAME_PSYCHOS && (data & 0x80)) ? 0x80 : 0;
                return;
            case 0xc900:
                if (snk.game_type == SNK_GAME_TDFEVER)
                {
                    snk.sp32_scrolly = (snk.sp32_scrolly & 0x00ff) | ((uint16_t)(data & 0x80) << 1);
                    snk.sp32_scrollx = (snk.sp32_scrollx & 0x00ff) | ((uint16_t)(data & 0x40) << 2);
                }
                else
                    snk.sp16_scrolly = (snk.sp16_scrolly & 0xff00) | data;
                return;
            case 0xc940: snk.sp16_scrollx = (snk.sp16_scrollx & 0xff00) | data; return;
            case 0xc980: snk.sp32_scrolly = (snk.sp32_scrolly & 0xff00) | data; return;
            case 0xc9c0: snk.sp32_scrollx = (snk.sp32_scrollx & 0xff00) | data; return;
            case 0xca80:
                snk.sp32_scrollx = (snk.sp32_scrollx & 0x00ff) | ((uint16_t)(data & 0x20) << 3);
                snk.sp16_scrollx = (snk.sp16_scrollx & 0x00ff) | ((uint16_t)(data & 0x10) << 4);
                snk.sp32_scrolly = (snk.sp32_scrolly & 0x00ff) | ((uint16_t)(data & 0x08) << 5);
                snk.sp16_scrolly = (snk.sp16_scrolly & 0x00ff) | ((uint16_t)(data & 0x04) << 6);
                return;
            default: break;
        }
    }

    if (snk.current_cpu == SNK_CPU_MAIN)
    {
        switch (address)
        {
            case 0xc400:
                if (snk.game_type != SNK_GAME_TDFEVER) { snk_sound_command_w(data); return; }
                break;
            case 0xc500:
                if (snk.game_type == SNK_GAME_TDFEVER) { snk_sound_command_w(data); return; }
                break;
            case 0xc700:
                z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);
                return;
            case 0xca00:
            case 0xca40:
                if (!snk_is_ikari_video_hw()) return;
                break;
            case 0xcac0:
                if (snk.game_type != SNK_GAME_TDFEVER) { snk.sprite_split = data; return; }
                break;
            default: break;
        }
    }
    else if (snk.current_cpu == SNK_CPU_SUB)
    {
        if ((snk.game_type == SNK_GAME_TDFEVER && (address == 0xc000 || address == 0xc700)) ||
            (snk.game_type != SNK_GAME_TDFEVER && address == 0xc700) ||
            ((snk_is_ikari_video_hw() || snk.game_type == SNK_GAME_GWAR || snk.game_type == SNK_GAME_CHOPPER) && address == 0xc000))
        {
            z80_set_irq_line(INPUT_LINE_NMI, CLEAR_LINE);
            return;
        }
    }

    if (snk_is_ikari_video_hw())
    {
        if (address >= 0xd000 && address < 0xe000) { snk.bg_vram[address & 0x07ff] = data; return; }
        if (address >= 0xe000 && address < 0xf800) { snk.spriteram[address - 0xe000] = data; return; }
        if (address >= 0xf800) { snk.tx_vram[address & 0x07ff] = data; return; }
    }
    else
    {
        if (address >= 0xd000 && address < 0xd800) { snk.bg_vram[address & 0x07ff] = data; return; }
        if (address >= 0xd800 && address < 0xe000) { snk.sharedram[address & 0x07ff] = data; return; }
        if (address >= 0xe000 && address < 0xf800) { snk.spriteram[address - 0xe000] = data; return; }
        if (address >= 0xf800) { snk.tx_vram[address & 0x07ff] = data; return; }
    }
}

uint8_t snk_psychos_port_r(uint16_t port)
{
    (void)port;
    return 0xff;
}

void snk_psychos_port_w(uint16_t port, uint8_t data)
{
    (void)port;
    (void)data;
}

static void snk_decode_packed4(uint8_t *dst, const uint8_t *src, uint32_t elements, int width)
{
    uint32_t tile;
    for (tile = 0; tile < elements; tile++)
    {
        uint8_t *tdst = dst + (size_t)tile * (size_t)width * (size_t)width;
        const uint8_t *tsrc = src + (size_t)tile * (size_t)width * (size_t)width / 2u;
        int y, x;
        for (y = 0; y < width; y++)
        {
            for (x = 0; x < width; x += 2)
            {
                uint8_t b = tsrc[((size_t)y * (size_t)width + (size_t)x) >> 1];
                tdst[(size_t)y * (size_t)width + (size_t)x + 0u] = (uint8_t)(b & 0x0f);
                tdst[(size_t)y * (size_t)width + (size_t)x + 1u] = (uint8_t)(b >> 4);
            }
        }
    }
}

static uint8_t snk_sprite4_raw_pixel(const uint8_t *base, uint32_t size, uint32_t tile, int width, int x, int y)
{
    uint32_t plane_size = size >> 2;
    uint32_t bytes_per_tile = (width == 16) ? 32u : 128u;
    uint32_t tiles = plane_size / bytes_per_tile;
    uint8_t pix = 0;
    int p;
    if (!tiles) return 0;
    tile %= tiles;
    for (p = 0; p < 4; p++)
    {
        uint32_t plane_offset = plane_size * (uint32_t)(3 - p);
        uint32_t xo;
        uint32_t bit;
        uint32_t off;
        if (width == 16)
            xo = (x < 8) ? (8u + (uint32_t)x) : (uint32_t)(x - 8);
        else
            xo = (uint32_t)(3 - (x >> 3)) * 8u + (uint32_t)(x & 7);
        bit = tile * (uint32_t)width * (uint32_t)width + (uint32_t)y * (uint32_t)width + xo;
        off = plane_offset + (bit >> 3);
        if (base[off % size] & (uint8_t)(0x80u >> (bit & 7)))
            pix |= (uint8_t)(1u << (3 - p));
    }
    return pix;
}


static uint8_t snk_sprite3_raw_pixel(const uint8_t *base, uint32_t size, uint32_t tile, int width, int x, int y)
{
    uint32_t plane_size = size / 3u;
    uint32_t bytes_per_tile = (width == 16) ? 32u : 128u;
    uint32_t tiles = plane_size / bytes_per_tile;
    uint8_t pix = 0;
    int p;
    if (!tiles) return 0;
    tile %= tiles;
    for (p = 0; p < 3; p++)
    {
        uint32_t plane_offset = plane_size * (uint32_t)(2 - p);
        uint32_t xo;
        uint32_t bit;
        uint32_t off;
        if (width == 16)
            xo = (x < 8) ? (uint32_t)(7 - x) : (uint32_t)(15 - (x - 8));
        else
            xo = ((uint32_t)(x >> 3) * 8u) + (uint32_t)(7 - (x & 7));
        bit = tile * (uint32_t)width * (uint32_t)width + (uint32_t)y * (uint32_t)width + xo;
        off = plane_offset + (bit >> 3);
        if (base[off % size] & (uint8_t)(0x80u >> (bit & 7)))
            pix |= (uint8_t)(1u << (2 - p));
    }
    return pix;
}

static void snk_decode_sprite3(uint8_t *dst, const uint8_t *src, uint32_t size, uint32_t elements, int width)
{
    uint32_t tile;
    for (tile = 0; tile < elements; tile++)
    {
        uint8_t *tdst = dst + (size_t)tile * (size_t)width * (size_t)width;
        int y, x;
        for (y = 0; y < width; y++)
            for (x = 0; x < width; x++)
                tdst[(size_t)y * (size_t)width + (size_t)x] = snk_sprite3_raw_pixel(src, size, tile, width, x, y);
    }
}

static void snk_decode_sprite4(uint8_t *dst, const uint8_t *src, uint32_t size, uint32_t elements, int width)
{
    uint32_t tile;
    for (tile = 0; tile < elements; tile++)
    {
        uint8_t *tdst = dst + (size_t)tile * (size_t)width * (size_t)width;
        int y, x;
        for (y = 0; y < width; y++)
            for (x = 0; x < width; x++)
                tdst[(size_t)y * (size_t)width + (size_t)x] = snk_sprite4_raw_pixel(src, size, tile, width, x, y);
    }
}

static void snk_decode_gfx(void)
{
    if (snk.gfx_decoded || !snk.tx_dec || !snk.bg_dec || !snk.sp16_dec || !snk.sp32_dec)
        return;
    snk_decode_packed4(snk.tx_dec, snk.tx_rom, SNK_TX_ELEMENTS, 8);
    if (snk.game_type == SNK_GAME_ATHENA)
        snk_decode_packed4(snk.bg_dec, snk.bg_rom, SNK_TNK3_BG_ELEMENTS, 8);
    else
        snk_decode_packed4(snk.bg_dec, snk.bg_rom, SNK_BG_ELEMENTS, 16);
    if (snk.sprite_bpp == 3)
    {
        if (snk.game_type == SNK_GAME_ATHENA)
            snk_decode_sprite3(snk.sp16_dec, snk.sp16_rom, SNK_TNK3_SP16_SIZE, SNK_TNK3_SP16_ELEMENTS, 16);
        else if (snk_is_ikari_video_hw())
        {
            snk_decode_sprite3(snk.sp16_dec, snk.sp16_rom, SNK_IKARI_SP16_SIZE, SNK_IKARI_SP16_ELEMENTS, 16);
            snk_decode_sprite3(snk.sp32_dec, snk.sp32_rom, SNK_IKARI_SP32_SIZE, SNK_IKARI_SP32_ELEMENTS, 32);
        }
        else
        {
            snk_decode_sprite3(snk.sp16_dec, snk.sp16_rom, SNK_SP16_SIZE, SNK_SP16_3B_ELEMENTS, 16);
            snk_decode_sprite3(snk.sp32_dec, snk.sp32_rom, SNK_SP32_SIZE, SNK_SP32_3B_ELEMENTS, 32);
        }
    }
    else
    {
        if (snk.game_type == SNK_GAME_PSYCHOS)
        {
            /* Psycho Soldier only has 0x20000 bytes of 16x16 sprite ROMs.
             * MAME still receives banked 16x16 sprite codes above that range
             * and wraps them through the decoded gfx element count.  Decoding
             * this region as the larger GWAR/Chopper 0x40000 layout uses the
             * wrong plane stride and reads the empty 0xff-filled half of the
             * allocation, causing sparse/incorrect 16x16 sprites. */
            snk_decode_sprite4(snk.sp16_dec, snk.sp16_rom, SNK_PSYCHOS_SP16_SIZE, SNK_PSYCHOS_SP16_ELEMENTS, 16);
        }
        else if (snk.game_type == SNK_GAME_CHOPPER)
        {
            /* Chopper I / Legend of Air Cavalry also uses a 0x20000 16x16
             * sprite region in MAME.  The GWAR parent has 0x40000, so using
             * the parent stride here decodes the four 0x8000 plane ROMs as if
             * they were four 0x10000 planes; helicopters and title objects then
             * render with the wrong pixels. */
            snk_decode_sprite4(snk.sp16_dec, snk.sp16_rom, SNK_CHOPPER_SP16_SIZE, SNK_CHOPPER_SP16_ELEMENTS, 16);
        }
        else
            snk_decode_sprite4(snk.sp16_dec, snk.sp16_rom, SNK_SP16_SIZE, SNK_SP16_ELEMENTS, 16);
        snk_decode_sprite4(snk.sp32_dec, snk.sp32_rom, SNK_SP32_SIZE, SNK_SP32_ELEMENTS, 32);
    }
    snk.gfx_decoded = 1;
}

static inline uint8_t tx_pixel(uint16_t code, int x, int y)
{
    code %= SNK_TX_ELEMENTS;
    return snk.tx_dec[((size_t)code << 6) + (size_t)y * 8u + (size_t)x];
}

static inline uint8_t bg_pixel(uint16_t code, int x, int y)
{
    return snk.bg_dec[((size_t)code << 8) + (size_t)y * 16u + (size_t)x];
}

static inline const uint8_t *sp16_tile_pixels(uint16_t code)
{
    if (snk.sprite_bpp == 3)
    {
        if (snk.game_type == SNK_GAME_ATHENA)
            code %= SNK_TNK3_SP16_ELEMENTS;
        else if (snk_is_ikari_video_hw())
            code %= SNK_IKARI_SP16_ELEMENTS;
        else
            code %= SNK_SP16_3B_ELEMENTS;
    }
    else if (snk.game_type == SNK_GAME_PSYCHOS)
        code %= SNK_PSYCHOS_SP16_ELEMENTS;
    else if (snk.game_type == SNK_GAME_CHOPPER)
        code %= SNK_CHOPPER_SP16_ELEMENTS;
    else
        code %= SNK_SP16_ELEMENTS;
    return snk.sp16_dec + ((size_t)code << 8);
}

static inline const uint8_t *sp32_tile_pixels(uint16_t code)
{
    if (snk.sprite_bpp == 3)
        code %= snk_is_ikari_video_hw() ? SNK_IKARI_SP32_ELEMENTS : SNK_SP32_3B_ELEMENTS;
    else
        code %= SNK_SP32_ELEMENTS;
    return snk.sp32_dec + ((size_t)code << 10);
}

static void snk_present_framebuf(void)
{
    int out_w, out_h, x, y;

    if (!bitmap.data || !snk.framebuf)
        return;

    if (snk.rotate == SNK_ROT_NONE)
    {
        out_w = snk.crop_w;
        out_h = snk.crop_h;
    }
    else
    {
        out_w = snk.crop_h;
        out_h = snk.crop_w;
    }
    if (out_w > (int)bitmap.width) out_w = (int)bitmap.width;
    if (out_h > (int)bitmap.height) out_h = (int)bitmap.height;
    if (out_w <= 0 || out_h <= 0) return;

    for (y = 0; y < out_h; y++)
    {
#ifdef MULTIREXZ80_RENDER_32BPP
        uint32_t *dst = (uint32_t *)(void *)(bitmap.data + (size_t)y * bitmap.pitch);
#else
        uint16_t *dst = (uint16_t *)(void *)(bitmap.data + (size_t)y * bitmap.pitch);
#endif
        for (x = 0; x < out_w; x++)
        {
            int sx, sy;
            uint32_t pval;
            if (snk.rotate == SNK_ROT_NONE)
            {
                sx = snk.crop_x + x;
                sy = snk.crop_y + y;
            }
            else if (snk.rotate == SNK_ROT_CW)
            {
                sx = snk.crop_x + y;
                sy = snk.crop_y + (snk.crop_h - 1 - x);
            }
            else
            {
                sx = snk.crop_x + (snk.crop_w - 1 - y);
                sy = snk.crop_y + x;
            }
            if (sx < 0 || sy < 0 || sx >= SNK_PSYCHOS_FRAME_WIDTH || sy >= SNK_PSYCHOS_FRAME_HEIGHT)
                pval = 0;
            else
                pval = snk.framebuf[(size_t)sy * SNK_PSYCHOS_FRAME_WIDTH + (size_t)sx];
#ifdef MULTIREXZ80_RENDER_32BPP
            dst[x] = pval;
#else
            dst[x] = (uint16_t)pval;
#endif
        }
    }

    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = out_w;
    bitmap.viewport.h = out_h;
    bitmap.viewport.changed = 1;
}

static void snk_draw_tnk3_bg(void)
{
    int sy;
    uint32_t *pal = snk.palette;
    const uint8_t *bg = snk.bg_dec;

    for (sy = 0; sy < (int)snk.screen_h; sy++)
    {
        int by = (sy + (int)snk.bg_scrolly - snk.bg_scrolldy) & 0x1ff;
        int ty = (by >> 3) & 63;
        int py = by & 7;
        int bx = ((int)snk.bg_scrollx - snk.bg_scrolldx) & 0x1ff;
        int remaining = (int)snk.screen_w;
        uint32_t *dst = snk.framebuf + (size_t)sy * SNK_PSYCHOS_FRAME_WIDTH;
        uint16_t *idxdst = snk.frameidx + (size_t)sy * SNK_PSYCHOS_FRAME_WIDTH;

        while (remaining > 0)
        {
            int px = bx & 7;
            int run = 8 - px;
            int tx = (bx >> 3) & 63;
            uint32_t offs = ((uint32_t)tx * 64u + (uint32_t)ty) << 1;
            uint8_t code_l = snk.bg_vram[offs & 0x1fff];
            uint8_t attr = snk.bg_vram[(offs + 1) & 0x1fff];
            uint16_t code = (uint16_t)code_l | ((uint16_t)(attr & 0x30) << 4);
            uint16_t penbase = (uint16_t)(snk.pal_bg_base + (((attr & 0x0f) ^ 8) << 4));
            int i;

            if (run > remaining) run = remaining;
            code %= SNK_TNK3_BG_ELEMENTS;
            {
                const uint8_t *src = bg + ((size_t)code << 6) + (size_t)py * 8u + (size_t)px;
                for (i = 0; i < run; i++)
                {
                    int screen_x = (int)(dst - (snk.framebuf + (size_t)sy * SNK_PSYCHOS_FRAME_WIDTH)) + i;
                    if (snk.game_type == SNK_GAME_ATHENA && (screen_x < 16 || screen_x >= 272))
                    {
                        dst[i] = 0;
                        idxdst[i] = 0;
                        continue;
                    }
                    uint16_t pen = (uint16_t)((penbase + src[i]) & 0x3ff);
                    dst[i] = pal[pen];
                    idxdst[i] = pen;
                }
            }
            dst += run;
            idxdst += run;
            bx = (bx + run) & 0x1ff;
            remaining -= run;
        }
    }
}

static void snk_draw_bg(void)
{
    int sy;
    uint32_t *pal = snk.palette;
    const uint8_t *bg = snk.bg_dec;
    const uint16_t pal_base = (uint16_t)(snk.pal_bg_base + snk.bg_palette_offset);

    if (snk.game_type == SNK_GAME_ATHENA)
    {
        snk_draw_tnk3_bg();
        return;
    }

    for (sy = 0; sy < (int)snk.screen_h; sy++)
    {
        int by = (sy + (int)snk.bg_scrolly - snk.bg_scrolldy) & 0x1ff;
        int ty = (by >> 4) & 31;
        int py = by & 15;
        int bx = ((int)snk.bg_scrollx - snk.bg_scrolldx) & 0x1ff;
        int remaining = (int)snk.screen_w;
        uint32_t *dst = snk.framebuf + (size_t)sy * SNK_PSYCHOS_FRAME_WIDTH;
        uint16_t *idxdst = snk.frameidx + (size_t)sy * SNK_PSYCHOS_FRAME_WIDTH;

        while (remaining > 0)
        {
            /* MAME draws Ikari/Victory Road through a 36x28 visible area with
             * the 16x16 background tilemap scrolled/clipped by set_scrolldx().
             * The side columns remain the cleared black bitmap; after ROT270
             * those source-side columns become the top/bottom black bands. */
            int screen_x = (int)(dst - (snk.framebuf + (size_t)sy * SNK_PSYCHOS_FRAME_WIDTH));
            int px = bx & 15;
            int run = 16 - px;
            int tx = (bx >> 4) & 31;
            uint32_t offs = (uint32_t)(tx * 32 + ty) << 1;
            uint8_t code_l = snk.bg_vram[offs & 0x7ff];
            uint8_t attr = snk.bg_vram[(offs + 1) & 0x7ff];
            uint16_t code;
            uint16_t penbase;
            if (snk_is_ikari_video_hw())
            {
                code = (uint16_t)code_l | ((uint16_t)(attr & 0x03) << 8);
                penbase = (uint16_t)(pal_base + (((attr & 0x70) >> 4) << 4));
            }
            else
            {
                code = (uint16_t)code_l | ((uint16_t)(attr & 0x0f) << 8);
                penbase = (uint16_t)(pal_base + (((attr >> 4) & 0x07) << 4));
            }
            int i;

            if (run > remaining) run = remaining;
            if (code < SNK_BG_ELEMENTS)
            {
                const uint8_t *src = bg + ((size_t)code << 8) + (size_t)py * 16u + (size_t)px;
                for (i = 0; i < run; i++)
                {
                    if (snk_is_ikari_video_hw() && (screen_x + i < 16 || screen_x + i >= 272))
                    {
                        dst[i] = 0;
                        idxdst[i] = 0;
                        continue;
                    }
                    uint8_t pix = src[i];
                    uint16_t pen = (uint16_t)((penbase + pix) & 0x3ff);
                    dst[i] = pal[pen];
                    idxdst[i] = pen;
                }
            }
            else
            {
                uint32_t fill = pal[(penbase + 0x0f) & 0x3ff];
                for (i = 0; i < run; i++)
                {
                    if (snk_is_ikari_video_hw() && (screen_x + i < 16 || screen_x + i >= 272))
                    {
                        dst[i] = 0;
                        idxdst[i] = 0;
                        continue;
                    }
                    dst[i] = fill;
                    idxdst[i] = (uint16_t)((penbase + 0x0f) & 0x3ff);
                }
            }
            dst += run;
            idxdst += run;
            bx = (bx + run) & 0x1ff;
            remaining -= run;
        }
    }
}

static uint32_t snk_marvins_tx_offset(int col, int row)
{
    int c = (col - 2) & 0x3f;
    if (c & 0x20)
        return 0x400u + (uint32_t)row + ((uint32_t)(c & 0x1f) << 5);
    return (uint32_t)row + ((uint32_t)c << 5);
}

static void snk_draw_tx(void)
{
    int sy;
    uint32_t *pal = snk.palette;
    const uint8_t *txdec = snk.tx_dec;
    const uint16_t penbase = (uint16_t)(snk.pal_tx_base + snk.tx_palette_offset);

    for (sy = 0; sy < (int)snk.screen_h; sy++)
    {
        /* MAME's tx tilemap uses set_scrolldy(8,8).  This is a display
         * offset, not a request to sample eight rows later. */
        int tx_sy = sy - (int)snk.tx_scroll_y;
        int ty = (tx_sy >> 3) % snk.tx_rows;
        int py = tx_sy & 7;
        if (ty < 0) ty += snk.tx_rows;
        int tx;
        uint32_t *dst = snk.framebuf + (size_t)sy * SNK_PSYCHOS_FRAME_WIDTH;
        uint16_t *idxdst = snk.frameidx + (size_t)sy * SNK_PSYCHOS_FRAME_WIDTH;

        for (tx = 0; tx < (int)snk.tx_cols; tx++, dst += 8, idxdst += 8)
        {
            uint32_t offs = (snk.game_type == SNK_GAME_ATHENA || snk_is_ikari_video_hw()) ? snk_marvins_tx_offset(tx, ty) : ((uint32_t)tx * 32u + (uint32_t)ty);
            uint8_t raw = snk.tx_vram[offs & 0x7ff];
            uint16_t code = (uint16_t)raw + snk.tx_tile_offset;
            uint16_t local_penbase = penbase;
            const uint8_t *src;
            int i;

            if (snk.game_type == SNK_GAME_ATHENA)
            {
                code %= SNK_TNK3_TX_ELEMENTS;
                local_penbase = (uint16_t)(snk.pal_tx_base + ((raw >> 5) << 4));
            }
            else
                code %= SNK_TX_ELEMENTS;

            src = txdec + ((size_t)code << 6) + (size_t)py * 8u;
            for (i = 0; i < 8; i++)
            {
                uint8_t pix = src[i];
                if (pix != 0x0f)
                {
                    uint16_t pen = (uint16_t)((local_penbase + pix) & 0x3ff);
                    dst[i] = pal[pen];
                    idxdst[i] = pen;
                }
            }
        }
    }
}

static void snk_draw_sprite_tile(int sx, int sy, int size, uint16_t code, uint16_t palbase, uint8_t color)
{
    int y, x;
    int xmin = sx < 0 ? 0 : sx;
    int ymin = sy < 0 ? 0 : sy;
    int xmax = (sx + size > (int)snk.screen_w) ? (int)snk.screen_w : sx + size;
    int ymax = (sy + size > (int)snk.screen_h) ? (int)snk.screen_h : sy + size;
    const uint8_t *tilepix;
    uint32_t *pal = snk.palette;
    uint16_t penbase = (uint16_t)(palbase + ((uint16_t)color << (snk.sprite_bpp == 3 ? 3 : 4)));

    if (xmax <= xmin || ymax <= ymin)
        return;

    tilepix = (size == 16) ? sp16_tile_pixels(code) : sp32_tile_pixels(code);
    for (y = ymin; y < ymax; y++)
    {
        const uint8_t *src = tilepix + (size_t)(y - sy) * (size_t)size + (size_t)(xmin - sx);
        uint32_t *dst = snk.framebuf + (size_t)y * SNK_PSYCHOS_FRAME_WIDTH + (size_t)xmin;
        uint16_t *idxdst = snk.frameidx + (size_t)y * SNK_PSYCHOS_FRAME_WIDTH + (size_t)xmin;
        for (x = xmin; x < xmax; x++, src++, dst++, idxdst++)
        {
            /* MAME draws Athena through the TNK3 36x28 visible area, but the
             * side HUD columns are not part of the sprite playfield.  The
             * text/HUD layer is drawn after sprites; clipping sprite pixels
             * here prevents off-playfield objects from bleeding over the
             * black side meters while preserving the HUD graphics themselves. */
            if (snk.game_type == SNK_GAME_ATHENA && (x < 16 || x >= 272))
                continue;

            uint8_t pix = *src;
            if (snk.sprite_bpp == 3)
            {
                if (pix == 7)
                    continue;
                if (pix == 6)
                {
                    uint16_t shadow = (uint16_t)((*idxdst | 0x200u) & 0x3ffu);
                    *idxdst = shadow;
                    *dst = pal[shadow];
                    continue;
                }
                {
                    uint16_t pen = (uint16_t)((penbase + pix) & 0x3ff);
                    *idxdst = pen;
                    *dst = pal[pen];
                }
            }
            else if (snk.game_type == SNK_GAME_TDFEVER)
            {
                if (pix == 15)
                    continue;
                if (pix == 14)
                {
                    uint16_t shadow = *idxdst;
                    if (shadow >= 0x200 && shadow < 0x300)
                    {
                        shadow = (uint16_t)(shadow + 0x100);
                        *idxdst = shadow;
                        *dst = pal[shadow];
                    }
                    continue;
                }
                {
                    uint16_t pen = (uint16_t)((penbase + pix) & 0x3ff);
                    *idxdst = pen;
                    *dst = pal[pen];
                }
            }
            else
            {
                if (pix != 0x0f)
                {
                    uint16_t pen = (uint16_t)((penbase + pix) & 0x3ff);
                    *idxdst = pen;
                    *dst = pal[pen];
                }
            }
        }
    }
}

static void snk_draw_tnk3_sprites(void)
{
    const uint8_t *source = snk.spriteram;
    int offs;
    const int size = 16;

    for (offs = 0; offs < 50 * 4; offs += 4)
    {
        uint16_t tile_number = source[offs + 1];
        uint8_t attributes = source[offs + 3];
        uint8_t color = attributes & 0x0f;
        int sx = (int)snk.sp16_scrollx + 301 - size - source[offs + 2];
        int sy = -(int)snk.sp16_scrolly + 7 - size + source[offs + 0];

        sx += (attributes & 0x80) << 1;
        sy += (attributes & 0x10) << 4;
        tile_number |= (uint16_t)((attributes & 0x40) << 2);
        tile_number |= (uint16_t)((attributes & 0x20) << 4);

        sx &= 0x1ff;
        sy &= 0x1ff;
        if (sx > 512 - size) sx -= 512;
        if (sy > 512 - size) sy -= 512;
        snk_draw_sprite_tile(sx, sy, size, tile_number, snk.pal_sp16_base, color);
    }
}

static void snk_draw_sprites_group(const uint8_t *source, int gfxnum, int from, int to)
{
    int which;
    int size = (gfxnum == 2) ? 16 : 32;
    int xscroll = (gfxnum == 2) ? snk.sp16_scrollx : snk.sp32_scrollx;
    int yscroll = (gfxnum == 2) ? snk.sp16_scrolly : snk.sp32_scrolly;
    int hw_xflip = (snk.game_type == SNK_GAME_TDFEVER);

    for (which = from * 4; which < to * 4; which += 4)
    {
        uint16_t tile_number = source[which + 1];
        uint8_t attributes = source[which + 3];
        uint8_t color = attributes & 0x0f;
        int sx, sy;

        if (snk_is_ikari_video_hw())
        {
            sx = xscroll + 300 - size - source[which + 2];
            sy = -yscroll + 7 - size + source[which + 0];
            sx += (attributes & 0x80) << 1;
            sy += (attributes & 0x10) << 4;
            if (size == 16)
                tile_number |= (uint16_t)((attributes & 0x60) << 3);
            else
                tile_number |= (uint16_t)((attributes & 0x40) << 2);
            color &= 0x0f;
        }
        else
        {
            sx = -xscroll - 9 + source[which + 2];
            sy = -yscroll + 1 - size + source[which + 0];
            sx += (attributes & 0x80) << 1;
            sy += (attributes & 0x10) << 4;
            if (hw_xflip)
                sx = 495 - size - sx;
            if (size == 16)
            {
                tile_number |= (uint16_t)(((attributes & 0x08) << 5) | ((attributes & 0x60) << 4));
                color &= 7;
                if (from == 0) color |= 8;
            }
            else
            {
                tile_number |= (uint16_t)((attributes & 0x60) << 3);
            }
        }

        sx &= 0x1ff;
        sy &= 0x1ff;
        if (sx > 512 - size) sx -= 512;
        if (sy > 512 - size) sy -= 512;
        snk_draw_sprite_tile(sx, sy, size, tile_number, (size == 16) ? snk.pal_sp16_base : snk.pal_sp32_base, color);
    }
}

static int snk_can_render_direct(void)
{
#ifdef MULTIREXZ80_RENDER_32BPP
    return snk.rotate == SNK_ROT_NONE && snk.crop_x == 0 && snk.crop_y == 0 &&
           snk.crop_w == snk.screen_w && snk.crop_h == snk.screen_h &&
           bitmap.data && bitmap.width == snk.screen_w &&
           bitmap.height >= snk.screen_h &&
           bitmap.pitch == (int)(snk.screen_w * sizeof(uint32_t));
#else
    return 0;
#endif
}

static void snk_render(void)
{
    int direct;
    if (!bitmap.data || !snk.framebuf_shadow || !snk.frameidx_shadow) return;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = (snk.rotate == SNK_ROT_NONE) ? snk.crop_w : snk.crop_h;
    bitmap.viewport.h = (snk.rotate == SNK_ROT_NONE) ? snk.crop_h : snk.crop_w;
    bitmap.viewport.changed = 1;
    snk_decode_gfx();

    direct = snk_can_render_direct();
#ifdef MULTIREXZ80_RENDER_32BPP
    snk.framebuf = direct ? (uint32_t *)(void *)bitmap.data : snk.framebuf_shadow;
#else
    snk.framebuf = snk.framebuf_shadow;
#endif
    snk.frameidx = snk.frameidx_shadow;

    if (!direct)
        memset(snk.framebuf, 0, (size_t)SNK_PSYCHOS_FRAME_WIDTH * SNK_PSYCHOS_FRAME_HEIGHT * sizeof(uint32_t));
    memset(snk.frameidx, 0, (size_t)SNK_PSYCHOS_FRAME_WIDTH * SNK_PSYCHOS_FRAME_HEIGHT * sizeof(uint16_t));

    snk_draw_bg();
    if (snk.game_type == SNK_GAME_ATHENA)
    {
        snk_draw_tnk3_sprites();
    }
    else if (snk_is_ikari_video_hw())
    {
        snk_draw_sprites_group(snk.spriteram + 0x800, 2, 0, 25);
        /* MAME ikari_draw_sprites() always draws 25 entries per call; the
         * 32x32 bank is not a 32-entry list on this hardware. */
        snk_draw_sprites_group(snk.spriteram,         3, 0, 25);
        snk_draw_sprites_group(snk.spriteram + 0x800, 2, 25, 50);
    }
    else if (snk.game_type == SNK_GAME_TDFEVER)
    {
        snk_draw_sprites_group(snk.spriteram, 3, 0, 32);
    }
    else
    {
        snk_draw_sprites_group(snk.spriteram + 0x800, 2, 0, snk.sprite_split);
        snk_draw_sprites_group(snk.spriteram,         3, 0, 32);
        snk_draw_sprites_group(snk.spriteram + 0x800, 2, snk.sprite_split, 64);
    }
    snk_draw_tx();

    if (!direct)
        snk_present_framebuf();
    else
    {
        bitmap.viewport.w = snk.screen_w;
        bitmap.viewport.h = snk.screen_h;
        bitmap.viewport.changed = 1;
    }
}

static void snk_reset_one_cpu(int which)
{
    snk_load_cpu(which);
    z80_reset();
    z80_get_context()->irq_callback = snk_irq_callback;
    z80_set_irq_line(INPUT_LINE_IRQ0, CLEAR_LINE);
    snk_save_cpu(which);
    snk.cpu[which].cycles = 0;
}

void snk_psychos_reset(void)
{
    int i;
    snk_psychos_memory_map(1);
    snk.athena_main_ramtest_done = 0;
    snk_build_palette();
    snk_psychos_sound_reset();
    for (i = 0; i < SNK_CPU_COUNT; i++) snk_reset_one_cpu(i);
    snk_load_cpu(SNK_CPU_MAIN);
    vdp.height = (snk.rotate == SNK_ROT_NONE) ? snk.crop_h : snk.crop_w;
    vdp.lpf = SNK_PSYCHOS_LINES_PER_FRAME;
    vdp.line = 0;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = (snk.rotate == SNK_ROT_NONE) ? snk.crop_w : snk.crop_h;
    bitmap.viewport.h = (snk.rotate == SNK_ROT_NONE) ? snk.crop_h : snk.crop_w;
    bitmap.viewport.changed = 1;
}

static void snk_run_cpu_delta(int which, int32_t cycles)
{
    if (snk.game_type == SNK_GAME_IKARI && which == SNK_CPU_SUB)
    {
        uint16_t pc_a = snk.cpu[SNK_CPU_MAIN].regs.pc.w.l;
        /* Ikari's two main Z80s both test the shared D800-FFFF work/video RAM
         * at power-on.  The compact line scheduler can overlap those tests and
         * produce the false 0250H ERROR seen on boot; MAME's fine-grained
         * interleave does not.  Keep CPU B idle while CPU A is in its initial
         * D800-FFFF check, then run both CPUs normally. */
        if (pc_a >= 0x0dd5 && pc_a <= 0x0e25)
            return;
    }
    snk_load_cpu(which);
    if (which == SNK_CPU_AUDIO)
        z80_set_irq_line(INPUT_LINE_IRQ0, (snk.sound_status & (SNK_SOUND_YM1_IRQ | SNK_SOUND_YM2_IRQ | SNK_SOUND_CMD_IRQ)) ? ASSERT_LINE : CLEAR_LINE);
    z80_execute(cycles);
    if (snk.game_type == SNK_GAME_ATHENA && which == SNK_CPU_MAIN && !snk.athena_main_ramtest_done)
    {
        uint16_t pc = z80_get_context()->pc.w.l;
        if (pc >= 0x0aa9 && pc < 0x0b30)
            snk.athena_main_ramtest_done = 1;
    }
    snk_save_cpu(which);
}

void snk_psychos_frame(uint32_t skip_render)
{
    int line;
    const int32_t cycles_per_line = (snk.game_type == SNK_GAME_ATHENA || snk.game_type == SNK_GAME_IKARI) ? 213 : SNK_PSYCHOS_CYCLES_PER_LINE;
    const int32_t audio_cycles_per_line = SNK_PSYCHOS_CYCLES_PER_LINE;

    if (input.system & INPUT_RESET)
        snk_psychos_reset();

    snk_update_ls30_inputs();

    sms.paused = 0;
    vdp.height = (snk.rotate == SNK_ROT_NONE) ? snk.crop_h : snk.crop_w;
    vdp.lpf = SNK_PSYCHOS_LINES_PER_FRAME;

    for (line = 0; line < SNK_PSYCHOS_LINES_PER_FRAME; line++)
    {
        vdp.line = line;
        if (line == SNK_PSYCHOS_VISIBLE_HEIGHT)
        {
            if (!skip_render) snk_render();
            snk_set_irq_saved(SNK_CPU_MAIN, ASSERT_LINE);
            snk_set_irq_saved(SNK_CPU_SUB, ASSERT_LINE);
        }
        snk_run_cpu_delta(SNK_CPU_MAIN, cycles_per_line);
        snk_run_cpu_delta(SNK_CPU_SUB, cycles_per_line);
        snk_run_cpu_delta(SNK_CPU_AUDIO, audio_cycles_per_line);
        MULTIREXZ80_sound_update(line);
    }
    snk.cpu[SNK_CPU_MAIN].cycles = 0;
    snk.cpu[SNK_CPU_SUB].cycles = 0;
    snk.cpu[SNK_CPU_AUDIO].cycles = 0;
    snk_load_cpu(SNK_CPU_MAIN);
}
