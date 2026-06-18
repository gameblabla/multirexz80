/*
 * MultiRexZ80 native Win32 frontend.
 *
 * This port intentionally does not depend on SDL or Qt.  The window/menu/audio
 * structure is adapted from the GP32emu Win32 frontend supplied with this task,
 * with GP32-specific code removed and MultiRexZ80 core integration added.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shared.h"
#include "input_script.h"
#include "win_d3d11.h"
#include "win_sdl3_input.h"
#include "win_winmm_input.h"

#define APP_TITLE "MultiRexZ80"
#define IDI_MULTIREXZ80 101
#define WIN32_BITMAP_W 400u
#define WIN32_BITMAP_H 400u
#define WIN32_AUDIO_BUFFERS 4u
#define WIN32_AUDIO_FRAMES_MAX 1024u
#define WIN32_REWIND_MAX 600u
#define WIN32_REWIND_INTERVAL 6u
#define WIN32_REWIND_STEP 3u

/* File menu */
#define IDM_FILE_OPEN_ROM          1001
#define IDM_FILE_SAVE_STATE        1002
#define IDM_FILE_LOAD_STATE        1003
#define IDM_FILE_SCREENSHOT        1004
#define IDM_FILE_INPUT_RECORD      1005
#define IDM_FILE_INPUT_STOP_RECORD 1006
#define IDM_FILE_INPUT_PLAY        1007
#define IDM_FILE_INPUT_STOP_PLAY   1008
#define IDM_FILE_EXIT              1009
/* Emulation */
#define IDM_EMU_PAUSE              1101
#define IDM_EMU_RESET              1102
#define IDM_EMU_FAST_FORWARD       1103
#define IDM_EMU_REWIND             1104
#define IDM_EMU_SOFT_RESET         1105
/* Video */
#define IDM_VIDEO_KEEP_ASPECT      1201
#define IDM_VIDEO_INTEGER          1202
#define IDM_VIDEO_STRETCH          1203
#define IDM_VIDEO_FULLSCREEN       1204
#define IDM_VIDEO_LIGHTGUN_CURSOR  1205
#define IDM_VIDEO_D3D11            1206
#define IDM_VIDEO_MODE_BASE        1230
/* Machine */
#define IDM_CONSOLE_BASE           1300
/* BIOS */
#define IDM_BIOS_DIALOG            1401
#define IDM_BIOS_SET_SMS           1402
#define IDM_BIOS_CLEAR_SMS         1403
#define IDM_BIOS_SET_COLECO        1404
#define IDM_BIOS_CLEAR_COLECO      1405
#define IDM_BIOS_SET_M5            1406
#define IDM_BIOS_CLEAR_M5          1407
/* State slots */
#define IDM_STATE_SLOT_BASE        1500
#define IDM_STATE_SAVE_SELECTED    1510
#define IDM_STATE_LOAD_SELECTED    1511
/* Help */
#define IDM_HELP_ABOUT             1900
/* Controls */
#define IDM_CTRL_C1_KEYBOARD       1600
#define IDM_CTRL_C1_GAMEPAD        1601
#define IDM_CTRL_C2_KEYBOARD       1602
#define IDM_CTRL_C2_GAMEPAD        1603
#define IDM_CTRL_RESET             1680
#define IDM_HOTKEY_BASE            1700
#define IDM_HOTKEY_RESET           1780
#define IDT_EMU_PUMP               2001
#define WM_APP_FRAME_READY          (WM_APP + 1)
#define WM_APP_LOAD_ROM             (WM_APP + 2)

extern t_config option;
t_config option;

static void *g_pixels;
static char g_sram_path[MAX_PATH];

typedef enum video_mode_choice { VMODE_AUTO = 0, VMODE_PAL, VMODE_NTSC } video_mode_choice_t;
typedef enum console_choice {
    C_AUTO = 0, C_COLECO, C_SG1000, C_SORDM5, C_SMS1_JP, C_SMS1_EXPORT,
    C_SMS2, C_GG, C_GGMS, C_SYSTEME, C_SYSTEM1, C_SNK, C_TAITOL
} console_choice_t;

typedef enum action_id {
    /* Keep the original Controller 1 action order for old INI compatibility. */
    ACT_UP = 0, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_BUTTON1, ACT_BUTTON2,
    ACT_PAUSE_BUTTON, ACT_COIN1, ACT_START1, ACT_COIN2, ACT_START2,
    ACT_M5_1, ACT_M5_2,
    ACT_KP0, ACT_KP1, ACT_KP2, ACT_KP3, ACT_KP4, ACT_KP5, ACT_KP6, ACT_KP7, ACT_KP8, ACT_KP9,
    ACT_KP_STAR, ACT_KP_HASH,

    /* Controller 2. */
    ACT_P2_UP, ACT_P2_DOWN, ACT_P2_LEFT, ACT_P2_RIGHT, ACT_P2_BUTTON1, ACT_P2_BUTTON2,
    ACT_P2_KP0, ACT_P2_KP1, ACT_P2_KP2, ACT_P2_KP3, ACT_P2_KP4, ACT_P2_KP5,
    ACT_P2_KP6, ACT_P2_KP7, ACT_P2_KP8, ACT_P2_KP9, ACT_P2_KP_STAR, ACT_P2_KP_HASH,
    ACT_ROTATE_LEFT, ACT_ROTATE_RIGHT, ACT_AIM_UP, ACT_AIM_DOWN, ACT_AIM_LEFT, ACT_AIM_RIGHT,
    ACT_P2_ROTATE_LEFT, ACT_P2_ROTATE_RIGHT, ACT_P2_AIM_UP, ACT_P2_AIM_DOWN, ACT_P2_AIM_LEFT, ACT_P2_AIM_RIGHT,
    ACT_COUNT
} action_id_t;
#define ACT_BIT(n)              (1ull << (unsigned)(n))
#define ACT_PAD_COUNT           6
#define ACT_P1_PAD_FIRST        ACT_UP
#define ACT_P2_PAD_FIRST        ACT_P2_UP
#define ACT_KP_FIRST            ACT_KP0
#define ACT_P1_KP_FIRST         ACT_KP0
#define ACT_P2_KP_FIRST         ACT_P2_KP0
#define ACT_KP_COUNT            12
#define JOY_BIND_BUTTON_MAX     32
#define JOY_BIND_DIR_UP         32
#define JOY_BIND_DIR_DOWN       33
#define JOY_BIND_DIR_LEFT       34
#define JOY_BIND_DIR_RIGHT      35
#define JOY_BIND_COUNT          36

typedef enum hotkey_id {
    HK_PAUSE = 0, HK_FAST_FORWARD, HK_REWIND, HK_SAVE_STATE, HK_LOAD_STATE,
    HK_SCREENSHOT, HK_RESET, HK_FULLSCREEN, HK_COUNT
} hotkey_id_t;

/* A control can be driven by a keyboard virtual-key and/or a joystick button.
 * joy == -1 means "no joystick button bound". */
typedef struct key_binding { const char *name; UINT vk; int joy; } key_binding_t;
typedef struct console_item { const char *label; uint8_t option_console; uint8_t auto_country; } console_item_t;

typedef struct rewind_snapshot {
    uint8_t *data;
    uint32_t size;
    uint64_t frame;
} rewind_snapshot_t;

typedef struct win32_audio {
    HWAVEOUT wave;
    WAVEHDR hdr[WIN32_AUDIO_BUFFERS];
    int16_t *buf[WIN32_AUDIO_BUFFERS];
    int next;
    int enabled;
} win32_audio_t;

typedef struct app_state {
    HINSTANCE inst;
    HWND hwnd;
    HMENU menu;
    HACCEL accel;
    HANDLE emu_thread;
    DWORD ui_thread_id;
    volatile LONG emu_thread_stop;
    volatile LONG repaint_pending;
    volatile LONG pending_load;
    CRITICAL_SECTION core_lock;
    CRITICAL_SECTION present_lock;
    int locks_ready;
    int quit;
    int loading;
    int running;
    int fast_forward;
    int rewind_held;
    int fullscreen;
    int keep_aspect;
    int integer_scaling;
    int stretch;
    int lightgun_cursor;
    int mouse_captured;
    int rom_loaded;
    int rom_is_lightgun;
    int selected_console;
    int selected_video_mode;
    int save_slot;
    uint64_t frame;
    LARGE_INTEGER qpf;
    LARGE_INTEGER last_tick;
    uint64_t accum_us;
    char rom_path[MAX_PATH];
    char pending_load_path[MAX_PATH];
    char sms_bios_path[MAX_PATH];
    char coleco_bios_path[MAX_PATH];
    char m5_bios_path[MAX_PATH];
    char config_path[MAX_PATH];
    char state_dir[MAX_PATH];
    char save_dir[MAX_PATH];
    char last_screenshot_dir[MAX_PATH];
    char status[512];
    key_binding_t controls[ACT_COUNT];
    key_binding_t hotkeys[HK_COUNT];
    win32_audio_t audio;
    uint32_t *present_pixels;
    uint32_t present_w;
    uint32_t present_h;
    /* Persistent GDI double-buffer.  Painting into an off-screen DIB and doing a
     * single BitBlt to the window eliminates the resize flicker and avoids the
     * redundant full-window clears that made CPU scale with window resolution. */
    HDC back_dc;
    HBITMAP back_bmp;
    HGDIOBJ back_old_bmp;
    int back_w;
    int back_h;
    /* Renderer selection.  use_d3d11 is the user preference (Win64 only); the
     * GDI path above is always available as a fallback. */
    int use_d3d11;
#ifdef _WIN64
    win_d3d11_t *d3d;
#endif
    /* Input is merged each frame from keyboard-held actions and (Win64) the SDL3
     * joystick, both as action_id_t bitmasks, so the two sources never fight
     * over the emulator input bits. */
    uint64_t kbd_actions;
    uint64_t joy_actions;
#ifdef MULTIREXZ80_HAVE_SDL3
    win_sdl3_input_t *sdl_input;
#endif
#ifndef _WIN64
    win_winmm_input_t *winmm_input;
#endif
    rewind_snapshot_t rewind[WIN32_REWIND_MAX];
    size_t rewind_count;
    size_t rewind_head;
    uint32_t rewind_tick;
    multirexz80_input_script_t *playback;
    multirexz80_input_recorder_t *recorder;
} app_state_t;

static app_state_t *g_app;

static void app_reset_timing(app_state_t *a, int run_immediately);
static void update_present_pixels(app_state_t *a);
static DWORD WINAPI emu_thread_proc(LPVOID user);
static int app_start_emu_thread(app_state_t *a);
static void app_stop_emu_thread(app_state_t *a);
static void app_request_repaint(app_state_t *a);
static void app_force_video_refresh(app_state_t *a);
static void app_queue_load_game(app_state_t *a, const char *path);
static void app_lock_core(app_state_t *a);
static void app_unlock_core(app_state_t *a);
static void app_lock_present(app_state_t *a);
static void app_unlock_present(app_state_t *a);
static void app_rebuild_menu(app_state_t *a);
static void default_hotkeys(app_state_t *a);

static const console_item_t k_console_items[] = {
    {"Auto (extension/CRC/ZIP)", 0, 0},
    {"ColecoVision", 6, 0},
    {"SG-1000", 5, 0},
    {"Sord M5", 7, 0},
    {"Master System 1 JP", 1, 3},
    {"Master System 1 EU/US", 1, 0},
    {"Master System 2", 2, 0},
    {"Game Gear", 3, 0},
    {"Game Gear SMS compatibility", 4, 0},
    {"Sega System E", 8, 0},
    {"Sega System 1/2", 9, 0},
    {"SNK Ikari/Psychos", 10, 0},
    {"Taito L", 11, 0},
};
static const char *k_video_modes[] = { "Auto", "PAL 50 Hz", "NTSC 60 Hz" };
static const char *k_hotkey_names[] = { "Pause", "Fast forward", "Rewind", "Save state", "Load state", "Screenshot", "Reset", "Fullscreen" };

static const char *basename_a(const char *p) {
    const char *b = p ? p : "";
    if (!p) return "";
    while (*p) { if (*p == '\\' || *p == '/') b = p + 1; ++p; }
    return b;
}

static int ascii_tolower(int c) { return (c >= 'A' && c <= 'Z') ? (c + 32) : c; }
static int str_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (ascii_tolower((unsigned char)*a++) != ascii_tolower((unsigned char)*b++)) return 0; }
    return *a == *b;
}
static const char *ext_of(const char *path) {
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot ? dot : "";
}

static void ensure_dir(const char *path) {
    char tmp[MAX_PATH];
    char *p;
    if (!path || !path[0]) return;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp; *p; ++p) {
        if ((*p == '\\' || *p == '/') && p != tmp && p[-1] != ':') {
            char c = *p; *p = 0; CreateDirectoryA(tmp, NULL); *p = c;
        }
    }
    CreateDirectoryA(tmp, NULL);
}

static void path_dirname(const char *path, char *out, size_t out_sz) {
    const char *s1; const char *s2; const char *s;
    if (!out || out_sz == 0) return;
    out[0] = 0;
    if (!path || !path[0]) return;
    s1 = strrchr(path, '\\'); s2 = strrchr(path, '/');
    s = (s2 && (!s1 || s2 > s1)) ? s2 : s1;
    if (!s) return;
    if ((size_t)(s - path + 1) >= out_sz) return;
    memcpy(out, path, (size_t)(s - path + 1)); out[s - path + 1] = 0;
}

static void sanitize_component(const char *in, char *out, size_t out_sz) {
    size_t j = 0;
    if (!out || out_sz == 0) return;
    if (!in || !*in) in = "cart";
    for (size_t i = 0; in[i] && j + 1 < out_sz; ++i) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out[j++] = (char)c;
        else out[j++] = '_';
    }
    out[j] = 0;
}

static void app_lock_core(app_state_t *a) { if (a && a->locks_ready) EnterCriticalSection(&a->core_lock); }
static void app_unlock_core(app_state_t *a) { if (a && a->locks_ready) LeaveCriticalSection(&a->core_lock); }
static void app_lock_present(app_state_t *a) { if (a && a->locks_ready) EnterCriticalSection(&a->present_lock); }
static void app_unlock_present(app_state_t *a) { if (a && a->locks_ready) LeaveCriticalSection(&a->present_lock); }

static void app_set_status(app_state_t *a, const char *msg) {
    if (!a) return;
    snprintf(a->status, sizeof(a->status), "%s", msg ? msg : "");
    if (a->hwnd) InvalidateRect(a->hwnd, NULL, FALSE);
}

static void app_request_repaint(app_state_t *a) {
    if (!a || !a->hwnd) return;

    /* Post at most one frame-ready message per completed WM_PAINT.  The old
     * Win32 path cleared repaint_pending in WM_APP_FRAME_READY, before the
     * invalidated client area had actually painted.  Heavy arcade drivers then
     * generated a tight stream of WM_APP_FRAME_READY + WM_PAINT traffic from
     * the UI pump; menu/non-client work could be starved badly enough that the
     * menu bar appeared to disappear after loading a ZIP arcade set.
     *
     * Keep repaint_pending set until WM_PAINT finishes.  This makes repaint
     * scheduling edge-triggered and coalesced like SDL's present path: the core
     * may run more frames, but Win32 only has one outstanding paint request. */
    if (InterlockedCompareExchange(&a->repaint_pending, 1, 0) == 0)
        PostMessageA(a->hwnd, WM_APP_FRAME_READY, 0, 0);
}

static void app_queue_load_game(app_state_t *a, const char *path) {
    if (!a || !a->hwnd || !path || !path[0]) return;
    snprintf(a->pending_load_path, sizeof(a->pending_load_path), "%s", path);
    if (InterlockedExchange(&a->pending_load, 1) == 0)
        PostMessageA(a->hwnd, WM_APP_LOAD_ROM, 0, 0);
}

