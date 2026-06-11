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

// SDL3 + Dear ImGui frontend for MultiRexZ80.
// Frontend/shell integration code for this port is MultiRexZ80 project code.
// This port is intentionally separate from the old SDL 1.2 frontend and the
// headless runner.  It drives the Sord M5 keyboard matrix directly while still
// exposing normal gamepad-style controls for SMS/GG/SG/Coleco games.

extern "C" {
#include "shared.h"
#include "input_script.h"
}

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

#ifndef SDL_BUTTON_LMASK
#define SDL_BUTTON_LMASK 0x01u
#endif
#ifndef SDL_SCANCODE_F2
#define SDL_SCANCODE_F2 ((SDL_Scancode)59)
#endif
#ifndef SDL_SCANCODE_F5
#define SDL_SCANCODE_F5 ((SDL_Scancode)62)
#endif
#ifndef SDL_SCANCODE_F6
#define SDL_SCANCODE_F6 ((SDL_Scancode)63)
#endif
#ifndef SDL_SCANCODE_F8
#define SDL_SCANCODE_F8 ((SDL_Scancode)65)
#endif
#ifndef SDL_SCANCODE_F11
#define SDL_SCANCODE_F11 ((SDL_Scancode)68)
#endif
#ifndef SDL_GAMEPAD_BUTTON_INVALID
#define SDL_GAMEPAD_BUTTON_INVALID ((SDL_GamepadButton)-1)
#endif

#ifndef SDL_TEXTUREACCESS_STREAMING
#define SDL_TEXTUREACCESS_STREAMING SDL_TEXTUREACCESS_STREAMING
#endif
#ifndef SDL_SCANCODE_KP_0
#define SDL_SCANCODE_KP_0 SDL_SCANCODE_0
#endif
#ifndef SDL_SCANCODE_KP_1
#define SDL_SCANCODE_KP_1 SDL_SCANCODE_1
#endif
#ifndef SDL_SCANCODE_KP_2
#define SDL_SCANCODE_KP_2 SDL_SCANCODE_2
#endif
#ifndef SDL_SCANCODE_KP_3
#define SDL_SCANCODE_KP_3 SDL_SCANCODE_3
#endif
#ifndef SDL_SCANCODE_KP_4
#define SDL_SCANCODE_KP_4 SDL_SCANCODE_4
#endif
#ifndef SDL_SCANCODE_KP_5
#define SDL_SCANCODE_KP_5 SDL_SCANCODE_5
#endif
#ifndef SDL_SCANCODE_KP_6
#define SDL_SCANCODE_KP_6 SDL_SCANCODE_6
#endif
#ifndef SDL_SCANCODE_KP_7
#define SDL_SCANCODE_KP_7 SDL_SCANCODE_7
#endif
#ifndef SDL_SCANCODE_KP_8
#define SDL_SCANCODE_KP_8 SDL_SCANCODE_8
#endif
#ifndef SDL_SCANCODE_KP_9
#define SDL_SCANCODE_KP_9 SDL_SCANCODE_9
#endif
#ifndef SDL_SCANCODE_KP_MULTIPLY
#define SDL_SCANCODE_KP_MULTIPLY SDL_SCANCODE_UNKNOWN
#endif
#ifndef SDL_SCANCODE_KP_HASH
#define SDL_SCANCODE_KP_HASH SDL_SCANCODE_UNKNOWN
#endif
#ifndef SDL_SCANCODE_APOSTROPHE
#define SDL_SCANCODE_APOSTROPHE SDL_SCANCODE_UNKNOWN
#endif
#ifndef SDL_SCANCODE_BACKSLASH
#define SDL_SCANCODE_BACKSLASH SDL_SCANCODE_UNKNOWN
#endif

extern "C" { t_config option; }

static constexpr const char *MULTIREXZ80_SDL3_WINDOW_TITLE = "MultiRexZ80";
static constexpr const char *MULTIREXZ80_SDL3_MENU_TITLE = "MultiRexZ80 SDL3";
static constexpr Uint64 MENU_HINT_DURATION_NS = 3ull * 1000ull * 1000ull * 1000ull;

static void *g_pixels = nullptr;
static std::string g_sram_path;
static uint8_t g_m5_text_pulse[SORDM5_KEY_ROWS];

static constexpr int BITMAP_W = 400;
static constexpr int BITMAP_H = 313;

struct UiSettings
{
    bool keep_aspect = true;
    bool stretch = false;
    bool linear_filter = false;
    bool pixel_perfect = true;
    bool fullscreen = false;
    bool show_menu = true;
    bool show_keyboard = false;
    bool audio = true;
    bool force_lightgun = false;
    bool db_lightgun = false;
    bool mouse_lightgun = true;
    bool show_lightgun_cursor = true;
    int browser_index = 0;
    std::filesystem::path launch_cwd = std::filesystem::current_path();
    std::filesystem::path browser_dir = launch_cwd;
};

struct Binding
{
    const char *name;
    SDL_Scancode scan;
    SDL_GamepadButton button;
};

enum Action
{
    ACT_UP,
    ACT_DOWN,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_A,
    ACT_B,
    ACT_ROTATE_LEFT,
    ACT_ROTATE_RIGHT,
    ACT_AIM_UP,
    ACT_AIM_DOWN,
    ACT_AIM_LEFT,
    ACT_AIM_RIGHT,
    ACT_M5_1,
    ACT_M5_2,
    ACT_PAUSE,
    ACT_MENU,
    ACT_VKBD,
    ACT_SAVE_STATE,
    ACT_LOAD_STATE,
    ACT_REWIND,
    ACT_COUNT
};

static std::array<Binding, ACT_COUNT> g_bindings = {{
    {"Up", SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"Down", SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"Left", SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"Right", SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"Button 1 / Space", SDL_SCANCODE_Z, SDL_GAMEPAD_BUTTON_SOUTH},
    {"Button 2 / Enter", SDL_SCANCODE_X, SDL_GAMEPAD_BUTTON_EAST},
    {"SNK Rotate Left", SDL_SCANCODE_Q, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"SNK Rotate Right", SDL_SCANCODE_E, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"SNK Aim Up", SDL_SCANCODE_W, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"SNK Aim Down", SDL_SCANCODE_S, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"SNK Aim Left", SDL_SCANCODE_A, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"SNK Aim Right", SDL_SCANCODE_D, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"M5 1", SDL_SCANCODE_1, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"M5 2", SDL_SCANCODE_2, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"Pause", SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_START},
    {"Menu", SDL_SCANCODE_ESCAPE, SDL_GAMEPAD_BUTTON_BACK},
    {"Virtual Keyboard", SDL_SCANCODE_TAB, SDL_GAMEPAD_BUTTON_NORTH},
    {"Save State", SDL_SCANCODE_F5, SDL_GAMEPAD_BUTTON_INVALID},
    {"Load State", SDL_SCANCODE_F8, SDL_GAMEPAD_BUTTON_INVALID},
    {"Rewind", SDL_SCANCODE_F6, SDL_GAMEPAD_BUTTON_INVALID},
}};

struct ColecoKeyBinding
{
    const char *name;
    uint8_t value;
    SDL_Scancode scan;
    SDL_Scancode alt_scan;
    SDL_GamepadButton button;
};

static std::array<ColecoKeyBinding, 12> g_coleco_key_bindings = {{
    {"Keypad 0", 0,  SDL_SCANCODE_0, SDL_SCANCODE_KP_0, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad 1", 1,  SDL_SCANCODE_1, SDL_SCANCODE_KP_1, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad 2", 2,  SDL_SCANCODE_2, SDL_SCANCODE_KP_2, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad 3", 3,  SDL_SCANCODE_3, SDL_SCANCODE_KP_3, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad 4", 4,  SDL_SCANCODE_4, SDL_SCANCODE_KP_4, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad 5", 5,  SDL_SCANCODE_5, SDL_SCANCODE_KP_5, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad 6", 6,  SDL_SCANCODE_6, SDL_SCANCODE_KP_6, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad 7", 7,  SDL_SCANCODE_7, SDL_SCANCODE_KP_7, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad 8", 8,  SDL_SCANCODE_8, SDL_SCANCODE_KP_8, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad 9", 9,  SDL_SCANCODE_9, SDL_SCANCODE_KP_9, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad *", 10, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_KP_MULTIPLY, SDL_GAMEPAD_BUTTON_INVALID},
    {"Keypad #", 11, SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_KP_HASH, SDL_GAMEPAD_BUTTON_INVALID},
}};

struct ArcadeBinding
{
    const char *name;
    uint8_t mask;
    SDL_Scancode scan;
    SDL_GamepadButton button;
};

static std::array<ArcadeBinding, 6> g_arcade_bindings = {{
    {"Coin 1",   INPUT_ARCADE_COIN1,   SDL_SCANCODE_5, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"Coin 2",   INPUT_ARCADE_COIN2,   SDL_SCANCODE_6, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"Service",  INPUT_ARCADE_SERVICE, SDL_SCANCODE_9, SDL_GAMEPAD_BUTTON_INVALID},
    {"Test",     INPUT_ARCADE_TEST,    SDL_SCANCODE_F2, SDL_GAMEPAD_BUTTON_INVALID},
    {"Start 1",  INPUT_ARCADE_START1,  SDL_SCANCODE_1, SDL_GAMEPAD_BUTTON_START},
    {"Start 2",  INPUT_ARCADE_START2,  SDL_SCANCODE_2, SDL_GAMEPAD_BUTTON_INVALID},
}};

struct M5Key
{
    const char *label;
    uint8_t row;
    uint8_t mask;
    SDL_Keycode key;
    SDL_Scancode scan;
};

// Rows are active-high and follow the MAME m5.cpp input port layout where known.
// The direct number-row mapping is required by Pooyan's title screen.
static constexpr M5Key kM5Keys[] = {
    {"Ctrl", 0, 0x01, SDLK_LCTRL, SDL_SCANCODE_LCTRL},
    {"Func", 0, 0x02, SDLK_TAB, SDL_SCANCODE_TAB},
    {"Shift", 0, 0x04, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT},
    {"RShift", 0, 0x08, SDLK_RSHIFT, SDL_SCANCODE_RSHIFT},
    {"Space", 0, 0x40, SDLK_SPACE, SDL_SCANCODE_SPACE},
    {"Enter", 0, 0x80, SDLK_RETURN, SDL_SCANCODE_RETURN},

    {"1", 1, 0x01, SDLK_1, SDL_SCANCODE_1}, {"2", 1, 0x02, SDLK_2, SDL_SCANCODE_2},
    {"3", 1, 0x04, SDLK_3, SDL_SCANCODE_3}, {"4", 1, 0x08, SDLK_4, SDL_SCANCODE_4},
    {"5", 1, 0x10, SDLK_5, SDL_SCANCODE_5}, {"6", 1, 0x20, SDLK_6, SDL_SCANCODE_6},
    {"7", 1, 0x40, SDLK_7, SDL_SCANCODE_7}, {"8", 1, 0x80, SDLK_8, SDL_SCANCODE_8},

    {"Q", 2, 0x01, SDLK_Q, SDL_SCANCODE_Q}, {"W", 2, 0x02, SDLK_W, SDL_SCANCODE_W},
    {"E", 2, 0x04, SDLK_E, SDL_SCANCODE_E}, {"R", 2, 0x08, SDLK_R, SDL_SCANCODE_R},
    {"T", 2, 0x10, SDLK_T, SDL_SCANCODE_T}, {"Y", 2, 0x20, SDLK_Y, SDL_SCANCODE_Y},
    {"U", 2, 0x40, SDLK_U, SDL_SCANCODE_U}, {"I", 2, 0x80, SDLK_I, SDL_SCANCODE_I},

    {"A", 3, 0x01, SDLK_A, SDL_SCANCODE_A}, {"S", 3, 0x02, SDLK_S, SDL_SCANCODE_S},
    {"D", 3, 0x04, SDLK_D, SDL_SCANCODE_D}, {"F", 3, 0x08, SDLK_F, SDL_SCANCODE_F},
    {"G", 3, 0x10, SDLK_G, SDL_SCANCODE_G}, {"H", 3, 0x20, SDLK_H, SDL_SCANCODE_H},
    {"J", 3, 0x40, SDLK_J, SDL_SCANCODE_J}, {"K", 3, 0x80, SDLK_K, SDL_SCANCODE_K},

    {"Z", 4, 0x01, SDLK_Z, SDL_SCANCODE_Z}, {"X", 4, 0x02, SDLK_X, SDL_SCANCODE_X},
    {"C", 4, 0x04, SDLK_C, SDL_SCANCODE_C}, {"V", 4, 0x08, SDLK_V, SDL_SCANCODE_V},
    {"B", 4, 0x10, SDLK_B, SDL_SCANCODE_B}, {"N", 4, 0x20, SDLK_N, SDL_SCANCODE_N},
    {"M", 4, 0x40, SDLK_M, SDL_SCANCODE_M}, {",", 4, 0x80, SDLK_COMMA, SDL_SCANCODE_COMMA},

    {"9", 5, 0x01, SDLK_9, SDL_SCANCODE_9}, {"0", 5, 0x02, SDLK_0, SDL_SCANCODE_0},
    {"-", 5, 0x04, SDLK_MINUS, SDL_SCANCODE_MINUS}, {"^", 5, 0x08, SDLK_EQUALS, SDL_SCANCODE_EQUALS},
    {".", 5, 0x10, SDLK_PERIOD, SDL_SCANCODE_PERIOD}, {"Down", 5, 0x20, SDLK_DOWN, SDL_SCANCODE_DOWN},
    {"_", 5, 0x40, SDLK_UNKNOWN, SDL_SCANCODE_GRAVE}, {"Back", 5, 0x80, SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE},

    {"O", 6, 0x01, SDLK_O, SDL_SCANCODE_O}, {"P", 6, 0x02, SDLK_P, SDL_SCANCODE_P},
    {"Up", 6, 0x04, SDLK_UP, SDL_SCANCODE_UP}, {"[", 6, 0x08, SDLK_LEFTBRACKET, SDL_SCANCODE_LEFTBRACKET},
    {"L", 6, 0x10, SDLK_L, SDL_SCANCODE_L}, {"Left", 6, 0x20, SDLK_LEFT, SDL_SCANCODE_LEFT},
    {"Right", 6, 0x40, SDLK_RIGHT, SDL_SCANCODE_RIGHT}, {"]", 6, 0x80, SDLK_RIGHTBRACKET, SDL_SCANCODE_RIGHTBRACKET},
};

struct RewindSnapshot
{
    uint64_t frame = 0;
    std::vector<uint8_t> data;
    /* Full active-resolution preview in XRGB8888.  This is deliberately not a
     * thumbnail: during rewind playback it is drawn through the same SDL
     * texture/source-rect/destination-rect path as the live game frame, so
     * the image does not resize, recenter, or jump while scrubbing. */
    std::vector<uint32_t> preview_pixels;
    int preview_w = 0;
    int preview_h = 0;
    int preview_view_x = 0;
    int preview_view_y = 0;
    int preview_view_w = 0;
    int preview_view_h = 0;
};

