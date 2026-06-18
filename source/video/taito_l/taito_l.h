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

#ifndef TAITO_L_H_
#define TAITO_L_H_

/*
 * Taito L-System support for MultiRexZ80.
 *
 * This is an independent compact C implementation of the Taito L-System
 * arcade hardware, built around the custom TC0090LVC (a Z80-based SoC with
 * integrated video).  Hardware maps, ROM layout, video formats, palette
 * equations and the TC0090LVC register model are derived from MAME's
 * BSD-3-Clause Taito L driver (src/mame/taito/taito_l.cpp) by Olivier
 * Galibert, and the TC0090LVC device
 * (src/devices/machine/tc009xlvc.cpp/.h) by Angelo Salese.  See
 * docs/THIRD_PARTY_NOTICES.md.
 *
 * The single-TC0090LVC games (Plotting, Puzznic, Palamedes, Cachat/Tube-It,
 * American Horseshoes, Play Girls / Play Girls 2 / LA Girl, Cuby Bop) are
 * fully emulated here.  The multi-processor games (Raimais, Fighting Hawk,
 * Champion Wrestler, Kuri Kinton, Evil Stone) are present in the ROM
 * database for completeness but are not yet driven by this compact core
 * because they need additional slave/sound Z80s.
 */

#include <stdint.h>
#include <stdio.h>

/* Z80 timing: 13.33056 MHz XTAL / 2 = 6.66528 MHz master clock.
 * 6.66528 MHz / 60 Hz = 111088 cycles/frame; 262 scanlines -> ~424 cpl.
 * We use the same 256 cycles-per-line baseline as the other arcade cores
 * and let the frame scheduler run 262 lines. */
#define TAITOL_CYCLES_PER_LINE     424
#define TAITOL_LINES_PER_FRAME     262
#define TAITOL_FRAME_WIDTH         320
#define TAITOL_FRAME_HEIGHT        256
#define TAITOL_VISIBLE_WIDTH       320
#define TAITOL_VISIBLE_HEIGHT      224
#define TAITOL_VISIBLE_X           0
#define TAITOL_VISIBLE_Y           16

/* ROM region tags (mirroring MAME).  Only MAIN and GFX are consumed by the
 * single-CPU emulation path; the others are loaded for completeness. */
#define TAITOL_REGION_MAIN         0   /* TC0090LVC program ROM          */
#define TAITOL_REGION_GFX          1   /* tile/sprite graphics ROM       */
#define TAITOL_REGION_AUDIO        2   /* sound CPU ROM (multi-CPU games) */
#define TAITOL_REGION_SLAVE        3   /* slave CPU ROM (multi-CPU games)  */
#define TAITOL_REGION_ADPCM        4   /* MSM5205 ADPCM ROM (champwr)     */
#define TAITOL_REGION_MCU          5   /* 68705 MCU ROM (puzznic)         */
#define TAITOL_REGION_YM2610       6   /* YM2610 ADPCM-A ROM (raimais)    */

/* Memory-map variant used by each game.  This corresponds to the address
 * map selected by MAME's machine-config functions. */
#define TAITOL_MAP_PLOTTING        0   /* base() / plotting() map        */
#define TAITOL_MAP_PALAMED         1   /* palamed() map (cachat/plgirls) */
#define TAITOL_MAP_PUZZNIC         2   /* puzznic() map (with MCU)       */
#define TAITOL_MAP_PUZZNICI        3   /* puzznici() map (bootleg, no MCU) */
#define TAITOL_MAP_HORSHOES        4   /* horshoes() map                 */

/* Per-game configuration variant.  The values are only used to pick the
 * memory map, screen orientation and DIP defaults. */
enum
{
    TAITOL_GAME_PLOTTING = 0,
    TAITOL_GAME_PLOTTINGA,
    TAITOL_GAME_PLOTTINGB,
    TAITOL_GAME_PLOTTINGU,
    TAITOL_GAME_FLIPULL,
    TAITOL_GAME_PUZZNIC,
    TAITOL_GAME_PUZZNICU,
    TAITOL_GAME_PUZZNICJ,
    TAITOL_GAME_PUZZNICI,
    TAITOL_GAME_PUZZNICB,
    TAITOL_GAME_PUZZNICBA,
    TAITOL_GAME_HORSHOES,
    TAITOL_GAME_PALAMED,
    TAITOL_GAME_PALAMEDJ,
    TAITOL_GAME_CACHAT,
    TAITOL_GAME_TUBEIT,
    TAITOL_GAME_CUBYBOP,
    TAITOL_GAME_PLGIRLS,
    TAITOL_GAME_LAGIRL,
    TAITOL_GAME_PLGIRLS2,
    TAITOL_GAME_PLGIRLS2B,
    /* Multi-CPU games: present in the ROM database, not yet driven. */
    TAITOL_GAME_RAIMAIS,
    TAITOL_GAME_RAIMAISJ,
    TAITOL_GAME_RAIMAISJO,
    TAITOL_GAME_FHAWK,
    TAITOL_GAME_FHAWKJ,
    TAITOL_GAME_CHAMPWR,
    TAITOL_GAME_CHAMPWRU,
    TAITOL_GAME_CHAMPWRJ,
    TAITOL_GAME_KURIKINT,
    TAITOL_GAME_KURIKINTW,
    TAITOL_GAME_KURIKINTU,
    TAITOL_GAME_KURIKINTJ,
    TAITOL_GAME_KURIKINTA,
    TAITOL_GAME_EVILSTON,
    TAITOL_GAME_COUNT
};

int      taitol_alloc(void);
void     taitol_free(void);
void     taitol_clear_roms(void);
int      taitol_set_region(int region, uint32_t offset, const uint8_t *data, uint32_t size);
void     taitol_set_game_variant(int variant);

/* Direct ROM-buffer accessors for the ZIP loader's interleaved
 * ROM_LOAD16_BYTE / ROM_LOAD32_BYTE path.  Returns NULL before
 * taitol_alloc().  taitol_invalidate_gfx() forces a re-decode. */
uint8_t *taitol_main_rom_ptr(void);
uint8_t *taitol_gfx_rom_ptr(void);
void     taitol_invalidate_gfx(void);

void     taitol_memory_map(int clear_ram);
uint8_t  taitol_readmem(uint16_t address);
void     taitol_writemem(uint16_t address, uint8_t data);
uint8_t  taitol_port_r(uint16_t port);
void     taitol_port_w(uint16_t port, uint8_t data);
void     taitol_reset(void);
void     taitol_frame(uint32_t skip_render);
int32_t  taitol_irq_callback(int32_t param);
int      taitol_needs_rotation(void);
void     taitol_sound_reset(void);
void     taitol_sound_update(int16_t **buffer, int32_t length);
int      taitol_audio_mixer_gain_num(int headroom_db);
uint32_t taitol_state_size(void);
int      taitol_save_state(FILE *fd);
int      taitol_load_state(FILE *fd, uint32_t size);

#endif /* TAITO_L_H_ */