static void app_error(app_state_t *a, const char *msg) {
    app_set_status(a, msg);
    MessageBoxA(a ? a->hwnd : NULL, msg ? msg : "Error", APP_TITLE, MB_OK | MB_ICONERROR);
}

static const char *runtime_console_name(uint8_t console) {
    switch (console) {
    case CONSOLE_SMS: return "Master System 1";
    case CONSOLE_SMS2: return "Master System 2";
    case CONSOLE_GG: return "Game Gear";
    case CONSOLE_GGMS: return "Game Gear SMS compatibility";
    case CONSOLE_SG1000: return "SG-1000";
    case CONSOLE_SC3000: return "SC-3000";
    case CONSOLE_SF7000: return "SF-7000";
    case CONSOLE_COLECO: return "ColecoVision";
    case CONSOLE_SORDM5: return "Sord M5";
    case CONSOLE_SYSTEME: return "Sega System E";
    case CONSOLE_SYSTEM1: return "Sega System 1/2";
    case CONSOLE_SNKPSYCHOS: return "SNK Ikari/Psychos";
    case CONSOLE_TAITOL: return "Taito L";
    default: return "Unknown";
    }
}

static void app_update_title(app_state_t *a) {
    char title[768];
    if (!a || !a->hwnd) return;
    if (a->rom_loaded) {
        snprintf(title, sizeof(title), "%s - %s [%s, %s]", APP_TITLE, basename_a(a->rom_path), runtime_console_name(sms.console), sms.display == DISPLAY_PAL ? "PAL" : "NTSC");
    } else snprintf(title, sizeof(title), "%s", APP_TITLE);
    SetWindowTextA(a->hwnd, title);
}

static void defaults(void) {
    memset(&option, 0, sizeof(option));
    option.fullspeed = 1;
    option.fm = 1;
    option.spritelimit = 1;
    option.tms_pal = 2;
    option.soundlevel = 1;
    option.use_bios = 1;
    option.lcd_persistence = 1;
    option.lightgun_cursor = 1;
    option.lightgun_dpad_speed = 3;
    option.audio_dc_blocker = 0;
    option.audio_highpass_hz = 220;
    option.audio_lowpass_hz = 5000;
    option.audio_limiter = 0;
    option.audio_headroom_db = 0;
}

static void default_controls(app_state_t *a) {
    static const key_binding_t c[ACT_COUNT] = {
        {"Controller 1 Up", VK_UP, JOY_BIND_DIR_UP},
        {"Controller 1 Down", VK_DOWN, JOY_BIND_DIR_DOWN},
        {"Controller 1 Left", VK_LEFT, JOY_BIND_DIR_LEFT},
        {"Controller 1 Right", VK_RIGHT, JOY_BIND_DIR_RIGHT},
        {"Controller 1 Button 1", 'Z', 0},
        {"Controller 1 Button 2", 'X', 1},
        {"Controller 1 Pause/Start", VK_RETURN, 7},
        {"Arcade Coin 1", '5', 6},
        {"Arcade Start 1", '1', 7},
        {"Arcade Coin 2", '6', -1},
        {"Arcade Start 2", '2', -1},
        {"Sord M5 1", '1', -1},
        {"Sord M5 2", '2', -1},
        {"Controller 1 Keypad 0", VK_NUMPAD0, -1},
        {"Controller 1 Keypad 1", VK_NUMPAD1, -1},
        {"Controller 1 Keypad 2", VK_NUMPAD2, -1},
        {"Controller 1 Keypad 3", VK_NUMPAD3, -1},
        {"Controller 1 Keypad 4", VK_NUMPAD4, -1},
        {"Controller 1 Keypad 5", VK_NUMPAD5, -1},
        {"Controller 1 Keypad 6", VK_NUMPAD6, -1},
        {"Controller 1 Keypad 7", VK_NUMPAD7, -1},
        {"Controller 1 Keypad 8", VK_NUMPAD8, -1},
        {"Controller 1 Keypad 9", VK_NUMPAD9, -1},
        {"Controller 1 Keypad *", VK_MULTIPLY, -1},
        {"Controller 1 Keypad #", VK_DIVIDE, -1},
        {"Controller 2 Up", 'I', -1},
        {"Controller 2 Down", 'K', -1},
        {"Controller 2 Left", 'J', -1},
        {"Controller 2 Right", 'L', -1},
        {"Controller 2 Button 1", 'N', -1},
        {"Controller 2 Button 2", 'M', -1},
        {"Controller 2 Keypad 0", 0, -1},
        {"Controller 2 Keypad 1", 0, -1},
        {"Controller 2 Keypad 2", 0, -1},
        {"Controller 2 Keypad 3", 0, -1},
        {"Controller 2 Keypad 4", 0, -1},
        {"Controller 2 Keypad 5", 0, -1},
        {"Controller 2 Keypad 6", 0, -1},
        {"Controller 2 Keypad 7", 0, -1},
        {"Controller 2 Keypad 8", 0, -1},
        {"Controller 2 Keypad 9", 0, -1},
        {"Controller 2 Keypad *", 0, -1},
        {"Controller 2 Keypad #", 0, -1},
        {"Controller 1 Rotate Left", 'Q', 4},
        {"Controller 1 Rotate Right", 'E', 5},
        {"Controller 1 Aim Up", 'W', JOY_BIND_DIR_UP},
        {"Controller 1 Aim Down", 'S', JOY_BIND_DIR_DOWN},
        {"Controller 1 Aim Left", 'A', JOY_BIND_DIR_LEFT},
        {"Controller 1 Aim Right", 'D', JOY_BIND_DIR_RIGHT},
        {"Controller 2 Rotate Left", 'U', -1},
        {"Controller 2 Rotate Right", 'O', -1},
        {"Controller 2 Aim Up", 0, -1},
        {"Controller 2 Aim Down", 0, -1},
        {"Controller 2 Aim Left", 0, -1},
        {"Controller 2 Aim Right", 0, -1}
    };
    for (int i = 0; i < ACT_COUNT; ++i) a->controls[i] = c[i];
}

static void default_hotkeys(app_state_t *a) {
    static const UINT h[HK_COUNT] = { 'P', VK_TAB, VK_F6, VK_F5, VK_F8, VK_F12, VK_F1, VK_F11 };
    if (!a) return;
    for (int i = 0; i < HK_COUNT; ++i) { a->hotkeys[i].name = k_hotkey_names[i]; a->hotkeys[i].vk = h[i]; a->hotkeys[i].joy = -1; }
}

static void default_bindings(app_state_t *a) {
    if (!a) return;
    default_controls(a);
    default_hotkeys(a);
}

static void app_sanitize_video_scaling(app_state_t *a) {
    if (!a) return;
    /* The three scaling policies are intentionally exclusive.  Older INI files
     * could have KeepAspect and IntegerScaling both set; resolve that to
     * integer scaling because it is the more specific pixel-grid mode. */
    if (a->stretch) {
        a->keep_aspect = 0;
        a->integer_scaling = 0;
    } else if (a->integer_scaling) {
        a->keep_aspect = 0;
    } else if (!a->keep_aspect) {
        a->keep_aspect = 1;
    }
}

static void app_make_paths(app_state_t *a) {
    char module[MAX_PATH]; char dir[MAX_PATH];
    GetModuleFileNameA(NULL, module, sizeof(module));
    path_dirname(module, dir, sizeof(dir));
    snprintf(a->config_path, sizeof(a->config_path), "%sMultiRexZ80.ini", dir[0] ? dir : "");
    snprintf(a->state_dir, sizeof(a->state_dir), "%sstates", dir[0] ? dir : "");
    snprintf(a->save_dir, sizeof(a->save_dir), "%ssaves", dir[0] ? dir : "");
    snprintf(a->last_screenshot_dir, sizeof(a->last_screenshot_dir), "%s", dir[0] ? dir : ".\\");
    ensure_dir(a->state_dir); ensure_dir(a->save_dir);
}

static void load_config(app_state_t *a) {
    char tmp[32], key[64];
    if (!a) return;
    GetPrivateProfileStringA("Paths", "SMSBIOS", "", a->sms_bios_path, sizeof(a->sms_bios_path), a->config_path);
    GetPrivateProfileStringA("Paths", "ColecoBIOS", "", a->coleco_bios_path, sizeof(a->coleco_bios_path), a->config_path);
    GetPrivateProfileStringA("Paths", "SordM5BIOS", "", a->m5_bios_path, sizeof(a->m5_bios_path), a->config_path);
    a->selected_console = GetPrivateProfileIntA("Machine", "Console", a->selected_console, a->config_path);
    a->selected_video_mode = GetPrivateProfileIntA("Machine", "VideoMode", a->selected_video_mode, a->config_path);
    a->keep_aspect = GetPrivateProfileIntA("Video", "KeepAspect", a->keep_aspect, a->config_path) ? 1 : 0;
    a->integer_scaling = GetPrivateProfileIntA("Video", "IntegerScaling", a->integer_scaling, a->config_path) ? 1 : 0;
    a->stretch = GetPrivateProfileIntA("Video", "Stretch", a->stretch, a->config_path) ? 1 : 0;
    a->lightgun_cursor = GetPrivateProfileIntA("Video", "LightgunCursor", a->lightgun_cursor, a->config_path) ? 1 : 0;
    a->use_d3d11 = GetPrivateProfileIntA("Video", "D3D11", a->use_d3d11, a->config_path) ? 1 : 0;
    a->save_slot = GetPrivateProfileIntA("States", "Slot", a->save_slot, a->config_path);
    if (a->save_slot < 0 || a->save_slot > 9) a->save_slot = 0;
    app_sanitize_video_scaling(a);
    for (int i = 0; i < ACT_COUNT; ++i) {
        snprintf(key, sizeof(key), "Control%d", i);
        snprintf(tmp, sizeof(tmp), "%u", a->controls[i].vk);
        GetPrivateProfileStringA("Controls", key, tmp, tmp, sizeof(tmp), a->config_path);
        a->controls[i].vk = (UINT)strtoul(tmp, NULL, 0);
        snprintf(key, sizeof(key), "ControlJoy%d", i);
        snprintf(tmp, sizeof(tmp), "%d", a->controls[i].joy);
        GetPrivateProfileStringA("Controls", key, tmp, tmp, sizeof(tmp), a->config_path);
        a->controls[i].joy = (int)strtol(tmp, NULL, 0);
    }
    for (int i = 0; i < HK_COUNT; ++i) {
        snprintf(key, sizeof(key), "Hotkey%d", i);
        snprintf(tmp, sizeof(tmp), "%u", a->hotkeys[i].vk);
        GetPrivateProfileStringA("Hotkeys", key, tmp, tmp, sizeof(tmp), a->config_path);
        a->hotkeys[i].vk = (UINT)strtoul(tmp, NULL, 0);
    }
}

static void save_config(app_state_t *a) {
    char tmp[32], key[64];
    if (!a) return;
    WritePrivateProfileStringA("Paths", "SMSBIOS", a->sms_bios_path[0] ? a->sms_bios_path : NULL, a->config_path);
    WritePrivateProfileStringA("Paths", "ColecoBIOS", a->coleco_bios_path[0] ? a->coleco_bios_path : NULL, a->config_path);
    WritePrivateProfileStringA("Paths", "SordM5BIOS", a->m5_bios_path[0] ? a->m5_bios_path : NULL, a->config_path);
    snprintf(tmp, sizeof(tmp), "%d", a->selected_console); WritePrivateProfileStringA("Machine", "Console", tmp, a->config_path);
    snprintf(tmp, sizeof(tmp), "%d", a->selected_video_mode); WritePrivateProfileStringA("Machine", "VideoMode", tmp, a->config_path);
    WritePrivateProfileStringA("Video", "KeepAspect", a->keep_aspect ? "1" : "0", a->config_path);
    WritePrivateProfileStringA("Video", "IntegerScaling", a->integer_scaling ? "1" : "0", a->config_path);
    WritePrivateProfileStringA("Video", "Stretch", a->stretch ? "1" : "0", a->config_path);
    WritePrivateProfileStringA("Video", "LightgunCursor", a->lightgun_cursor ? "1" : "0", a->config_path);
    WritePrivateProfileStringA("Video", "D3D11", a->use_d3d11 ? "1" : "0", a->config_path);
    snprintf(tmp, sizeof(tmp), "%d", a->save_slot); WritePrivateProfileStringA("States", "Slot", tmp, a->config_path);
    for (int i = 0; i < ACT_COUNT; ++i) {
        snprintf(key, sizeof(key), "Control%d", i); snprintf(tmp, sizeof(tmp), "%u", a->controls[i].vk); WritePrivateProfileStringA("Controls", key, tmp, a->config_path);
        snprintf(key, sizeof(key), "ControlJoy%d", i); snprintf(tmp, sizeof(tmp), "%d", a->controls[i].joy); WritePrivateProfileStringA("Controls", key, tmp, a->config_path);
    }
    for (int i = 0; i < HK_COUNT; ++i) { snprintf(key, sizeof(key), "Hotkey%d", i); snprintf(tmp, sizeof(tmp), "%u", a->hotkeys[i].vk); WritePrivateProfileStringA("Hotkeys", key, tmp, a->config_path); }
}

static int select_open_file(HWND hwnd, const char *title, const char *filter, char *path, DWORD path_sz) {
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    if (path && path_sz) path[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = path_sz;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? 1 : 0;
}
static int select_save_file(HWND hwnd, const char *title, const char *filter, char *path, DWORD path_sz) {
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    if (path && path_sz) path[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = path_sz;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) ? 1 : 0;
}

static int load_exact(const char *path, uint8_t *dst, size_t dst_size, size_t min_size, size_t *actual) {
    FILE *fp; long sz;
    if (actual) *actual = 0;
    if (!path || !path[0] || !dst) return 0;
    fp = fopen(path, "rb"); if (!fp) return 0;
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < (long)min_size || (size_t)sz > dst_size) { fclose(fp); return 0; }
    memset(dst, 0, dst_size);
    if (fread(dst, 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); return 0; }
    fclose(fp); if (actual) *actual = (size_t)sz; return 1;
}

static void append_candidate(char candidates[][MAX_PATH], size_t *n, const char *dir, const char *name) {
    if (!dir || !dir[0] || !name || !name[0] || *n >= 16) return;
    snprintf(candidates[*n], MAX_PATH, "%s%s%s", dir, (dir[strlen(dir)-1] == '\\' || dir[strlen(dir)-1] == '/') ? "" : "\\", name);
    (*n)++;
}
static int find_local_bios(const char *rom_path, const char **names, size_t name_count, char *out, size_t out_sz) {
    char module[MAX_PATH], exe_dir[MAX_PATH], rom_dir[MAX_PATH];
    char candidates[16][MAX_PATH]; size_t n = 0;
    GetModuleFileNameA(NULL, module, sizeof(module)); path_dirname(module, exe_dir, sizeof(exe_dir)); path_dirname(rom_path, rom_dir, sizeof(rom_dir));
    for (size_t i = 0; i < name_count; ++i) { append_candidate(candidates, &n, exe_dir, names[i]); append_candidate(candidates, &n, rom_dir, names[i]); }
    for (size_t i = 0; i < n; ++i) { FILE *fp = fopen(candidates[i], "rb"); if (fp) { fclose(fp); snprintf(out, out_sz, "%s", candidates[i]); return 1; } }
    if (out_sz) out[0] = 0; return 0;
}

static int init_bitmap(void) {
    if (!g_pixels) g_pixels = calloc((size_t)WIN32_BITMAP_W * WIN32_BITMAP_H, MULTIREXZ80_RENDER_BYTES_PER_PIXEL);
    if (!g_pixels) return 0;
    bitmap.width = WIN32_BITMAP_W;
    bitmap.height = WIN32_BITMAP_H;
    bitmap.depth = MULTIREXZ80_RENDER_DEPTH;
    bitmap.data = (uint8_t *)g_pixels;
    bitmap.pitch = WIN32_BITMAP_W * MULTIREXZ80_RENDER_BYTES_PER_PIXEL;
    bitmap.viewport.x = 0; bitmap.viewport.y = 0; bitmap.viewport.w = VIDEO_WIDTH_SMS; bitmap.viewport.h = VIDEO_HEIGHT_SMS;
    return 1;
}