struct AppState
{
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    SDL_AudioStream *audio_stream = nullptr;
    SDL_Gamepad *gamepad = nullptr;
    UiSettings ui;
    std::string rom_path;
    std::string bios_path;      // Generic/SMS BIOS path, also honored for legacy systems.
    std::string coleco_bios_path;
    std::string resolved_bios_path;
    std::string status;
    int console_choice = 0;      // kConsoleChoices index; 0 means Auto.
    int video_mode = 0;          // 0=Auto, 1=PAL 50 Hz, 2=NTSC 60 Hz.
    bool running = true;
    bool rom_loaded = false;
    bool paused = false;
    int capture_binding = -1;
    int capture_gamepad_binding = -1;
    int capture_coleco_key_binding = -1;
    int capture_coleco_alt_key_binding = -1;
    int capture_coleco_gamepad_binding = -1;
    int capture_arcade_key_binding = -1;
    int capture_arcade_gamepad_binding = -1;
    uint8_t arcade_ui_mask = 0;
    uint8_t arcade_ui_mask_next = 0;
    int vk_index = 0;
    int save_slot = 0;
    uint64_t frame_counter = 0;
    uint64_t last_autosave_frame = 0;
    uint64_t last_rewind_capture_frame = 0;
    bool autosave_enabled = true;
    bool rewind_enabled = true;
    bool rewind_hold = false;          // physical hotkey/gamepad hold state
    bool rewind_ui_hold = false;       // ImGui hold state carried from previous frame
    bool rewind_ui_hold_next = false;
    bool rewind_was_active = false;
    int autosave_interval_frames = 600;
    int rewind_interval_frames = 4;
    int rewind_step_display_frames = 2;
    int rewind_display_counter = 0;
    size_t rewind_max_snapshots = 900;
    std::vector<RewindSnapshot> rewind_snapshots;
    std::vector<uint32_t> rewind_preview_pixels;
    int rewind_preview_w = 0;
    int rewind_preview_h = 0;
    int rewind_preview_view_x = 0;
    int rewind_preview_view_y = 0;
    int rewind_preview_view_w = 0;
    int rewind_preview_view_h = 0;
    SDL_Texture *rewind_thumb_texture = nullptr;
    uint64_t rewind_thumb_frame = UINT64_MAX;
    int rewind_thumb_w = 0;
    int rewind_thumb_h = 0;
    std::filesystem::path state_dir;
    std::filesystem::path save_dir;
    std::filesystem::path config_dir;
    SDL_Texture *state_thumb_texture = nullptr;
    std::filesystem::path state_thumb_path;
    int state_thumb_w = 0;
    int state_thumb_h = 0;
    Uint64 menu_hint_until_ns = 0;
    bool vsync = true;
    bool frame_limit = true;
    /* Queue limiting keeps SDL3 from building unlimited latency if the host runs
     * ahead, but it must never clear the whole stream during normal playback.  The
     * old clear-on-overflow path caused deterministic audible gaps in Psycho Soldier
     * attract mode when queued audio briefly crossed the threshold. */
    bool audio_drop_stale = true;
    int audio_latency_frames = 8;
    int audio_device_sample_frames = 768;
    uint64_t audio_drop_count = 0;
    uint64_t audio_backpressure_count = 0;
    std::string audio_dump_path;
    std::FILE *audio_dump_file = nullptr;
    uint32_t audio_dump_bytes = 0;
    int run_seconds = 0;
    std::string input_playback_path;
    std::string input_record_path;
    std::string active_input_record_path;
    uint32_t input_tap_frames = MULTIREXZ80_INPUT_TAP_FRAMES_DEFAULT;
    multirexz80_input_script_t *input_playback = nullptr;
    multirexz80_input_recorder_t *input_recorder = nullptr;
    Uint64 next_frame_ns = 0;
};

static void sdl3_update_window_title(AppState &app)
{
    if (!app.window) return;
    if (app.rom_loaded && !app.rom_path.empty())
    {
        std::filesystem::path rp(app.rom_path);
        std::string title = std::string(MULTIREXZ80_SDL3_WINDOW_TITLE) + " - " + rp.filename().string();
        SDL_SetWindowTitle(app.window, title.c_str());
    }
    else
    {
        SDL_SetWindowTitle(app.window, MULTIREXZ80_SDL3_WINDOW_TITLE);
    }
}

static void sdl3_set_fullscreen(AppState &app, bool enabled)
{
    app.ui.fullscreen = enabled;
    if (app.window)
        SDL_SetWindowFullscreen(app.window, enabled);
}

static int sdl3_audio_frame_bytes()
{
    if (snd.sample_count <= 0) return 0;
    return snd.sample_count * 2 * static_cast<int>(sizeof(int16_t));
}

static void wav_write_u16(std::FILE *f, uint16_t v)
{
    std::fputc(v & 0xff, f);
    std::fputc((v >> 8) & 0xff, f);
}

static void wav_write_u32(std::FILE *f, uint32_t v)
{
    wav_write_u16(f, static_cast<uint16_t>(v & 0xffff));
    wav_write_u16(f, static_cast<uint16_t>(v >> 16));
}

static bool sdl3_audio_dump_open(AppState &app)
{
    if (app.audio_dump_path.empty() || app.audio_dump_file) return true;

    app.audio_dump_file = std::fopen(app.audio_dump_path.c_str(), "wb");
    if (!app.audio_dump_file)
    {
        std::fprintf(stderr, "Unable to open audio dump '%s': %s\n", app.audio_dump_path.c_str(), std::strerror(errno));
        return false;
    }

    std::fwrite("RIFF", 1, 4, app.audio_dump_file);
    wav_write_u32(app.audio_dump_file, 0);
    std::fwrite("WAVEfmt ", 1, 8, app.audio_dump_file);
    wav_write_u32(app.audio_dump_file, 16);
    wav_write_u16(app.audio_dump_file, 1);
    wav_write_u16(app.audio_dump_file, 2);
    wav_write_u32(app.audio_dump_file, SOUND_FREQUENCY);
    wav_write_u32(app.audio_dump_file, SOUND_FREQUENCY * 2u * static_cast<uint32_t>(sizeof(int16_t)));
    wav_write_u16(app.audio_dump_file, 2u * static_cast<uint16_t>(sizeof(int16_t)));
    wav_write_u16(app.audio_dump_file, 16);
    std::fwrite("data", 1, 4, app.audio_dump_file);
    wav_write_u32(app.audio_dump_file, 0);
    app.audio_dump_bytes = 0;
    return true;
}

static void sdl3_audio_dump_frame(AppState &app, const void *data, int bytes)
{
    if (!app.audio_dump_file || !data || bytes <= 0) return;
    const size_t wrote = std::fwrite(data, 1, static_cast<size_t>(bytes), app.audio_dump_file);
    app.audio_dump_bytes += static_cast<uint32_t>(wrote);
}

static void sdl3_audio_dump_close(AppState &app)
{
    if (!app.audio_dump_file) return;
    const uint32_t riff_size = 36u + app.audio_dump_bytes;
    std::fseek(app.audio_dump_file, 4, SEEK_SET);
    wav_write_u32(app.audio_dump_file, riff_size);
    std::fseek(app.audio_dump_file, 40, SEEK_SET);
    wav_write_u32(app.audio_dump_file, app.audio_dump_bytes);
    std::fclose(app.audio_dump_file);
    app.audio_dump_file = nullptr;
}

static void sdl3_clear_audio_queue(AppState &app)
{
    if (app.audio_stream)
        SDL_ClearAudioStream(app.audio_stream);
}

static void sdl3_queue_audio_frame(AppState &app)
{
    if (!snd.output || snd.sample_count <= 0) return;

    const int bytes = sdl3_audio_frame_bytes();
    if (bytes <= 0) return;

    if (!app.ui.audio || !app.audio_stream)
    {
        sdl3_audio_dump_frame(app, snd.output, bytes);
        return;
    }

    if (app.audio_drop_stale)
    {
        int queued = SDL_GetAudioStreamQueued(app.audio_stream);
        const int max_queued = bytes * std::max(1, app.audio_latency_frames);
        if (queued >= max_queued)
        {
            const Uint64 start = SDL_GetTicksNS();
            const Uint64 max_wait_ns = 20ull * 1000ull * 1000ull;
            const int target_queued = std::max(0, max_queued - bytes);
            app.audio_backpressure_count++;
            do
            {
                SDL_Delay(1);
                queued = SDL_GetAudioStreamQueued(app.audio_stream);
            } while (queued > target_queued && SDL_GetTicksNS() - start < max_wait_ns);

            if (queued >= max_queued * 2)
            {
                app.audio_drop_count++;
                return;
            }
        }
    }

    sdl3_audio_dump_frame(app, snd.output, bytes);
    SDL_PutAudioStreamData(app.audio_stream, snd.output, bytes);
}

static void sdl3_apply_vsync(AppState &app)
{
    if (!app.renderer) return;
    SDL_SetRenderVSync(app.renderer, app.vsync ? 1 : 0);
    app.next_frame_ns = 0;
}

static int sdl3_current_fps()
{
    if (sms.display == DISPLAY_PAL) return FPS_PAL;
    return FPS_NTSC;
}

static void sdl3_pace_frame(AppState &app)
{
    if (!app.frame_limit || !app.rom_loaded || app.paused)
    {
        app.next_frame_ns = 0;
        return;
    }

    const int fps = std::max(1, sdl3_current_fps());
    const Uint64 frame_ns = 1000000000ull / static_cast<Uint64>(fps);
    const Uint64 now = SDL_GetTicksNS();
    if (app.next_frame_ns == 0)
    {
        app.next_frame_ns = now + frame_ns;
        return;
    }

    if (now < app.next_frame_ns)
        SDL_DelayNS(app.next_frame_ns - now);

    const Uint64 after = SDL_GetTicksNS();
    if (after > app.next_frame_ns + frame_ns * 4)
        app.next_frame_ns = after + frame_ns;
    else
        app.next_frame_ns += frame_ns;
}

static void set_defaults()
{
    std::memset(&option, 0, sizeof(option));
    option.fullscreen = 0;
    option.fullspeed = 1;
    option.fm = 1;
    option.spritelimit = 1;
    option.tms_pal = 2;
    option.nosound = 0;
    option.soundlevel = 1;
    option.use_bios = 1;
    option.lcd_persistence = 1;
    option.lightgun_cursor = 1;
    option.lightgun_dpad_speed = 3;
    
    // To turn back on later if it does sound better
    
    option.audio_dc_blocker = 0;
    option.audio_highpass_hz = 220;
    option.audio_lowpass_hz = 5000;
    option.audio_limiter = 0;
    option.audio_headroom_db = 0;
}

static void normalize_audio_options()
{
    option.audio_dc_blocker = option.audio_dc_blocker ? 1 : 0;
    option.audio_highpass_hz = std::clamp(option.audio_highpass_hz, 0, 20000);
    option.audio_lowpass_hz = std::clamp(option.audio_lowpass_hz, 0, 20000);
    option.audio_limiter = option.audio_limiter ? 1 : 0;
    option.audio_headroom_db = std::clamp(option.audio_headroom_db, 0, 9);
}

static const char *ext_of(const std::string &path)
{
    const char *dot = std::strrchr(path.c_str(), '.');
    return dot ? dot : "";
}

struct ConsoleChoice
{
    const char *label;
    const char *cli_name;
    uint8_t option_console;
    uint8_t auto_country;   // Used only when video mode is Auto.  0 leaves CRC/header detection in control.
};

static constexpr ConsoleChoice kConsoleChoices[] = {
    {"Auto (extension/CRC/ZIP set)", "auto", 0, 0},
    {"ColecoVision", "coleco", 6, 0},
    {"SG-1000", "sg1000", 5, 0},
    {"Sord M5", "sordm5", 7, 0},
    {"Master System 1 JP", "sms1jp", 1, 3},
    {"Master System 1 EU/US", "sms1", 1, 0},
    {"Master System 2", "sms2", 2, 0},
    {"Game Gear", "gg", 3, 0},
    {"Game Gear SMS compatibility", "ggsms", 4, 0},
    {"Sega System E", "systeme", 8, 0},
    {"Sega System 1/2", "system1", 9, 0},
    {"SNK Ikari/Psychos", "snk", 10, 0},
};

static constexpr const char *kVideoModeLabels[] = {
    "Auto",
    "PAL 50 Hz",
    "NTSC 60 Hz",
};

static bool ext_equal(const char *ext, const char *wanted)
{
    return ext && wanted && SDL_strcasecmp(ext, wanted) == 0;
}

static const char *runtime_console_name(uint8_t console)
{
    switch (console)
    {
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
        default: return "Unknown";
    }
}

static const char *runtime_display_name(uint8_t display)
{
    return display == DISPLAY_PAL ? "PAL 50 Hz" : "NTSC 60 Hz";
}

static std::filesystem::path user_multirexz80_dir()
{
#ifdef MULTIREXZ80_PORTABLE
    return std::filesystem::current_path();
#else
    const char *home = std::getenv("HOME");
    if (home && home[0]) return std::filesystem::path(home) / ".multirexz80";
    return std::filesystem::current_path() / ".multirexz80";
#endif
}

static void init_user_paths(AppState &app)
{
    std::filesystem::path root = user_multirexz80_dir();
    app.config_dir = root;
    app.state_dir = root / "states";
    app.save_dir = root / "saves";
}

static std::filesystem::path sdl3_config_path(const AppState &app)
{
    return app.config_dir / "sdl3.cfg";
}

static bool parse_bool_value(const std::string &value, bool fallback)
{
    if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
    if (value == "0" || value == "false" || value == "no" || value == "off") return false;
    return fallback;
}

static int parse_clamped_int_value(const std::string &value, int fallback, int lo, int hi)
{
    char *end = nullptr;
    long v = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || (end && *end)) return fallback;
    return std::clamp(static_cast<int>(v), lo, hi);
}

