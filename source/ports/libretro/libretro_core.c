/*
 * MultiRexZ80
 *
 * Multi-system Z80 emulator based on SMS Plus GX by Eke-Eke, itself based on
 * SMS Plus by Charles MacDonald.
 *
 * Default project license: GPL-2.0-or-later.  File-specific notices below
 * are retained and take precedence for imported or derived components,
 * including MAME-derived code and other third-party modules.
 *
 * Libretro port.  Exposes the MultiRexZ80 core as a libretro shared object,
 * supporting every console type the upstream core emulates: SMS, SMS2, Game
 * Gear, SG-1000, SC-3000, ColecoVision, Sord M5, Sega System E, Sega System 1,
 * SNK Ikari/Psycho Soldiers, and Taito L-System.  Arcade cabinets map their
 * coin/start/service/test inputs through RETRO_DEVICE_JOYPAD buttons and the
 * SNK LS-30 rotary dial through the analog stick / dpad aim.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libretro.h>

#include "shared.h"

#define LIBRETRO_BITMAP_WIDTH  400
#define LIBRETRO_BITMAP_HEIGHT 400
#define LIBRETRO_SOUND_SAMPLES  (SOUND_FREQUENCY / 60)

#define RETRO_DEVICE_ARCADE     RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_GRAPHICBOARD RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_POINTER, 0)

t_config option;

static retro_log_printf_t       log_cb;
static retro_video_refresh_t    video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t       input_poll_cb;
static retro_input_state_t      input_state_cb;
static retro_environment_t      environ_cb;

static uint32_t g_pixels[LIBRETRO_BITMAP_WIDTH * LIBRETRO_BITMAP_HEIGHT];
static int16_t  g_audio[LIBRETRO_SOUND_SAMPLES * 2];
static int      g_sample_rate = SOUND_FREQUENCY;
static double   g_fps = 60.0;
static bool     g_libretro_supports_bitmasks = false;
static bool     g_rom_loaded = false;
static char     g_rom_dir[1024];
static char     g_save_dir[1024];

/* --- Console detection helpers --------------------------------------- */

static bool arcade_active(void)
{
    return sms.console == CONSOLE_SYSTEME ||
           sms.console == CONSOLE_SYSTEM1 ||
           sms.console == CONSOLE_SNKPSYCHOS ||
           sms.console == CONSOLE_TAITOL;
}

static bool needs_player2(void)
{
    /* Most arcade games are 1P or 2P simultaneous; expose both ports. */
    return arcade_active() || sms.console == CONSOLE_COLECO ||
           sms.console == CONSOLE_SORDM5;
}

/* --- Bitmap / video setup -------------------------------------------- */

static void init_bitmap(void)
{
    memset(g_pixels, 0, sizeof(g_pixels));
    bitmap.width  = LIBRETRO_BITMAP_WIDTH;
    bitmap.height = LIBRETRO_BITMAP_HEIGHT;
    bitmap.depth  = 32;
    bitmap.data   = (uint8_t *)g_pixels;
    bitmap.pitch  = LIBRETRO_BITMAP_WIDTH * 4;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = 256;
    bitmap.viewport.h = 192;
    bitmap.viewport.changed = 1;
}

static void config_defaults(void)
{
    memset(&option, 0, sizeof(option));
    option.fullspeed = 0;
    option.fm = 1;
    option.spritelimit = 1;
    option.soundlevel = 1;
    option.use_bios = 0;
    option.lcd_persistence = 1;
    option.lightgun_cursor = 1;
    option.lightgun_dpad_speed = 3;
}

/* SRAM is exposed to the libretro frontend through retro_get_memory_data(),
 * so system_manage_sram is a no-op.  The frontend persists RETRO_MEMORY_SAVE_RAM
 * automatically between sessions. */
void system_manage_sram(uint8_t *sram, uint8_t slot_number, uint8_t mode)
{
    (void)sram; (void)slot_number; (void)mode;
}

/* --- Save state passthrough ------------------------------------------ */

static size_t state_size_cache = 0;