static void apply_machine_options(app_state_t *a, const char *path) {
    const char *ext;
    option.console = 0;
    option.country = 0;
    if (a->selected_console > 0 && a->selected_console < (int)(sizeof(k_console_items)/sizeof(k_console_items[0]))) {
        option.console = k_console_items[a->selected_console].option_console;
        if (a->selected_video_mode == VMODE_AUTO && k_console_items[a->selected_console].auto_country) option.country = k_console_items[a->selected_console].auto_country;
    }
    if (a->selected_video_mode == VMODE_PAL) option.country = 2;
    else if (a->selected_video_mode == VMODE_NTSC) option.country = 1;
    if (option.console == 0 && path) {
        ext = ext_of(path);
        if (str_ieq(ext, ".cv") || str_ieq(ext, ".col")) option.console = 6;
        else if (str_ieq(ext, ".sg")) option.console = 5;
        else if (str_ieq(ext, ".m5")) option.console = 7;
        else if (str_ieq(ext, ".gg")) option.console = 3;
    }
}

static int init_bios(app_state_t *a) {
    char path[MAX_PATH];
    if (!bios.rom) bios.rom = (uint8_t *)calloc(1, 0x100000);
    if (!bios.rom) return 0;
    bios.enabled = 0;
    if ((sms.console == CONSOLE_SMS || sms.console == CONSOLE_SMS2 || sms.console == CONSOLE_GGMS) && a->sms_bios_path[0]) {
        size_t size = 0;
        if (!load_exact(a->sms_bios_path, bios.rom, 0x100000, 1, &size)) { app_error(a, "Failed to load Master System BIOS"); return 0; }
        if (size < 0x4000) size = 0x4000;
        bios.enabled = (uint8_t)(option.use_bios | 2);
        bios.pages = (uint16_t)(size / 0x4000);
    }
    if (sms.console == CONSOLE_COLECO) {
        const char *names[] = { "BIOS.col", "bios.col", "coleco.rom", "coleco.bin", "colecovision.rom", "COLECO.ROM" };
        if (a->coleco_bios_path[0]) snprintf(path, sizeof(path), "%s", a->coleco_bios_path);
        else if (!find_local_bios(a->rom_path, names, sizeof(names)/sizeof(names[0]), path, sizeof(path))) {
            app_error(a, "ColecoVision BIOS missing. Configure BIOS paths or place BIOS.col next to the executable/ROM."); return 0;
        }
        if (!load_exact(path, coleco.rom, sizeof(coleco.rom), 0x2000, NULL)) { app_error(a, "Failed to load ColecoVision BIOS"); return 0; }
        snprintf(a->coleco_bios_path, sizeof(a->coleco_bios_path), "%s", path); save_config(a);
    } else if (sms.console == CONSOLE_SORDM5) {
        const char *names[] = { "sordm5bios.bin", "SORDM5BIOS.BIN", "m5bios.bin", "M5BIOS.BIN" };
        path[0] = 0;
        if (a->m5_bios_path[0]) snprintf(path, sizeof(path), "%s", a->m5_bios_path);
        else find_local_bios(a->rom_path, names, sizeof(names)/sizeof(names[0]), path, sizeof(path));
        if (path[0]) {
            if (!load_exact(path, coleco.rom, sizeof(coleco.rom), 0x2000, NULL)) { app_error(a, "Failed to load Sord M5 BIOS"); return 0; }
            snprintf(a->m5_bios_path, sizeof(a->m5_bios_path), "%s", path); save_config(a);
        }
    }
    return 1;
}

static void audio_close(app_state_t *a) {
    win32_audio_t *wa = a ? &a->audio : NULL;
    if (!wa || !wa->enabled) return;
    waveOutReset(wa->wave);
    for (int i = 0; i < WIN32_AUDIO_BUFFERS; ++i) {
        if (wa->hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wa->wave, &wa->hdr[i], sizeof(WAVEHDR));
        free(wa->buf[i]); wa->buf[i] = NULL;
    }
    waveOutClose(wa->wave); memset(wa, 0, sizeof(*wa));
}
static void audio_open(app_state_t *a) {
    WAVEFORMATEX fmt;
    win32_audio_t *wa = a ? &a->audio : NULL;
    if (!wa) return;
    audio_close(a);
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 2; fmt.nSamplesPerSec = SOUND_FREQUENCY; fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8); fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    if (waveOutOpen(&wa->wave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;
    for (int i = 0; i < WIN32_AUDIO_BUFFERS; ++i) {
        wa->buf[i] = (int16_t *)calloc(WIN32_AUDIO_FRAMES_MAX * 2, sizeof(int16_t));
        if (!wa->buf[i]) { audio_close(a); return; }
        memset(&wa->hdr[i], 0, sizeof(WAVEHDR));
        wa->hdr[i].lpData = (LPSTR)wa->buf[i];
        wa->hdr[i].dwBufferLength = WIN32_AUDIO_FRAMES_MAX * 4;
        waveOutPrepareHeader(wa->wave, &wa->hdr[i], sizeof(WAVEHDR));
        wa->hdr[i].dwFlags |= WHDR_DONE;
    }
    wa->enabled = 1; wa->next = 0;
}
static void audio_submit_frame(app_state_t *a) {
    win32_audio_t *wa = &a->audio;
    WAVEHDR *h;
    uint32_t frames;
    if (!wa->enabled || !snd.enabled || !snd.output || snd.sample_count <= 0) return;
    frames = (uint32_t)snd.sample_count;
    if (frames > WIN32_AUDIO_FRAMES_MAX) frames = WIN32_AUDIO_FRAMES_MAX;
    h = &wa->hdr[wa->next];
    if (!(h->dwFlags & WHDR_DONE)) return;
    memcpy(wa->buf[wa->next], snd.output, (size_t)frames * 2u * sizeof(int16_t));
    h->dwBufferLength = frames * 4u;
    h->dwFlags &= ~WHDR_DONE;
    waveOutWrite(wa->wave, h, sizeof(WAVEHDR));
    wa->next = (wa->next + 1) % WIN32_AUDIO_BUFFERS;
}

static void rewind_clear(app_state_t *a) {
    for (size_t i = 0; i < WIN32_REWIND_MAX; ++i) { free(a->rewind[i].data); a->rewind[i].data = NULL; a->rewind[i].size = 0; a->rewind[i].frame = 0; }
    a->rewind_count = 0; a->rewind_head = 0; a->rewind_tick = 0;
}
static void rewind_capture(app_state_t *a) {
    uint8_t *data = NULL; uint32_t size = 0; size_t idx;
    if (!a->rom_loaded || !system_save_state_buffer(&data, &size)) return;
    idx = a->rewind_head;
    free(a->rewind[idx].data);
    a->rewind[idx].data = data;
    a->rewind[idx].size = size;
    a->rewind[idx].frame = a->frame;
    a->rewind_head = (a->rewind_head + 1) % WIN32_REWIND_MAX;
    if (a->rewind_count < WIN32_REWIND_MAX) a->rewind_count++;
}

static void render_loaded_state_preview_frame(void) {
    int32_t old_line = vdp.line;
    text_counter = 0;
    for (vdp.line = 0; vdp.line < vdp.lpf; ++vdp.line)
        render_line(vdp.line);
    vdp.line = old_line;
}

static void rewind_step(app_state_t *a) {
    size_t idx;
    if (!a->rewind_count) { app_set_status(a, "Rewind buffer empty"); return; }
    a->rewind_head = (a->rewind_head + WIN32_REWIND_MAX - 1) % WIN32_REWIND_MAX;
    idx = a->rewind_head;
    if (a->rewind[idx].data && a->rewind[idx].size) {
        if (system_load_state_buffer(a->rewind[idx].data, a->rewind[idx].size)) {
            a->frame = a->rewind[idx].frame;
            render_loaded_state_preview_frame();
            app_reset_timing(a, 0);
            app_set_status(a, "Rewinding...");
        }
    }
    free(a->rewind[idx].data); a->rewind[idx].data = NULL; a->rewind[idx].size = 0; a->rewind[idx].frame = 0; a->rewind_count--;
}

static void build_state_path(app_state_t *a, int slot, char *out, size_t out_sz) {
    char stem[MAX_PATH], name[MAX_PATH];
    const char *base = basename_a(a->rom_path);
    snprintf(stem, sizeof(stem), "%s", base[0] ? base : "cart");
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    sanitize_component(stem, name, sizeof(name));
    snprintf(out, out_sz, "%s\\%s_%08X.slot%d.png", a->state_dir, name, cart.crc, slot);
}
static uint32_t read_xrgb(uint32_t x, uint32_t y) {
    if (!bitmap.data || x >= bitmap.width || y >= bitmap.height) return 0xff000000u;
    return ((const uint32_t *)(const void *)(bitmap.data + (size_t)y * bitmap.pitch))[x] | 0xff000000u;
}
static uint32_t *capture_thumb(app_state_t *a, uint32_t *out_w, uint32_t *out_h, uint32_t *out_pitch) {
    uint32_t x0 = bitmap.viewport.x < 0 ? 0u : (uint32_t)bitmap.viewport.x;
    uint32_t y0 = bitmap.viewport.y < 0 ? 0u : (uint32_t)bitmap.viewport.y;
    uint32_t w = bitmap.viewport.w > 0 ? (uint32_t)bitmap.viewport.w : 256u;
    uint32_t h = bitmap.viewport.h > 0 ? (uint32_t)bitmap.viewport.h : 192u;
    uint32_t *p;
    (void)a;
    if (x0 + w > bitmap.width) w = bitmap.width - x0;
    if (y0 + h > bitmap.height) h = bitmap.height - y0;
    p = (uint32_t *)malloc((size_t)w * h * sizeof(uint32_t)); if (!p) return NULL;
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) p[(size_t)y*w+x] = read_xrgb(x0 + x, y0 + y);
    *out_w = w; *out_h = h; *out_pitch = w * 4u; return p;
}
static void save_slot(app_state_t *a, int slot) {
    char path[MAX_PATH]; uint32_t w,h,pitch; uint32_t *thumb;
    if (!a->rom_loaded) return;
    build_state_path(a, slot, path, sizeof(path)); ensure_dir(a->state_dir);
    app_lock_core(a);
    thumb = capture_thumb(a, &w, &h, &pitch);
    if (!system_save_state_file_ex(path, thumb, w, h, pitch)) { app_unlock_core(a); app_error(a, "Failed to save state"); }
    else { app_unlock_core(a); app_set_status(a, "State saved"); }
    free(thumb);
}
static void load_slot(app_state_t *a, int slot) {
    char path[MAX_PATH];
    if (!a->rom_loaded) return;
    build_state_path(a, slot, path, sizeof(path));
    app_lock_core(a);
    if (!system_load_state_file(path)) { app_unlock_core(a); app_error(a, "Failed to load state"); }
    else { render_loaded_state_preview_frame(); app_reset_timing(a, 1); update_present_pixels(a); app_unlock_core(a); app_set_status(a, "State loaded"); app_request_repaint(a); }
}

static void save_ppm(app_state_t *a, const char *path) {
    FILE *fp; uint32_t x0,y0,w,h;
    if (!a->rom_loaded || !path || !path[0]) return;
    x0 = bitmap.viewport.x < 0 ? 0u : (uint32_t)bitmap.viewport.x; y0 = bitmap.viewport.y < 0 ? 0u : (uint32_t)bitmap.viewport.y;
    w = bitmap.viewport.w > 0 ? (uint32_t)bitmap.viewport.w : 256u; h = bitmap.viewport.h > 0 ? (uint32_t)bitmap.viewport.h : 192u;
    fp = fopen(path, "wb"); if (!fp) { app_error(a, "Failed to open screenshot file"); return; }
    app_lock_core(a);
    fprintf(fp, "P6\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) { uint32_t p = read_xrgb(x0+x, y0+y); unsigned char rgb[3] = { (unsigned char)(p>>16), (unsigned char)(p>>8), (unsigned char)p }; fwrite(rgb,1,3,fp); }
    app_unlock_core(a);
    fclose(fp); app_set_status(a, "Screenshot saved");
}

static void update_menu_checks(app_state_t *a);

static int load_game(app_state_t *a, const char *path) {
    int ok = 0;
    int had_rom;
    char load_path[MAX_PATH];

    if (!a || !path || !path[0]) return 0;

    /* The source path can be a->rom_path during CLI/reload.  Preserve it before
     * the application state is mutated. */
    snprintf(load_path, sizeof(load_path), "%s", path);

    /* Treat ROM replacement as a hard frontend quiesce point.  In particular,
     * do not allow the previous machine's frame/audio path to run while a ZIP
     * arcade driver is being selected or installed. */
    app_stop_emu_thread(a);
    app_lock_core(a);
    a->loading = 1;
    a->running = 0;
    had_rom = a->rom_loaded;
    a->rom_loaded = 0;
    a->rom_is_lightgun = 0;
    InterlockedExchange(&a->repaint_pending, 0);
    a->fast_forward = 0;
    a->rewind_held = 0;
    a->kbd_actions = 0;
    a->joy_actions = 0;
    memset(&input, 0, sizeof(input));
    if (a->playback) multirexz80_input_script_reset(a->playback);

    audio_close(a);
    if (had_rom)
        system_poweroff();
    rewind_clear(a);
    g_sram_path[0] = 0;
    if (g_pixels)
        memset(g_pixels, 0, (size_t)WIN32_BITMAP_W * WIN32_BITMAP_H * MULTIREXZ80_RENDER_BYTES_PER_PIXEL);
    app_lock_present(a);
    free(a->present_pixels); a->present_pixels = NULL; a->present_w = a->present_h = 0;
    app_unlock_present(a);

    defaults(); apply_machine_options(a, load_path); snprintf(option.game_name, sizeof(option.game_name), "%s", load_path);
    if (!load_rom(load_path)) { app_unlock_core(a); app_error(a, "Failed to load ROM/ZIP"); goto finish; }

    if (!init_bitmap()) { app_unlock_core(a); app_error(a, "Failed to initialize video bitmap"); goto finish; }
    snprintf(a->rom_path, sizeof(a->rom_path), "%s", load_path);
    if (!init_bios(a)) { app_unlock_core(a); goto finish; }
    snprintf(g_sram_path, sizeof(g_sram_path), "%s\\%08X.sav", a->save_dir, cart.crc);
    system_poweron();
    memset(&input, 0, sizeof(input));
    a->rom_loaded = 1; a->running = 1; a->frame = 0; a->rom_is_lightgun = (sms.device[0] == DEVICE_LIGHTGUN || sms.device[1] == DEVICE_LIGHTGUN);
    option.lightgun_cursor = a->lightgun_cursor;
    audio_open(a);
    app_reset_timing(a, 1);
    update_present_pixels(a);
    ok = 1;
    app_unlock_core(a);

    app_update_title(a);
    update_menu_checks(a);
    app_set_status(a, "ROM loaded");
    app_force_video_refresh(a);

finish:
    if (!ok) {
        a->running = 0;
        a->rom_loaded = 0;
        audio_close(a);
        free_rom();
        g_sram_path[0] = 0;
        app_lock_present(a);
        free(a->present_pixels); a->present_pixels = NULL; a->present_w = a->present_h = 0;
        app_unlock_present(a);
        app_update_title(a);
        update_menu_checks(a);
        app_request_repaint(a);
    }
    a->loading = 0;
    return ok;
}

static void reload_current(app_state_t *a) { char path[MAX_PATH]; if (a && a->rom_loaded) { snprintf(path, sizeof(path), "%s", a->rom_path); load_game(a, path); } }

static void calc_dest_rect(app_state_t *a, RECT *dst) {
    RECT rc; int cw,ch,sw,sh,dw,dh;
    GetClientRect(a->hwnd, &rc); cw = rc.right - rc.left; ch = rc.bottom - rc.top;
    sw = (int)(a->present_w ? a->present_w : 256); sh = (int)(a->present_h ? a->present_h : 192);
    if (a->stretch || sw <= 0 || sh <= 0) { *dst = rc; return; }
    if (a->integer_scaling) { int scale = cw / sw; int sy = ch / sh; if (sy < scale) scale = sy; if (scale < 1) scale = 1; dw = sw * scale; dh = sh * scale; }
    else if (a->keep_aspect) { double s = (double)cw / sw; double sy = (double)ch / sh; if (sy < s) s = sy; dw = (int)(sw * s + 0.5); dh = (int)(sh * s + 0.5); }
    else { dw = cw; dh = ch; }
    dst->left = (cw - dw) / 2; dst->top = (ch - dh) / 2; dst->right = dst->left + dw; dst->bottom = dst->top + dh;
}

static void update_present_pixels(app_state_t *a) {
    uint32_t x0,y0,w,h; size_t need;
    if (!a || !bitmap.data) return;
    x0 = bitmap.viewport.x < 0 ? 0u : (uint32_t)bitmap.viewport.x; y0 = bitmap.viewport.y < 0 ? 0u : (uint32_t)bitmap.viewport.y;
    w = bitmap.viewport.w > 0 ? (uint32_t)bitmap.viewport.w : 256u; h = bitmap.viewport.h > 0 ? (uint32_t)bitmap.viewport.h : 192u;
    if (x0 >= bitmap.width || y0 >= bitmap.height) return;
    if (x0 + w > bitmap.width) w = bitmap.width - x0;
    if (y0 + h > bitmap.height) h = bitmap.height - y0;
    need = (size_t)w * h;
    app_lock_present(a);
    if (!a->present_pixels || (size_t)a->present_w * a->present_h < need) { free(a->present_pixels); a->present_pixels = (uint32_t *)malloc(need * sizeof(uint32_t)); }
    if (!a->present_pixels) { app_unlock_present(a); return; }
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) a->present_pixels[(size_t)y*w+x] = read_xrgb(x0+x,y0+y);
    a->present_w = w; a->present_h = h;
    app_unlock_present(a);
}