static void load_sdl3_config(AppState &app)
{
    std::ifstream in(sdl3_config_path(app));
    if (!in) return;

    std::string line;
    int audio_config_version = 0;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c){ return std::isspace(c); }), key.end());
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c){ return !std::isspace(c); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), value.end());

        if (key == "fullscreen") app.ui.fullscreen = parse_bool_value(value, app.ui.fullscreen);
        else if (key == "keep_aspect") app.ui.keep_aspect = parse_bool_value(value, app.ui.keep_aspect);
        else if (key == "stretch") app.ui.stretch = parse_bool_value(value, app.ui.stretch);
        else if (key == "pixel_perfect") app.ui.pixel_perfect = parse_bool_value(value, app.ui.pixel_perfect);
        else if (key == "linear_filter") app.ui.linear_filter = parse_bool_value(value, app.ui.linear_filter);
        else if (key == "rewind_enabled") app.rewind_enabled = parse_bool_value(value, app.rewind_enabled);
        else if (key == "rewind_interval_frames") app.rewind_interval_frames = parse_clamped_int_value(value, app.rewind_interval_frames, 1, 600);
        else if (key == "rewind_step_display_frames") app.rewind_step_display_frames = parse_clamped_int_value(value, app.rewind_step_display_frames, 1, 60);
        else if (key == "rewind_max_snapshots") app.rewind_max_snapshots = static_cast<size_t>(parse_clamped_int_value(value, static_cast<int>(app.rewind_max_snapshots), 1, 7200));
        else if (key == "autosave_enabled") app.autosave_enabled = parse_bool_value(value, app.autosave_enabled);
        else if (key == "autosave_interval_frames") app.autosave_interval_frames = parse_clamped_int_value(value, app.autosave_interval_frames, 60, 36000);
        else if (key == "vsync") app.vsync = parse_bool_value(value, app.vsync);
        else if (key == "frame_limit") app.frame_limit = parse_bool_value(value, app.frame_limit);
        else if (key == "frame_limit_when_vsync_off") app.frame_limit = parse_bool_value(value, app.frame_limit);
        else if (key == "audio_drop_stale") app.audio_drop_stale = parse_bool_value(value, app.audio_drop_stale);
        else if (key == "audio_latency_frames") app.audio_latency_frames = parse_clamped_int_value(value, app.audio_latency_frames, 1, 12);
        else if (key == "audio_device_sample_frames") app.audio_device_sample_frames = parse_clamped_int_value(value, app.audio_device_sample_frames, 64, 4096);
        else if (key == "audio_config_version") audio_config_version = parse_clamped_int_value(value, audio_config_version, 0, 999);
        else if (key == "audio_dc_blocker") option.audio_dc_blocker = parse_bool_value(value, option.audio_dc_blocker) ? 1 : 0;
        else if (key == "audio_highpass_hz") option.audio_highpass_hz = parse_clamped_int_value(value, option.audio_highpass_hz, 0, 20000);
        else if (key == "audio_lowpass_hz") option.audio_lowpass_hz = parse_clamped_int_value(value, option.audio_lowpass_hz, 0, 20000);
        else if (key == "audio_limiter") option.audio_limiter = parse_bool_value(value, option.audio_limiter) ? 1 : 0;
        else if (key == "audio_headroom_db") option.audio_headroom_db = parse_clamped_int_value(value, option.audio_headroom_db, 0, 9);
    }

    if (audio_config_version < 5)
    {
        option.audio_dc_blocker = 1;
        option.audio_highpass_hz = 220;
        option.audio_lowpass_hz = 5000;
        option.audio_limiter = 1;
        option.audio_headroom_db = 0;
    }
}

static void save_sdl3_config(const AppState &app)
{
    normalize_audio_options();
    std::error_code ec;
    std::filesystem::create_directories(app.config_dir, ec);
    std::ofstream out(sdl3_config_path(app), std::ios::trunc);
    if (!out) return;
    out << "# MultiRexZ80 SDL3 settings\n";
    out << "fullscreen=" << (app.ui.fullscreen ? 1 : 0) << "\n";
    out << "keep_aspect=" << (app.ui.keep_aspect ? 1 : 0) << "\n";
    out << "stretch=" << (app.ui.stretch ? 1 : 0) << "\n";
    out << "pixel_perfect=" << (app.ui.pixel_perfect ? 1 : 0) << "\n";
    out << "linear_filter=" << (app.ui.linear_filter ? 1 : 0) << "\n";
    out << "rewind_enabled=" << (app.rewind_enabled ? 1 : 0) << "\n";
    out << "rewind_interval_frames=" << app.rewind_interval_frames << "\n";
    out << "rewind_step_display_frames=" << app.rewind_step_display_frames << "\n";
    out << "rewind_max_snapshots=" << app.rewind_max_snapshots << "\n";
    out << "autosave_enabled=" << (app.autosave_enabled ? 1 : 0) << "\n";
    out << "autosave_interval_frames=" << app.autosave_interval_frames << "\n";
    out << "vsync=" << (app.vsync ? 1 : 0) << "\n";
    out << "frame_limit=" << (app.frame_limit ? 1 : 0) << "\n";
    out << "audio_drop_stale=" << (app.audio_drop_stale ? 1 : 0) << "\n";
    out << "audio_latency_frames=" << app.audio_latency_frames << "\n";
    out << "audio_device_sample_frames=" << app.audio_device_sample_frames << "\n";
    out << "audio_config_version=5\n";
    out << "audio_dc_blocker=" << (option.audio_dc_blocker ? 1 : 0) << "\n";
    out << "audio_highpass_hz=" << option.audio_highpass_hz << "\n";
    out << "audio_lowpass_hz=" << option.audio_lowpass_hz << "\n";
    out << "audio_limiter=" << (option.audio_limiter ? 1 : 0) << "\n";
    out << "audio_headroom_db=" << option.audio_headroom_db << "\n";
}


static void set_console_from_path(const std::string &path)
{
    const char *ext = ext_of(path);
    if (ext_equal(ext, ".m5")) option.console = 7;
    else if (ext_equal(ext, ".col") || ext_equal(ext, ".cv")) option.console = 6;
    else if (ext_equal(ext, ".gg")) option.console = 3;
    else if (ext_equal(ext, ".sg")) option.console = 5;
}

static int console_choice_from_name(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kConsoleChoices)); i++)
    {
        if (!SDL_strcasecmp(name, kConsoleChoices[i].cli_name)) return i;
    }
    if (!SDL_strcasecmp(name, "sms") || !SDL_strcasecmp(name, "sms1export") || !SDL_strcasecmp(name, "sms1us") || !SDL_strcasecmp(name, "sms1eu")) return 5;
    if (!SDL_strcasecmp(name, "jp") || !SDL_strcasecmp(name, "smsjp") || !SDL_strcasecmp(name, "sms1japan")) return 4;
    if (!SDL_strcasecmp(name, "sordm5") || !SDL_strcasecmp(name, "m5")) return 3;
    if (!SDL_strcasecmp(name, "systeme") || !SDL_strcasecmp(name, "segae")) return 9;
    if (!SDL_strcasecmp(name, "system1") || !SDL_strcasecmp(name, "segas1") || !SDL_strcasecmp(name, "sega1") || !SDL_strcasecmp(name, "system2")) return 10;
    if (!SDL_strcasecmp(name, "psychos") || !SDL_strcasecmp(name, "snkpsychos") || !SDL_strcasecmp(name, "ikari")) return 11;
    if (!SDL_strcasecmp(name, "coleco") || !SDL_strcasecmp(name, "colecovision") || !SDL_strcasecmp(name, "cv")) return 1;
    if (!SDL_strcasecmp(name, "ggms") || !SDL_strcasecmp(name, "ggsms")) return 8;
    if (!SDL_strcasecmp(name, "gg")) return 7;
    if (!SDL_strcasecmp(name, "sg") || !SDL_strcasecmp(name, "sg1000")) return 2;
    if (!SDL_strcasecmp(name, "sms2")) return 6;
    return -1;
}

static int video_mode_from_name(const char *name)
{
    if (!name) return -1;
    if (!SDL_strcasecmp(name, "auto")) return 0;
    if (!SDL_strcasecmp(name, "pal") || !SDL_strcasecmp(name, "50") || !SDL_strcasecmp(name, "50hz") || !SDL_strcasecmp(name, "europe")) return 1;
    if (!SDL_strcasecmp(name, "ntsc") || !SDL_strcasecmp(name, "60") || !SDL_strcasecmp(name, "60hz") || !SDL_strcasecmp(name, "usa") || !SDL_strcasecmp(name, "us")) return 2;
    return -1;
}

static void apply_requested_machine_options(const AppState &app, const std::string &path)
{
    const ConsoleChoice &choice = kConsoleChoices[std::clamp(app.console_choice, 0, static_cast<int>(IM_ARRAYSIZE(kConsoleChoices)) - 1)];

    option.console = choice.option_console;
    option.country = 0;
    if (option.console == 0)
        set_console_from_path(path);

    if (app.video_mode == 1)
        option.country = 2;      // PAL/export
    else if (app.video_mode == 2)
        option.country = 1;      // NTSC/export
    else if (choice.auto_country != 0)
        option.country = choice.auto_country;
}

static bool load_exact(const std::string &path, uint8_t *dst, size_t dst_size, size_t min_size, size_t *actual = nullptr)
{
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz < 0 || static_cast<size_t>(sz) > dst_size || static_cast<size_t>(sz) < min_size)
    {
        std::fclose(fp);
        return false;
    }
    std::memset(dst, 0xFF, dst_size);
    size_t got = std::fread(dst, 1, static_cast<size_t>(sz), fp);
    std::fclose(fp);
    if (got != static_cast<size_t>(sz)) return false;
    if (actual) *actual = static_cast<size_t>(sz);
    return true;
}


static bool file_readable(const std::filesystem::path &path)
{
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

static void append_bios_candidates(std::vector<std::filesystem::path> &out,
                                   const std::filesystem::path &dir,
                                   std::initializer_list<const char *> names)
{
    if (dir.empty()) return;
    for (const char *name : names)
        out.emplace_back(dir / name);
}

static std::string find_local_bios(const std::string &rom_path,
                                   std::initializer_list<const char *> names)
{
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;

    append_bios_candidates(candidates, std::filesystem::current_path(ec), names);

    if (!rom_path.empty())
    {
        std::filesystem::path rp(rom_path);
        append_bios_candidates(candidates, rp.parent_path(), names);
    }

    const char *home = std::getenv("HOME");
    if (home && *home)
    {
        std::filesystem::path hp(home);
        append_bios_candidates(candidates, hp / ".multirexz80" / "bios", names);
        append_bios_candidates(candidates, hp / ".multirexz80" / "bios", names);
        append_bios_candidates(candidates, hp / ".config" / "multirexz80" / "bios", names);
    }

    for (const auto &candidate : candidates)
        if (file_readable(candidate))
            return candidate.string();
    return {};
}

static bool init_bios(AppState &app)
{
    if (!bios.rom)
        bios.rom = static_cast<uint8_t *>(std::calloc(1, 0x100000));
    if (!bios.rom) return false;
    bios.enabled = 0;

    if (IS_SMS && !app.bios_path.empty())
    {
        size_t size = 0;
        if (!load_exact(app.bios_path, bios.rom, 0x100000, 1, &size)) return false;
        if (size < 0x4000) size = 0x4000;
        bios.enabled = static_cast<uint8_t>(option.use_bios | 2);
        bios.pages = static_cast<uint16_t>(size / 0x4000);
    }

    if (sms.console == CONSOLE_COLECO)
    {
        std::string path = app.coleco_bios_path.empty() ? app.bios_path : app.coleco_bios_path;
        if (path.empty())
            path = find_local_bios(app.rom_path, {"BIOS.col", "bios.col", "coleco.rom", "coleco.bin", "colecovision.rom", "COLECO.ROM"});
        if (path.empty())
        {
            app.status = "ColecoVision BIOS missing. Put BIOS.col next to the executable/ROM or pass --coleco-bios BIOS.col.";
            return false;
        }
        if (!load_exact(path, coleco.rom, sizeof(coleco.rom), 0x2000))
        {
            app.status = "Failed to load ColecoVision BIOS: " + path;
            return false;
        }
        app.coleco_bios_path = path;
        app.resolved_bios_path = path;
    }
    else if (sms.console == CONSOLE_SORDM5)
    {
        std::string path = app.bios_path;
        if (path.empty())
            path = find_local_bios(app.rom_path, {"sordm5bios.bin", "SORDM5BIOS.BIN", "m5bios.bin", "M5BIOS.BIN"});
        if (!path.empty() && !load_exact(path, coleco.rom, sizeof(coleco.rom), 0x2000))
        {
            app.status = "Failed to load Sord M5 BIOS: " + path;
            return false;
        }
        if (!path.empty()) app.resolved_bios_path = path;
    }
    return true;
}

static bool init_bitmap()
{
    if (!g_pixels)
        g_pixels = std::calloc(static_cast<size_t>(BITMAP_W) * BITMAP_H, MULTIREXZ80_RENDER_BYTES_PER_PIXEL);
    if (!g_pixels) return false;
    bitmap.width = BITMAP_W;
    bitmap.height = BITMAP_H;
    bitmap.depth = MULTIREXZ80_RENDER_DEPTH;
    bitmap.data = reinterpret_cast<uint8_t *>(g_pixels);
    bitmap.pitch = BITMAP_W * MULTIREXZ80_RENDER_BYTES_PER_PIXEL;
    bitmap.viewport.w = VIDEO_WIDTH_SMS;
    bitmap.viewport.h = VIDEO_HEIGHT_SMS;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    return true;
}

static std::string sanitize_component(std::string s)
{
    if (s.empty()) s = "cart";
    for (char &c : s)
    {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '-' && c != '_' && c != '.') c = '_';
    }
    return s;
}

static bool ensure_state_dir(const std::filesystem::path &dir)
{
    std::error_code ec;

    /*
     * An empty parent_path() means "use the current working directory".
     * std::filesystem::create_directories("") fails on POSIX, which broke
     * simple relative paths such as --input-record playthrough.input.
     */
    if (dir.empty()) return true;

    if (std::filesystem::is_directory(dir, ec)) return true;
    return std::filesystem::create_directories(dir, ec) || std::filesystem::is_directory(dir, ec);
}

static std::filesystem::path state_path_for_slot(const std::string &rom_path, const std::filesystem::path &dir, int slot)
{
    std::filesystem::path rp(rom_path.empty() ? "cart" : rom_path);
    char suffix[64];
    std::snprintf(suffix, sizeof(suffix), "_%08X.slot%d.png", cart.crc, slot);
    return dir / (sanitize_component(rp.stem().string()) + suffix);
}

static std::filesystem::path autosave_path(const AppState &app)
{
    std::filesystem::path rp(app.rom_path.empty() ? "cart" : app.rom_path);
    char suffix[64];
    std::snprintf(suffix, sizeof(suffix), "_%08X.auto.png", cart.crc);
    return app.state_dir / (sanitize_component(rp.stem().string()) + suffix);
}

static std::filesystem::path input_recording_path(const AppState &app)
{
    std::filesystem::path rp(app.rom_path.empty() ? "cart" : app.rom_path);
    char suffix[64];
    std::snprintf(suffix, sizeof(suffix), "_%08X.input", cart.crc);
    return app.config_dir / "inputs" / (sanitize_component(rp.stem().string()) + suffix);
}

static std::filesystem::path resolve_output_path(const AppState &app, const std::filesystem::path &path)
{
    if (path.empty()) return path;
    if (path.is_absolute()) return path.lexically_normal();

    std::filesystem::path base = app.ui.launch_cwd;
    if (base.empty())
    {
        std::error_code ec;
        base = std::filesystem::current_path(ec);
        if (ec || base.empty()) return path;
    }
    return (base / path).lexically_normal();
}

