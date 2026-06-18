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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "shared.h"
#include "headless_platform.h"
#include "debug_console.h"

#define HEADLESS_BITMAP_WIDTH 400
#define HEADLESS_BITMAP_HEIGHT 400

t_config option;
static void *headless_pixels;
static const char *headless_sram_path;

typedef struct cli_options
{
    const char *rom_path;
    const char *bios_path;
    const char *sms_bios_path;
    const char *coleco_bios_path;
    const char *save_state_path;
    const char *load_state_path;
    const char *debug_console_path;
    const char *debug_console_stop_text;
    uint64_t frames;
    uint8_t skip_render;
    uint8_t force_console;
    uint8_t quiet;
    uint8_t force_lightgun;
    int32_t lightgun_x;
    int32_t lightgun_y;
    multirexz80_headless_platform_options_t platform;
} cli_options_t;

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options] ROM\n"
        "\n"
        "Headless MultiRexZ80 runner. No SDL, video display, or host audio backend is used.\n"
        "\n"
        "Core options:\n"
        "  --frames N                 Run N frames (default: 300)\n"
        "  --console NAME             auto,sms,sms2,gg,ggms/ggsms,sg1000,coleco,sordm5,systeme,system1,psychos,taitol\n"
        "  --region NAME              auto,ntsc,pal,japan\n"
        "  --bios PATH                BIOS for the selected legacy machine; for .m5 this is Sord M5\n"
        "  --sms-bios PATH            SMS BIOS file\n"
        "  --coleco-bios PATH         ColecoVision BIOS file\n"
        "  --sram PATH                SRAM save/load path\n"
        "  --load-state PATH          Load PNG/.sgxst/legacy raw state after power-on\n"
        "  --save-state PATH          Save PNG state after the final frame\n"
        "  --no-render                Execute without producing the internal video bitmap\n"
        "  --lcd-persistence          Enable Game Gear LCD persistence filter (default)\n"
        "  --no-lcd-persistence       Disable Game Gear LCD persistence filter\n"
        "  --lightgun                 Force Light Phaser on port 1\n"
        "  --lightgun-x N             Initial Light Phaser X coordinate (0..255)\n"
        "  --lightgun-y N             Initial Light Phaser Y coordinate\n"
        "  --lightgun-cursor          Draw software lightgun cursor in captures (default)\n"
        "  --no-lightgun-cursor       Hide software lightgun cursor in captures\n"
        "\n"
        "Inspection options:\n"
        "  --input-playback PATH      Input script: Nf:A taps, Nf:+A/-A holds, or legacy snapshots\n"
        "  --input-record PATH        Record compact replay input script\n"
        "  --input-tap-frames N       Hold Nf:ACTION tap entries for N frames (default 6)\n"
        "  --audio-wav PATH           Write generated stereo 16-bit PCM to WAV\n"
        "  --trace PATH               CSV trace of CPU frame snapshots and VDP/PSG/YM writes\n"
        "  --debug-console PATH       Optional Emulicious/BGB debug console log; use - for stdout\n"
        "  --debug-console-stop TEXT  Stop early once TEXT appears in the debug console log\n"
        "  --dump-prefix PREFIX       Write final memory/device dumps using PREFIX_* names\n"
        "  --dump-every N             Also dump every N frames\n"
        "  --screenshot PATH          Final screenshot as binary PPM\n"
        "  --screenshot-prefix PREFIX Write PPM screenshots every --screenshot-every frames\n"
        "  --screenshot-every N       Screenshot cadence for --screenshot-prefix\n"
        "  --video-y4m PATH           Lightweight raw YUV4MPEG2 video; SMS frames are padded to 256x240\n"
        "  --quiet                    Suppress progress output\n",
        argv0);
}

static int parse_u64_arg(const char *s, uint64_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno || end == s || *end) return 0;
    *out = (uint64_t)v;
    return 1;
}

static int need_value(int argc, char **argv, int *i)
{
    if (*i + 1 >= argc)
    {
        fprintf(stderr, "Missing value for %s\n", argv[*i]);
        return 0;
    }
    (*i)++;
    return 1;
}

