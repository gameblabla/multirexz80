/*
 * MultiRexZ80
 *
 * Multi-system Z80 emulator based on SMS Plus GX by Eke-Eke, itself based on
 * SMS Plus by Charles MacDonald.
 *
 * Default project license: GPL-2.0-or-later.
 */

#ifndef MULTIREXZ80_INPUT_SCRIPT_H_
#define MULTIREXZ80_INPUT_SCRIPT_H_

#include <stdint.h>
#include <stdio.h>
#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MULTIREXZ80_INPUT_TAP_FRAMES_DEFAULT 6u

typedef struct multirexz80_input_script multirexz80_input_script_t;
typedef struct multirexz80_input_recorder multirexz80_input_recorder_t;

int multirexz80_input_script_load(multirexz80_input_script_t **out, const char *path, uint32_t default_tap_frames);
void multirexz80_input_script_free(multirexz80_input_script_t *script);
void multirexz80_input_script_reset(multirexz80_input_script_t *script);
void multirexz80_input_script_apply_frame(multirexz80_input_script_t *script, uint64_t frame, input_t *dst);
int multirexz80_input_script_active(const multirexz80_input_script_t *script);
uint32_t multirexz80_input_script_event_count(const multirexz80_input_script_t *script);

int multirexz80_input_recorder_open(multirexz80_input_recorder_t **out, const char *path, uint8_t compact);
void multirexz80_input_recorder_close(multirexz80_input_recorder_t *recorder);
void multirexz80_input_recorder_write_action(multirexz80_input_recorder_t *recorder, uint64_t frame, const char *name, int pressed);
void multirexz80_input_recorder_write_state_changes(multirexz80_input_recorder_t *recorder, uint64_t frame, const input_t *state);

#ifdef __cplusplus
}
#endif

#endif /* MULTIREXZ80_INPUT_SCRIPT_H_ */