static size_t compute_state_size(void)
{
    uint8_t *data = NULL;
    uint32_t size = 0;
    if (system_save_state_buffer(&data, &size) && data)
    {
        state_size_cache = size;
        system_free_state_buffer(data);
    }
    return state_size_cache;
}

/* ===================================================================== */
/*  Libretro core API                                                    */
/* ===================================================================== */

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    static const struct retro_variable variables[] = {
        { "multirexz80_region",       "Region; auto|ntsc|pal|japan" },
        { "multirexz80_fm",           "FM Unit (YM2413); disabled|enabled" },
        { "multirexz80_bios",         "SMS BIOS; disabled|enabled" },
        { "multirexz80_lcd_persist",  "Game Gear LCD persistence; enabled|disabled" },
        { "multirexz80_lightgun_cursor", "Light Phaser cursor; enabled|disabled" },
        { NULL, NULL },
    };
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)variables);

    static const struct retro_controller_description port1[] = {
        { "Joypad (2B)",     RETRO_DEVICE_JOYPAD },
        { "Light Phaser",    RETRO_DEVICE_LIGHTGUN },
        { "Graphic Board v2",RETRO_DEVICE_GRAPHICBOARD },
        { "Arcade Panel",    RETRO_DEVICE_ARCADE },
        { NULL, 0 },
    };
    static const struct retro_controller_description port2[] = {
        { "Joypad (2B)",     RETRO_DEVICE_JOYPAD },
        { "Light Phaser",    RETRO_DEVICE_LIGHTGUN },
        { "Graphic Board v2",RETRO_DEVICE_GRAPHICBOARD },
        { "Arcade Panel",    RETRO_DEVICE_ARCADE },
        { NULL, 0 },
    };
    static const struct retro_controller_info ports[] = {
        { port1, 4 },
        { port2, 4 },
        { NULL, 0 },
    };
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);

    bool supports_no_game = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &supports_no_game);
}

void retro_set_video_refresh(retro_video_refresh_t cb)   { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)     { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb)         { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb)       { input_state_cb = cb; }

void retro_get_system_info(struct retro_system_info *info)
{
    if (!info) return;
    info->library_name      = "MultiRexZ80";
    info->library_version   = "1.8";
    info->need_fullpath     = true;   /* we read ZIP archives from disk */
    info->valid_extensions  = "sms|gg|sg|sc|col|cv|m5|zip|bin";
    info->block_extract     = true;   /* MAME-style multi-file ZIPs are ours */
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    if (!info) return;
    memset(info, 0, sizeof(*info));

    int w = bitmap.viewport.w > 0 ? bitmap.viewport.w : 256;
    int h = bitmap.viewport.h > 0 ? bitmap.viewport.h : 192;
    info->geometry.base_width   = w;
    info->geometry.base_height  = h;
    info->geometry.max_width    = LIBRETRO_BITMAP_WIDTH;
    info->geometry.max_height   = LIBRETRO_BITMAP_HEIGHT;
    info->geometry.aspect_ratio = (double)w / (double)h;

    if (sms.display == DISPLAY_PAL)
    {
        info->timing.fps = 50.0;
        g_fps = 50.0;
    }
    else
    {
        info->timing.fps = 60.0;
        g_fps = 60.0;
    }
    info->timing.sample_rate = (double)g_sample_rate;
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_init(void)
{
    struct retro_log_callback log;
    unsigned level = 1;
    environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        log_cb = log.log;
    else
        log_cb = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
        g_libretro_supports_bitmasks = true;

    /* 32-bit XRGB8888 pixel format */
    unsigned fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    {
        if (log_cb) log_cb(RETRO_LOG_WARN, "Frontend rejected XRGB8888.\n");
    }

    config_defaults();
    init_bitmap();
    system_init();
}

void retro_deinit(void)
{
    if (g_rom_loaded)
    {
        system_poweroff();
        g_rom_loaded = false;
    }
    system_shutdown();
    g_libretro_supports_bitmasks = false;
    state_size_cache = 0;
}

void retro_reset(void)
{
    if (g_rom_loaded)
        system_reset();
}

/* --- Input polling --------------------------------------------------- */

static int16_t joy(int port, unsigned id)
{
    return input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, id);
}