static int set_console_option(const char *name)
{
    if (!strcasecmp(name, "auto")) option.console = 0;
    else if (!strcasecmp(name, "sms")) option.console = 1;
    else if (!strcasecmp(name, "sms2")) option.console = 2;
    else if (!strcasecmp(name, "gg")) option.console = 3;
    else if (!strcasecmp(name, "ggms") || !strcasecmp(name, "ggsms")) option.console = 4;
    else if (!strcasecmp(name, "sg1000")) option.console = 5;
    else if (!strcasecmp(name, "coleco")) option.console = 6;
    else if (!strcasecmp(name, "sordm5") || !strcasecmp(name, "m5")) option.console = 7;
    else if (!strcasecmp(name, "systeme") || !strcasecmp(name, "segae")) option.console = 8;
    else if (!strcasecmp(name, "system1") || !strcasecmp(name, "segas1") || !strcasecmp(name, "sega1")) option.console = 9;
    else if (!strcasecmp(name, "psychos") || !strcasecmp(name, "snkpsychos") || !strcasecmp(name, "snk")) option.console = 10;
    else if (!strcasecmp(name, "taitol") || !strcasecmp(name, "taito")) option.console = 11;
    else return 0;
    return 1;
}

static int set_region_option(const char *name)
{
    if (!strcasecmp(name, "auto")) option.country = 0;
    else if (!strcasecmp(name, "ntsc") || !strcasecmp(name, "usa")) option.country = 1;
    else if (!strcasecmp(name, "pal") || !strcasecmp(name, "europe")) option.country = 2;
    else if (!strcasecmp(name, "japan") || !strcasecmp(name, "jp")) option.country = 3;
    else return 0;
    return 1;
}

static const char *path_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot ? dot : "";
}

static void apply_extension_console_hint(const char *path)
{
    const char *ext = path_ext(path);
    if (option.console != 0) return;
    if (!strcasecmp(ext, ".col") || !strcasecmp(ext, ".cv")) option.console = 6;
    else if (!strcasecmp(ext, ".gg")) option.console = 3;
    else if (!strcasecmp(ext, ".sg")) option.console = 5;
    else if (!strcasecmp(ext, ".m5")) option.console = 7;
}

