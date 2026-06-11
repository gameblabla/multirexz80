/*
 * MultiRexZ80 native Windows frontend - SDL3 joystick input (Win64 only).
 *
 * The Win64 build uses SDL3 for game-controller input because of SDL's gamepad
 * database and its handling of unusual controllers.  The Win32 (Win95-class)
 * build does not use SDL; this whole module compiles to nothing unless
 * MULTIREXZ80_HAVE_SDL3 is defined (set only for the Win64 target).
 *
 * The poll returns the raw joystick button bitmask (bit i == button i) plus,
 * via out_dir_actions, the directional actions (ACT_UP/DOWN/LEFT/RIGHT, bits
 * 0-3) derived from the hat/analog axes.  The frontend maps raw buttons to
 * actions through the user's remappable bindings.
 */
#ifndef MULTIREXZ80_WIN_SDL3_INPUT_H
#define MULTIREXZ80_WIN_SDL3_INPUT_H

#include <stdint.h>

typedef struct win_sdl3_input win_sdl3_input_t;

/* Create the SDL3 joystick layer and open the first available joystick.
 * Returns NULL if SDL3 could not be initialised (caller keeps keyboard only). */
win_sdl3_input_t *win_sdl3_input_create(void);
void win_sdl3_input_destroy(win_sdl3_input_t *s);

/* Pump SDL events and poll the joystick.  Returns the raw button bitmask;
 * *out_dir_actions receives ACT_UP/DOWN/LEFT/RIGHT bits (0-3) from hat/axes.
 * *quit_requested is set if SDL asked to quit. */
uint32_t win_sdl3_input_poll(win_sdl3_input_t *s, int *quit_requested, uint32_t *out_dir_actions);

/* One-shot frontend actions (save/load state) requested via the controller,
 * consumed (cleared) by this call.  Bit 0 = save state, bit 1 = load state. */
uint32_t win_sdl3_input_take_hotkeys(win_sdl3_input_t *s);

const char *win_sdl3_input_status(const win_sdl3_input_t *s);

#endif /* MULTIREXZ80_WIN_SDL3_INPUT_H */