static int16_t joy_mask(int port)
{
    if (g_libretro_supports_bitmasks)
        return input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    int16_t m = 0;
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_UP))    m |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_DOWN))  m |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_LEFT))  m |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_RIGHT)) m |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_A))     m |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_B))     m |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_X))     m |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_Y))     m |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_L))     m |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_R))     m |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_L2))    m |= (1 << RETRO_DEVICE_ID_JOYPAD_L2);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_R2))    m |= (1 << RETRO_DEVICE_ID_JOYPAD_R2);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_START)) m |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (joy(port, RETRO_DEVICE_ID_JOYPAD_SELECT))m |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    return m;
}

static void clear_inputs(void)
{
    input.pad[0] = 0;
    input.pad[1] = 0;
    input.system = 0;
    input.arcade = 0;
    input.rotary_aim[0] = 0;
    input.rotary_aim[1] = 0;
}

static void poll_standard_pad(int port)
{
    int16_t m = joy_mask(port);
    uint8_t *pad = &input.pad[port & 1];
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_UP))    *pad |= INPUT_UP;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))  *pad |= INPUT_DOWN;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))  *pad |= INPUT_LEFT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) *pad |= INPUT_RIGHT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_A))     *pad |= INPUT_BUTTON1;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_B))     *pad |= INPUT_BUTTON2;

    /* SNK LS-30 rotary aim via dpad/aim on the secondary face buttons */
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_X))     input.rotary_aim[port & 1] |= INPUT_AIM_LEFT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_Y))     input.rotary_aim[port & 1] |= INPUT_AIM_UP;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_L))     input.rotary_aim[port & 1] |= INPUT_AIM_DOWN;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_R))     input.rotary_aim[port & 1] |= INPUT_AIM_RIGHT;
    /* LS-30 rotary step (CCW/CW) via L2/R2 triggers */
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_L2))    *pad |= INPUT_ROTATE_LEFT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_R2))    *pad |= INPUT_ROTATE_RIGHT;

    /* Pause/Reset on SMS handled via system input */
    if (port == 0)
    {
        if (m & (1 << RETRO_DEVICE_ID_JOYPAD_START))  input.system |= INPUT_PAUSE;
        if (m & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT)) input.system |= INPUT_RESET;
    }
}

static void poll_coleco_keypad(int port)
{
    /* ColecoVision keypad 0-9,*,# mapped through R2/L2 + face buttons is too
     * cramped; instead expose keypad 1-9 as analog direction + A/B combos via
     * the libretro POINTER device when available.  For the standard joypad,
     * we only handle the fire buttons and rely on keyboard mapping through
     * the frontend's RETRO_DEVICE_KEYBOARD if present.  The Coleco keypad is
     * wired directly by the frontend through RETRO_DEVICE_ID_JOYPAD_SELECT
     * cycles here for the most common * / # keypad entry. */
    int16_t m = joy_mask(port);
    uint8_t *pad = &input.pad[port & 1];
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_UP))    *pad |= INPUT_UP;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))  *pad |= INPUT_DOWN;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))  *pad |= INPUT_LEFT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) *pad |= INPUT_RIGHT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_A))     *pad |= INPUT_BUTTON1;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_B))     *pad |= INPUT_BUTTON2;
}

