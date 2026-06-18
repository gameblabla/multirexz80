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
 * Disabled arcade component ABI stubs.
 *
 * Build with ENABLE_ARCADE=0 to omit the Sega System E/System 1 and SNK arcade
 * video/machine implementations while keeping the rest of the emulator linkable.
 ******************************************************************************/

#include "shared.h"

int system1_alloc(void) { return 0; }
void system1_free(void) { }
void system1_clear_roms(void) { }
int system1_set_region(int region, uint32_t offset, const uint8_t *data, uint32_t size)
{ (void)region; (void)offset; (void)data; (void)size; return 0; }
void system1_set_game_blockgal(void) { }
void system1_set_game_blockgal_mc8123(void) { }
void system1_set_game_choplifter(void) { }
void system1_set_game_flicky(void) { }
void system1_set_game_teddybb(void) { }
void system1_set_game_wboy(void) { }
void system1_set_game_wbml(void) { }
void system1_set_game_gardia(void) { }
void system1_set_game_ufosensi(void) { }
void system1_set_game_brain(void) { }
void system1_set_game_starjack(void) { }
void system1_set_game_upndown(void) { }
void system1_set_game_swat(void) { }
void system1_set_game_wmatch(void) { }
void system1_set_game_spatter(void) { }
void system1_set_game_pitfall2(void) { }
void system1_set_game_seganinj(void) { }
void system1_set_game_imsorry(void) { }
void system1_set_game_myhero(void) { }
void system1_set_game_nob(void) { }
int system1_uses_dial(void) { return 0; }
void system1_memory_map(int clear_ram) { (void)clear_ram; }
void system1_bank_w(uint8_t data) { (void)data; }
uint8_t system1_readmem(uint16_t address) { (void)address; return 0xff; }
void system1_writemem(uint16_t address, uint8_t data) { (void)address; (void)data; }
void system1_port_w(uint16_t port, uint8_t data) { (void)port; (void)data; }
uint8_t system1_port_r(uint16_t port) { (void)port; return 0xff; }
void system1_reset(void) { }
void system1_frame(uint32_t skip_render) { (void)skip_render; }

int snk_psychos_alloc(void) { return 0; }
void snk_psychos_free(void) { }
void snk_psychos_clear_roms(void) { }
int snk_psychos_set_region(int region, uint32_t offset, const uint8_t *data, uint32_t size)
{ (void)region; (void)offset; (void)data; (void)size; return 0; }
void snk_psychos_set_game(void) { }
void snk_psychos_set_game_variant(int variant) { (void)variant; }
void snk_psychos_memory_map(int clear_ram) { (void)clear_ram; }
uint8_t snk_psychos_readmem(uint16_t address) { (void)address; return 0xff; }
void snk_psychos_writemem(uint16_t address, uint8_t data) { (void)address; (void)data; }
uint8_t snk_psychos_port_r(uint16_t port) { (void)port; return 0xff; }
void snk_psychos_port_w(uint16_t port, uint8_t data) { (void)port; (void)data; }
void snk_psychos_reset(void) { }
void snk_psychos_frame(uint32_t skip_render) { (void)skip_render; }
void snk_psychos_sound_reset(void) { }
void snk_psychos_sound_update(int16_t **buffer, int32_t length) { (void)buffer; (void)length; }
int snk_psychos_audio_mixer_gain_num(int headroom_db) { (void)headroom_db; return 384; }
uint32_t snk_psychos_state_size(void) { return 0; }
int snk_psychos_save_state(FILE *fd) { (void)fd; return 0; }
int snk_psychos_load_state(FILE *fd, uint32_t size) { (void)fd; (void)size; return 0; }

int taitol_alloc(void) { return 0; }
void taitol_free(void) { }
void taitol_clear_roms(void) { }
int taitol_set_region(int region, uint32_t offset, const uint8_t *data, uint32_t size)
{ (void)region; (void)offset; (void)data; (void)size; return 0; }
void taitol_set_game_variant(int variant) { (void)variant; }
void taitol_memory_map(int clear_ram) { (void)clear_ram; }
uint8_t taitol_readmem(uint16_t address) { (void)address; return 0xff; }
void taitol_writemem(uint16_t address, uint8_t data) { (void)address; (void)data; }
uint8_t taitol_port_r(uint16_t port) { (void)port; return 0xff; }
void taitol_port_w(uint16_t port, uint8_t data) { (void)port; (void)data; }
void taitol_reset(void) { }
void taitol_frame(uint32_t skip_render) { (void)skip_render; }
int32_t taitol_irq_callback(int32_t param) { (void)param; return 0xff; }
int taitol_needs_rotation(void) { return 0; }
void taitol_sound_reset(void) { }
void taitol_sound_update(int16_t **buffer, int32_t length) { (void)buffer; (void)length; }
int taitol_audio_mixer_gain_num(int headroom_db) { (void)headroom_db; return 384; }
uint32_t taitol_state_size(void) { return 0; }
int taitol_save_state(FILE *fd) { (void)fd; return 0; }
int taitol_load_state(FILE *fd, uint32_t size) { (void)fd; (void)size; return 0; }