static int parse_cli(int argc, char **argv, cli_options_t *cli)
{
    memset(cli, 0, sizeof(*cli));
    cli->frames = 300;
    cli->lightgun_x = 128;
    cli->lightgun_y = 96;

    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h"))
        {
            usage(argv[0]);
            exit(0);
        }
        else if (!strcmp(a, "--frames"))
        {
            if (!need_value(argc, argv, &i) || !parse_u64_arg(argv[i], &cli->frames)) return 0;
        }
        else if (!strcmp(a, "--console"))
        {
            if (!need_value(argc, argv, &i) || !set_console_option(argv[i])) return 0;
            cli->force_console = 1;
        }
        else if (!strcmp(a, "--region"))
        {
            if (!need_value(argc, argv, &i) || !set_region_option(argv[i])) return 0;
        }
        else if (!strcmp(a, "--bios")) { if (!need_value(argc, argv, &i)) return 0; cli->bios_path = argv[i]; }
        else if (!strcmp(a, "--sms-bios")) { if (!need_value(argc, argv, &i)) return 0; cli->sms_bios_path = argv[i]; }
        else if (!strcmp(a, "--coleco-bios")) { if (!need_value(argc, argv, &i)) return 0; cli->coleco_bios_path = argv[i]; }
        else if (!strcmp(a, "--sram")) { if (!need_value(argc, argv, &i)) return 0; headless_sram_path = argv[i]; }
        else if (!strcmp(a, "--load-state")) { if (!need_value(argc, argv, &i)) return 0; cli->load_state_path = argv[i]; }
        else if (!strcmp(a, "--save-state")) { if (!need_value(argc, argv, &i)) return 0; cli->save_state_path = argv[i]; }
        else if (!strcmp(a, "--no-render")) cli->skip_render = 1;
        else if (!strcmp(a, "--input-playback")) { if (!need_value(argc, argv, &i)) return 0; cli->platform.input_playback_path = argv[i]; }
        else if (!strcmp(a, "--input-record")) { if (!need_value(argc, argv, &i)) return 0; cli->platform.input_record_path = argv[i]; }
        else if (!strcmp(a, "--input-tap-frames")) { uint64_t v = 0; if (!need_value(argc, argv, &i) || !parse_u64_arg(argv[i], &v)) return 0; cli->platform.input_tap_frames = (uint32_t)v; }
        else if (!strcmp(a, "--audio-wav")) { if (!need_value(argc, argv, &i)) return 0; cli->platform.audio_wav_path = argv[i]; }
        else if (!strcmp(a, "--audio-highpass-hz")) { if (!need_value(argc, argv, &i)) return 0; option.audio_highpass_hz = atoi(argv[i]); if (option.audio_highpass_hz < 0) option.audio_highpass_hz = 0; }
        else if (!strcmp(a, "--audio-lowpass-hz")) { if (!need_value(argc, argv, &i)) return 0; option.audio_lowpass_hz = atoi(argv[i]); if (option.audio_lowpass_hz < 0) option.audio_lowpass_hz = 0; }
        else if (!strcmp(a, "--audio-headroom-db")) { if (!need_value(argc, argv, &i)) return 0; option.audio_headroom_db = atoi(argv[i]); if (option.audio_headroom_db < 0) option.audio_headroom_db = 0; if (option.audio_headroom_db > 9) option.audio_headroom_db = 9; }
        else if (!strcmp(a, "--audio-dc-blocker")) option.audio_dc_blocker = 1;
        else if (!strcmp(a, "--no-audio-dc-blocker")) option.audio_dc_blocker = 0;
        else if (!strcmp(a, "--audio-limiter")) option.audio_limiter = 1;
        else if (!strcmp(a, "--no-audio-limiter")) option.audio_limiter = 0;
        else if (!strcmp(a, "--trace")) { if (!need_value(argc, argv, &i)) return 0; cli->platform.trace_path = argv[i]; }
        else if (!strcmp(a, "--debug-console")) { if (!need_value(argc, argv, &i)) return 0; cli->debug_console_path = argv[i]; }
        else if (!strcmp(a, "--debug-console-stop")) { if (!need_value(argc, argv, &i)) return 0; cli->debug_console_stop_text = argv[i]; }
        else if (!strcmp(a, "--dump-prefix")) { if (!need_value(argc, argv, &i)) return 0; cli->platform.dump_prefix = argv[i]; }
        else if (!strcmp(a, "--dump-every")) { uint64_t v; if (!need_value(argc, argv, &i) || !parse_u64_arg(argv[i], &v)) return 0; cli->platform.dump_every = (uint32_t)v; }
        else if (!strcmp(a, "--screenshot")) { if (!need_value(argc, argv, &i)) return 0; cli->platform.screenshot_path = argv[i]; }
        else if (!strcmp(a, "--screenshot-prefix")) { if (!need_value(argc, argv, &i)) return 0; cli->platform.screenshot_prefix = argv[i]; }
        else if (!strcmp(a, "--screenshot-every")) { uint64_t v; if (!need_value(argc, argv, &i) || !parse_u64_arg(argv[i], &v)) return 0; cli->platform.screenshot_every = (uint32_t)v; }
        else if (!strcmp(a, "--video-y4m")) { if (!need_value(argc, argv, &i)) return 0; cli->platform.video_y4m_path = argv[i]; }
        else if (!strcmp(a, "--lcd-persistence")) option.lcd_persistence = 1;
        else if (!strcmp(a, "--no-lcd-persistence")) option.lcd_persistence = 0;
        else if (!strcmp(a, "--lightgun")) cli->force_lightgun = 1;
        else if (!strcmp(a, "--lightgun-x")) { uint64_t v; if (!need_value(argc, argv, &i) || !parse_u64_arg(argv[i], &v)) return 0; cli->lightgun_x = (int32_t)v; }
        else if (!strcmp(a, "--lightgun-y")) { uint64_t v; if (!need_value(argc, argv, &i) || !parse_u64_arg(argv[i], &v)) return 0; cli->lightgun_y = (int32_t)v; }
        else if (!strcmp(a, "--lightgun-cursor")) option.lightgun_cursor = 1;
        else if (!strcmp(a, "--no-lightgun-cursor")) option.lightgun_cursor = 0;
        else if (!strcmp(a, "--quiet")) { cli->quiet = 1; cli->platform.quiet = 1; }
        else if (a[0] == '-')
        {
            fprintf(stderr, "Unknown option: %s\n", a);
            return 0;
        }
        else
        {
            if (cli->rom_path)
            {
                fprintf(stderr, "Only one ROM path may be supplied.\n");
                return 0;
            }
            cli->rom_path = a;
        }
    }

    if (!cli->rom_path)
    {
        usage(argv[0]);
        return 0;
    }
    return 1;
}