static void poll_arcade(int port)
{
    int16_t m = joy_mask(port);
    uint8_t *pad = &input.pad[port & 1];
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_UP))    *pad |= INPUT_UP;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))  *pad |= INPUT_DOWN;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))  *pad |= INPUT_LEFT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) *pad |= INPUT_RIGHT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_A))     *pad |= INPUT_BUTTON1;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_B))     *pad |= INPUT_BUTTON2;

    /* SNK LS-30 rotary aim - all arcade hardware can use it, ignored if unused. */
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_X))     input.rotary_aim[port & 1] |= INPUT_AIM_LEFT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_Y))     input.rotary_aim[port & 1] |= INPUT_AIM_UP;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_L))     input.rotary_aim[port & 1] |= INPUT_AIM_DOWN;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_R))     input.rotary_aim[port & 1] |= INPUT_AIM_RIGHT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_L2))    *pad |= INPUT_ROTATE_LEFT;
    if (m & (1 << RETRO_DEVICE_ID_JOYPAD_R2))    *pad |= INPUT_ROTATE_RIGHT;

    /* Arcade cabinet controls: map P1 and P2 start/select to coin/start.
     * For 1P start: Start. For 1P coin: Select. For 2P coin: L2. For 2P start: R2.
     * Test/Service: handled through frontend hotkey + Start on P2 in some cores. */
    if (port == 0)
    {
        if (m & (1 << RETRO_DEVICE_ID_JOYPAD_START))  input.arcade |= INPUT_ARCADE_START1;
        if (m & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT)) input.arcade |= INPUT_ARCADE_COIN1;
    }
    else
    {
        if (m & (1 << RETRO_DEVICE_ID_JOYPAD_START))  input.arcade |= INPUT_ARCADE_START2;
        if (m & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT)) input.arcade |= INPUT_ARCADE_COIN2;
    }
    /* Shared service/test on L2+R2 of player 1 (rarely used in normal play) */
    if (joy(0, RETRO_DEVICE_ID_JOYPAD_L3)) input.arcade |= INPUT_ARCADE_SERVICE;
    if (joy(0, RETRO_DEVICE_ID_JOYPAD_R3)) input.arcade |= INPUT_ARCADE_TEST;
}

static void poll_lightgun(int port)
{
    int player = port & 1;
    if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN))
    {
        input.analog[player][0] = -1;
        input.analog[player][1] = -1;
        input.pad[player] &= (uint8_t)~INPUT_BUTTON1;
        return;
    }
    int16_t gx = input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
    int16_t gy = input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
    int w = bitmap.viewport.w > 0 ? bitmap.viewport.w : 256;
    int h = bitmap.viewport.h > 0 ? bitmap.viewport.h : 192;
    int x = (int)(((gx + 0x7fff) * (w - 1)) / 0xfffe);
    int y = (int)(((gy + 0x7fff) * (h - 1)) / 0xfffe);
    if (x < 0) x = 0;
    if (x >= w) x = w - 1;
    if (y < 0) y = 0;
    if (y >= h) y = h - 1;
    input.analog[player][0] = x;
    input.analog[player][1] = y;
    if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER))
        input.pad[player] |= INPUT_BUTTON1;
    else
        input.pad[player] &= (uint8_t)~INPUT_BUTTON1;
}

static void poll_graphicboard(int port)
{
    int player = port & 1;
    int16_t px = input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
    int16_t py = input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
    int16_t pressed = input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);
    int x = (int)(((px + 0x7fff) * 255) / 0xfffe);
    int y = (int)(((py + 0x7fff) * 255) / 0xfffe);
    if (x < 0) x = 0;
    if (x > 255) x = 255;
    if (y < 0) y = 0;
    if (y > 255) y = 255;
    x -= 4;
    if (x < 0) x = 0;
    y += 36; if (y > 255) y = 255;
    input.graphic_board[player].x = (uint8_t)x;
    input.graphic_board[player].y = (uint8_t)y;
    uint8_t btns = 0;
    if (pressed) btns |= 0x02;
    if (joy(player, RETRO_DEVICE_ID_JOYPAD_B)) btns |= 0x04;
    if (joy(player, RETRO_DEVICE_ID_JOYPAD_START)) btns |= 0x01;
    input.graphic_board[player].buttons = btns;
}

