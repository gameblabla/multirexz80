/*
 * MultiRexZ80
 *
 * Default project license: GPL-2.0-or-later.
 *
 * Compact YM2203 (OPN) sound core for the Taito L-System driver.
 *
 * Register model and SSG behaviour follow the Yamaha YM2203 datasheet and
 * MAME's BSD-3-Clause YM2203 implementation (src/devices/sound/ymopn.cpp,
 * ay8910.cpp).  The SSG section is a full AY-3-8910 equivalent (3 tone
 * channels, noise, envelope, two I/O ports).  The FM section is a compact
 * 3-channel / 4-operator FM synthesizer following the OPN register map;
 * it is not bit-for-bit sample-accurate against ymfm but reproduces the
 * instruments, pitch and envelope shapes closely enough for gameplay.
 */

#ifndef TAITO_L_YM2203_H_
#define TAITO_L_YM2203_H_

#include <stdint.h>

typedef struct ym2203_state ym2203_state;

ym2203_state *ym2203_create(uint32_t clock, uint32_t sample_rate);
void          ym2203_destroy(ym2203_state *st);
void          ym2203_reset(ym2203_state *st);
void          ym2203_write(ym2203_state *st, uint32_t offset, uint8_t data);
uint8_t       ym2203_read(ym2203_state *st, uint32_t offset);
void          ym2203_update(ym2203_state *st, int16_t *left, int16_t *right, int32_t samples);

/* Save / load native state. */
uint32_t ym2203_state_size(ym2203_state *st);
int      ym2203_save_state(ym2203_state *st, void *data, uint32_t size);
int      ym2203_load_state(ym2203_state *st, const void *data, uint32_t size);

#endif /* TAITO_L_YM2203_H_ */
