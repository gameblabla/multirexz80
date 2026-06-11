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

#ifndef SNK_PSYCHOS_H_
#define SNK_PSYCHOS_H_

/*
 * Compact SNK arcade hardware support, including Psycho Soldier, Athena and
 * Ikari Warriors.  Hardware maps, graphics layouts,
 * palette equations and ROM definitions are derived from MAME's BSD-3-Clause
 * SNK driver (src/mame/snk/snk.cpp and snk_v.cpp), credited there to Ernesto
 * Corvi, Tim Lindquist, Carlos A. Lozano, Bryan McPhail, Jarek Parchanski,
 * Nicola Salmoria, Tomasz Slanina, Phil Stroffolino, Acho A. Tang and Victor
 * Trucco, with thanks to Marco Cassili.  See docs/THIRD_PARTY_NOTICES.md.
 */

#include <stdint.h>
#include <stdio.h>

#define SNK_PSYCHOS_CYCLES_PER_LINE 256
#define SNK_PSYCHOS_FRAME_WIDTH    400
#define SNK_PSYCHOS_FRAME_HEIGHT   224
#define SNK_PSYCHOS_VISIBLE_WIDTH  400
#define SNK_PSYCHOS_VISIBLE_HEIGHT 224
#define SNK_PSYCHOS_LINES_PER_FRAME 262

#define SNK_REGION_MAIN      0
#define SNK_REGION_SUB       1
#define SNK_REGION_AUDIO     2
#define SNK_REGION_PROM      3
#define SNK_REGION_TX        4
#define SNK_REGION_BG        5
#define SNK_REGION_SP16      6
#define SNK_REGION_SP32      7
#define SNK_REGION_YM2       8

#define SNK_GAME_PSYCHOS      0
#define SNK_GAME_VICTROAD     1
#define SNK_GAME_GWAR         2
#define SNK_GAME_CHOPPER      3
#define SNK_GAME_TDFEVER      4
#define SNK_GAME_ATHENA       5
#define SNK_GAME_IKARI        6

int snk_psychos_alloc(void);
void snk_psychos_free(void);
void snk_psychos_clear_roms(void);
int snk_psychos_set_region(int region, uint32_t offset, const uint8_t *data, uint32_t size);
void snk_psychos_set_game(void);
void snk_psychos_set_game_variant(int variant);
void snk_psychos_memory_map(int clear_ram);
uint8_t snk_psychos_readmem(uint16_t address);
void snk_psychos_writemem(uint16_t address, uint8_t data);
uint8_t snk_psychos_port_r(uint16_t port);
void snk_psychos_port_w(uint16_t port, uint8_t data);
void snk_psychos_reset(void);
void snk_psychos_frame(uint32_t skip_render);
void snk_psychos_sound_reset(void);
void snk_psychos_sound_update(int16_t **buffer, int32_t length);
int snk_psychos_audio_mixer_gain_num(int headroom_db);
uint32_t snk_psychos_state_size(void);
int snk_psychos_save_state(FILE *fd);
int snk_psychos_load_state(FILE *fd, uint32_t size);

#endif