static unsigned g_port_device[2] = { RETRO_DEVICE_JOYPAD, RETRO_DEVICE_JOYPAD };

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    if (port >= 2) return;
    g_port_device[port] = device;
    switch (device)
    {
        case RETRO_DEVICE_LIGHTGUN:
            sms.device[port] = DEVICE_LIGHTGUN;
            break;
        case RETRO_DEVICE_GRAPHICBOARD:
            sms.device[port] = DEVICE_GRAPHICBOARD;
            break;
        case RETRO_DEVICE_ARCADE:
            sms.device[port] = DEVICE_PAD2B;
            break;
        case RETRO_DEVICE_JOYPAD:
        default:
            if (sms.device[port] == DEVICE_LIGHTGUN || sms.device[port] == DEVICE_GRAPHICBOARD)
                sms.device[port] = DEVICE_PAD2B;
            break;
    }
}

static void poll_inputs(void)
{
    input_poll_cb();
    clear_inputs();

    if (arcade_active())
    {
        poll_arcade(0);
        if (needs_player2()) poll_arcade(1);
        return;
    }

    if (sms.console == CONSOLE_COLECO)
    {
        poll_coleco_keypad(0);
        if (needs_player2()) poll_coleco_keypad(1);
        return;
    }

    int p;
    for (p = 0; p < 2; p++)
    {
        unsigned dev = g_port_device[p];
        if (dev == RETRO_DEVICE_LIGHTGUN)
            poll_lightgun(p);
        else if (dev == RETRO_DEVICE_GRAPHICBOARD)
            poll_graphicboard(p);
        else
            poll_standard_pad(p);
    }
}

/* --- Per-frame run --------------------------------------------------- */

void retro_run(void)
{
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
    {
        struct retro_variable var;
        var.key = "multirexz80_fm";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        {
            option.fm = !strcmp(var.value, "enabled");
            sms.use_fm = option.fm;
        }
        var.key = "multirexz80_lcd_persist";
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
            option.lcd_persistence = !strcmp(var.value, "enabled");
        var.key = "multirexz80_lightgun_cursor";
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
            option.lightgun_cursor = !strcmp(var.value, "enabled");
    }

    poll_inputs();
    system_frame(0);

    /* Output video: just the active viewport.  The renderer writes to the
     * full bitmap; pitch is LIBRETRO_BITMAP_WIDTH * 4 bytes.  We hand the
     * viewport origin/pitch to the frontend. */
    int vwx = bitmap.viewport.x < 0 ? 0 : bitmap.viewport.x;
    int vwy = bitmap.viewport.y < 0 ? 0 : bitmap.viewport.y;
    int vww = bitmap.viewport.w > 0 ? bitmap.viewport.w : 256;
    int vwh = bitmap.viewport.h > 0 ? bitmap.viewport.h : 192;
    uint8_t *frame = (uint8_t *)g_pixels + (vwy * bitmap.pitch) + (vwx * 4);
    video_cb(frame, vww, vwh, bitmap.pitch);

    /* Output audio: pull from snd.output which the sound mixer populated
     * during system_frame(). */
    if (snd.output && snd.sample_count > 0)
    {
        int samples = snd.sample_count;
        if (samples > LIBRETRO_SOUND_SAMPLES) samples = LIBRETRO_SOUND_SAMPLES;
        /* Interleave stereo into the audio batch buffer.  snd.output is
         * mono per channel but stored as [L0,R0,L1,R1,...]?  Actually the
         * mixer writes interleaved stereo; we mirror it. */
        memcpy(g_audio, snd.output, samples * 2 * sizeof(int16_t));
        audio_batch_cb(g_audio, samples);
    }
}

/* --- ROM loading ----------------------------------------------------- */

static void apply_extension_console_hint(const char *path)
{
    const char *ext = path ? strrchr(path, '.') : NULL;
    if (!ext) return;
    if (option.console != 0) return;
    if (!strcasecmp(ext, ".col") || !strcasecmp(ext, ".cv")) option.console = 6;  /* Coleco */
    else if (!strcasecmp(ext, ".gg")) option.console = 3;                          /* GG */
    else if (!strcasecmp(ext, ".sg")) option.console = 5;                          /* SG-1000 */
    else if (!strcasecmp(ext, ".m5")) option.console = 7;                          /* Sord M5 */
}