static bool start_input_recording(AppState &app, const std::filesystem::path &path)
{
    if (app.input_recorder)
    {
        app.status = "Input recording is already active: " + app.active_input_record_path;
        return true;
    }

    std::filesystem::path resolved = resolve_output_path(app, path);
    if (!ensure_state_dir(resolved.parent_path()))
    {
        app.status = "Could not create input recording directory: " + resolved.parent_path().string();
        errno = ENOENT;
        return false;
    }

    std::string resolved_string = resolved.string();

    /* Create/truncate probe first so failures report the resolved path and so
     * simple relative names are guaranteed to be created in the launch CWD. */
    FILE *probe = std::fopen(resolved_string.c_str(), "wb");
    if (!probe)
    {
        app.status = "Could not create input recording: " + resolved_string;
        return false;
    }
    std::fclose(probe);

    if (!multirexz80_input_recorder_open(&app.input_recorder, resolved_string.c_str(), 1))
    {
        app.status = "Could not open input recording: " + resolved_string;
        return false;
    }
    app.active_input_record_path = resolved_string;
    app.status = "Recording input: " + app.active_input_record_path;
    return true;
}

static void stop_input_recording(AppState &app)
{
    if (!app.input_recorder) return;
    multirexz80_input_recorder_close(app.input_recorder);
    app.input_recorder = nullptr;
    app.status = "Stopped input recording: " + app.active_input_record_path;
}

#ifndef MULTIREXZ80_RENDER_32BPP
static uint32_t rgb565_to_xrgb8888(uint16_t p)
{
    uint32_t r = (p >> 11) & 0x1F;
    uint32_t g = (p >> 5) & 0x3F;
    uint32_t b = p & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
#endif

static std::vector<uint32_t> capture_thumbnail(uint32_t &tw, uint32_t &th)
{
    int active_w = bitmap.viewport.w > 0 ? bitmap.viewport.w : 256;
    int active_h = bitmap.viewport.h > 0 ? bitmap.viewport.h : vdp.height;
    int src_x0 = std::max(0, bitmap.viewport.x);
    if (active_w <= 0) active_w = 256;
    if (active_h <= 0) active_h = 192;

    tw = static_cast<uint32_t>(std::min(active_w, 160));
    th = static_cast<uint32_t>(std::max(1, (active_h * static_cast<int>(tw) + active_w / 2) / active_w));
    std::vector<uint32_t> thumb(static_cast<size_t>(tw) * th, 0xFF000000u);

    for (uint32_t y = 0; y < th; y++)
    {
        int sy = static_cast<int>((static_cast<uint64_t>(y) * active_h) / th);
        const uint8_t *src_line = bitmap.data + sy * bitmap.pitch;
        for (uint32_t x = 0; x < tw; x++)
        {
            int sx = src_x0 + static_cast<int>((static_cast<uint64_t>(x) * active_w) / tw);
#ifdef MULTIREXZ80_RENDER_32BPP
            uint32_t p = *reinterpret_cast<const uint32_t *>(src_line + sx * 4);
            thumb[static_cast<size_t>(y) * tw + x] = 0xFF000000u | (p & 0x00FFFFFFu);
#else
            uint16_t p = *reinterpret_cast<const uint16_t *>(src_line + sx * 2);
            thumb[static_cast<size_t>(y) * tw + x] = rgb565_to_xrgb8888(p);
#endif
        }
    }
    return thumb;
}

static void clear_rewind_preview(AppState &app)
{
    app.rewind_preview_pixels.clear();
    app.rewind_preview_w = 0;
    app.rewind_preview_h = 0;
    app.rewind_preview_view_x = 0;
    app.rewind_preview_view_y = 0;
    app.rewind_preview_view_w = 0;
    app.rewind_preview_view_h = 0;
}

static void destroy_rewind_thumb_texture(AppState &app)
{
    if (app.rewind_thumb_texture)
    {
        SDL_DestroyTexture(app.rewind_thumb_texture);
        app.rewind_thumb_texture = nullptr;
    }
    app.rewind_thumb_frame = UINT64_MAX;
    app.rewind_thumb_w = 0;
    app.rewind_thumb_h = 0;
}

static void clear_rewind_buffer(AppState &app)
{
    app.rewind_snapshots.clear();
    app.last_rewind_capture_frame = 0;
    app.rewind_display_counter = 0;
    app.rewind_was_active = false;
    app.rewind_hold = false;
    app.rewind_ui_hold = false;
    app.rewind_ui_hold_next = false;
    clear_rewind_preview(app);
    destroy_rewind_thumb_texture(app);
}

static void set_rewind_enabled(AppState &app, bool enabled)
{
    if (app.rewind_enabled == enabled) return;
    app.rewind_enabled = enabled;
    if (!enabled)
    {
        clear_rewind_buffer(app);
        app.status = "Rewind disabled; snapshot history cleared.";
    }
    else
    {
        app.status = "Rewind enabled.";
    }
}

static std::vector<uint32_t> capture_active_preview(int &preview_w, int &preview_h,
                                                    int &view_x, int &view_y,
                                                    int &view_w, int &view_h)
{
    int active_w = bitmap.viewport.w > 0 ? bitmap.viewport.w : 256;
    int active_h = bitmap.viewport.h > 0 ? bitmap.viewport.h : vdp.height;
    int src_x0 = std::max(0, bitmap.viewport.x);
    int src_y0 = std::max(0, bitmap.viewport.y);

    if (active_w <= 0) active_w = 256;
    if (active_h <= 0) active_h = 192;

    active_w = std::min(active_w, std::max(0, BITMAP_W - src_x0));
    active_h = std::min(active_h, std::max(0, BITMAP_H - src_y0));
    if (active_w <= 0 || active_h <= 0)
    {
        active_w = std::min(256, BITMAP_W);
        active_h = std::min(192, BITMAP_H);
        src_x0 = src_y0 = 0;
    }

    preview_w = active_w;
    preview_h = active_h;
    view_x = src_x0;
    view_y = src_y0;
    view_w = active_w;
    view_h = active_h;

    std::vector<uint32_t> preview(static_cast<size_t>(active_w) * active_h, 0xFF000000u);
    for (int y = 0; y < active_h; y++)
    {
        const uint8_t *src_line = bitmap.data + static_cast<size_t>(src_y0 + y) * bitmap.pitch;
        uint32_t *dst_line = preview.data() + static_cast<size_t>(y) * active_w;
        for (int x = 0; x < active_w; x++)
        {
#ifdef MULTIREXZ80_RENDER_32BPP
            uint32_t p = *reinterpret_cast<const uint32_t *>(src_line + static_cast<size_t>(src_x0 + x) * 4);
            dst_line[x] = 0xFF000000u | (p & 0x00FFFFFFu);
#else
            uint16_t p = *reinterpret_cast<const uint16_t *>(src_line + static_cast<size_t>(src_x0 + x) * 2);
            dst_line[x] = rgb565_to_xrgb8888(p);
#endif
        }
    }
    return preview;
}

static void capture_rewind_preview(RewindSnapshot &snap)
{
    snap.preview_pixels = capture_active_preview(snap.preview_w, snap.preview_h,
                                                 snap.preview_view_x, snap.preview_view_y,
                                                 snap.preview_view_w, snap.preview_view_h);
}

static void restore_rewind_preview(AppState &app, const RewindSnapshot &snap)
{
    if (!snap.preview_pixels.empty() && snap.preview_w > 0 && snap.preview_h > 0)
    {
        app.rewind_preview_pixels = snap.preview_pixels;
        app.rewind_preview_w = snap.preview_w;
        app.rewind_preview_h = snap.preview_h;
        app.rewind_preview_view_x = snap.preview_view_x;
        app.rewind_preview_view_y = snap.preview_view_y;
        app.rewind_preview_view_w = snap.preview_view_w > 0 ? snap.preview_view_w : snap.preview_w;
        app.rewind_preview_view_h = snap.preview_view_h > 0 ? snap.preview_view_h : snap.preview_h;
    }
    else
    {
        clear_rewind_preview(app);
    }
}

static bool save_state_file(AppState &app, const std::filesystem::path &path)
{
    if (!app.rom_loaded)
    {
        app.status = "No game loaded.";
        return false;
    }
    if (!ensure_state_dir(path.parent_path()))
    {
        app.status = "Could not create state directory: " + path.parent_path().string();
        return false;
    }
    uint32_t tw = 0, th = 0;
    std::vector<uint32_t> thumb = capture_thumbnail(tw, th);
    if (!system_save_state_file_ex(path.string().c_str(), thumb.data(), tw, th, tw * sizeof(uint32_t)))
    {
        app.status = "Save state failed: " + path.string();
        return false;
    }
    app.status = "Saved state: " + path.string();
    return true;
}

static bool load_state_file(AppState &app, const std::filesystem::path &path)
{
    if (!app.rom_loaded)
    {
        app.status = "Load a game before loading a state.";
        return false;
    }
    if (!file_readable(path))
    {
        app.status = "State not found: " + path.string();
        return false;
    }
    if (!system_load_state_file(path.string().c_str()))
    {
        app.status = "Load state failed: " + path.string();
        return false;
    }
    app.status = "Loaded state: " + path.string();
    sdl3_clear_audio_queue(app);
    clear_rewind_preview(app);
    return true;
}

static void invalidate_state_thumbnail(AppState &app)
{
    if (app.state_thumb_texture)
    {
        SDL_DestroyTexture(app.state_thumb_texture);
        app.state_thumb_texture = nullptr;
    }
    app.state_thumb_path.clear();
    app.state_thumb_w = app.state_thumb_h = 0;
}

static void load_state_thumbnail(AppState &app, const std::filesystem::path &path)
{
    if (app.state_thumb_texture && app.state_thumb_path == path) return;
    invalidate_state_thumbnail(app);
    uint32_t *pixels = nullptr;
    uint32_t w = 0, h = 0;
    if (!system_state_png_read_thumbnail(path.string().c_str(), &pixels, &w, &h)) return;
    SDL_Texture *tex = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, (int)w, (int)h);
    if (tex)
    {
        SDL_UpdateTexture(tex, nullptr, pixels, (int)w * 4);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        app.state_thumb_texture = tex;
        app.state_thumb_path = path;
        app.state_thumb_w = (int)w;
        app.state_thumb_h = (int)h;
    }
    system_free_state_buffer(pixels);
}


static bool save_slot(AppState &app, int slot)
{
    return save_state_file(app, state_path_for_slot(app.rom_path, app.state_dir, slot));
}

static bool load_slot(AppState &app, int slot)
{
    return load_state_file(app, state_path_for_slot(app.rom_path, app.state_dir, slot));
}

static void capture_rewind_snapshot(AppState &app)
{
    if (!app.rom_loaded || !app.rewind_enabled) return;
    uint8_t *data = nullptr;
    uint32_t size = 0;
    if (!system_save_state_buffer(&data, &size)) return;
    RewindSnapshot snap;
    snap.frame = app.frame_counter;
    snap.data.assign(data, data + size);
    system_free_state_buffer(data);
    capture_rewind_preview(snap);
    app.rewind_snapshots.push_back(std::move(snap));
    destroy_rewind_thumb_texture(app);
    if (app.rewind_snapshots.size() > app.rewind_max_snapshots)
        app.rewind_snapshots.erase(app.rewind_snapshots.begin());
    app.last_rewind_capture_frame = app.frame_counter;
}

static void render_loaded_state_preview_frame()
{
    /*
     * Save states restore VDP/VRAM/CRAM/registers, not the host framebuffer.
     * For Nintendo-Online-style rewind feedback, repaint the current VDP state
     * immediately after each rewind step without running CPU/audio forward.
     */
    int32_t old_line = vdp.line;
    text_counter = 0;
    for (vdp.line = 0; vdp.line < vdp.lpf; vdp.line++)
        render_line(vdp.line);
    vdp.line = old_line;
}

static bool rewind_one_snapshot(AppState &app)
{
    if (!app.rom_loaded || app.rewind_snapshots.empty()) return false;
    RewindSnapshot snap = std::move(app.rewind_snapshots.back());
    app.rewind_snapshots.pop_back();
    if (!snap.data.empty())
    {
        if (system_load_state_buffer(snap.data.data(), static_cast<uint32_t>(snap.data.size())))
        {
            app.frame_counter = snap.frame;
            restore_rewind_preview(app, snap);
            if (app.rewind_preview_pixels.empty())
                render_loaded_state_preview_frame();
            sdl3_clear_audio_queue(app);
            app.status = "Rewinding... frame " + std::to_string(static_cast<unsigned long long>(snap.frame));
            return true;
        }
    }
    return false;
}

static bool rewind_active(const AppState &app)
{
    return app.rewind_enabled && app.rom_loaded && (app.rewind_hold || app.rewind_ui_hold);
}

static void run_rewind(AppState &app)
{
    if (!rewind_active(app))
    {
        if (app.rewind_was_active)
        {
            app.rewind_display_counter = 0;
            clear_rewind_preview(app);
            app.status = "Rewind released; resumed from frame " +
                         std::to_string(static_cast<unsigned long long>(app.frame_counter));
        }
        app.rewind_was_active = false;
        return;
    }

    if (!app.rewind_was_active)
    {
        app.rewind_was_active = true;
        app.rewind_display_counter = 0;
        app.status = "Rewind: hold to move backward, release to resume.";
    }

    if (app.rewind_snapshots.empty())
    {
        app.status = "Rewind buffer empty.";
        return;
    }

    if ((app.rewind_display_counter++ % std::max(1, app.rewind_step_display_frames)) == 0)
        rewind_one_snapshot(app);
}

static void maybe_autosave(AppState &app)
{
    if (!app.autosave_enabled || !app.rom_loaded) return;
    if (app.frame_counter - app.last_autosave_frame < static_cast<uint64_t>(app.autosave_interval_frames)) return;
    save_state_file(app, autosave_path(app));
    app.last_autosave_frame = app.frame_counter;
}

static void maybe_capture_rewind(AppState &app)
{
    if (!app.rewind_enabled || !app.rom_loaded || rewind_active(app)) return;
    if (app.rewind_snapshots.empty() ||
        app.frame_counter - app.last_rewind_capture_frame >= static_cast<uint64_t>(app.rewind_interval_frames))
        capture_rewind_snapshot(app);
}


extern "C" void smsp_state(uint8_t slot_number, uint8_t mode)
{
    std::filesystem::path dir = user_multirexz80_dir() / "states";
    std::filesystem::path path = state_path_for_slot(option.game_name, dir, slot_number);
    ensure_state_dir(dir);
    if (mode == 0)
        system_save_state_file_ex(path.string().c_str(), nullptr, 0, 0, 0);
    else if (mode == 1)
        system_load_state_file(path.string().c_str());
}

extern "C" void system_manage_sram(uint8_t *sram, uint8_t slot_number, uint8_t mode)
{
    (void)slot_number;
    if (g_sram_path.empty())
    {
        if (mode == SRAM_LOAD) std::memset(sram, 0, 0x8000);
        return;
    }
    if (mode == SRAM_LOAD)
    {
        FILE *fp = std::fopen(g_sram_path.c_str(), "rb");
        if (fp)
        {
            std::fread(sram, 1, 0x8000, fp);
            std::fclose(fp);
            sms.save = 1;
        }
        else std::memset(sram, 0, 0x8000);
    }
    else if (mode == SRAM_SAVE && sms.save)
    {
        FILE *fp = std::fopen(g_sram_path.c_str(), "wb");
        if (fp)
        {
            std::fwrite(sram, 1, 0x8000, fp);
            std::fclose(fp);
        }
    }
}