static void destroy_backbuffer(app_state_t *a) {
    if (!a) return;
    if (a->back_dc && a->back_old_bmp) SelectObject(a->back_dc, a->back_old_bmp);
    if (a->back_bmp) DeleteObject(a->back_bmp);
    if (a->back_dc) DeleteDC(a->back_dc);
    a->back_dc = NULL; a->back_bmp = NULL; a->back_old_bmp = NULL; a->back_w = a->back_h = 0;
}

static void app_force_video_refresh(app_state_t *a) {
    RECT rc;
    if (!a || !a->hwnd) return;

    /* Video-mode/scaling changes do not necessarily produce WM_SIZE.  Force the
     * same renderer maintenance normally done by resize, clear the coalesced
     * repaint flag, and synchronously service one paint so the new mode appears
     * immediately in windowed mode. */
    GetClientRect(a->hwnd, &rc);
#ifdef _WIN64
    if (a->d3d)
        win_d3d11_resize(a->d3d, rc.right - rc.left, rc.bottom - rc.top);
#endif
    destroy_backbuffer(a);
    InterlockedExchange(&a->repaint_pending, 0);
    RedrawWindow(a->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

#ifdef _WIN64
/* Create/destroy the D3D11 presenter to match the user preference.  If creation
 * fails (no GPU/driver/compiler), the preference is cleared so we stay on GDI. */
static void app_set_d3d11(app_state_t *a, int enable) {
    if (!a) return;
    if (enable && !a->d3d) {
        a->d3d = win_d3d11_create(a->hwnd);
        a->use_d3d11 = a->d3d ? 1 : 0;
    } else if (!enable && a->d3d) {
        win_d3d11_destroy(a->d3d); a->d3d = NULL;
        a->use_d3d11 = 0;
    }
}
#endif

/* Lazily (re)create the off-screen DIB sized to the client area.  Returns 1 on
 * success.  The buffer is reused across frames and only rebuilt when the window
 * size actually changes, so steady-state painting allocates nothing. */
static int ensure_backbuffer(app_state_t *a, HDC ref_dc, int w, int h) {
    BITMAPINFO bi; void *bits = NULL; HBITMAP bmp;
    if (!a) return 0;
    if (w < 1) w = 1; if (h < 1) h = 1;
    if (a->back_dc && a->back_bmp && a->back_w == w && a->back_h == h) return 1;
    destroy_backbuffer(a);
    a->back_dc = CreateCompatibleDC(ref_dc);
    if (!a->back_dc) return 0;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(ref_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); DeleteDC(a->back_dc); a->back_dc = NULL; return 0; }
    a->back_bmp = bmp; a->back_old_bmp = SelectObject(a->back_dc, bmp); a->back_w = w; a->back_h = h;
    return 1;
}

static void fill_margin(HDC dc, HBRUSH br, int l, int t, int r, int b) {
    RECT m; if (r <= l || b <= t) return; m.left = l; m.top = t; m.right = r; m.bottom = b; FillRect(dc, &m, br);
}

static void paint_frame(app_state_t *a, HDC hdc) {
    RECT rc, dst; HBRUSH br; BITMAPINFO bmi; int cw, ch;
    GetClientRect(a->hwnd, &rc);
    cw = rc.right - rc.left; ch = rc.bottom - rc.top;
    if (cw < 1 || ch < 1) return;
#ifdef _WIN64
    /* GPU path: present the framebuffer as a textured quad so scaling cost does
     * not grow with the window resolution.  Falls back to GDI on any failure. */
    if (a->use_d3d11 && a->d3d) {
        int ok = 0;
        app_lock_present(a);
        if (a->present_pixels && a->present_w && a->present_h) {
            calc_dest_rect(a, &dst);
            if (dst.left < 0) dst.left = 0; if (dst.top < 0) dst.top = 0;
            if (dst.right > cw) dst.right = cw; if (dst.bottom > ch) dst.bottom = ch;
            ok = win_d3d11_present(a->d3d, a->present_pixels, (int)a->present_w, (int)a->present_h, &dst, cw, ch);
        }
        app_unlock_present(a);
        if (ok) return;
    }
#endif
    app_lock_present(a);
    if (!a->present_pixels || !a->present_w || !a->present_h) {
        /* No frame yet: clear the window once (cheap, not per-emulated-frame). */
        app_unlock_present(a);
        br = (HBRUSH)GetStockObject(BLACK_BRUSH); FillRect(hdc, &rc, br);
        return;
    }
    if (!ensure_backbuffer(a, hdc, cw, ch)) {
        /* Fallback to direct blit if the backbuffer could not be created. */
        calc_dest_rect(a, &dst);
        br = (HBRUSH)GetStockObject(BLACK_BRUSH); FillRect(hdc, &rc, br);
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = (LONG)a->present_w; bmi.bmiHeader.biHeight = -(LONG)a->present_h;
        bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(hdc, dst.left, dst.top, dst.right-dst.left, dst.bottom-dst.top, 0,0,a->present_w,a->present_h, a->present_pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
        app_unlock_present(a);
        return;
    }
    calc_dest_rect(a, &dst);
    /* Clamp dst into the client just in case. */
    if (dst.left < 0) dst.left = 0; if (dst.top < 0) dst.top = 0;
    if (dst.right > cw) dst.right = cw; if (dst.bottom > ch) dst.bottom = ch;
    /* Only clear the letterbox/pillarbox margins around the image instead of the
     * whole client.  In stretch mode the image covers everything and we skip the
     * fill entirely, so per-frame clearing no longer scales with window size. */
    br = (HBRUSH)GetStockObject(BLACK_BRUSH);
    fill_margin(a->back_dc, br, 0, 0, cw, dst.top);                 /* top    */
    fill_margin(a->back_dc, br, 0, dst.bottom, cw, ch);            /* bottom */
    fill_margin(a->back_dc, br, 0, dst.top, dst.left, dst.bottom); /* left   */
    fill_margin(a->back_dc, br, dst.right, dst.top, cw, dst.bottom);/* right  */
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = (LONG)a->present_w; bmi.bmiHeader.biHeight = -(LONG)a->present_h;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(a->back_dc, COLORONCOLOR);
    StretchDIBits(a->back_dc, dst.left, dst.top, dst.right-dst.left, dst.bottom-dst.top, 0,0,a->present_w,a->present_h, a->present_pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
    app_unlock_present(a);
    BitBlt(hdc, 0, 0, cw, ch, a->back_dc, 0, 0, SRCCOPY);
}

static int arcade_active(void) { return sms.console == CONSOLE_SYSTEME || sms.console == CONSOLE_SYSTEM1 || sms.console == CONSOLE_SNKPSYCHOS || sms.console == CONSOLE_TAITOL; }
static int heavyweight_frame_system(void) { return arcade_active() || sms.console == CONSOLE_COLECO || sms.console == CONSOLE_SORDM5; }
/* Apply the merged keyboard+joystick action mask to the emulator input each
 * frame.  Idempotent: every control action is set or cleared from the combined
 * held state, so releasing a key while the pad still holds the direction (or
 * vice versa) keeps the input correct. */
static void set_pad_bit(int port, uint8_t bit, int down) {
    if (port < 0 || port > 1 || !bit) return;
    if (down) input.pad[port] |= bit;
    else input.pad[port] &= (uint8_t)~bit;
}

static int action_pad_port(action_id_t act, uint8_t *out_bit) {
    uint8_t bit = 0;
    int port = -1;
    switch (act) {
    case ACT_UP: bit = INPUT_UP; port = 0; break;
    case ACT_DOWN: bit = INPUT_DOWN; port = 0; break;
    case ACT_LEFT: bit = INPUT_LEFT; port = 0; break;
    case ACT_RIGHT: bit = INPUT_RIGHT; port = 0; break;
    case ACT_BUTTON1: bit = INPUT_BUTTON1; port = 0; break;
    case ACT_BUTTON2: bit = INPUT_BUTTON2; port = 0; break;
    case ACT_ROTATE_LEFT: bit = INPUT_ROTATE_LEFT; port = 0; break;
    case ACT_ROTATE_RIGHT: bit = INPUT_ROTATE_RIGHT; port = 0; break;
    case ACT_P2_UP: bit = INPUT_UP; port = 1; break;
    case ACT_P2_DOWN: bit = INPUT_DOWN; port = 1; break;
    case ACT_P2_LEFT: bit = INPUT_LEFT; port = 1; break;
    case ACT_P2_RIGHT: bit = INPUT_RIGHT; port = 1; break;
    case ACT_P2_BUTTON1: bit = INPUT_BUTTON1; port = 1; break;
    case ACT_P2_BUTTON2: bit = INPUT_BUTTON2; port = 1; break;
    case ACT_P2_ROTATE_LEFT: bit = INPUT_ROTATE_LEFT; port = 1; break;
    case ACT_P2_ROTATE_RIGHT: bit = INPUT_ROTATE_RIGHT; port = 1; break;
    default: break;
    }
    if (out_bit) *out_bit = bit;
    return port;
}

static void set_action_state(app_state_t *a, action_id_t act, int down) {
    uint8_t bit = 0;
    int port;
    (void)a;
    port = action_pad_port(act, &bit);
    if (port >= 0) set_pad_bit(port, bit, down);

    switch (act) {
    case ACT_AIM_UP: if (down) input.rotary_aim[0] |= INPUT_AIM_UP; else input.rotary_aim[0] &= (uint8_t)~INPUT_AIM_UP; break;
    case ACT_AIM_DOWN: if (down) input.rotary_aim[0] |= INPUT_AIM_DOWN; else input.rotary_aim[0] &= (uint8_t)~INPUT_AIM_DOWN; break;
    case ACT_AIM_LEFT: if (down) input.rotary_aim[0] |= INPUT_AIM_LEFT; else input.rotary_aim[0] &= (uint8_t)~INPUT_AIM_LEFT; break;
    case ACT_AIM_RIGHT: if (down) input.rotary_aim[0] |= INPUT_AIM_RIGHT; else input.rotary_aim[0] &= (uint8_t)~INPUT_AIM_RIGHT; break;
    case ACT_P2_AIM_UP: if (down) input.rotary_aim[1] |= INPUT_AIM_UP; else input.rotary_aim[1] &= (uint8_t)~INPUT_AIM_UP; break;
    case ACT_P2_AIM_DOWN: if (down) input.rotary_aim[1] |= INPUT_AIM_DOWN; else input.rotary_aim[1] &= (uint8_t)~INPUT_AIM_DOWN; break;
    case ACT_P2_AIM_LEFT: if (down) input.rotary_aim[1] |= INPUT_AIM_LEFT; else input.rotary_aim[1] &= (uint8_t)~INPUT_AIM_LEFT; break;
    case ACT_P2_AIM_RIGHT: if (down) input.rotary_aim[1] |= INPUT_AIM_RIGHT; else input.rotary_aim[1] &= (uint8_t)~INPUT_AIM_RIGHT; break;
    default: break;
    }

    if (act == ACT_PAUSE_BUTTON) {
        uint8_t sys = IS_GG ? INPUT_START : INPUT_PAUSE;
        if (down) input.system |= sys;
        else input.system &= (uint8_t)~sys;
    }
    if (arcade_active()) {
        uint8_t m = 0;
        if (act == ACT_COIN1) m = INPUT_ARCADE_COIN1;
        else if (act == ACT_COIN2) m = INPUT_ARCADE_COIN2;
        else if (act == ACT_START1) m = INPUT_ARCADE_START1;
        else if (act == ACT_START2) m = INPUT_ARCADE_START2;
        if (m) { if (down) input.arcade |= m; else input.arcade &= (uint8_t)~m; }
    }
    if (sms.console == CONSOLE_SORDM5) {
        if (act == ACT_BUTTON1) { if (down) input.m5_key[0] |= 0x40; else input.m5_key[0] &= (uint8_t)~0x40; }
        if (act == ACT_BUTTON2) { if (down) input.m5_key[0] |= 0x80; else input.m5_key[0] &= (uint8_t)~0x80; }
        if (act == ACT_M5_1) { if (down) input.m5_key[1] |= 0x01; else input.m5_key[1] &= (uint8_t)~0x01; }
        if (act == ACT_M5_2) { if (down) input.m5_key[1] |= 0x02; else input.m5_key[1] &= (uint8_t)~0x02; }
    }
}

static uint8_t keypad_from_actions(uint64_t acts, int first) {
    for (int k = 0; k < ACT_KP_COUNT; ++k)
        if (acts & ACT_BIT(first + k)) return (uint8_t)k;
    return 0xff;
}

static void apply_actions(app_state_t *a) {
    uint64_t acts = a->kbd_actions | a->joy_actions;
    for (int i = 0; i < ACT_COUNT; ++i)
        set_action_state(a, (action_id_t)i, (acts & ACT_BIT(i)) != 0);
    if (sms.console == CONSOLE_COLECO) {
        coleco.keypad[0] = keypad_from_actions(acts, ACT_P1_KP_FIRST);
        coleco.keypad[1] = keypad_from_actions(acts, ACT_P2_KP_FIRST);
    }
}

static uint64_t joy_bits_from_poll(uint32_t buttons, uint32_t dirs) {
    uint64_t bits = (uint64_t)buttons;
    if (dirs & (1u << ACT_UP)) bits |= (1ull << JOY_BIND_DIR_UP);
    if (dirs & (1u << ACT_DOWN)) bits |= (1ull << JOY_BIND_DIR_DOWN);
    if (dirs & (1u << ACT_LEFT)) bits |= (1ull << JOY_BIND_DIR_LEFT);
    if (dirs & (1u << ACT_RIGHT)) bits |= (1ull << JOY_BIND_DIR_RIGHT);
    return bits;
}

/* Read the active joystick backend as bindable inputs.  Bits 0-31 are raw
 * buttons; bits 32-35 are the hat/axis directions, so directions can be mapped
 * to either controller instead of being hard-wired to Controller 1. */
static uint64_t app_read_joy_bits(app_state_t *a) {
    uint32_t buttons = 0, dirs = 0;
    if (!a) return 0;
#if defined(MULTIREXZ80_HAVE_SDL3)
    if (a->sdl_input) {
        int q = 0;
        buttons = win_sdl3_input_poll(a->sdl_input, &q, &dirs);
        if (q) a->quit = 1;
    }
#elif !defined(_WIN64)
    if (a->winmm_input) buttons = win_winmm_input_poll(a->winmm_input, &dirs);
#endif
    return joy_bits_from_poll(buttons, dirs);
}

static int joy_binding_down(uint64_t bits, int binding) {
    return binding >= 0 && binding < JOY_BIND_COUNT && (bits & (1ull << binding));
}

/* Poll the joystick and fold it into joy_actions through the user's bindings. */
static void app_poll_joystick(app_state_t *a) {
    uint64_t bits, acts = 0;
    if (!a) return;
    bits = app_read_joy_bits(a);
    for (int i = 0; i < ACT_COUNT; ++i)
        if (joy_binding_down(bits, a->controls[i].joy)) acts |= ACT_BIT(i);
    a->joy_actions = acts;
}

static int vk_matches(UINT vk, UINT target) { return target && vk == target; }
static uint64_t action_mask_for_vk(app_state_t *a, UINT vk) { uint64_t m = 0; for (int i=0;i<ACT_COUNT;i++) if (vk_matches(vk, a->controls[i].vk)) m |= ACT_BIT(i); return m; }
static hotkey_id_t hotkey_for_vk(app_state_t *a, UINT vk) { for (int i=0;i<HK_COUNT;i++) if (vk_matches(vk, a->hotkeys[i].vk)) return (hotkey_id_t)i; return HK_COUNT; }

static void vk_name(UINT vk, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (!vk) { snprintf(out, out_sz, "Unbound"); return; }
    if (vk >= 'A' && vk <= 'Z') { snprintf(out, out_sz, "%c", (char)vk); return; }
    if (vk >= '0' && vk <= '9') { snprintf(out, out_sz, "%c", (char)vk); return; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(out, out_sz, "Numpad %u", (unsigned)(vk - VK_NUMPAD0)); return; }
    switch (vk) {
    case VK_UP: snprintf(out, out_sz, "Up Arrow"); break;
    case VK_DOWN: snprintf(out, out_sz, "Down Arrow"); break;
    case VK_LEFT: snprintf(out, out_sz, "Left Arrow"); break;
    case VK_RIGHT: snprintf(out, out_sz, "Right Arrow"); break;
    case VK_RETURN: snprintf(out, out_sz, "Enter"); break;
    case VK_SPACE: snprintf(out, out_sz, "Space"); break;
    case VK_TAB: snprintf(out, out_sz, "Tab"); break;
    case VK_BACK: snprintf(out, out_sz, "Backspace"); break;
    case VK_ESCAPE: snprintf(out, out_sz, "Esc"); break;
    case VK_SHIFT: snprintf(out, out_sz, "Shift"); break;
    case VK_CONTROL: snprintf(out, out_sz, "Ctrl"); break;
    case VK_MENU: snprintf(out, out_sz, "Alt"); break;
    case VK_MULTIPLY: snprintf(out, out_sz, "Numpad *"); break;
    case VK_DIVIDE: snprintf(out, out_sz, "Numpad /"); break;
    case VK_ADD: snprintf(out, out_sz, "Numpad +"); break;
    case VK_SUBTRACT: snprintf(out, out_sz, "Numpad -"); break;
    case VK_DECIMAL: snprintf(out, out_sz, "Numpad ."); break;
    default:
        if (vk >= VK_F1 && vk <= VK_F24) snprintf(out, out_sz, "F%u", (unsigned)(vk - VK_F1 + 1));
        else snprintf(out, out_sz, "VK %u", (unsigned)vk);
        break;
    }
}

static void joy_binding_name(int joy, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (joy < 0) { snprintf(out, out_sz, "Unbound"); return; }
    if (joy < JOY_BIND_BUTTON_MAX) { snprintf(out, out_sz, "Button %d", joy + 1); return; }
    switch (joy) {
    case JOY_BIND_DIR_UP: snprintf(out, out_sz, "D-pad/Axis Up"); break;
    case JOY_BIND_DIR_DOWN: snprintf(out, out_sz, "D-pad/Axis Down"); break;
    case JOY_BIND_DIR_LEFT: snprintf(out, out_sz, "D-pad/Axis Left"); break;
    case JOY_BIND_DIR_RIGHT: snprintf(out, out_sz, "D-pad/Axis Right"); break;
    default: snprintf(out, out_sz, "Joy %d", joy); break;
    }
}

static int first_set_joy_binding(uint64_t bits) {
    for (int i = 0; i < JOY_BIND_COUNT; ++i)
        if (bits & (1ull << i)) return i;
    return -1;
}

#define SINGLEKEY_ID_TEXT      4101
#define SINGLEKEY_ID_CANCEL    4102

typedef struct single_key_state { app_state_t *app; const char *what; HWND text; int done; UINT vk; } single_key_state_t;

static LRESULT CALLBACK single_key_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    single_key_state_t *d = (single_key_state_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lparam;
        char title[256];
        d = (single_key_state_t *)cs->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        snprintf(title, sizeof(title), "Press a key for: %s", d && d->what ? d->what : "action");
        d->text = CreateWindowA("STATIC", title, WS_CHILD|WS_VISIBLE|SS_CENTER, 12, 18, 396, 42, hwnd, (HMENU)(UINT_PTR)SINGLEKEY_ID_TEXT, d->app->inst, NULL);
        CreateWindowA("STATIC", "Esc cancels.", WS_CHILD|WS_VISIBLE|SS_CENTER, 12, 64, 396, 20, hwnd, NULL, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 164, 98, 92, 26, hwnd, (HMENU)(UINT_PTR)SINGLEKEY_ID_CANCEL, d->app->inst, NULL);
        SetFocus(hwnd);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == SINGLEKEY_ID_CANCEL) { if (d) d->done = 1; DestroyWindow(hwnd); return 0; }
        break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (d) {
            if (wparam != VK_ESCAPE) d->vk = (UINT)wparam;
            d->done = 1;
        }
        DestroyWindow(hwnd); return 0;
    case WM_CLOSE:
        if (d) d->done = 1;
        DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static UINT capture_key(app_state_t *a, const char *what) {
    WNDCLASSEXA wc; HWND dlg; MSG msg; RECT rc = {0,0,430,165}; single_key_state_t d;
    int old_running;
    if (!a || !a->hwnd) return 0;
    old_running = a->running;
    a->running = 0; a->kbd_actions = 0; a->joy_actions = 0;
    if (a->rom_loaded) { app_lock_core(a); apply_actions(a); app_unlock_core(a); }
    update_menu_checks(a);
    memset(&wc, 0, sizeof(wc)); wc.cbSize = sizeof(wc); wc.lpfnWndProc = single_key_proc; wc.hInstance = a->inst; wc.lpszClassName = "MultiRexZ80SingleKeyDialog"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExA(&wc);
    memset(&d, 0, sizeof(d)); d.app = a; d.what = what;
    AdjustWindowRect(&rc, WS_CAPTION|WS_SYSMENU, FALSE);
    EnableWindow(a->hwnd, FALSE);
    dlg = CreateWindowExA(WS_EX_DLGMODALFRAME, wc.lpszClassName, "Set Key", WS_CAPTION|WS_SYSMENU|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rc.right-rc.left, rc.bottom-rc.top, a->hwnd, NULL, a->inst, &d);
    SetFocus(dlg);
    while (!d.done && GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            SendMessageA(dlg, msg.message, msg.wParam, msg.lParam);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    if (!d.done) a->quit = 1;
    EnableWindow(a->hwnd, TRUE); SetForegroundWindow(a->hwnd);
    a->running = old_running;
    app_reset_timing(a, old_running);
    update_menu_checks(a);
    return d.vk;
}

#define CTRLSET_ID_ACTION       4201
#define CTRLSET_ID_STEP         4202
#define CTRLSET_ID_HINT         4203
#define CTRLSET_ID_SKIP         4204
#define CTRLSET_ID_CANCEL       4205
#define CTRLSET_TIMER_JOY       4206

typedef struct control_setup_state {
    app_state_t *app;
    int controller;
    int device; /* 0 = keyboard, 1 = gamepad */
    const action_id_t *seq;
    int count;
    int index;
    int done;
    int accepted;
    uint64_t joy_baseline;
    key_binding_t tmp[ACT_COUNT];
    HWND action_text;
    HWND step_text;
    HWND hint_text;
} control_setup_state_t;

static const action_id_t k_ctrl1_sequence[] = {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_BUTTON1, ACT_BUTTON2,
    ACT_AIM_UP, ACT_AIM_RIGHT, ACT_AIM_DOWN, ACT_AIM_LEFT, ACT_ROTATE_LEFT, ACT_ROTATE_RIGHT,
    ACT_PAUSE_BUTTON, ACT_COIN1, ACT_START1, ACT_M5_1, ACT_M5_2,
    ACT_KP0, ACT_KP1, ACT_KP2, ACT_KP3, ACT_KP4, ACT_KP5, ACT_KP6,
    ACT_KP7, ACT_KP8, ACT_KP9, ACT_KP_STAR, ACT_KP_HASH
};
static const action_id_t k_ctrl2_sequence[] = {
    ACT_P2_UP, ACT_P2_DOWN, ACT_P2_LEFT, ACT_P2_RIGHT, ACT_P2_BUTTON1, ACT_P2_BUTTON2,
    ACT_P2_AIM_UP, ACT_P2_AIM_RIGHT, ACT_P2_AIM_DOWN, ACT_P2_AIM_LEFT, ACT_P2_ROTATE_LEFT, ACT_P2_ROTATE_RIGHT,
    ACT_COIN2, ACT_START2,
    ACT_P2_KP0, ACT_P2_KP1, ACT_P2_KP2, ACT_P2_KP3, ACT_P2_KP4, ACT_P2_KP5,
    ACT_P2_KP6, ACT_P2_KP7, ACT_P2_KP8, ACT_P2_KP9, ACT_P2_KP_STAR, ACT_P2_KP_HASH
};

static void control_setup_rebaseline(control_setup_state_t *d) {
    d->joy_baseline = (d->device == 1) ? app_read_joy_bits(d->app) : 0;
}

static void control_setup_finish(control_setup_state_t *d, HWND hwnd, int accepted) {
    if (!d) return;
    d->accepted = accepted;
    d->done = 1;
    KillTimer(hwnd, CTRLSET_TIMER_JOY);
    DestroyWindow(hwnd);
}

static void control_setup_refresh(control_setup_state_t *d) {
    char text[256], step[96], hint[256], current[96];
    action_id_t act;
    if (!d || d->index >= d->count) return;
    act = d->seq[d->index];
    if (d->device == 0) vk_name(d->tmp[act].vk, current, sizeof(current));
    else joy_binding_name(d->tmp[act].joy, current, sizeof(current));
    snprintf(text, sizeof(text), "%s\n(current: %s)", d->tmp[act].name, current);
    snprintf(step, sizeof(step), "Controller %d %s setup: step %d of %d", d->controller + 1, d->device == 0 ? "keyboard" : "gamepad", d->index + 1, d->count);
    snprintf(hint, sizeof(hint), "%s", d->device == 0 ? "Press the keyboard key to assign. Backspace clears/skips. Esc cancels." : "Press a gamepad button or move the D-pad/axis. Backspace clears/skips. Esc cancels.");
    SetWindowTextA(d->action_text, text);
    SetWindowTextA(d->step_text, step);
    SetWindowTextA(d->hint_text, hint);
}

static void control_setup_advance(control_setup_state_t *d, HWND hwnd) {
    d->index++;
    if (d->index >= d->count) { control_setup_finish(d, hwnd, 1); return; }
    control_setup_rebaseline(d);
    control_setup_refresh(d);
}

static void control_setup_clear_current(control_setup_state_t *d, HWND hwnd) {
    action_id_t act;
    if (!d || d->index >= d->count) return;
    act = d->seq[d->index];
    if (d->device == 0) d->tmp[act].vk = 0;
    else d->tmp[act].joy = -1;
    control_setup_advance(d, hwnd);
}

static LRESULT CALLBACK control_setup_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    control_setup_state_t *d = (control_setup_state_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lparam;
        d = (control_setup_state_t *)cs->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        d->step_text = CreateWindowA("STATIC", "", WS_CHILD|WS_VISIBLE|SS_CENTER, 12, 14, 500, 20, hwnd, (HMENU)(UINT_PTR)CTRLSET_ID_STEP, d->app->inst, NULL);
        d->action_text = CreateWindowA("STATIC", "", WS_CHILD|WS_VISIBLE|SS_CENTER, 12, 46, 500, 48, hwnd, (HMENU)(UINT_PTR)CTRLSET_ID_ACTION, d->app->inst, NULL);
        d->hint_text = CreateWindowA("STATIC", "", WS_CHILD|WS_VISIBLE|SS_CENTER, 12, 104, 500, 34, hwnd, (HMENU)(UINT_PTR)CTRLSET_ID_HINT, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Skip/Clear", WS_CHILD|WS_VISIBLE, 154, 154, 100, 28, hwnd, (HMENU)(UINT_PTR)CTRLSET_ID_SKIP, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 268, 154, 100, 28, hwnd, (HMENU)(UINT_PTR)CTRLSET_ID_CANCEL, d->app->inst, NULL);
        control_setup_rebaseline(d);
        control_setup_refresh(d);
        if (d->device == 1) SetTimer(hwnd, CTRLSET_TIMER_JOY, 10, NULL);
        SetFocus(hwnd);
        return 0;
    }
    case WM_COMMAND:
        if (!d) break;
        if (LOWORD(wparam) == CTRLSET_ID_SKIP) { control_setup_clear_current(d, hwnd); SetFocus(hwnd); return 0; }
        if (LOWORD(wparam) == CTRLSET_ID_CANCEL) { control_setup_finish(d, hwnd, 0); return 0; }
        break;
    case WM_TIMER:
        if (d && d->device == 1 && wparam == CTRLSET_TIMER_JOY && d->index < d->count) {
            uint64_t now = app_read_joy_bits(d->app);
            int joy = first_set_joy_binding(now & ~d->joy_baseline);
            if (joy >= 0) {
                d->tmp[d->seq[d->index]].joy = joy;
                control_setup_advance(d, hwnd);
            } else if (now == 0) d->joy_baseline = 0;
            return 0;
        }
        break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (!d) break;
        if (wparam == VK_ESCAPE) { control_setup_finish(d, hwnd, 0); return 0; }
        if (wparam == VK_BACK) { control_setup_clear_current(d, hwnd); return 0; }
        if (d->device == 0 && d->index < d->count) {
            d->tmp[d->seq[d->index]].vk = (UINT)wparam;
            control_setup_advance(d, hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (d) control_setup_finish(d, hwnd, 0);
        else DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static int show_controller_setup(app_state_t *a, int controller, int device) {
    WNDCLASSEXA wc; HWND dlg; MSG msg; RECT rc = {0,0,530,225}; control_setup_state_t d;
    char title[128]; int old_running;
    if (!a || !a->hwnd) return 0;
    old_running = a->running;
    a->running = 0; a->kbd_actions = 0; a->joy_actions = 0;
    if (a->rom_loaded) { app_lock_core(a); apply_actions(a); app_unlock_core(a); }
    update_menu_checks(a);

    memset(&wc, 0, sizeof(wc)); wc.cbSize = sizeof(wc); wc.lpfnWndProc = control_setup_proc; wc.hInstance = a->inst; wc.lpszClassName = "MultiRexZ80ControllerSetup"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExA(&wc);
    memset(&d, 0, sizeof(d)); d.app = a; d.controller = controller; d.device = device;
    d.seq = controller == 0 ? k_ctrl1_sequence : k_ctrl2_sequence;
    d.count = controller == 0 ? (int)(sizeof(k_ctrl1_sequence)/sizeof(k_ctrl1_sequence[0])) : (int)(sizeof(k_ctrl2_sequence)/sizeof(k_ctrl2_sequence[0]));
    memcpy(d.tmp, a->controls, sizeof(d.tmp));
    snprintf(title, sizeof(title), "Controller %d - %s Setup", controller + 1, device == 0 ? "Keyboard" : "Gamepad");
    AdjustWindowRect(&rc, WS_CAPTION|WS_SYSMENU, FALSE);
    EnableWindow(a->hwnd, FALSE);
    dlg = CreateWindowExA(WS_EX_DLGMODALFRAME, wc.lpszClassName, title, WS_CAPTION|WS_SYSMENU|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rc.right-rc.left, rc.bottom-rc.top, a->hwnd, NULL, a->inst, &d);
    SetFocus(dlg);
    while (!d.done && GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            SendMessageA(dlg, msg.message, msg.wParam, msg.lParam);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    if (!d.done) a->quit = 1;
    EnableWindow(a->hwnd, TRUE); SetForegroundWindow(a->hwnd);
    if (d.accepted) memcpy(a->controls, d.tmp, sizeof(a->controls));
    a->running = old_running;
    app_reset_timing(a, old_running);
    update_menu_checks(a);
    return d.accepted;
}

static void apply_hotkey(app_state_t *a, hotkey_id_t hk) {
    char path[MAX_PATH];
    switch (hk) {
    case HK_PAUSE: a->running = !a->running; break;
    case HK_FAST_FORWARD: a->fast_forward = !a->fast_forward; app_reset_timing(a, 1); break;
    case HK_REWIND: a->rewind_held = 1; app_reset_timing(a, 1); break;
    case HK_SAVE_STATE: save_slot(a, a->save_slot); break;
    case HK_LOAD_STATE: load_slot(a, a->save_slot); break;
    case HK_SCREENSHOT: snprintf(path, sizeof(path), "%s\\screenshot_%08X_%llu.ppm", a->last_screenshot_dir, cart.crc, (unsigned long long)a->frame); save_ppm(a, path); break;
    case HK_RESET: if (a->rom_loaded) { app_lock_core(a); system_reset(); app_reset_timing(a, 1); app_unlock_core(a); } break;
    case HK_FULLSCREEN: SendMessageA(a->hwnd, WM_COMMAND, IDM_VIDEO_FULLSCREEN, 0); break;
    default: break;
    }
    update_menu_checks(a);
}

static void update_lightgun_mouse(app_state_t *a, LPARAM lparam, WPARAM wparam) {
    RECT dst; int x,y,port=0; int emux,emuy;
    if (!a->rom_loaded || !a->rom_is_lightgun) return;
    x = (short)LOWORD(lparam); y = (short)HIWORD(lparam);
    calc_dest_rect(a, &dst);
    if (x < dst.left || x >= dst.right || y < dst.top || y >= dst.bottom) return;
    emux = (int)(((int64_t)(x - dst.left) * (int)a->present_w) / (dst.right - dst.left));
    emuy = (int)(((int64_t)(y - dst.top) * (int)a->present_h) / (dst.bottom - dst.top));
    if (sms.device[1] == DEVICE_LIGHTGUN) port = 1;
    input.analog[port][0] = emux; input.analog[port][1] = emuy;
    if (wparam & MK_LBUTTON) input.pad[port] |= INPUT_BUTTON1; else input.pad[port] &= (uint8_t)~INPUT_BUTTON1;
}

static void audio_and_frame(app_state_t *a) {
    if (!a->rom_loaded || !a->running) return;
    app_lock_core(a);
    if (!a->rom_loaded || !a->running) { app_unlock_core(a); return; }
    app_poll_joystick(a);
    /* Merge keyboard + joystick before applying playback so a recording stays
     * authoritative during playback. */
    apply_actions(a);
    if (a->playback) multirexz80_input_script_apply_frame(a->playback, a->frame, &input);
    if (a->rewind_held) {
        if ((a->rewind_tick++ % WIN32_REWIND_STEP) == 0) rewind_step(a);
        update_present_pixels(a);
        app_unlock_core(a);
        app_request_repaint(a);
        return;
    } else {
        a->rewind_tick = 0;
    }
    system_frame(0);
    if (a->recorder) multirexz80_input_recorder_write_state_changes(a->recorder, a->frame, &input);
    audio_submit_frame(a);
    update_present_pixels(a);
    app_unlock_core(a);
    app_request_repaint(a);
    /* Keep rewind history for every loaded machine.  The earlier Win32 path
     * skipped Coleco/Sord/arcade as "heavyweight", which made the Rewind menu
     * and hotkey appear broken on exactly those systems. */
    if (a->frame > 0 && (a->frame % WIN32_REWIND_INTERVAL) == 0)
        rewind_capture(a);
    a->frame++;
}

static void app_reset_timing(app_state_t *a, int run_immediately) {
    int fps;
    if (!a) return;
    QueryPerformanceCounter(&a->last_tick);
    fps = (sms.display == DISPLAY_PAL) ? FPS_PAL : FPS_NTSC;
    if (fps <= 0) fps = FPS_NTSC;
    a->accum_us = run_immediately ? (uint64_t)(1000000 / fps) : 0;
}

/* Runs due frames and returns how many milliseconds the caller should wait
 * before pumping again (i.e. time until the next frame is due).  Driving the
 * message loop off this instead of a fixed 1 ms poll keeps wakeups near the
 * frame rate (~60 Hz) rather than ~1000 Hz, which is what made the Windows
 * build burn far more CPU than the SDL builds while idling between frames. */
static DWORD pace_and_run(app_state_t *a) {
    LARGE_INTEGER now;
    uint64_t elapsed_us, frame_us, remain_us;
    int fps, max_frames, ran = 0;
    int on_ui_thread;
    DWORD wait_ms;

    if (!a) return 10;
    on_ui_thread = (GetCurrentThreadId() == a->ui_thread_id);
    QueryPerformanceCounter(&now);
    if (!a->last_tick.QuadPart) a->last_tick = now;

    if (a->loading || !a->rom_loaded || !a->running)
    {
        a->last_tick = now;
        a->accum_us = 0;
        if (!on_ui_thread) Sleep(1);
        return 16; /* idle: wake ~60 Hz so menus/repaints stay responsive */
    }

    elapsed_us = (uint64_t)(((now.QuadPart - a->last_tick.QuadPart) * 1000000LL) / a->qpf.QuadPart);
    a->last_tick = now;
    if (elapsed_us > 250000) elapsed_us = 250000;

    fps = (sms.display == DISPLAY_PAL) ? FPS_PAL : FPS_NTSC;
    if (fps <= 0) fps = FPS_NTSC;
    frame_us = (uint64_t)(1000000 / fps);

    /* Fast-forward must not wait for real-time frame debt.  Run a small fixed
     * burst every pump, then return a zero wait so the outer message loop keeps
     * dispatching Windows events while emulation runs as fast as the host can. */
    if (a->fast_forward) {
        max_frames = heavyweight_frame_system() ? 2 : 8;
        a->accum_us = 0;
        for (int i = 0; i < max_frames; ++i) {
            audio_and_frame(a);
            ran = 1;
        }
    } else {
        a->accum_us += elapsed_us;

        /* Arcade drivers have multiple CPUs and heavier video/audio paths.  Do
         * not run catch-up bursts on the window thread for them; one frame per
         * pump is enough to keep the UI responsive, and if the host cannot keep
         * up we drop accumulated time rather than blocking Windows dispatch. */
        max_frames = heavyweight_frame_system() ? 1 : 2;

        for (int i = 0; a->accum_us >= frame_us && i < max_frames; ++i)
        {
            a->accum_us -= frame_us;
            audio_and_frame(a);
            ran = 1;
        }

        if (a->accum_us >= frame_us)
            a->accum_us = frame_us - 1;
    }

    /* Do not synchronously paint from the pump.  Let WM_PAINT be dispatched by
     * the normal message loop so menu/non-client work cannot be starved by a
     * heavyweight arcade frame. */
    if (!on_ui_thread)
        Sleep(ran ? 0 : 1);

    /* Fast-forward wants to run flat out; otherwise sleep until the next frame
     * is due (minus ~1 ms of scheduler headroom so we wake just before it). */
    if (a->fast_forward) return 0;
    remain_us = (a->accum_us < frame_us) ? (frame_us - a->accum_us) : 0;
    wait_ms = (DWORD)(remain_us / 1000u);
    return (wait_ms > 1u) ? (wait_ms - 1u) : 1u;
}

void smsp_state(uint8_t slot_number, uint8_t mode) { if (!g_app) return; if (mode == 0) save_slot(g_app, slot_number); else if (mode == 1) load_slot(g_app, slot_number); }
void system_manage_sram(uint8_t *sram, uint8_t slot_number, uint8_t mode) {
    FILE *fp; (void)slot_number;
    if (!g_sram_path[0]) { if (mode == SRAM_LOAD) memset(sram, 0, 0x8000); return; }
    if (mode == SRAM_LOAD) { fp = fopen(g_sram_path, "rb"); if (fp) { fread(sram,1,0x8000,fp); fclose(fp); sms.save = 1; } else memset(sram,0,0x8000); }
    else if (mode == SRAM_SAVE && sms.save) { fp = fopen(g_sram_path, "wb"); if (fp) { fwrite(sram,1,0x8000,fp); fclose(fp); } }
}

static void toggle_fullscreen(app_state_t *a) {
    static WINDOWPLACEMENT wp; static LONG_PTR style, exstyle;
    if (!a || !a->hwnd) return;
    if (!a->fullscreen) {
        MONITORINFO mi; mi.cbSize = sizeof(mi);
        wp.length = sizeof(wp); style = GetWindowLongPtrA(a->hwnd, GWL_STYLE); exstyle = GetWindowLongPtrA(a->hwnd, GWL_EXSTYLE); GetWindowPlacement(a->hwnd, &wp);
        GetMonitorInfoA(MonitorFromWindow(a->hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetMenu(a->hwnd, NULL); SetWindowLongPtrA(a->hwnd, GWL_STYLE, style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW);
        SetWindowPos(a->hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right-mi.rcMonitor.left, mi.rcMonitor.bottom-mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        a->fullscreen = 1;
    } else {
        SetWindowLongPtrA(a->hwnd, GWL_STYLE, style); SetWindowLongPtrA(a->hwnd, GWL_EXSTYLE, exstyle); SetWindowPlacement(a->hwnd, &wp); SetMenu(a->hwnd, a->menu); SetWindowPos(a->hwnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED); a->fullscreen = 0;
    }
}

static void append_control_menu(HMENU menu, app_state_t *a) {
    HMENU controls = CreatePopupMenu();
    HMENU c1 = CreatePopupMenu();
    HMENU c2 = CreatePopupMenu();
    HMENU hot = CreatePopupMenu();
    (void)a;
    AppendMenuA(c1, MF_STRING, IDM_CTRL_C1_KEYBOARD, "Configure keyboard...");
    AppendMenuA(c1, MF_STRING, IDM_CTRL_C1_GAMEPAD, "Configure gamepad...");
    AppendMenuA(c2, MF_STRING, IDM_CTRL_C2_KEYBOARD, "Configure keyboard...");
    AppendMenuA(c2, MF_STRING, IDM_CTRL_C2_GAMEPAD, "Configure gamepad...");
    AppendMenuA(controls, MF_POPUP, (UINT_PTR)c1, "Controller 1");
    AppendMenuA(controls, MF_POPUP, (UINT_PTR)c2, "Controller 2");
    AppendMenuA(controls, MF_SEPARATOR, 0, NULL);
    AppendMenuA(controls, MF_STRING, IDM_CTRL_RESET, "Reset controller mappings");
    AppendMenuA(controls, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < HK_COUNT; ++i) {
        char item[160], key[64];
        vk_name(a->hotkeys[i].vk, key, sizeof(key));
        snprintf(item, sizeof(item), "%s...\t%s", a->hotkeys[i].name, key);
        AppendMenuA(hot, MF_STRING, IDM_HOTKEY_BASE + i, item);
    }
    AppendMenuA(hot, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hot, MF_STRING, IDM_HOTKEY_RESET, "Reset hotkeys");
    AppendMenuA(controls, MF_POPUP, (UINT_PTR)hot, "Hotkeys");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)controls, "Controls");
}

static HMENU create_menu(app_state_t *a) {
    HMENU menu = CreateMenu(); HMENU file = CreatePopupMenu(); HMENU emu = CreatePopupMenu(); HMENU video = CreatePopupMenu(); HMENU machine = CreatePopupMenu(); HMENU biosm = CreatePopupMenu(); HMENU states = CreatePopupMenu(); HMENU slots = CreatePopupMenu(); HMENU help = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, IDM_FILE_OPEN_ROM, "Open ROM/ZIP...\tCtrl+O");
    AppendMenuA(file, MF_SEPARATOR,0,NULL);
    AppendMenuA(file, MF_STRING, IDM_FILE_SAVE_STATE, "Save State As..."); AppendMenuA(file, MF_STRING, IDM_FILE_LOAD_STATE, "Load State...");
    AppendMenuA(file, MF_STRING, IDM_FILE_SCREENSHOT, "Take Screenshot\tF12");
    AppendMenuA(file, MF_SEPARATOR,0,NULL);
    AppendMenuA(file, MF_STRING, IDM_FILE_INPUT_RECORD, "Start input recording..."); AppendMenuA(file, MF_STRING, IDM_FILE_INPUT_STOP_RECORD, "Stop input recording");
    AppendMenuA(file, MF_STRING, IDM_FILE_INPUT_PLAY, "Play input recording..."); AppendMenuA(file, MF_STRING, IDM_FILE_INPUT_STOP_PLAY, "Stop input playback");
    AppendMenuA(file, MF_SEPARATOR,0,NULL); AppendMenuA(file, MF_STRING, IDM_FILE_EXIT, "Exit");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)file, "File");
    AppendMenuA(emu, MF_STRING, IDM_EMU_PAUSE, "Pause/Run"); AppendMenuA(emu, MF_STRING, IDM_EMU_RESET, "Reset"); AppendMenuA(emu, MF_STRING, IDM_EMU_FAST_FORWARD, "Fast forward"); AppendMenuA(emu, MF_STRING, IDM_EMU_REWIND, "Rewind while checked"); AppendMenuA(menu, MF_POPUP, (UINT_PTR)emu, "Emulation");
    for (int i = 0; i < 10; ++i) { char t[64]; snprintf(t, sizeof(t), "Slot %d", i); AppendMenuA(slots, MF_STRING, IDM_STATE_SLOT_BASE + i, t); }
    { char slot_label[64]; snprintf(slot_label, sizeof(slot_label), "Save State Slot = %d", a ? a->save_slot : 0); AppendMenuA(states, MF_POPUP, (UINT_PTR)slots, slot_label); }
    AppendMenuA(states, MF_SEPARATOR, 0, NULL);
    AppendMenuA(states, MF_STRING, IDM_STATE_SAVE_SELECTED, "Save State");
    AppendMenuA(states, MF_STRING, IDM_STATE_LOAD_SELECTED, "Load State");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)states, "States");
    AppendMenuA(video, MF_STRING, IDM_VIDEO_KEEP_ASPECT, "Keep aspect ratio"); AppendMenuA(video, MF_STRING, IDM_VIDEO_INTEGER, "Integer scaling"); AppendMenuA(video, MF_STRING, IDM_VIDEO_STRETCH, "Stretch to window"); AppendMenuA(video, MF_STRING, IDM_VIDEO_FULLSCREEN, "Fullscreen"); AppendMenuA(video, MF_STRING, IDM_VIDEO_LIGHTGUN_CURSOR, "Light Phaser cursor"); AppendMenuA(video, MF_SEPARATOR,0,NULL);
    for (int i=0;i<3;i++) AppendMenuA(video, MF_STRING, IDM_VIDEO_MODE_BASE+i, k_video_modes[i]);
#ifdef _WIN64
    AppendMenuA(video, MF_SEPARATOR,0,NULL); AppendMenuA(video, MF_STRING, IDM_VIDEO_D3D11, "Direct3D 11 renderer");
#endif
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)video, "Video");
    for (int i=0;i<(int)(sizeof(k_console_items)/sizeof(k_console_items[0]));i++) {
        AppendMenuA(machine, MF_STRING, IDM_CONSOLE_BASE+i, k_console_items[i].label);
    }
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)machine, "Machine");
    AppendMenuA(biosm, MF_STRING, IDM_BIOS_DIALOG, "BIOS paths..."); AppendMenuA(biosm, MF_SEPARATOR,0,NULL); AppendMenuA(biosm, MF_STRING, IDM_BIOS_SET_SMS, "Set Master System BIOS..."); AppendMenuA(biosm, MF_STRING, IDM_BIOS_CLEAR_SMS, "Clear Master System BIOS"); AppendMenuA(biosm, MF_STRING, IDM_BIOS_SET_COLECO, "Set ColecoVision BIOS..."); AppendMenuA(biosm, MF_STRING, IDM_BIOS_CLEAR_COLECO, "Clear ColecoVision BIOS"); AppendMenuA(biosm, MF_STRING, IDM_BIOS_SET_M5, "Set Sord M5 BIOS..."); AppendMenuA(biosm, MF_STRING, IDM_BIOS_CLEAR_M5, "Clear Sord M5 BIOS"); AppendMenuA(menu, MF_POPUP, (UINT_PTR)biosm, "BIOS");
    append_control_menu(menu, a);
    AppendMenuA(help, MF_STRING, IDM_HELP_ABOUT, "About MultiRexZ80...");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)help, "Help");
    return menu;
}