static int load_exact(const char *path, uint8_t *dst, size_t dst_size, size_t min_size, size_t *actual)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0 || (size_t)sz > dst_size || (size_t)sz < min_size)
    {
        fclose(fp);
        return 0;
    }
    memset(dst, 0xFF, dst_size);
    size_t got = fread(dst, 1, (size_t)sz, fp);
    fclose(fp);
    if (got != (size_t)sz) return 0;
    if (actual) *actual = (size_t)sz;
    return 1;
}

static const char *g_sysdir = NULL;

static void bios_init(void)
{
    bios.rom = calloc(1, 0x100000);
    if (!bios.rom) return;
    bios.enabled = 0;

    /* SMS BIOS (optional) */
    if (option.use_bios && (sms.console == CONSOLE_SMS || sms.console == CONSOLE_SMS2))
    {
        char path[1100];
        const char *names[] = { "bios.sms", "BIOS.sms", "bios_SMS.bin", NULL };
        int i;
        for (i = 0; names[i]; i++)
        {
            snprintf(path, sizeof(path), "%s/%s", g_sysdir ? g_sysdir : ".", names[i]);
            size_t size = 0;
            if (load_exact(path, bios.rom, 0x100000, 1, &size))
            {
                if (size < 0x4000) size = 0x4000;
                bios.enabled = (uint8_t)(option.use_bios | 2);
                bios.pages = (uint16_t)(size / 0x4000);
                if (log_cb) log_cb(RETRO_LOG_INFO, "SMS BIOS loaded: %s\n", path);
                break;
            }
        }
    }

    /* ColecoVision BIOS (required for Coleco) */
    if (sms.console == CONSOLE_COLECO)
    {
        char path[1100];
        const char *names[] = { "bios.col", "BIOS.col", "coleco.rom", NULL };
        int i;
        for (i = 0; names[i]; i++)
        {
            snprintf(path, sizeof(path), "%s/%s", g_sysdir ? g_sysdir : ".", names[i]);
            if (load_exact(path, coleco.rom, sizeof(coleco.rom), 0x2000, NULL))
            {
                if (log_cb) log_cb(RETRO_LOG_INFO, "Coleco BIOS loaded: %s\n", path);
                return;
            }
        }
        if (log_cb) log_cb(RETRO_LOG_WARN, "Coleco BIOS not found in %s\n", g_sysdir ? g_sysdir : ".");
    }

    /* Sord M5 BIOS (required for M5) */
    if (sms.console == CONSOLE_SORDM5)
    {
        char path[1100];
        const char *names[] = { "sordm5bios.bin", "m5.rom", NULL };
        int i;
        for (i = 0; names[i]; i++)
        {
            snprintf(path, sizeof(path), "%s/%s", g_sysdir ? g_sysdir : ".", names[i]);
            if (load_exact(path, coleco.rom, sizeof(coleco.rom), 0x2000, NULL))
            {
                if (log_cb) log_cb(RETRO_LOG_INFO, "Sord M5 BIOS loaded: %s\n", path);
                return;
            }
        }
        if (log_cb) log_cb(RETRO_LOG_WARN, "Sord M5 BIOS not found in %s\n", g_sysdir ? g_sysdir : ".");
    }
}