static bool load_game(AppState &app, const std::string &path)
{
    if (path.empty()) return false;
    if (app.rom_loaded)
    {
        system_poweroff();
        app.rom_loaded = false;
    }

    set_defaults();
    apply_requested_machine_options(app, path);
    std::snprintf(option.game_name, sizeof(option.game_name), "%s", path.c_str());
    if (!load_rom(const_cast<char *>(path.c_str())))
    {
        app.status = "Failed to load ROM: " + path;
        return false;
    }
    if (!init_bitmap())
    {
        app.status = "Failed to initialize bitmap";
        return false;
    }
    if (!init_bios(app))
    {
        if (app.status.empty()) app.status = "Failed to initialize BIOS";
        return false;
    }
    system_poweron();
    sdl3_clear_audio_queue(app);
    app.ui.db_lightgun = (sms.device[0] == DEVICE_LIGHTGUN);
    if (app.ui.force_lightgun) sms.device[0] = DEVICE_LIGHTGUN;
    app.rom_path = path;
    if (g_sram_path.empty())
    {
        ensure_state_dir(app.save_dir);
        std::filesystem::path rp(app.rom_path.empty() ? "cart" : app.rom_path);
        char suffix[64];
        std::snprintf(suffix, sizeof(suffix), "_%08X.sav", cart.crc);
        g_sram_path = (app.save_dir / (sanitize_component(rp.stem().string()) + suffix)).string();
    }
    app.rom_loaded = true;
    app.paused = false;
    app.ui.show_menu = false;
    app.menu_hint_until_ns = SDL_GetTicksNS() + MENU_HINT_DURATION_NS;
    app.frame_counter = 0;
    multirexz80_input_script_reset(app.input_playback);
    app.last_autosave_frame = 0;
    app.arcade_ui_mask = 0;
    app.arcade_ui_mask_next = 0;
    input.arcade = 0;
    clear_rewind_buffer(app);
    ensure_state_dir(app.state_dir);
    invalidate_state_thumbnail(app);
    app.status = "Loaded " + path + " [" + runtime_console_name(sms.console) + ", " + runtime_display_name(sms.display) + "]";
    sdl3_update_window_title(app);
    return true;
}


static void set_m5_key(uint8_t row, uint8_t mask, bool down)
{
    if (row >= SORDM5_KEY_ROWS) return;
    if (down) input.m5_key[row] |= mask;
    else input.m5_key[row] &= static_cast<uint8_t>(~mask);
}

static bool m5_key_from_sdl(SDL_Keycode key, SDL_Scancode scan, bool down)
{
    bool handled = false;
    for (const M5Key &mk : kM5Keys)
    {
        if ((key != SDLK_UNKNOWN && key == mk.key) || (scan != SDL_SCANCODE_UNKNOWN && scan == mk.scan))
        {
            set_m5_key(mk.row, mk.mask, down);
            handled = true;
        }
    }
    return handled;
}

static bool m5_text_char_is_bound_action(unsigned char c)
{
    if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(std::toupper(c));
    for (int i = 0; i < ACT_COUNT; i++)
    {
        if (i == ACT_M5_1 || i == ACT_M5_2 || i == ACT_MENU || i == ACT_VKBD)
            continue;
        SDL_Scancode sc = g_bindings[i].scan;
        if ((c >= 'A' && c <= 'Z') && sc == static_cast<SDL_Scancode>(SDL_SCANCODE_A + (c - 'A')))
            return true;
        if ((c >= '0' && c <= '9') && sc == static_cast<SDL_Scancode>(SDL_SCANCODE_0 + (c - '0')))
            return true;
        if (c == ' ' && sc == SDL_SCANCODE_SPACE)
            return true;
        if ((c == '\r' || c == '\n') && sc == SDL_SCANCODE_RETURN)
            return true;
    }
    return false;
}

static void m5_key_from_text(const char *txt)
{
    if (!txt || !txt[0]) return;
    unsigned char c = static_cast<unsigned char>(txt[0]);
    if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(std::toupper(c));
    if (m5_text_char_is_bound_action(c)) return;
    for (const M5Key &mk : kM5Keys)
    {
        if (mk.label[0] && !mk.label[1] && static_cast<unsigned char>(mk.label[0]) == c)
        {
            set_m5_key(mk.row, mk.mask, true);
            g_m5_text_pulse[mk.row] |= mk.mask;
        }
    }
}

static bool arcade_machine_active()
{
    return sms.console == CONSOLE_SYSTEME || sms.console == CONSOLE_SYSTEM1 || sms.console == CONSOLE_SNKPSYCHOS;
}

static bool keyboard_scancode_down(const bool *state, int nkeys, SDL_Scancode sc);

static bool action_is_machine_specific(Action action)
{
    return action == ACT_M5_1 || action == ACT_M5_2 || action == ACT_PAUSE;
}

static void update_arcade_state_from_inputs(AppState &app)
{
    if (!arcade_machine_active())
    {
        input.arcade = 0;
        return;
    }

    uint8_t arcade = app.arcade_ui_mask;
    int nkeys = 0;
    const bool *state = SDL_GetKeyboardState(&nkeys);
    for (const ArcadeBinding &binding : g_arcade_bindings)
    {
        if (keyboard_scancode_down(state, nkeys, binding.scan))
            arcade |= binding.mask;
        if (app.gamepad && binding.button != SDL_GAMEPAD_BUTTON_INVALID &&
            SDL_GetGamepadButton(app.gamepad, binding.button))
            arcade |= binding.mask;
    }
    input.arcade = arcade;
}

static void apply_action(Action action, bool down)
{
    if (arcade_machine_active() && action_is_machine_specific(action)) return;

    switch (action)
    {
        case ACT_UP:    if (down) input.pad[0] |= INPUT_UP; else input.pad[0] &= ~INPUT_UP; break;
        case ACT_DOWN:  if (down) input.pad[0] |= INPUT_DOWN; else input.pad[0] &= ~INPUT_DOWN; break;
        case ACT_LEFT:  if (down) input.pad[0] |= INPUT_LEFT; else input.pad[0] &= ~INPUT_LEFT; break;
        case ACT_RIGHT: if (down) input.pad[0] |= INPUT_RIGHT; else input.pad[0] &= ~INPUT_RIGHT; break;
        case ACT_A:
            if (down) input.pad[0] |= INPUT_BUTTON1; else input.pad[0] &= ~INPUT_BUTTON1;
            set_m5_key(0, 0x40, down); // Space
            break;
        case ACT_B:
            if (down) input.pad[0] |= INPUT_BUTTON2; else input.pad[0] &= ~INPUT_BUTTON2;
            set_m5_key(0, 0x80, down); // Enter
            break;
        case ACT_ROTATE_LEFT:
            if (down) input.pad[0] |= INPUT_ROTATE_LEFT; else input.pad[0] &= (uint8_t)~INPUT_ROTATE_LEFT;
            break;
        case ACT_ROTATE_RIGHT:
            if (down) input.pad[0] |= INPUT_ROTATE_RIGHT; else input.pad[0] &= (uint8_t)~INPUT_ROTATE_RIGHT;
            break;
        case ACT_AIM_UP:
            if (down) input.rotary_aim[0] |= INPUT_AIM_UP; else input.rotary_aim[0] &= static_cast<uint8_t>(~INPUT_AIM_UP);
            break;
        case ACT_AIM_DOWN:
            if (down) input.rotary_aim[0] |= INPUT_AIM_DOWN; else input.rotary_aim[0] &= static_cast<uint8_t>(~INPUT_AIM_DOWN);
            break;
        case ACT_AIM_LEFT:
            if (down) input.rotary_aim[0] |= INPUT_AIM_LEFT; else input.rotary_aim[0] &= static_cast<uint8_t>(~INPUT_AIM_LEFT);
            break;
        case ACT_AIM_RIGHT:
            if (down) input.rotary_aim[0] |= INPUT_AIM_RIGHT; else input.rotary_aim[0] &= static_cast<uint8_t>(~INPUT_AIM_RIGHT);
            break;
        case ACT_M5_1:
            set_m5_key(1, 0x01, down);
            if (down) input.system |= INPUT_START; else input.system &= ~INPUT_START;
            break;
        case ACT_M5_2:
            set_m5_key(1, 0x02, down);
            if (down) input.system |= INPUT_PAUSE; else input.system &= ~INPUT_PAUSE;
            break;
        case ACT_PAUSE:
            if (down) input.system |= (IS_GG ? INPUT_START : INPUT_PAUSE);
            else input.system &= static_cast<uint8_t>(~(IS_GG ? INPUT_START : INPUT_PAUSE));
            break;
        default: break;
    }
}

static void update_keyboard_state_from_bindings()
{
    int nkeys = 0;
    const bool *state = SDL_GetKeyboardState(&nkeys);
    if (!state) return;
    for (int i = 0; i < ACT_COUNT; i++)
    {
        SDL_Scancode sc = g_bindings[i].scan;
        if (sc > SDL_SCANCODE_UNKNOWN && sc < nkeys)
            apply_action(static_cast<Action>(i), state[sc]);
    }
}

static bool keyboard_scancode_down(const bool *state, int nkeys, SDL_Scancode sc)
{
    return state && sc > SDL_SCANCODE_UNKNOWN && sc < nkeys && state[sc];
}

static void update_coleco_keypad_from_inputs(AppState &app)
{
    if (sms.console != CONSOLE_COLECO) return;

    int nkeys = 0;
    const bool *state = SDL_GetKeyboardState(&nkeys);
    coleco.keypad[0] = 0xff;
    coleco.keypad[1] = 0xff;

    for (const ColecoKeyBinding &binding : g_coleco_key_bindings)
    {
        bool pressed = keyboard_scancode_down(state, nkeys, binding.scan) ||
                       keyboard_scancode_down(state, nkeys, binding.alt_scan);
        if (!pressed && app.gamepad && binding.button != SDL_GAMEPAD_BUTTON_INVALID)
            pressed = SDL_GetGamepadButton(app.gamepad, binding.button);
        if (pressed)
        {
            coleco.keypad[0] = binding.value;
            return;
        }
    }
}

static SDL_FRect compute_dest_rect(const UiSettings &ui, int active_w, int active_h, int win_w, int win_h);

static bool lightgun_active()
{
    return sms.device[0] == DEVICE_LIGHTGUN || sms.device[1] == DEVICE_LIGHTGUN;
}

static int lightgun_port()
{
    if (sms.device[0] == DEVICE_LIGHTGUN) return 0;
    if (sms.device[1] == DEVICE_LIGHTGUN) return 1;
    return 0;
}

static void set_lightgun_trigger(int port, bool down)
{
    if (down) input.pad[port] |= INPUT_BUTTON1;
    else input.pad[port] &= static_cast<uint8_t>(~INPUT_BUTTON1);
}

static bool window_point_to_lightgun(AppState &app, float wx, float wy, bool trigger)
{
    if (!app.rom_loaded || !lightgun_active()) return false;
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(app.window, &win_w, &win_h);
    int active_w = bitmap.viewport.w > 0 ? bitmap.viewport.w : 256;
    int active_h = bitmap.viewport.h > 0 ? bitmap.viewport.h : vdp.height;
    SDL_FRect dst = compute_dest_rect(app.ui, active_w, active_h, win_w, win_h);
    if (dst.w <= 0.0f || dst.h <= 0.0f) return false;
    if (wx < dst.x || wy < dst.y || wx >= dst.x + dst.w || wy >= dst.y + dst.h)
    {
        set_lightgun_trigger(lightgun_port(), trigger);
        return false;
    }

    int port = lightgun_port();
    float nx = (wx - dst.x) / dst.w;
    float ny = (wy - dst.y) / dst.h;
    int x = static_cast<int>(nx * active_w + 0.5f);
    int y = static_cast<int>(ny * active_h + 0.5f);
    if (x < 0) x = 0;
    if (x > 255) x = 255;
    if (y < 0) y = 0;
    if (y >= active_h) y = active_h - 1;
    input.analog[port][0] = x;
    input.analog[port][1] = y;
    set_lightgun_trigger(port, trigger);
    return true;
}

static void update_lightgun_mouse(AppState &app)
{
    if (!app.ui.mouse_lightgun || !app.rom_loaded || !lightgun_active()) return;
    float mx = 0.0f, my = 0.0f;
    uint32_t buttons = static_cast<uint32_t>(SDL_GetMouseState(&mx, &my));
    window_point_to_lightgun(app, mx, my, (buttons & SDL_BUTTON_LMASK) != 0);
}

static void open_first_gamepad(AppState &app)
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids && count > 0)
        app.gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
}

static bool handle_action_command(AppState &app, Action action, bool down)
{
    if (action == ACT_SAVE_STATE)
    {
        if (down) save_slot(app, app.save_slot);
        return true;
    }
    if (action == ACT_LOAD_STATE)
    {
        if (down) load_slot(app, app.save_slot);
        return true;
    }
    if (action == ACT_REWIND)
    {
        app.rewind_hold = down;
        if (down && !app.rewind_enabled)
            app.status = "Rewind is disabled.";
        return true;
    }
    return false;
}

static const char *input_action_script_name(Action action)
{
    switch (action)
    {
        case ACT_UP: return "UP";
        case ACT_DOWN: return "DOWN";
        case ACT_LEFT: return "LEFT";
        case ACT_RIGHT: return "RIGHT";
        case ACT_A: return "A";
        case ACT_B: return "B";
        case ACT_ROTATE_LEFT: return "ROTATE_LEFT";
        case ACT_ROTATE_RIGHT: return "ROTATE_RIGHT";
        case ACT_AIM_UP: return "AIM_UP";
        case ACT_AIM_DOWN: return "AIM_DOWN";
        case ACT_AIM_LEFT: return "AIM_LEFT";
        case ACT_AIM_RIGHT: return "AIM_RIGHT";
        case ACT_M5_1: return "M5_1";
        case ACT_M5_2: return "M5_2";
        case ACT_PAUSE: return "P";
        default: return nullptr;
    }
}

static void sdl3_record_action(AppState &app, Action action, bool down)
{
    const char *name = input_action_script_name(action);
    if (name) multirexz80_input_recorder_write_action(app.input_recorder, app.frame_counter, name, down ? 1 : 0);
}

static const char *arcade_script_name(uint8_t mask)
{
    switch (mask)
    {
        case INPUT_ARCADE_COIN1: return "COIN1";
        case INPUT_ARCADE_COIN2: return "COIN2";
        case INPUT_ARCADE_SERVICE: return "SERVICE";
        case INPUT_ARCADE_TEST: return "TEST";
        case INPUT_ARCADE_START1: return "START1";
        case INPUT_ARCADE_START2: return "START2";
        default: return nullptr;
    }
}

static bool input_playback_active(const AppState &app)
{
    return multirexz80_input_script_active(app.input_playback) != 0;
}

