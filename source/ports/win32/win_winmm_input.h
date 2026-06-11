/*
 * MultiRexZ80 native Windows frontend - WinMM joystick input (Win32 only).
 *
 * The Win32 (Win95-class) build reads game controllers through the classic
 * WinMM joystick API (joyGetPosEx), which needs no extra DLLs and works back to
 * Windows 95.  The Win64 build uses SDL3 instead; this whole module compiles to
 * nothing when _WIN64 is defined.
 *
 * The poll returns the raw joystick button bitmask (bit i == button i) plus,
 * via out_dir_actions, the directional actions (ACT_UP/DOWN/LEFT/RIGHT, bits
 * 0-3) from the analog axes / POV hat — matching win_sdl3_input so the frontend
 * maps raw buttons to actions through the user's remappable bindings.
 */
#ifndef MULTIREXZ80_WIN_WINMM_INPUT_H
#define MULTIREXZ80_WIN_WINMM_INPUT_H

#include <stdint.h>

typedef struct win_winmm_input win_winmm_input_t;

/* Create the WinMM joystick layer.  Always returns a handle on Win32 (joystick
 * presence is re-checked on every poll so hot-plugged pads work); returns NULL
 * on the Win64 build where this module is disabled. */
win_winmm_input_t *win_winmm_input_create(void);
void win_winmm_input_destroy(win_winmm_input_t *s);

/* Poll joystick 0.  Returns the raw button bitmask; *out_dir_actions receives
 * ACT_UP/DOWN/LEFT/RIGHT bits (0-3) from the axes/POV hat. */
uint32_t win_winmm_input_poll(win_winmm_input_t *s, uint32_t *out_dir_actions);

const char *win_winmm_input_status(const win_winmm_input_t *s);

#endif /* MULTIREXZ80_WIN_WINMM_INPUT_H */
