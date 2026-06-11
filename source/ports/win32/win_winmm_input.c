#include "win_winmm_input.h"

#ifndef _WIN64  /* Win32 (Win95-class) build only; Win64 uses SDL3 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Action bit positions must match action_id_t in multirexz80_win32.c:
 *   UP=0 DOWN=1 LEFT=2 RIGHT=3 BUTTON1=4 BUTTON2=5 PAUSE_BUTTON=6
 *   COIN1=7 START1=8 COIN2=9 START2=10 M5_1=11 M5_2=12
 */
enum { A_UP = 0, A_DOWN, A_LEFT, A_RIGHT, A_BUTTON1, A_BUTTON2, A_PAUSE,
       A_COIN1, A_START1, A_COIN2, A_START2 };
#define BIT(n) (1u << (n))

struct win_winmm_input {
    int present;            /* a joystick responded on the last poll */
    int have_caps;
    JOYCAPSA caps;
    char status[96];
};

win_winmm_input_t *win_winmm_input_create(void) {
    win_winmm_input_t *s = (win_winmm_input_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (joyGetNumDevs() == 0)
        snprintf(s->status, sizeof(s->status), "WinMM: no joystick driver");
    else
        snprintf(s->status, sizeof(s->status), "WinMM: ready");
    return s;
}

void win_winmm_input_destroy(win_winmm_input_t *s) { free(s); }

/* Map one analog axis to a -1/0/+1 direction using the device's reported range
 * with a 50%-deflection threshold (and a little hysteresis isn't needed here
 * because WinMM axes are already smoothed/centered by the driver). */
static int axis_dir(unsigned value, unsigned vmin, unsigned vmax) {
    unsigned span, lo, hi;
    if (vmax <= vmin) return 0;
    span = vmax - vmin;
    lo = vmin + span / 4u;        /* below 25% -> negative */
    hi = vmax - span / 4u;        /* above 75% -> positive */
    if (value <= lo) return -1;
    if (value >= hi) return 1;
    return 0;
}

uint32_t win_winmm_input_poll(win_winmm_input_t *s, uint32_t *out_dir_actions) {
    JOYINFOEX jx;
    uint32_t dirs = 0;
    uint32_t buttons = 0;
    DWORD btns;
    int i;
    if (out_dir_actions) *out_dir_actions = 0;
    if (!s) return 0;

    /* Refresh capabilities lazily; cheap and lets a pad plugged in after start
     * begin working without a restart. */
    if (!s->have_caps) {
        if (joyGetDevCapsA(JOYSTICKID1, &s->caps, sizeof(s->caps)) == JOYERR_NOERROR)
            s->have_caps = 1;
    }

    memset(&jx, 0, sizeof(jx));
    jx.dwSize = sizeof(jx);
    jx.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNPOV | JOY_RETURNBUTTONS;
    if (joyGetPosEx(JOYSTICKID1, &jx) != JOYERR_NOERROR) {
        if (s->present) { s->present = 0; s->have_caps = 0; snprintf(s->status, sizeof(s->status), "WinMM: no joystick"); }
        return 0;
    }
    if (!s->present) { s->present = 1; snprintf(s->status, sizeof(s->status), "WinMM: %s", s->have_caps && s->caps.szPname[0] ? s->caps.szPname : "joystick"); }

    /* Directions from the main analog axes. */
    if (s->have_caps) {
        int dx = axis_dir(jx.dwXpos, s->caps.wXmin, s->caps.wXmax);
        int dy = axis_dir(jx.dwYpos, s->caps.wYmin, s->caps.wYmax);
        if (dx < 0) dirs |= BIT(A_LEFT);
        if (dx > 0) dirs |= BIT(A_RIGHT);
        if (dy < 0) dirs |= BIT(A_UP);
        if (dy > 0) dirs |= BIT(A_DOWN);
    }

    /* Directions from the POV hat (0=up, 9000=right, 18000=down, 27000=left;
     * 0xFFFF means centered). */
    if (s->have_caps && (s->caps.wCaps & JOYCAPS_HASPOV) && jx.dwPOV != JOY_POVCENTERED && jx.dwPOV != 0xFFFFu) {
        DWORD p = jx.dwPOV;
        if (p > 27000u || p < 9000u)   dirs |= BIT(A_UP);
        if (p > 0u && p < 18000u)      dirs |= BIT(A_RIGHT);
        if (p > 9000u && p < 27000u)   dirs |= BIT(A_DOWN);
        if (p > 18000u)                dirs |= BIT(A_LEFT);
    }

    /* Raw buttons: WinMM JOY_BUTTONn maps to button index n-1. */
    btns = jx.dwButtons;
    for (i = 0; i < 32; ++i)
        if (btns & (1u << i)) buttons |= (1u << i);

    if (out_dir_actions) *out_dir_actions = dirs;
    return buttons;
}

const char *win_winmm_input_status(const win_winmm_input_t *s) { return s ? s->status : "WinMM: disabled"; }

#else /* _WIN64 : Win64 build uses SDL3, not WinMM */

typedef int win_winmm_input_translation_unit_not_empty;

#endif /* !_WIN64 */