static const char *gamepad_button_name(SDL_GamepadButton button)
{
    switch (button)
    {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad Up";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad Down";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad Left";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad Right";
        case SDL_GAMEPAD_BUTTON_SOUTH: return "South";
        case SDL_GAMEPAD_BUTTON_EAST: return "East";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "L Shoulder";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "R Shoulder";
        case SDL_GAMEPAD_BUTTON_START: return "Start";
        case SDL_GAMEPAD_BUTTON_BACK: return "Back";
        case SDL_GAMEPAD_BUTTON_NORTH: return "North";
        default: return "Unbound";
    }
}

static void handle_gamepad_button(AppState &app, SDL_GamepadButton button, bool down)
{
    if (app.capture_gamepad_binding >= 0 && down)
    {
        g_bindings[app.capture_gamepad_binding].button = button;
        app.capture_gamepad_binding = -1;
        return;
    }
    if (app.capture_coleco_gamepad_binding >= 0 && down)
    {
        g_coleco_key_bindings[app.capture_coleco_gamepad_binding].button = button;
        app.capture_coleco_gamepad_binding = -1;
        return;
    }
    if (app.capture_arcade_gamepad_binding >= 0 && down)
    {
        g_arcade_bindings[app.capture_arcade_gamepad_binding].button = button;
        app.capture_arcade_gamepad_binding = -1;
        return;
    }

    if (!input_playback_active(app) && arcade_machine_active())
    {
        for (const ArcadeBinding &binding : g_arcade_bindings)
        {
            if (binding.button == button)
            {
                const char *name = arcade_script_name(binding.mask);
                if (name) multirexz80_input_recorder_write_action(app.input_recorder, app.frame_counter, name, down ? 1 : 0);
            }
        }
    }

    for (int i = 0; i < ACT_COUNT; i++)
    {
        if (g_bindings[i].button == button)
        {
            Action action = static_cast<Action>(i);
            if (!input_playback_active(app)) sdl3_record_action(app, action, down);
            if (handle_action_command(app, action, down)) continue;
            if (i == ACT_MENU && down) app.ui.show_menu = !app.ui.show_menu;
            else if (i == ACT_VKBD && down) app.ui.show_keyboard = !app.ui.show_keyboard;
            else if (!input_playback_active(app)) apply_action(action, down);
        }
    }
}

static void parse_args(AppState &app, int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        auto need = [&](const char *opt) -> const char * {
            if (i + 1 >= argc) { std::fprintf(stderr, "Missing value for %s\n", opt); return nullptr; }
            return argv[++i];
        };
        if (!std::strcmp(a, "--rom")) { if (const char *v = need(a)) app.rom_path = v; }
        else if (!std::strcmp(a, "--bios")) { if (const char *v = need(a)) app.bios_path = v; }
        else if (!std::strcmp(a, "--coleco-bios")) { if (const char *v = need(a)) app.coleco_bios_path = v; }
        else if (!std::strcmp(a, "--sram")) { if (const char *v = need(a)) g_sram_path = v; }
        else if (!std::strcmp(a, "--state-dir")) { if (const char *v = need(a)) app.state_dir = v; }
        else if (!std::strcmp(a, "--console"))
        {
            if (const char *v = need(a))
            {
                int choice = console_choice_from_name(v);
                if (choice < 0)
                    std::fprintf(stderr, "Unknown console '%s'\n", v);
                else
                    app.console_choice = choice;
            }
        }
        else if (!std::strcmp(a, "--video-mode") || !std::strcmp(a, "--region"))
        {
            if (const char *v = need(a))
            {
                int mode = video_mode_from_name(v);
                if (mode < 0)
                    std::fprintf(stderr, "Unknown video mode '%s'\n", v);
                else
                    app.video_mode = mode;
            }
        }
        else if (!std::strcmp(a, "--fullscreen")) app.ui.fullscreen = true;
        else if (!std::strcmp(a, "--windowed") || !std::strcmp(a, "--no-fullscreen")) app.ui.fullscreen = false;
        else if (!std::strcmp(a, "--stretch")) { app.ui.stretch = true; app.ui.keep_aspect = false; app.ui.pixel_perfect = false; }
        else if (!std::strcmp(a, "--linear")) app.ui.linear_filter = true;
        else if (!std::strcmp(a, "--nearest") || !std::strcmp(a, "--pixel-perfect")) { app.ui.linear_filter = false; app.ui.pixel_perfect = true; }
        else if (!std::strcmp(a, "--lcd-persistence")) option.lcd_persistence = 1;
        else if (!std::strcmp(a, "--no-lcd-persistence")) option.lcd_persistence = 0;
        else if (!std::strcmp(a, "--no-audio")) app.ui.audio = false;
        else if (!std::strcmp(a, "--audio-latency-frames")) { if (const char *v = need(a)) app.audio_latency_frames = std::clamp(std::atoi(v), 1, 12); }
        else if (!std::strcmp(a, "--audio-device-sample-frames")) { if (const char *v = need(a)) app.audio_device_sample_frames = std::clamp(std::atoi(v), 64, 4096); }
        else if (!std::strcmp(a, "--audio-highpass-hz")) { if (const char *v = need(a)) option.audio_highpass_hz = std::clamp(std::atoi(v), 0, 20000); }
        else if (!std::strcmp(a, "--audio-lowpass-hz")) { if (const char *v = need(a)) option.audio_lowpass_hz = std::clamp(std::atoi(v), 0, 20000); }
        else if (!std::strcmp(a, "--audio-headroom-db")) { if (const char *v = need(a)) option.audio_headroom_db = std::clamp(std::atoi(v), 0, 9); }
        else if (!std::strcmp(a, "--audio-dump-wav")) { if (const char *v = need(a)) app.audio_dump_path = v; }
        else if (!std::strcmp(a, "--run-seconds")) { if (const char *v = need(a)) app.run_seconds = std::max(1, std::atoi(v)); }
        else if (!std::strcmp(a, "--input-playback")) { if (const char *v = need(a)) app.input_playback_path = v; }
        else if (!std::strcmp(a, "--input-record")) { if (const char *v = need(a)) app.input_record_path = v; }
        else if (!std::strcmp(a, "--input-tap-frames")) { if (const char *v = need(a)) app.input_tap_frames = static_cast<uint32_t>(std::max(1, std::atoi(v))); }
        else if (!std::strcmp(a, "--no-audio-dc-blocker")) option.audio_dc_blocker = 0;
        else if (!std::strcmp(a, "--no-audio-limiter")) option.audio_limiter = 0;
        else if (!std::strcmp(a, "--no-audio-drop")) app.audio_drop_stale = false;
        else if (!std::strcmp(a, "--vsync")) app.vsync = true;
        else if (!std::strcmp(a, "--no-vsync")) app.vsync = false;
        else if (!std::strcmp(a, "--frame-limit")) app.frame_limit = true;
        else if (!std::strcmp(a, "--no-frame-limit")) app.frame_limit = false;
        else if (!std::strcmp(a, "--no-autosave")) app.autosave_enabled = false;
        else if (!std::strcmp(a, "--rewind")) app.rewind_enabled = true;
        else if (!std::strcmp(a, "--no-rewind")) app.rewind_enabled = false;
        else if (!std::strcmp(a, "--rewind-interval")) { if (const char *v = need(a)) app.rewind_interval_frames = std::max(1, std::atoi(v)); }
        else if (!std::strcmp(a, "--rewind-steps")) { if (const char *v = need(a)) app.rewind_max_snapshots = static_cast<size_t>(std::max(1, std::atoi(v))); }
        else if (!std::strcmp(a, "--lightgun")) app.ui.force_lightgun = true;
        else if (!std::strcmp(a, "--no-mouse-lightgun")) app.ui.mouse_lightgun = false;
        else if (!std::strcmp(a, "--lightgun-cursor")) { app.ui.show_lightgun_cursor = true; option.lightgun_cursor = 1; }
        else if (!std::strcmp(a, "--no-lightgun-cursor")) { app.ui.show_lightgun_cursor = false; option.lightgun_cursor = 0; }
        else if (!std::strcmp(a, "--hide-menu")) app.ui.show_menu = false;
        else if (a[0] != '-') app.rom_path = a;
    }
}

static int scaling_mode_index(const UiSettings &ui)
{
    return (ui.stretch || !ui.keep_aspect) ? 1 : 0;
}

static void set_scaling_mode(UiSettings &ui, int mode)
{
    if (mode == 0)
    {
        ui.keep_aspect = true;
        ui.stretch = false;
    }
    else
    {
        ui.keep_aspect = false;
        ui.stretch = true;
        ui.pixel_perfect = false;
    }
}

static SDL_FRect compute_dest_rect(const UiSettings &ui, int active_w, int active_h, int win_w, int win_h)
{
    SDL_FRect dst{0, 0, static_cast<float>(win_w), static_cast<float>(win_h)};
    if (ui.stretch || !ui.keep_aspect) return dst;

    float sx = static_cast<float>(win_w) / static_cast<float>(active_w);
    float sy = static_cast<float>(win_h) / static_cast<float>(active_h);
    float s = std::min(sx, sy);
    if (ui.pixel_perfect) s = std::max(1.0f, std::floor(s));
    dst.w = active_w * s;
    dst.h = active_h * s;
    dst.x = (win_w - dst.w) * 0.5f;
    dst.y = (win_h - dst.h) * 0.5f;
    return dst;
}

static void draw_lightgun_cursor_overlay(AppState &app, const SDL_FRect &dst, int active_w, int active_h)
{
    if (!app.ui.show_lightgun_cursor || !lightgun_active()) return;
    int port = lightgun_port();
    float x = dst.x + (static_cast<float>(input.analog[port][0]) / static_cast<float>(active_w)) * dst.w;
    float y = dst.y + (static_cast<float>(input.analog[port][1]) / static_cast<float>(active_h)) * dst.h;
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderLine(app.renderer, x - 9.0f, y - 1.0f, x + 9.0f, y - 1.0f);
    SDL_RenderLine(app.renderer, x - 1.0f, y - 9.0f, x - 1.0f, y + 9.0f);
    SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
    SDL_RenderLine(app.renderer, x - 8.0f, y, x + 8.0f, y);
    SDL_RenderLine(app.renderer, x, y - 8.0f, x, y + 8.0f);
}

static void render_core(AppState &app)
{
    SDL_SetTextureScaleMode(app.texture, app.ui.linear_filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    int active_w = bitmap.viewport.w > 0 ? bitmap.viewport.w : 256;
    int active_h = bitmap.viewport.h > 0 ? bitmap.viewport.h : vdp.height;
    SDL_FRect src{};

    if (!app.rewind_preview_pixels.empty() && app.rewind_preview_w > 0 && app.rewind_preview_h > 0)
    {
        const int px = std::clamp(app.rewind_preview_view_x, 0, BITMAP_W - 1);
        const int py = std::clamp(app.rewind_preview_view_y, 0, BITMAP_H - 1);
        const int pw = std::min(app.rewind_preview_w, BITMAP_W - px);
        const int ph = std::min(app.rewind_preview_h, BITMAP_H - py);
        const int view_w = app.rewind_preview_view_w > 0 ? app.rewind_preview_view_w : pw;
        const int view_h = app.rewind_preview_view_h > 0 ? app.rewind_preview_view_h : ph;
        SDL_Rect update_rect{px, py, pw, ph};
        SDL_UpdateTexture(app.texture, &update_rect, app.rewind_preview_pixels.data(), app.rewind_preview_w * static_cast<int>(sizeof(uint32_t)));
        active_w = view_w;
        active_h = view_h;
        src = SDL_FRect{static_cast<float>(px), static_cast<float>(py),
                        static_cast<float>(view_w), static_cast<float>(view_h)};
    }
    else
    {
        SDL_UpdateTexture(app.texture, nullptr, bitmap.data, bitmap.pitch);
        src = SDL_FRect{static_cast<float>(std::max(0, bitmap.viewport.x)), static_cast<float>(std::max(0, bitmap.viewport.y)),
                        static_cast<float>(active_w), static_cast<float>(active_h)};
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(app.window, &win_w, &win_h);
    SDL_FRect dst = compute_dest_rect(app.ui, active_w, active_h, win_w, win_h);
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);
    SDL_RenderTexture(app.renderer, app.texture, &src, &dst);
    if (app.rewind_preview_pixels.empty())
        draw_lightgun_cursor_overlay(app, dst, active_w, active_h);
}

static bool browser_rom_extension_supported(const std::filesystem::path &path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return ext == ".sms" || ext == ".gg" || ext == ".sg" || ext == ".sc" || ext == ".sf" ||
           ext == ".col" || ext == ".cv" || ext == ".m5" || ext == ".bin" || ext == ".rom" || ext == ".zip";
}

static std::vector<std::filesystem::directory_entry> list_browser(const std::filesystem::path &dir)
{
    std::vector<std::filesystem::directory_entry> entries;
    std::error_code ec;
    for (auto &e : std::filesystem::directory_iterator(dir, ec))
    {
        if (e.is_directory() || browser_rom_extension_supported(e.path()))
            entries.push_back(e);
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        if (a.is_directory() != b.is_directory()) return a.is_directory() > b.is_directory();
        return a.path().filename().string() < b.path().filename().string();
    });
    return entries;
}

static void draw_browser(AppState &app)
{
    ImGui::TextUnformatted("ROM Browser");
    ImGui::SameLine();
    if (ImGui::Button("Up") && app.ui.browser_dir.has_parent_path()) app.ui.browser_dir = app.ui.browser_dir.parent_path();
    ImGui::TextWrapped("%s", app.ui.browser_dir.string().c_str());
    ImGui::BeginChild("rom_browser", ImVec2(0, 210), true);
    auto entries = list_browser(app.ui.browser_dir);
    for (int i = 0; i < static_cast<int>(entries.size()); i++)
    {
        const auto &e = entries[i];
        std::string label = std::string(e.is_directory() ? "[DIR]  " : "[CART] ") + e.path().filename().string();
        if (ImGui::Selectable(label.c_str(), app.ui.browser_index == i, ImGuiSelectableFlags_AllowDoubleClick))
        {
            app.ui.browser_index = i;
            if (ImGui::IsMouseDoubleClicked(0))
            {
                if (e.is_directory()) app.ui.browser_dir = e.path();
                else load_game(app, e.path().string());
            }
        }
    }
    ImGui::EndChild();
}

static void draw_arcade_keyboard_controls(AppState &app)
{
    ImGui::TextUnformatted("Sega arcade inputs");
    ImGui::TextUnformatted("Defaults: 5=Coin 1, 6=Coin 2, 1=Start 1, 2=Start 2, 9=Service, F2=Test.");
    for (int i = 0; i < static_cast<int>(g_arcade_bindings.size()); i++)
    {
        ArcadeBinding &binding = g_arcade_bindings[i];
        ImGui::PushID(5000 + i);
        ImGui::Text("%-12s", binding.name);
        ImGui::SameLine(150);
        const char *name = SDL_GetScancodeName(binding.scan);
        if (ImGui::Button(app.capture_arcade_key_binding == i ? "press a key..." : (name && name[0] ? name : "Unbound"), ImVec2(160, 0)))
            app.capture_arcade_key_binding = i;
        ImGui::PopID();
    }
}