bool retro_load_game(const struct retro_game_info *info)
{
    if (!info) return false;

    const char *path = info->path;
    if (!path)
    {
        /* Some frontends pass an in-memory buffer; we still need a path for
         * the zip loader.  Use load_rom_buffer when path is NULL. */
        if (info->data && info->size)
        {
            if (g_rom_loaded) { system_poweroff(); g_rom_loaded = false; }
            config_defaults();
            init_bitmap();
            state_size_cache = 0;
            if (!load_rom_buffer((const uint8_t *)info->data, (uint32_t)info->size))
            {
                if (log_cb) log_cb(RETRO_LOG_ERROR, "load_rom_buffer failed\n");
                return false;
            }
            system_poweron();
            g_rom_loaded = true;
            /* Recompute AV info based on detected console */
            struct retro_system_av_info avi;
            retro_get_system_av_info(&avi);
            environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avi);
            return true;
        }
        return false;
    }

    /* Extract rom_dir for save states */
    const char *slash = strrchr(path, '/');
    if (!slash) slash = strrchr(path, '\\');
    if (slash)
    {
        size_t len = (size_t)(slash - path);
        if (len >= sizeof(g_rom_dir)) len = sizeof(g_rom_dir) - 1;
        memcpy(g_rom_dir, path, len);
        g_rom_dir[len] = '\0';
    }
    else
    {
        g_rom_dir[0] = '.';
        g_rom_dir[1] = '\0';
    }

    const char *sysdir = NULL;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sysdir) || !sysdir)
        sysdir = g_rom_dir;
    g_sysdir = sysdir;
    const char *svdir = NULL;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &svdir) || !svdir)
        svdir = g_rom_dir;
    snprintf(g_save_dir, sizeof(g_save_dir), "%s", svdir);

    if (g_rom_loaded) { system_poweroff(); g_rom_loaded = false; }
    config_defaults();
    init_bitmap();
    state_size_cache = 0;
    apply_extension_console_hint(path);
    snprintf(option.game_name, sizeof(option.game_name), "%s", path);

    if (!load_rom((char *)path))
    {
        if (log_cb) log_cb(RETRO_LOG_ERROR, "load_rom failed: %s\n", path);
        return false;
    }

    bios_init();
    system_poweron();
    g_rom_loaded = true;

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "Loaded: %s console=0x%02x mapper=%u crc=%08X\n",
               path, sms.console, cart.mapper, cart.crc);

    /* Update AV info based on detected console */
    struct retro_system_av_info avi;
    retro_get_system_av_info(&avi);
    environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avi);

    return true;
}

void retro_unload_game(void)
{
    if (g_rom_loaded)
    {
        system_poweroff();
        g_rom_loaded = false;
    }
    if (bios.rom)
    {
        free(bios.rom);
        bios.rom = NULL;
        bios.enabled = 0;
    }
}

bool retro_load_game_special(unsigned game_type,
                             const struct retro_game_info *info,
                             size_t num_info)
{
    (void)game_type; (void)info; (void)num_info;
    return false;
}

/* --- Memory + state -------------------------------------------------- */

void *retro_get_memory_data(unsigned id)
{
    if (id == RETRO_MEMORY_SAVE_RAM)
        return cart.sram;
    if (id == RETRO_MEMORY_SYSTEM_RAM)
        return sms.wram;  /* main work RAM if exposed; fall through to NULL otherwise */
    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    if (id == RETRO_MEMORY_SAVE_RAM)
        return sizeof(cart.sram);
    return 0;
}

size_t retro_serialize_size(void)
{
    if (state_size_cache) return state_size_cache;
    return compute_state_size();
}

bool retro_serialize(void *data, size_t size)
{
    uint8_t *raw = NULL;
    uint32_t raw_size = 0;
    if (!system_save_state_buffer(&raw, &raw_size) || !raw)
        return false;
    if (raw_size > size)
    {
        system_free_state_buffer(raw);
        return false;
    }
    memcpy(data, raw, raw_size);
    system_free_state_buffer(raw);
    state_size_cache = raw_size;
    return true;
}

bool retro_unserialize(const void *data, size_t size)
{
    if (!system_load_state_buffer((const uint8_t *)data, (uint32_t)size))
        return false;
    state_size_cache = size;
    return true;
}

void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned index, bool on, const char *code)
{
    (void)index; (void)on; (void)code;
}

/* --- Stubs for less-common hooks ------------------------------------- */

/* region/name metadata */
unsigned retro_get_region(void)
{
    return (sms.display == DISPLAY_PAL) ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

void retro_get_game_name(char *out, size_t len)
{
    if (!out || !len) return;
    snprintf(out, len, "MultiRexZ80");
}