static void update_menu_checks(app_state_t *a) {
    if (!a || !a->menu) return;
    CheckMenuItem(a->menu, IDM_EMU_PAUSE, MF_BYCOMMAND | (!a->running ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(a->menu, IDM_EMU_FAST_FORWARD, MF_BYCOMMAND | (a->fast_forward ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(a->menu, IDM_EMU_REWIND, MF_BYCOMMAND | (a->rewind_held ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(a->menu, IDM_VIDEO_KEEP_ASPECT, MF_BYCOMMAND | (a->keep_aspect ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(a->menu, IDM_VIDEO_INTEGER, MF_BYCOMMAND | (a->integer_scaling ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(a->menu, IDM_VIDEO_STRETCH, MF_BYCOMMAND | (a->stretch ? MF_CHECKED : MF_UNCHECKED));
    for (int i = 0; i < 10; ++i)
        CheckMenuItem(a->menu, IDM_STATE_SLOT_BASE + i, MF_BYCOMMAND | (a->save_slot == i ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(a->menu, IDM_VIDEO_LIGHTGUN_CURSOR, MF_BYCOMMAND | (a->lightgun_cursor ? MF_CHECKED : MF_UNCHECKED));
#ifdef _WIN64
    CheckMenuItem(a->menu, IDM_VIDEO_D3D11, MF_BYCOMMAND | (a->use_d3d11 ? MF_CHECKED : MF_UNCHECKED));
#endif
    for (int i=0;i<3;i++) CheckMenuItem(a->menu, IDM_VIDEO_MODE_BASE+i, MF_BYCOMMAND | (a->selected_video_mode==i?MF_CHECKED:MF_UNCHECKED));
    for (int i=0;i<(int)(sizeof(k_console_items)/sizeof(k_console_items[0]));i++) CheckMenuItem(a->menu, IDM_CONSOLE_BASE+i, MF_BYCOMMAND | (a->selected_console==i?MF_CHECKED:MF_UNCHECKED));
}

static void app_rebuild_menu(app_state_t *a) {
    if (!a || !a->hwnd) return;
    SetMenu(a->hwnd, NULL);
    if (a->menu) DestroyMenu(a->menu);
    a->menu = create_menu(a);
    SetMenu(a->hwnd, a->menu);
    update_menu_checks(a);
}

#define BIOSDLG_ID_SMS_EDIT       3001
#define BIOSDLG_ID_COLECO_EDIT    3002
#define BIOSDLG_ID_M5_EDIT        3003
#define BIOSDLG_ID_SMS_BROWSE     3011
#define BIOSDLG_ID_COLECO_BROWSE  3012
#define BIOSDLG_ID_M5_BROWSE      3013
#define BIOSDLG_ID_SMS_CLEAR      3021
#define BIOSDLG_ID_COLECO_CLEAR   3022
#define BIOSDLG_ID_M5_CLEAR       3023
#define BIOSDLG_ID_OK             3031
#define BIOSDLG_ID_CANCEL         3032

typedef struct bios_dialog_state { app_state_t *app; HWND sms_edit, coleco_edit, m5_edit; int done; int accepted; } bios_dialog_state_t;

static void bios_dialog_browse(HWND hwnd, bios_dialog_state_t *d, int which) {
    char path[MAX_PATH];
    if (!d || !select_open_file(hwnd, "Select BIOS", "BIOS/ROM files (*.bin;*.rom;*.col;*.bios;*.zip)\0*.bin;*.rom;*.col;*.bios;*.zip\0All files\0*.*\0", path, sizeof(path))) return;
    if (which == 0) SetWindowTextA(d->sms_edit, path);
    else if (which == 1) SetWindowTextA(d->coleco_edit, path);
    else SetWindowTextA(d->m5_edit, path);
}

static LRESULT CALLBACK bios_dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    bios_dialog_state_t *d = (bios_dialog_state_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lparam;
        d = (bios_dialog_state_t *)cs->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        CreateWindowA("STATIC", "Master System BIOS (optional):", WS_CHILD|WS_VISIBLE, 12, 16, 180, 20, hwnd, NULL, d->app->inst, NULL);
        d->sms_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->app->sms_bios_path, WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 12, 38, 390, 22, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_SMS_EDIT, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE, 410, 37, 76, 24, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_SMS_BROWSE, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Clear", WS_CHILD|WS_VISIBLE, 492, 37, 58, 24, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_SMS_CLEAR, d->app->inst, NULL);
        CreateWindowA("STATIC", "ColecoVision BIOS:", WS_CHILD|WS_VISIBLE, 12, 72, 180, 20, hwnd, NULL, d->app->inst, NULL);
        d->coleco_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->app->coleco_bios_path, WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 12, 94, 390, 22, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_COLECO_EDIT, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE, 410, 93, 76, 24, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_COLECO_BROWSE, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Clear", WS_CHILD|WS_VISIBLE, 492, 93, 58, 24, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_COLECO_CLEAR, d->app->inst, NULL);
        CreateWindowA("STATIC", "Sord M5 BIOS (optional):", WS_CHILD|WS_VISIBLE, 12, 128, 180, 20, hwnd, NULL, d->app->inst, NULL);
        d->m5_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", d->app->m5_bios_path, WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 12, 150, 390, 22, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_M5_EDIT, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE, 410, 149, 76, 24, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_M5_BROWSE, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Clear", WS_CHILD|WS_VISIBLE, 492, 149, 58, 24, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_M5_CLEAR, d->app->inst, NULL);
        CreateWindowA("STATIC", "Paths are saved in MultiRexZ80.ini. Clearing SMS BIOS is valid; ROM database/CRC detection still selects SMS/SMS2/PAL/NTSC.", WS_CHILD|WS_VISIBLE, 12, 184, 540, 34, hwnd, NULL, d->app->inst, NULL);
        CreateWindowA("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 380, 226, 76, 26, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_OK, d->app->inst, NULL);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 466, 226, 76, 26, hwnd, (HMENU)(UINT_PTR)BIOSDLG_ID_CANCEL, d->app->inst, NULL);
        return 0;
    }
    case WM_COMMAND:
        if (!d) break;
        switch (LOWORD(wparam)) {
        case BIOSDLG_ID_SMS_BROWSE: bios_dialog_browse(hwnd, d, 0); return 0;
        case BIOSDLG_ID_COLECO_BROWSE: bios_dialog_browse(hwnd, d, 1); return 0;
        case BIOSDLG_ID_M5_BROWSE: bios_dialog_browse(hwnd, d, 2); return 0;
        case BIOSDLG_ID_SMS_CLEAR: SetWindowTextA(d->sms_edit, ""); return 0;
        case BIOSDLG_ID_COLECO_CLEAR: SetWindowTextA(d->coleco_edit, ""); return 0;
        case BIOSDLG_ID_M5_CLEAR: SetWindowTextA(d->m5_edit, ""); return 0;
        case BIOSDLG_ID_OK:
            GetWindowTextA(d->sms_edit, d->app->sms_bios_path, sizeof(d->app->sms_bios_path));
            GetWindowTextA(d->coleco_edit, d->app->coleco_bios_path, sizeof(d->app->coleco_bios_path));
            GetWindowTextA(d->m5_edit, d->app->m5_bios_path, sizeof(d->app->m5_bios_path));
            d->accepted = 1; d->done = 1; DestroyWindow(hwnd); return 0;
        case BIOSDLG_ID_CANCEL: d->done = 1; DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_CLOSE: if (d) d->done = 1; DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static void show_bios_paths(app_state_t *a) {
    WNDCLASSEXA wc; HWND dlg; MSG msg; bios_dialog_state_t d; RECT rc = {0,0,570,300};
    if (!a) return;
    memset(&wc, 0, sizeof(wc)); wc.cbSize = sizeof(wc); wc.lpfnWndProc = bios_dialog_proc; wc.hInstance = a->inst; wc.lpszClassName = "MultiRexZ80BiosDialog"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExA(&wc);
    memset(&d, 0, sizeof(d)); d.app = a;
    AdjustWindowRect(&rc, WS_CAPTION|WS_SYSMENU, FALSE);
    EnableWindow(a->hwnd, FALSE);
    dlg = CreateWindowExA(WS_EX_DLGMODALFRAME, wc.lpszClassName, "MultiRexZ80 BIOS Paths", WS_CAPTION|WS_SYSMENU|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rc.right-rc.left, rc.bottom-rc.top, a->hwnd, NULL, a->inst, &d);
    while (!d.done && GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    EnableWindow(a->hwnd, TRUE); SetForegroundWindow(a->hwnd);
    if (d.accepted) { save_config(a); reload_current(a); }
}

static void set_bios_path(app_state_t *a, int which) {
    char path[MAX_PATH];
    if (!select_open_file(a->hwnd, "Select BIOS", "BIOS/ROM files (*.bin;*.rom;*.col;*.bios;*.zip)\0*.bin;*.rom;*.col;*.bios;*.zip\0All files\0*.*\0", path, sizeof(path))) return;
    if (which == 0) snprintf(a->sms_bios_path, sizeof(a->sms_bios_path), "%s", path);
    else if (which == 1) snprintf(a->coleco_bios_path, sizeof(a->coleco_bios_path), "%s", path);
    else snprintf(a->m5_bios_path, sizeof(a->m5_bios_path), "%s", path);
    save_config(a); reload_current(a);
}


static void show_about(app_state_t *a) {
    MessageBoxA(a ? a->hwnd : NULL,
        "MultiRexZ80\n\n"
        "A multi Z80-based emulator.\n\n"
        "Author: gameblabla\n\n"
        "Win32 frontend integration for native Windows builds.",
        "About MultiRexZ80", MB_OK | MB_ICONINFORMATION);
}

static void app_command(app_state_t *a, UINT id) {
    char path[MAX_PATH]; UINT vk;
    if (!a) return;
    if (id >= IDM_CONSOLE_BASE && id < IDM_CONSOLE_BASE + 64) { a->selected_console = (int)(id - IDM_CONSOLE_BASE); save_config(a); reload_current(a); update_menu_checks(a); return; }
    if (id >= IDM_VIDEO_MODE_BASE && id < IDM_VIDEO_MODE_BASE + 3) { a->selected_video_mode = (int)(id - IDM_VIDEO_MODE_BASE); save_config(a); reload_current(a); update_menu_checks(a); app_force_video_refresh(a); return; }
    if (id >= IDM_STATE_SLOT_BASE && id < IDM_STATE_SLOT_BASE + 10) { a->save_slot = (int)(id - IDM_STATE_SLOT_BASE); save_config(a); app_rebuild_menu(a); return; }
    if (id == IDM_CTRL_C1_KEYBOARD) { if (show_controller_setup(a, 0, 0)) { save_config(a); app_rebuild_menu(a); } return; }
    if (id == IDM_CTRL_C1_GAMEPAD) { if (show_controller_setup(a, 0, 1)) { save_config(a); app_rebuild_menu(a); } return; }
    if (id == IDM_CTRL_C2_KEYBOARD) { if (show_controller_setup(a, 1, 0)) { save_config(a); app_rebuild_menu(a); } return; }
    if (id == IDM_CTRL_C2_GAMEPAD) { if (show_controller_setup(a, 1, 1)) { save_config(a); app_rebuild_menu(a); } return; }
    if (id >= IDM_HOTKEY_BASE && id < IDM_HOTKEY_BASE + HK_COUNT) {
        int idx = (int)(id - IDM_HOTKEY_BASE);
        vk = capture_key(a, a->hotkeys[idx].name);
        if (vk) { a->hotkeys[idx].vk = vk; save_config(a); app_rebuild_menu(a); }
        return;
    }
    switch (id) {
    case IDM_FILE_OPEN_ROM: if (select_open_file(a->hwnd, "Open ROM or ZIP", "ROM/ZIP files (*.sms;*.gg;*.sg;*.sc;*.sf;*.m5;*.cv;*.col;*.rom;*.bin;*.zip)\0*.sms;*.gg;*.sg;*.sc;*.sf;*.m5;*.cv;*.col;*.rom;*.bin;*.zip\0All files\0*.*\0", path, sizeof(path))) app_queue_load_game(a, path); break;
    case IDM_FILE_SAVE_STATE: if (select_save_file(a->hwnd, "Save state", "PNG save states (*.png)\0*.png\0All files\0*.*\0", path, sizeof(path))) { uint32_t w,h,p; uint32_t *thumb = capture_thumb(a,&w,&h,&p); system_save_state_file_ex(path, thumb, w,h,p); free(thumb); } break;
    case IDM_FILE_LOAD_STATE: if (select_open_file(a->hwnd, "Load state", "PNG/raw save states (*.png;*.sgxst;*.state)\0*.png;*.sgxst;*.state\0All files\0*.*\0", path, sizeof(path))) { app_lock_core(a); if (system_load_state_file(path)) { render_loaded_state_preview_frame(); app_reset_timing(a, 1); update_present_pixels(a); app_unlock_core(a); app_request_repaint(a); app_set_status(a, "State loaded"); } else { app_unlock_core(a); app_error(a, "Failed to load state"); } } break;
    case IDM_FILE_SCREENSHOT: if (select_save_file(a->hwnd, "Save screenshot", "PPM screenshot (*.ppm)\0*.ppm\0All files\0*.*\0", path, sizeof(path))) save_ppm(a, path); break;
    case IDM_FILE_INPUT_RECORD: if (!a->recorder && select_save_file(a->hwnd, "Record input", "Input records (*.input)\0*.input\0All files\0*.*\0", path, sizeof(path))) { if (!multirexz80_input_recorder_open(&a->recorder, path, 1)) app_error(a, "Failed to open input record"); } break;
    case IDM_FILE_INPUT_STOP_RECORD: if (a->recorder) { multirexz80_input_recorder_close(a->recorder); a->recorder=NULL; } break;
    case IDM_FILE_INPUT_PLAY: if (select_open_file(a->hwnd, "Play input record", "Input records (*.input;*.script)\0*.input;*.script\0All files\0*.*\0", path, sizeof(path))) { if (a->playback) multirexz80_input_script_free(a->playback); a->playback=NULL; if (!multirexz80_input_script_load(&a->playback, path, MULTIREXZ80_INPUT_TAP_FRAMES_DEFAULT)) app_error(a,"Failed to load input playback"); else multirexz80_input_script_reset(a->playback); } break;
    case IDM_FILE_INPUT_STOP_PLAY: if (a->playback) { multirexz80_input_script_free(a->playback); a->playback=NULL; } break;
    case IDM_FILE_EXIT: PostMessageA(a->hwnd, WM_CLOSE, 0, 0); break;
    case IDM_EMU_PAUSE: a->running = !a->running; update_menu_checks(a); break;
    case IDM_EMU_RESET: if (a->rom_loaded) { app_lock_core(a); system_reset(); app_reset_timing(a, 1); app_unlock_core(a); } break;
    case IDM_EMU_FAST_FORWARD: a->fast_forward = !a->fast_forward; app_reset_timing(a, 1); update_menu_checks(a); break;
    case IDM_EMU_REWIND: a->rewind_held = !a->rewind_held; app_reset_timing(a, 1); update_menu_checks(a); break;
    case IDM_STATE_SAVE_SELECTED: save_slot(a, a->save_slot); break;
    case IDM_STATE_LOAD_SELECTED: load_slot(a, a->save_slot); break;
    case IDM_VIDEO_KEEP_ASPECT: a->keep_aspect=1; a->integer_scaling=0; a->stretch=0; save_config(a); update_menu_checks(a); app_force_video_refresh(a); break;
    case IDM_VIDEO_INTEGER: a->keep_aspect=0; a->integer_scaling=1; a->stretch=0; save_config(a); update_menu_checks(a); app_force_video_refresh(a); break;
    case IDM_VIDEO_STRETCH: a->keep_aspect=0; a->integer_scaling=0; a->stretch=1; save_config(a); update_menu_checks(a); app_force_video_refresh(a); break;
    case IDM_VIDEO_FULLSCREEN: toggle_fullscreen(a); update_menu_checks(a); break;
    case IDM_VIDEO_LIGHTGUN_CURSOR: a->lightgun_cursor=!a->lightgun_cursor; option.lightgun_cursor=a->lightgun_cursor; save_config(a); update_menu_checks(a); break;
#ifdef _WIN64
    case IDM_VIDEO_D3D11:
        app_lock_present(a);
        app_set_d3d11(a, !a->use_d3d11);
        app_unlock_present(a);
        save_config(a); update_menu_checks(a); app_force_video_refresh(a);
        if (!a->use_d3d11) app_set_status(a, "Renderer: GDI"); else app_set_status(a, "Renderer: Direct3D 11");
        break;
#endif
    case IDM_BIOS_DIALOG: show_bios_paths(a); break;
    case IDM_BIOS_SET_SMS: set_bios_path(a,0); break; case IDM_BIOS_SET_COLECO: set_bios_path(a,1); break; case IDM_BIOS_SET_M5: set_bios_path(a,2); break;
    case IDM_BIOS_CLEAR_SMS: a->sms_bios_path[0]=0; save_config(a); reload_current(a); break; case IDM_BIOS_CLEAR_COLECO: a->coleco_bios_path[0]=0; save_config(a); reload_current(a); break; case IDM_BIOS_CLEAR_M5: a->m5_bios_path[0]=0; save_config(a); reload_current(a); break;
    case IDM_CTRL_RESET: default_controls(a); save_config(a); app_rebuild_menu(a); break;
    case IDM_HOTKEY_RESET: default_hotkeys(a); save_config(a); app_rebuild_menu(a); break;
    case IDM_HELP_ABOUT: show_about(a); break;
    default: break;
    }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    app_state_t *a = (app_state_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: { CREATESTRUCTA *cs = (CREATESTRUCTA *)lparam; a = (app_state_t *)cs->lpCreateParams; SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)a); a->hwnd = hwnd; return 0; }
    case WM_COMMAND: app_command(a, LOWORD(wparam)); return 0;
    case WM_APP_FRAME_READY: if (a) { InvalidateRect(hwnd, NULL, FALSE); } return 0;
    case WM_APP_LOAD_ROM: if (a) { char path[MAX_PATH]; if (InterlockedExchange(&a->pending_load, 0)) { snprintf(path, sizeof(path), "%s", a->pending_load_path); a->pending_load_path[0] = 0; if (path[0]) load_game(a, path); } } return 0;
    case WM_ERASEBKGND: return 1; /* paint_frame fully covers the client; suppress the default erase to stop resize flicker */
    case WM_PAINT: { PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); if (a) paint_frame(a, hdc); EndPaint(hwnd, &ps); if (a) InterlockedExchange(&a->repaint_pending, 0); return 0; }
    case WM_SIZE:
        if (a) {
#ifdef _WIN64
            if (a->d3d) win_d3d11_resize(a->d3d, (int)LOWORD(lparam), (int)HIWORD(lparam));
#endif
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: if (a && a->rom_is_lightgun) { SetCapture(hwnd); a->mouse_captured=1; update_lightgun_mouse(a,lparam,wparam|MK_LBUTTON); } return 0;
    case WM_LBUTTONUP: if (a && a->mouse_captured) { ReleaseCapture(); a->mouse_captured=0; update_lightgun_mouse(a,lparam,wparam); } return 0;
    case WM_MOUSEMOVE: if (a && a->mouse_captured) update_lightgun_mouse(a,lparam,wparam); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (a) {
            hotkey_id_t hk = hotkey_for_vk(a, (UINT)wparam);
            if (hk != HK_COUNT) {
                if (!(lparam & (1u << 30))) apply_hotkey(a, hk);
                return 0;
            }
            a->kbd_actions |= action_mask_for_vk(a, (UINT)wparam);
        }
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP:
        if (a) {
            hotkey_id_t hk = hotkey_for_vk(a, (UINT)wparam);
            if (hk != HK_COUNT) {
                if (hk == HK_REWIND) { a->rewind_held = 0; app_reset_timing(a, 1); update_menu_checks(a); }
                return 0;
            }
            a->kbd_actions &= ~action_mask_for_vk(a, (UINT)wparam);
        }
        return 0;
    case WM_DROPFILES: if (a) { char path[MAX_PATH]; HDROP h = (HDROP)wparam; if (DragQueryFileA(h, 0, path, sizeof(path))) app_queue_load_game(a, path); DragFinish(h); } return 0;
    case WM_CLOSE: if (a) a->quit = 1; DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
}

static int parse_cli(app_state_t *a, LPSTR cmdline) {
    int argc = 0; LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc); char path[MAX_PATH];
    if (!wargv) return 0;
    for (int i = 1; i < argc; ++i) {
        char arg[MAX_PATH]; WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, arg, sizeof(arg), NULL, NULL);
        if (!strcmp(arg, "--console") && i + 1 < argc) { char v[64]; WideCharToMultiByte(CP_UTF8,0,wargv[++i],-1,v,sizeof(v),NULL,NULL); for (int c=0;c<(int)(sizeof(k_console_items)/sizeof(k_console_items[0]));c++) if (str_ieq(v,k_console_items[c].label) || str_ieq(v, "auto")) { a->selected_console = c; break; } if (str_ieq(v,"sms2")) a->selected_console=C_SMS2; else if (str_ieq(v,"sms")||str_ieq(v,"sms1")) a->selected_console=C_SMS1_EXPORT; else if (str_ieq(v,"sms1jp")) a->selected_console=C_SMS1_JP; else if (str_ieq(v,"coleco")||str_ieq(v,"cv")) a->selected_console=C_COLECO; else if (str_ieq(v,"sg")||str_ieq(v,"sg1000")) a->selected_console=C_SG1000; else if (str_ieq(v,"m5")||str_ieq(v,"sordm5")) a->selected_console=C_SORDM5; else if (str_ieq(v,"gg")) a->selected_console=C_GG; else if (str_ieq(v,"ggms")||str_ieq(v,"ggsms")) a->selected_console=C_GGMS; else if (str_ieq(v,"systeme")) a->selected_console=C_SYSTEME; else if (str_ieq(v,"system1")) a->selected_console=C_SYSTEM1; else if (str_ieq(v,"snk")||str_ieq(v,"psychos")) a->selected_console=C_SNK; else if (str_ieq(v,"taitol")||str_ieq(v,"taito")) a->selected_console=C_TAITOL; }
        else if ((!strcmp(arg,"--region") || !strcmp(arg,"--video-mode")) && i + 1 < argc) { char v[64]; WideCharToMultiByte(CP_UTF8,0,wargv[++i],-1,v,sizeof(v),NULL,NULL); if (str_ieq(v,"pal")||str_ieq(v,"50")||str_ieq(v,"50hz")) a->selected_video_mode=VMODE_PAL; else if (str_ieq(v,"ntsc")||str_ieq(v,"60")||str_ieq(v,"60hz")) a->selected_video_mode=VMODE_NTSC; else a->selected_video_mode=VMODE_AUTO; }
        else if (!strcmp(arg,"--bios") && i+1<argc) WideCharToMultiByte(CP_UTF8,0,wargv[++i],-1,a->sms_bios_path,sizeof(a->sms_bios_path),NULL,NULL);
        else if (!strcmp(arg,"--coleco-bios") && i+1<argc) WideCharToMultiByte(CP_UTF8,0,wargv[++i],-1,a->coleco_bios_path,sizeof(a->coleco_bios_path),NULL,NULL);
        else if (!strcmp(arg,"--m5-bios") && i+1<argc) WideCharToMultiByte(CP_UTF8,0,wargv[++i],-1,a->m5_bios_path,sizeof(a->m5_bios_path),NULL,NULL);
        else if (arg[0] != '-') { WideCharToMultiByte(CP_UTF8,0,wargv[i],-1,path,sizeof(path),NULL,NULL); snprintf(a->rom_path,sizeof(a->rom_path),"%s",path); }
    }
    LocalFree(wargv); (void)cmdline; return a->rom_path[0] ? 1 : 0;
}

static DWORD WINAPI emu_thread_proc(LPVOID user) {
    app_state_t *a = (app_state_t *)user;
    while (a && !InterlockedCompareExchange(&a->emu_thread_stop, 0, 0)) {
        pace_and_run(a);
    }
    return 0;
}

static int app_start_emu_thread(app_state_t *a) {
    if (!a) return 0;
    if (a->emu_thread) return 1;
    InterlockedExchange(&a->emu_thread_stop, 0);
    a->emu_thread = CreateThread(NULL, 0, emu_thread_proc, a, 0, NULL);
    if (!a->emu_thread) return 0;
    return 1;
}

static void app_stop_emu_thread(app_state_t *a) {
    HANDLE thread;
    if (!a || !a->emu_thread) return;
    thread = a->emu_thread;
    a->emu_thread = NULL;
    InterlockedExchange(&a->emu_thread_stop, 1);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    a->last_tick.QuadPart = 0;
    a->accum_us = 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    WNDCLASSEXA wc; MSG msg; app_state_t app; RECT wr = {0,0,960,720}; int have_cli_rom;
    (void)prev; memset(&app, 0, sizeof(app)); g_app = &app; app.inst = inst; app.ui_thread_id = GetCurrentThreadId(); app.running = 0; app.keep_aspect = 1; app.lightgun_cursor = 1; app.selected_console = 0; app.selected_video_mode = 0; app.save_slot = 0; QueryPerformanceFrequency(&app.qpf); InitializeCriticalSection(&app.core_lock); InitializeCriticalSection(&app.present_lock); app.locks_ready = 1; default_bindings(&app); app_make_paths(&app);
#ifdef _WIN64
    app.use_d3d11 = 1; /* default on for the modern 64-bit build; config/menu can override */
#endif
    load_config(&app); have_cli_rom = parse_cli(&app, cmdline);
    memset(&wc, 0, sizeof(wc)); wc.cbSize = sizeof(wc); wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "MultiRexZ80Win32"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(IDI_MULTIREXZ80)); wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (!RegisterClassExA(&wc)) return 1;
    app.menu = create_menu(&app); AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, TRUE);
    app.hwnd = CreateWindowExA(0, wc.lpszClassName, APP_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, wr.right-wr.left, wr.bottom-wr.top, NULL, app.menu, inst, &app);
    if (!app.hwnd) return 1;
    DragAcceptFiles(app.hwnd, TRUE);
#ifdef _WIN64
    if (app.use_d3d11) app_set_d3d11(&app, 1); /* clears use_d3d11 if D3D11 is unavailable */
#endif
#ifdef MULTIREXZ80_HAVE_SDL3
    app.sdl_input = win_sdl3_input_create(); /* NULL if SDL/joystick unavailable; keyboard still works */
#endif
#ifndef _WIN64
    app.winmm_input = win_winmm_input_create(); /* WinMM joystick for the Win32 build */
#endif
    timeBeginPeriod(1);
    ShowWindow(app.hwnd, show);
    UpdateWindow(app.hwnd);
    update_menu_checks(&app);
    if (have_cli_rom) load_game(&app, app.rom_path);

    while (!app.quit) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { app.quit = 1; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (app.quit) break;
        if (InterlockedCompareExchange(&app.pending_load, 0, 0)) {
            char queued_path[MAX_PATH];
            InterlockedExchange(&app.pending_load, 0);
            snprintf(queued_path, sizeof(queued_path), "%s", app.pending_load_path);
            app.pending_load_path[0] = 0;
            if (queued_path[0]) load_game(&app, queued_path);
            continue;
        }

        /* Drive emulation only from the outer message loop.  Unlike WM_TIMER,
         * this cannot re-enter through modal file dialogs/menus while switching
         * from a console ROM to a MAME-style arcade ZIP. */
        {
            DWORD wait_ms = pace_and_run(&app);
            MsgWaitForMultipleObjects(0, NULL, FALSE, wait_ms, QS_ALLINPUT);
        }
    }

    app_stop_emu_thread(&app);
    timeEndPeriod(1);
    app.quit = 1;
    app_lock_core(&app);
    if (app.recorder) multirexz80_input_recorder_close(app.recorder);
    if (app.playback) multirexz80_input_script_free(app.playback);
    if (app.rom_loaded) { system_poweroff(); system_shutdown(); }
    audio_close(&app); rewind_clear(&app); if (bios.rom) free(bios.rom); free(g_pixels);
    app_unlock_core(&app);
    app_lock_present(&app); free(app.present_pixels); app.present_pixels = NULL; destroy_backbuffer(&app);
#ifdef _WIN64
    if (app.d3d) { win_d3d11_destroy(app.d3d); app.d3d = NULL; }
#endif
    app_unlock_present(&app);
#ifdef MULTIREXZ80_HAVE_SDL3
    if (app.sdl_input) { win_sdl3_input_destroy(app.sdl_input); app.sdl_input = NULL; }
#endif
#ifndef _WIN64
    if (app.winmm_input) { win_winmm_input_destroy(app.winmm_input); app.winmm_input = NULL; }
#endif
    save_config(&app);
    app.locks_ready = 0; DeleteCriticalSection(&app.present_lock); DeleteCriticalSection(&app.core_lock);
    return 0;
}