static void draw_keyboard_controls(AppState &app)
{
    if (arcade_machine_active())
    {
        draw_arcade_keyboard_controls(app);
        ImGui::Separator();
    }

    ImGui::TextUnformatted("Keyboard remapping");
    for (int i = 0; i < ACT_COUNT; i++)
    {
        Action action = static_cast<Action>(i);
        if (arcade_machine_active() && action_is_machine_specific(action)) continue;
        ImGui::PushID(i);
        ImGui::Text("%-20s", g_bindings[i].name);
        ImGui::SameLine(220);
        const char *name = SDL_GetScancodeName(g_bindings[i].scan);
        if (ImGui::Button(app.capture_binding == i ? "press a key..." : (name && name[0] ? name : "Unbound"), ImVec2(160, 0)))
            app.capture_binding = i;
        ImGui::PopID();
    }
    if (sms.console == CONSOLE_COLECO)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("ColecoVision keypad remapping");
        ImGui::TextUnformatted("Each keypad entry has a primary key and an alternate key. Defaults are number-row plus PC numpad.");
        for (int i = 0; i < static_cast<int>(g_coleco_key_bindings.size()); i++)
        {
            ColecoKeyBinding &binding = g_coleco_key_bindings[i];
            ImGui::PushID(2000 + i);
            ImGui::Text("%-12s", binding.name);
            ImGui::SameLine(150);
            const char *primary = SDL_GetScancodeName(binding.scan);
            if (ImGui::Button(app.capture_coleco_key_binding == i ? "press primary..." : (primary && primary[0] ? primary : "Unbound"), ImVec2(150, 0)))
                app.capture_coleco_key_binding = i;
            ImGui::SameLine();
            const char *alt = SDL_GetScancodeName(binding.alt_scan);
            if (ImGui::Button(app.capture_coleco_alt_key_binding == i ? "press alternate..." : (alt && alt[0] ? alt : "Unbound"), ImVec2(150, 0)))
                app.capture_coleco_alt_key_binding = i;
            ImGui::PopID();
        }
    }
    if (sms.console == CONSOLE_SORDM5)
        ImGui::TextWrapped("M5 keyboard is direct: PC number keys 1/2 drive the M5 rows used by Pooyan. Text events are also accepted so non-QWERTY layouts can enter logical characters.");
}

static void draw_controller_controls(AppState &app)
{
    ImGui::TextUnformatted("Game controller remapping");
    ImGui::Text("Gamepad: %s", app.gamepad ? "connected" : "not connected");

    if (arcade_machine_active())
    {
        ImGui::TextUnformatted("Sega arcade inputs");
        for (int i = 0; i < static_cast<int>(g_arcade_bindings.size()); i++)
        {
            ArcadeBinding &binding = g_arcade_bindings[i];
            ImGui::PushID(6000 + i);
            ImGui::Text("%-12s", binding.name);
            ImGui::SameLine(220);
            if (ImGui::Button(app.capture_arcade_gamepad_binding == i ? "press a button..." : gamepad_button_name(binding.button), ImVec2(160, 0)))
                app.capture_arcade_gamepad_binding = i;
            ImGui::PopID();
        }
        ImGui::Separator();
    }

    for (int i = 0; i < ACT_COUNT; i++)
    {
        Action action = static_cast<Action>(i);
        if (arcade_machine_active() && action_is_machine_specific(action)) continue;
        ImGui::PushID(1000 + i);
        ImGui::Text("%-20s", g_bindings[i].name);
        ImGui::SameLine(220);
        if (ImGui::Button(app.capture_gamepad_binding == i ? "press a button..." : gamepad_button_name(g_bindings[i].button), ImVec2(160, 0)))
            app.capture_gamepad_binding = i;
        ImGui::PopID();
    }

    if (sms.console == CONSOLE_COLECO)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("ColecoVision keypad");
        ImGui::TextUnformatted("Optional controller mappings for keypad digits, star, and pound.");
        for (int i = 0; i < static_cast<int>(g_coleco_key_bindings.size()); i++)
        {
            ColecoKeyBinding &binding = g_coleco_key_bindings[i];
            ImGui::PushID(3000 + i);
            ImGui::Text("%-12s", binding.name);
            ImGui::SameLine(220);
            if (ImGui::Button(app.capture_coleco_gamepad_binding == i ? "press a button..." : gamepad_button_name(binding.button), ImVec2(160, 0)))
                app.capture_coleco_gamepad_binding = i;
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Light Phaser");
    bool force = app.ui.force_lightgun;
    if (ImGui::Checkbox("Force Light Phaser on port 1", &force))
    {
        app.ui.force_lightgun = force;
        if (app.rom_loaded)
            sms.device[0] = (app.ui.force_lightgun || app.ui.db_lightgun) ? DEVICE_LIGHTGUN : DEVICE_PAD2B;
    }
    if (ImGui::Checkbox("Mouse aims Light Phaser", &app.ui.mouse_lightgun)) {}
    bool cursor = app.ui.show_lightgun_cursor;
    if (ImGui::Checkbox("Show on-screen cursor", &cursor))
    {
        app.ui.show_lightgun_cursor = cursor;
        option.lightgun_cursor = cursor ? 1 : 0;
    }
    ImGui::Text("Detected: %s  X:%d Y:%d Trigger:%s", lightgun_active() ? "yes" : "no",
                input.analog[lightgun_port()][0], input.analog[lightgun_port()][1],
                (input.pad[lightgun_port()] & INPUT_BUTTON1) ? "down" : "up");
}

static bool update_rewind_thumb_texture(AppState &app)
{
    if (app.rewind_snapshots.empty())
    {
        destroy_rewind_thumb_texture(app);
        return false;
    }
    const RewindSnapshot &snap = app.rewind_snapshots.back();
    if (snap.preview_pixels.empty() || snap.preview_w <= 0 || snap.preview_h <= 0)
    {
        destroy_rewind_thumb_texture(app);
        return false;
    }
    if (app.rewind_thumb_texture && app.rewind_thumb_frame == snap.frame &&
        app.rewind_thumb_w == snap.preview_w && app.rewind_thumb_h == snap.preview_h)
        return true;

    destroy_rewind_thumb_texture(app);
    SDL_Texture *tex = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, snap.preview_w, snap.preview_h);
    if (!tex) return false;
    SDL_UpdateTexture(tex, nullptr, snap.preview_pixels.data(), snap.preview_w * static_cast<int>(sizeof(uint32_t)));
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    app.rewind_thumb_texture = tex;
    app.rewind_thumb_frame = snap.frame;
    app.rewind_thumb_w = snap.preview_w;
    app.rewind_thumb_h = snap.preview_h;
    return true;
}

static void draw_states(AppState &app)
{
    ImGui::TextUnformatted("Save states");
    ImGui::Text("Directory: %s", app.state_dir.string().c_str());
    if (ImGui::Button("Slot -")) app.save_slot = std::max(0, app.save_slot - 1);
    ImGui::SameLine();
    ImGui::Text("Slot %d", app.save_slot);
    ImGui::SameLine();
    if (ImGui::Button("Slot +")) app.save_slot = std::min(9, app.save_slot + 1);

    std::filesystem::path slot_path = state_path_for_slot(app.rom_path, app.state_dir, app.save_slot);
    load_state_thumbnail(app, slot_path);
    if (app.state_thumb_texture)
    {
        ImGui::Image((ImTextureID)app.state_thumb_texture, ImVec2((float)app.state_thumb_w, (float)app.state_thumb_h));
        ImGui::Text("%s", slot_path.filename().string().c_str());
    }
    else
    {
        ImGui::TextUnformatted("No thumbnail for this slot yet.");
    }

    if (ImGui::Button("Save state")) { save_slot(app, app.save_slot); invalidate_state_thumbnail(app); }
    ImGui::SameLine();
    if (ImGui::Button("Load state")) load_slot(app, app.save_slot);
    ImGui::SameLine();
    if (ImGui::Button("Load autosave")) load_state_file(app, autosave_path(app));

    ImGui::Separator();
    ImGui::Checkbox("Auto-save", &app.autosave_enabled);
    ImGui::SameLine();
    ImGui::Text("every %d frames", app.autosave_interval_frames);
    if (ImGui::Button("Autosave -")) app.autosave_interval_frames = std::max(60, app.autosave_interval_frames - 60);
    ImGui::SameLine();
    if (ImGui::Button("Autosave +")) app.autosave_interval_frames = std::min(3600, app.autosave_interval_frames + 60);

    bool rewind = app.rewind_enabled;
    if (ImGui::Checkbox("Rewind", &rewind))
        set_rewind_enabled(app, rewind);
    ImGui::SameLine();
    ImGui::Text("snapshots: %d / %d", static_cast<int>(app.rewind_snapshots.size()), static_cast<int>(app.rewind_max_snapshots));
    if (!app.rewind_enabled)
        ImGui::TextWrapped("Rewind is disabled. This avoids periodic state serialization and releases the rewind buffer memory.");
    else
    {
        if (ImGui::Button("Hold to rewind", ImVec2(150, 0))) {}
        if (ImGui::IsItemActive()) app.rewind_ui_hold_next = true;
        ImGui::SameLine();
        ImGui::Text("%s", rewind_active(app) ? "rewinding" : "ready");
        ImGui::SameLine();
        if (ImGui::Button("Clear rewind buffer")) clear_rewind_buffer(app);
        ImGui::Text("Capture every %d emulated frames; playback step every %d display frames.",
                    app.rewind_interval_frames, app.rewind_step_display_frames);
        if (update_rewind_thumb_texture(app) && app.rewind_thumb_texture)
        {
            ImGui::Text("Newest rewind preview: frame %llu",
                        static_cast<unsigned long long>(app.rewind_thumb_frame));
            const float max_w = 160.0f;
            const float scale = (app.rewind_thumb_w > 0) ? std::min(1.0f, max_w / static_cast<float>(app.rewind_thumb_w)) : 1.0f;
            ImGui::Image((ImTextureID)app.rewind_thumb_texture,
                         ImVec2(static_cast<float>(app.rewind_thumb_w) * scale,
                                static_cast<float>(app.rewind_thumb_h) * scale));
        }
    }
    ImGui::TextWrapped("Hotkeys: F5 saves, F8 loads, hold F6 or the controller Rewind binding to scrub backward continuously. Release to resume from the selected point. State files are PNG images with an embedded compressed save-state chunk, so the thumbnail is visible outside the emulator too.");
}

static void draw_virtual_keyboard(AppState &app)
{
    if (!app.ui.show_keyboard) return;
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("Sord M5 Virtual Keyboard", &app.ui.show_keyboard, ImGuiWindowFlags_AlwaysAutoResize);
    const int cols = 8;
    for (int i = 0; i < static_cast<int>(std::size(kM5Keys)); i++)
    {
        ImGui::PushID(i);
        bool selected = (i == app.vk_index);
        if (selected) ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        if (ImGui::Button(kM5Keys[i].label, ImVec2(58, 30)))
        {
            set_m5_key(kM5Keys[i].row, kM5Keys[i].mask, true);
        }
        if (ImGui::IsItemDeactivated()) set_m5_key(kM5Keys[i].row, kM5Keys[i].mask, false);
        if (selected) ImGui::PopStyleVar();
        if ((i % cols) != cols - 1) ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::TextUnformatted("Gamepad: D-pad moves, South presses, East closes.");
    ImGui::End();
}


static void draw_menu_hint(AppState &app)
{
    if (!app.rom_loaded || app.ui.show_menu || app.menu_hint_until_ns == 0 || SDL_GetTicksNS() >= app.menu_hint_until_ns) return;
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const ImVec2 pos(10.0f, 8.0f);
    const char *text = "Press Escape for menu";
    draw->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 220), text);
    draw->AddText(pos, IM_COL32(255, 255, 255, 235), text);
}