static void defaults(void)
{
    memset(&option, 0, sizeof(option));
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

static int bios_init_headless(const cli_options_t *cli)
{
    bios.rom = calloc(1, 0x100000);
    if (!bios.rom) return 0;
    bios.enabled = 0;

    const char *sms_bios = cli->sms_bios_path;
    if (!sms_bios && cli->bios_path && IS_SMS) sms_bios = cli->bios_path;
    if (sms_bios)
    {
        size_t size = 0;
        if (!load_exact(sms_bios, bios.rom, 0x100000, 1, &size))
        {
            fprintf(stderr, "Failed to load SMS BIOS: %s\n", sms_bios);
            return 0;
        }
        if (size < 0x4000) size = 0x4000;
        bios.enabled = (uint8_t)(option.use_bios | 2);
        bios.pages = (uint16_t)(size / 0x4000);
    }

    const char *legacy_bios = cli->coleco_bios_path;
    if (!legacy_bios && cli->bios_path && sms.console == CONSOLE_COLECO) legacy_bios = cli->bios_path;
    if (!legacy_bios && cli->bios_path && sms.console == CONSOLE_SORDM5) legacy_bios = cli->bios_path;
    if (!legacy_bios && sms.console == CONSOLE_SORDM5) legacy_bios = "sordm5bios.bin";
    if (legacy_bios)
    {
        if (!load_exact(legacy_bios, coleco.rom, sizeof(coleco.rom), 0x2000, NULL))
        {
            fprintf(stderr, "Failed to load legacy/Sord M5 BIOS: %s\n", legacy_bios);
            return 0;
        }
    }
    else if (sms.console == CONSOLE_COLECO)
    {
        fprintf(stderr, "Warning: no ColecoVision BIOS supplied; cartridge mapper can be inspected, but software will not boot normally. Use --coleco-bios PATH.\n");
    }

    return 1;
}

void smsp_state(uint8_t slot_number, uint8_t mode)
{
    (void)slot_number;
    (void)mode;
}

void system_manage_sram(uint8_t *sram, uint8_t slot_number, uint8_t mode)
{
    (void)slot_number;
    if (!headless_sram_path)
    {
        if (mode == SRAM_LOAD) memset(sram, 0, 0x8000);
        return;
    }

    FILE *fp = NULL;
    if (mode == SRAM_LOAD)
    {
        fp = fopen(headless_sram_path, "rb");
        if (fp)
        {
            fread(sram, 1, 0x8000, fp);
            fclose(fp);
            sms.save = 1;
        }
        else
        {
            memset(sram, 0, 0x8000);
        }
    }
    else if (mode == SRAM_SAVE && sms.save)
    {
        fp = fopen(headless_sram_path, "wb");
        if (fp)
        {
            fwrite(sram, 1, 0x8000, fp);
            fclose(fp);
        }
    }
}

static int init_bitmap(void)
{
    size_t bytes = (size_t)HEADLESS_BITMAP_WIDTH * HEADLESS_BITMAP_HEIGHT * MULTIREXZ80_RENDER_BYTES_PER_PIXEL;
    headless_pixels = calloc(1, bytes);
    if (!headless_pixels) return 0;
    bitmap.width = HEADLESS_BITMAP_WIDTH;
    bitmap.height = HEADLESS_BITMAP_HEIGHT;
    bitmap.depth = MULTIREXZ80_RENDER_DEPTH;
    bitmap.data = (uint8_t *)(void *)headless_pixels;
    bitmap.pitch = HEADLESS_BITMAP_WIDTH * MULTIREXZ80_RENDER_BYTES_PER_PIXEL;
    bitmap.viewport.w = VIDEO_WIDTH_SMS;
    bitmap.viewport.h = VIDEO_HEIGHT_SMS;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    return 1;
}


static uint32_t headless_read_xrgb8888(uint32_t x, uint32_t y)
{
    if (!bitmap.data || x >= bitmap.width || y >= bitmap.height)
        return 0xff000000u;
#ifdef MULTIREXZ80_RENDER_32BPP
    return ((const uint32_t *)(const void *)(bitmap.data + (size_t)y * bitmap.pitch))[x] | 0xff000000u;
#else
    {
        uint16_t p = ((const uint16_t *)(const void *)(bitmap.data + (size_t)y * bitmap.pitch))[x];
        uint32_t r = ((p >> 11) & 0x1f) * 255u / 31u;
        uint32_t g = ((p >> 5) & 0x3f) * 255u / 63u;
        uint32_t b = (p & 0x1f) * 255u / 31u;
        return 0xff000000u | (r << 16) | (g << 8) | b;
    }
#endif
}

static uint32_t *capture_state_thumbnail(uint32_t *out_w, uint32_t *out_h, uint32_t *out_pitch)
{
    uint32_t x0 = (uint32_t)((bitmap.viewport.x < 0) ? 0 : bitmap.viewport.x);
    uint32_t w = (uint32_t)((bitmap.viewport.w > 0) ? bitmap.viewport.w : 256);
    uint32_t h = (uint32_t)((bitmap.viewport.h > 0) ? bitmap.viewport.h : vdp.height);
    uint32_t *pixels;
    uint32_t x, y;

    if (!out_w || !out_h || !out_pitch || !bitmap.data || w == 0 || h == 0)
        return NULL;
    if (x0 + w > bitmap.width) w = bitmap.width - x0;
    if (h > bitmap.height) h = bitmap.height;
    pixels = (uint32_t *)malloc((size_t)w * h * sizeof(uint32_t));
    if (!pixels) return NULL;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            pixels[(size_t)y * w + x] = headless_read_xrgb8888(x0 + x, y);
    *out_w = w;
    *out_h = h;
    *out_pitch = w * 4;
    return pixels;
}

static void cleanup(void)
{
    system_poweroff();
    system_shutdown();
    if (bios.rom)
    {
        free(bios.rom);
        bios.rom = NULL;
    }
    free(headless_pixels);
    headless_pixels = NULL;
}

int main(int argc, char **argv)
{
    cli_options_t cli;
    defaults();
    if (!parse_cli(argc, argv, &cli)) return 2;
    apply_extension_console_hint(cli.rom_path);

    snprintf(option.game_name, sizeof(option.game_name), "%s", cli.rom_path);

    if (!load_rom((char *)cli.rom_path))
    {
        fprintf(stderr, "Failed to load ROM: %s\n", cli.rom_path);
        return 1;
    }

    if (!init_bitmap())
    {
        fprintf(stderr, "Failed to allocate headless bitmap.\n");
        return 1;
    }

    if (!bios_init_headless(&cli))
    {
        cleanup();
        return 1;
    }

    if (!multirexz80_debug_console_open(cli.debug_console_path, cli.debug_console_stop_text))
    {
#if MULTIREXZ80_DEBUG_CONSOLE
        fprintf(stderr, "Failed to open debug console output: %s\n", cli.debug_console_path ? cli.debug_console_path : "(null)");
#else
        fprintf(stderr, "This build has no debug console support; rebuild with ENABLE_DEBUG_CONSOLE=1.\n");
#endif
        cleanup();
        return 1;
    }

    system_poweron();
    if (cli.load_state_path && !system_load_state_file(cli.load_state_path))
    {
        fprintf(stderr, "Failed to load state: %s\n", cli.load_state_path);
        multirexz80_debug_console_close();
        cleanup();
        return 1;
    }
    if (cli.force_lightgun) sms.device[0] = DEVICE_LIGHTGUN;
    input.analog[0][0] = cli.lightgun_x;
    input.analog[0][1] = cli.lightgun_y;

    multirexz80_headless_platform_t *platform = NULL;
    if (!multirexz80_headless_platform_create(&platform, &cli.platform))
    {
        fprintf(stderr, "Failed to initialize headless inspection outputs.\n");
        multirexz80_debug_console_close();
        cleanup();
        return 1;
    }

    if (!cli.quiet)
    {
        fprintf(stdout, "%s %s headless\n", APP_NAME, APP_VERSION);
        fprintf(stdout, "rom=%s crc=%08X console=%u mapper=%u pages=%u display=%s frames=%llu\n",
                cli.rom_path, cart.crc, sms.console, cart.mapper, cart.pages,
                sms.display == DISPLAY_PAL ? "PAL" : "NTSC",
                (unsigned long long)cli.frames);
    }

    int ok = 1;
    for (uint64_t frame = 0; frame < cli.frames; frame++)
    {
        if (!multirexz80_headless_platform_begin_frame(platform, frame)) { ok = 0; break; }
        system_frame(cli.skip_render);
        if (!multirexz80_headless_platform_end_frame(platform, frame)) { ok = 0; break; }
        if (multirexz80_debug_console_stop_requested()) break;
    }

    if (ok)
        ok = multirexz80_headless_platform_save_final(platform, cli.frames);

    if (ok && cli.save_state_path)
    {
        uint32_t thumb_w = 0, thumb_h = 0, thumb_pitch = 0;
        uint32_t *thumb = capture_state_thumbnail(&thumb_w, &thumb_h, &thumb_pitch);
        if (!system_save_state_file_ex(cli.save_state_path, thumb, thumb_w, thumb_h, thumb_pitch))
        {
            fprintf(stderr, "Failed to save state: %s\n", cli.save_state_path);
            ok = 0;
        }
        free(thumb);
    }

    multirexz80_headless_platform_destroy(platform);
    multirexz80_debug_console_close();
    cleanup();

    return ok ? 0 : 1;
}