static void draw_menu(AppState &app)
{
    if (!app.ui.show_menu) return;
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 620), ImGuiCond_FirstUseEver);
    ImGui::Begin(MULTIREXZ80_SDL3_MENU_TITLE, &app.ui.show_menu);
    if (ImGui::BeginTabBar("tabs"))
    {
        if (ImGui::BeginTabItem("Games")) { draw_browser(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("States")) { draw_states(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Video"))
        {
            int video_mode = app.video_mode;
            if (ImGui::Combo("Video mode", &video_mode, kVideoModeLabels, IM_ARRAYSIZE(kVideoModeLabels)))
            {
                app.video_mode = video_mode;
                if (app.rom_loaded && !app.rom_path.empty())
                {
                    std::string reload_path = app.rom_path;
                    load_game(app, reload_path);
                }
            }
            static const char *scaling_modes[] = {"Keep aspect ratio", "Stretch to window"};
            int scaling_mode = scaling_mode_index(app.ui);
            if (ImGui::Combo("Scaling", &scaling_mode, scaling_modes, IM_ARRAYSIZE(scaling_modes)))
                set_scaling_mode(app.ui, scaling_mode);
            ImGui::Checkbox("Pixel perfect", &app.ui.pixel_perfect);
            ImGui::Checkbox("Linear filtering", &app.ui.linear_filter);
            if (ImGui::Checkbox("VSync", &app.vsync)) sdl3_apply_vsync(app);
            ImGui::Checkbox("Frame limit", &app.frame_limit);
            bool lcd = option.lcd_persistence != 0;
            if (ImGui::Checkbox("Game Gear LCD persistence", &lcd)) option.lcd_persistence = lcd ? 1 : 0;
            bool fullscreen = app.ui.fullscreen;
            if (ImGui::Checkbox("Fullscreen (F11)", &fullscreen))
                sdl3_set_fullscreen(app, fullscreen);
            ImGui::Text("Active viewport: %d x %d", bitmap.viewport.w, bitmap.viewport.h);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Keyboard")) { draw_keyboard_controls(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Controllers")) { draw_controller_controls(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Machine"))
        {
            ImGui::Text("ROM: %s", app.rom_path.empty() ? "<none>" : app.rom_path.c_str());
            const char *bios_text = !app.resolved_bios_path.empty() ? app.resolved_bios_path.c_str() :
                                    (!app.coleco_bios_path.empty() ? app.coleco_bios_path.c_str() :
                                     (!app.bios_path.empty() ? app.bios_path.c_str() : "<auto/default>"));
            ImGui::Text("BIOS: %s", bios_text);
            int console_choice = std::clamp(app.console_choice, 0, static_cast<int>(IM_ARRAYSIZE(kConsoleChoices)) - 1);
            bool console_changed = false;
            if (ImGui::BeginCombo("Console type", kConsoleChoices[console_choice].label))
            {
                for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kConsoleChoices)); i++)
                {
                    const bool selected = (i == console_choice);
                    if (ImGui::Selectable(kConsoleChoices[i].label, selected))
                    {
                        console_choice = i;
                        console_changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (console_changed)
            {
                app.console_choice = console_choice;
                if (app.rom_loaded && !app.rom_path.empty())
                {
                    std::string reload_path = app.rom_path;
                    load_game(app, reload_path);
                }
            }
            ImGui::Text("Runtime: %s, %s, territory=%s", runtime_console_name(sms.console), runtime_display_name(sms.display),
                        sms.territory == TERRITORY_DOMESTIC ? "JP/domestic" : "export");
            if (ImGui::Button(app.paused ? "Resume" : "Pause")) app.paused = !app.paused;
            ImGui::SameLine();
            if (ImGui::Button("Reset") && app.rom_loaded) { system_reset(); sdl3_clear_audio_queue(app); }
            ImGui::Separator();
            ImGui::TextUnformatted("Latency");
            if (ImGui::Checkbox("Audio", &app.ui.audio) && !app.ui.audio)
                sdl3_clear_audio_queue(app);
            ImGui::SameLine();
            ImGui::Checkbox("Limit queued audio", &app.audio_drop_stale);
            ImGui::SliderInt("Audio queue limit (frames)", &app.audio_latency_frames, 1, 12);
            int queued_audio = app.audio_stream ? SDL_GetAudioStreamQueued(app.audio_stream) : 0;
            ImGui::Text("SDL queued audio: %d bytes; waits: %llu; drops: %llu", queued_audio,
                        static_cast<unsigned long long>(app.audio_backpressure_count),
                        static_cast<unsigned long long>(app.audio_drop_count));
            if (ImGui::Button("Clear queued audio")) sdl3_clear_audio_queue(app);
            ImGui::Separator();
            ImGui::TextUnformatted("Sound shaping");
            bool dc_blocker = option.audio_dc_blocker != 0;
            if (ImGui::Checkbox("DC blocker", &dc_blocker)) option.audio_dc_blocker = dc_blocker ? 1 : 0;
            ImGui::SameLine();
            bool limiter = option.audio_limiter != 0;
            if (ImGui::Checkbox("Soft limiter", &limiter)) option.audio_limiter = limiter ? 1 : 0;
            ImGui::SliderInt("High-pass Hz", &option.audio_highpass_hz, 0, 1000);
            ImGui::SliderInt("Low-pass Hz", &option.audio_lowpass_hz, 0, 20000);
            ImGui::SliderInt("Mixer headroom dB", &option.audio_headroom_db, 0, 9);
            if (arcade_machine_active())
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Arcade inputs");
                for (int i = 0; i < static_cast<int>(g_arcade_bindings.size()); i++)
                {
                    const ArcadeBinding &binding = g_arcade_bindings[i];
                    ImGui::PushID(7000 + i);
                    if (ImGui::Button(binding.name) || ImGui::IsItemActive())
                        app.arcade_ui_mask_next |= binding.mask;
                    if ((i % 3) != 2) ImGui::SameLine();
                    ImGui::PopID();
                }
                ImGui::Text("Current arcade mask: 0x%02X", input.arcade);
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Input scripts");
            if (input_playback_active(app))
                ImGui::Text("Playback: %s (%u events)", app.input_playback_path.c_str(), multirexz80_input_script_event_count(app.input_playback));
            else
                ImGui::TextUnformatted("Playback: inactive");
            if (app.input_recorder)
            {
                ImGui::Text("Recording: %s", app.active_input_record_path.c_str());
                if (ImGui::Button("Stop input recording")) stop_input_recording(app);
            }
            else
            {
                if (ImGui::Button("Start input recording"))
                    start_input_recording(app, input_recording_path(app));
                ImGui::Text("Default path: %s", input_recording_path(app).string().c_str());
            }
            if (sms.console == CONSOLE_SORDM5)
                ImGui::Checkbox("Sord M5 virtual keyboard", &app.ui.show_keyboard);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", app.status.c_str());
    ImGui::End();
}

static void handle_event(AppState &app, const SDL_Event &e)
{
    ImGui_ImplSDL3_ProcessEvent(&e);
    switch (e.type)
    {
        case SDL_EVENT_QUIT:
            app.running = false;
            break;
        case SDL_EVENT_DROP_FILE:
        {
            /*
             * SDL3 exposes drop.data as const in current headers, but the
             * allocation is still owned by the caller and must be released
             * with SDL_free().  Copy the path into std::string before freeing
             * it so load_game() never observes a dangling pointer.
             */
            if (e.drop.data)
            {
                const char *dropped_path = static_cast<const char *>(e.drop.data);
                std::string dropped(dropped_path);
                SDL_free(const_cast<char *>(dropped_path));
                load_game(app, dropped);
            }
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            bool down = e.type == SDL_EVENT_KEY_DOWN;
            if (down && !e.key.repeat &&
                (e.key.scancode == SDL_SCANCODE_F11 || e.key.key == SDLK_F11))
            {
                sdl3_set_fullscreen(app, !app.ui.fullscreen);
                break;
            }
            if (app.capture_binding >= 0 && down)
            {
                g_bindings[app.capture_binding].scan = e.key.scancode;
                app.capture_binding = -1;
                break;
            }
            if (app.capture_coleco_key_binding >= 0 && down)
            {
                g_coleco_key_bindings[app.capture_coleco_key_binding].scan = e.key.scancode;
                app.capture_coleco_key_binding = -1;
                break;
            }
            if (app.capture_coleco_alt_key_binding >= 0 && down)
            {
                g_coleco_key_bindings[app.capture_coleco_alt_key_binding].alt_scan = e.key.scancode;
                app.capture_coleco_alt_key_binding = -1;
                break;
            }
            if (app.capture_arcade_key_binding >= 0 && down)
            {
                g_arcade_bindings[app.capture_arcade_key_binding].scan = e.key.scancode;
                app.capture_arcade_key_binding = -1;
                break;
            }
            for (int i = 0; i < ACT_COUNT; i++)
            {
                if (g_bindings[i].scan == e.key.scancode)
                {
                    if (!input_playback_active(app) && !e.key.repeat)
                        sdl3_record_action(app, static_cast<Action>(i), down);
                    handle_action_command(app, static_cast<Action>(i), down);
                }
            }
            if (!input_playback_active(app) && !e.key.repeat)
            {
                for (const ArcadeBinding &binding : g_arcade_bindings)
                {
                    if (binding.scan == e.key.scancode)
                    {
                        const char *name = arcade_script_name(binding.mask);
                        if (name) multirexz80_input_recorder_write_action(app.input_recorder, app.frame_counter, name, down ? 1 : 0);
                    }
                }
            }
            if (e.key.key == SDLK_ESCAPE && down) app.ui.show_menu = !app.ui.show_menu;
            if (e.key.key == SDLK_TAB && down) app.ui.show_keyboard = !app.ui.show_keyboard;
            if (!input_playback_active(app))
            {
                m5_key_from_sdl(e.key.key, e.key.scancode, down);
                if (e.key.key == SDLK_ESCAPE) input.m5_reset = down ? SORDM5_KEY_RESET : 0;
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            if (!input_playback_active(app)) m5_key_from_text(e.text.text);
            break;
#if defined(SDL_EVENT_FINGER_DOWN) && defined(SDL_EVENT_FINGER_MOTION) && defined(SDL_EVENT_FINGER_UP)
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        {
            int win_w = 0, win_h = 0;
            SDL_GetWindowSize(app.window, &win_w, &win_h);
            bool down = e.type != SDL_EVENT_FINGER_UP;
            window_point_to_lightgun(app, e.tfinger.x * static_cast<float>(win_w), e.tfinger.y * static_cast<float>(win_h), down);
            break;
        }
#endif
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!app.gamepad) app.gamepad = SDL_OpenGamepad(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (app.gamepad && SDL_GetGamepadID(app.gamepad) == e.gdevice.which)
            {
                SDL_CloseGamepad(app.gamepad);
                app.gamepad = nullptr;
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            handle_gamepad_button(app, static_cast<SDL_GamepadButton>(e.gbutton.button), e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            break;
    }
}

static void update_virtual_keyboard_gamepad(AppState &app)
{
    if (!app.gamepad || !app.ui.show_keyboard) return;
    if (SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_EAST)) app.ui.show_keyboard = false;
    int cols = 8;
    int count = static_cast<int>(std::size(kM5Keys));
    // Edge detection is deliberately omitted here; the ImGui window remains usable with mouse/keyboard,
    // while gamepad navigation is basic and predictable.
    if (SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  app.vk_index = std::max(0, app.vk_index - 1);
    if (SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) app.vk_index = std::min(count - 1, app.vk_index + 1);
    if (SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))    app.vk_index = std::max(0, app.vk_index - cols);
    if (SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  app.vk_index = std::min(count - 1, app.vk_index + cols);
    bool press = SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    const M5Key &mk = kM5Keys[app.vk_index];
    set_m5_key(mk.row, mk.mask, press);
}

static bool init_sdl(AppState &app)
{
    const std::string audio_sample_frames = std::to_string(std::clamp(app.audio_device_sample_frames, 64, 4096));
#ifdef SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, audio_sample_frames.c_str());
#endif
#ifdef SDL_HINT_AUTO_UPDATE_JOYSTICKS
    SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS, "1");
#endif
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, app.vsync ? "1" : "0");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) return false;
    app.window = SDL_CreateWindow(MULTIREXZ80_SDL3_WINDOW_TITLE, HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION, SDL_WINDOW_RESIZABLE);
    if (!app.window) return false;
    app.renderer = SDL_CreateRenderer(app.window, nullptr);
    if (!app.renderer) return false;
    sdl3_apply_vsync(app);
    #ifdef MULTIREXZ80_RENDER_32BPP
    /*
     * Do not hide this behind #if defined(SDL_PIXELFORMAT_XRGB8888).  In SDL3
     * some pixel formats may be enum constants rather than preprocessor macros,
     * and the old conditional could silently fall back to an RGB565 texture while
     * the core was uploading 32 bpp XRGB8888 pixels.  That produces the yellow
     * vertical-stripe screen reported on real SDL3 builds.
     */
    app.texture = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, BITMAP_W, BITMAP_H);
#else
    app.texture = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, BITMAP_W, BITMAP_H);
#endif
    if (!app.texture) return false;
    SDL_SetTextureScaleMode(app.texture, SDL_SCALEMODE_NEAREST);
    sdl3_update_window_title(app);
    if (app.ui.fullscreen) sdl3_set_fullscreen(app, true);
    SDL_StartTextInput(app.window);

    if (app.ui.audio)
    {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = 2;
        spec.freq = SOUND_FREQUENCY;
        app.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (app.audio_stream) SDL_ResumeAudioStreamDevice(app.audio_stream);
    }
    open_first_gamepad(app);
    return true;
}

static void init_imgui(AppState &app)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForSDLRenderer(app.window, app.renderer);
    ImGui_ImplSDLRenderer3_Init(app.renderer);
}

static void shutdown_app(AppState &app)
{
    sdl3_audio_dump_close(app);
    multirexz80_input_recorder_close(app.input_recorder);
    app.input_recorder = nullptr;
    multirexz80_input_script_free(app.input_playback);
    app.input_playback = nullptr;
    save_sdl3_config(app);
    if (app.rom_loaded) system_poweroff();
    system_shutdown();
    if (bios.rom) { std::free(bios.rom); bios.rom = nullptr; }
    if (app.gamepad) SDL_CloseGamepad(app.gamepad);
    if (app.audio_stream) SDL_DestroyAudioStream(app.audio_stream);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (app.state_thumb_texture) SDL_DestroyTexture(app.state_thumb_texture);
    if (app.rewind_thumb_texture) SDL_DestroyTexture(app.rewind_thumb_texture);
    if (app.texture) SDL_DestroyTexture(app.texture);
    if (app.renderer) SDL_DestroyRenderer(app.renderer);
    if (app.window) SDL_DestroyWindow(app.window);
    SDL_Quit();
    std::free(g_pixels);
    g_pixels = nullptr;
}

int main(int argc, char **argv)
{
    AppState app;
    set_defaults();
    init_user_paths(app);
    load_sdl3_config(app);
    parse_args(app, argc, argv);
    normalize_audio_options();
    ensure_state_dir(app.state_dir);
    ensure_state_dir(app.save_dir);
    if (!init_bitmap()) return 1;
    if (!init_sdl(app))
    {
        std::fprintf(stderr, "SDL3 initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    system_init();
    init_imgui(app);

    if (!app.rom_path.empty()) load_game(app, app.rom_path);
    else app.status = "Drop a ROM, pass one on the command line, or choose it from the browser.";
    if (!sdl3_audio_dump_open(app))
        return 1;
    if (!app.input_playback_path.empty() &&
        !multirexz80_input_script_load(&app.input_playback, app.input_playback_path.c_str(), app.input_tap_frames))
    {
        std::fprintf(stderr, "Unable to load input playback script '%s': %s\n", app.input_playback_path.c_str(), std::strerror(errno));
        return 1;
    }
    if (!app.input_record_path.empty() && !start_input_recording(app, app.input_record_path))
    {
        std::fprintf(stderr, "Unable to open input recording '%s': %s\n%s\n",
                     app.input_record_path.c_str(), std::strerror(errno), app.status.c_str());
        return 1;
    }

    while (app.running)
    {
        app.rewind_ui_hold = app.rewind_ui_hold_next;
        app.rewind_ui_hold_next = false;
        app.arcade_ui_mask = app.arcade_ui_mask_next;
        app.arcade_ui_mask_next = 0;

        SDL_Event e;
        while (SDL_PollEvent(&e)) handle_event(app, e);
        SDL_UpdateGamepads();
        if (!input_playback_active(app))
        {
            update_keyboard_state_from_bindings();
            update_coleco_keypad_from_inputs(app);
            update_virtual_keyboard_gamepad(app);
            update_lightgun_mouse(app);
            update_arcade_state_from_inputs(app);
        }

        if (app.rom_loaded && !app.paused)
        {
            if (rewind_active(app))
            {
                run_rewind(app);
            }
            else
            {
                run_rewind(app); /* clears one-shot rewind state after release */
                multirexz80_input_script_apply_frame(app.input_playback, app.frame_counter, &input);
                system_frame(0);
                app.frame_counter++;
                for (size_t i = 0; i < SORDM5_KEY_ROWS; i++)
                {
                    input.m5_key[i] &= static_cast<uint8_t>(~g_m5_text_pulse[i]);
                    g_m5_text_pulse[i] = 0;
                }
                maybe_capture_rewind(app);
                maybe_autosave(app);
                sdl3_queue_audio_frame(app);
                if (app.run_seconds > 0 &&
                    app.frame_counter >= static_cast<uint64_t>(app.run_seconds) * static_cast<uint64_t>(sdl3_current_fps()))
                    app.running = false;
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (app.rom_loaded) render_core(app);
        else { SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255); SDL_RenderClear(app.renderer); }
        draw_menu_hint(app);
        draw_menu(app);
        draw_virtual_keyboard(app);
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), app.renderer);
        SDL_RenderPresent(app.renderer);
        sdl3_pace_frame(app);
    }

    shutdown_app(app);
    return 0;
}
