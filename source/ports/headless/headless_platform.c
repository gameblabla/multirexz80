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

#include "shared.h"
#include "headless_platform.h"
#include "input_script.h"



struct multirexz80_headless_platform
{
    multirexz80_headless_platform_options_t opt;
    multirexz80_input_recorder_t *input_record;
    FILE *audio_wav;
    FILE *trace;
    FILE *video_y4m;
    multirexz80_input_script_t *input_playback;
    uint64_t audio_bytes;
    uint32_t video_w;
    uint32_t video_h;
};

static uint8_t clamp_u8(int32_t v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static uint32_t read_native_pixel(uint32_t x, uint32_t y)
{
    if (!bitmap.data || x >= bitmap.width || y >= bitmap.height) return 0;
    if (bitmap.depth == 32)
        return ((const uint32_t *)(const void *)(bitmap.data + y * bitmap.pitch))[x];
    return ((const uint16_t *)(const void *)(bitmap.data + y * bitmap.pitch))[x];
}

static void pixel_to_rgb(uint32_t p, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (bitmap.depth == 32)
    {
        *r = (uint8_t)((p >> 16) & 0xff);
        *g = (uint8_t)((p >> 8) & 0xff);
        *b = (uint8_t)(p & 0xff);
    }
    else
    {
        *r = (uint8_t)((((p >> 11) & 0x1F) * 255) / 31);
        *g = (uint8_t)((((p >> 5) & 0x3F) * 255) / 63);
        *b = (uint8_t)(((p & 0x1F) * 255) / 31);
    }
}

static int write_file_bytes(const char *path, const void *data, size_t size)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    int ok = fwrite(data, 1, size, fp) == size;
    fclose(fp);
    return ok;
}

static void make_frame_path(char *out, size_t out_size, const char *prefix, uint64_t frame, const char *suffix)
{
    snprintf(out, out_size, "%s_%06llu%s", prefix, (unsigned long long)frame, suffix);
}

static int save_ppm(const char *path)
{
    uint32_t x0 = (uint32_t)((bitmap.viewport.x < 0) ? 0 : bitmap.viewport.x);
    uint32_t w = (uint32_t)((bitmap.viewport.w > 0) ? bitmap.viewport.w : 256);
    uint32_t h = (uint32_t)((bitmap.viewport.h > 0) ? bitmap.viewport.h : vdp.height);
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    fprintf(fp, "P6\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++)
    {
        for (uint32_t x = 0; x < w; x++)
        {
            uint8_t rgb[3];
            uint32_t p = read_native_pixel(x0 + x, y);
            pixel_to_rgb(p, &rgb[0], &rgb[1], &rgb[2]);
            fwrite(rgb, 1, sizeof(rgb), fp);
        }
    }
    fclose(fp);
    return 1;
}

static void wav_write_header(FILE *fp, uint32_t sample_rate, uint32_t data_size)
{
    uint32_t riff_size = 36u + data_size;
    uint16_t audio_format = 1;
    uint16_t channels = 2;
    uint16_t bits_per_sample = 16;
    uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8);
    uint16_t block_align = channels * (bits_per_sample / 8);

    fseek(fp, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, fp);
    fwrite(&riff_size, 4, 1, fp);
    fwrite("WAVEfmt ", 1, 8, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    fwrite(&audio_format, 2, 1, fp);
    fwrite(&channels, 2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits_per_sample, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);
}

static int wav_open(multirexz80_headless_platform_t *p)
{
    if (!p->opt.audio_wav_path) return 1;
    p->audio_wav = fopen(p->opt.audio_wav_path, "wb+");
    if (!p->audio_wav) return 0;
    wav_write_header(p->audio_wav, SOUND_FREQUENCY, 0);
    return 1;
}

static void wav_close(multirexz80_headless_platform_t *p)
{
    if (!p->audio_wav) return;
    uint32_t bounded = p->audio_bytes > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)p->audio_bytes;
    wav_write_header(p->audio_wav, SOUND_FREQUENCY, bounded);
    fclose(p->audio_wav);
    p->audio_wav = NULL;
}

static void yuv_from_rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t *y, uint8_t *u, uint8_t *v)
{
    *y = clamp_u8(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    *u = clamp_u8(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
    *v = clamp_u8(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
}

static int y4m_open(multirexz80_headless_platform_t *p)
{
    if (!p->opt.video_y4m_path) return 1;
    if (sms.console == CONSOLE_SNKPSYCHOS)
    {
        p->video_w = SNK_PSYCHOS_VISIBLE_WIDTH;
        p->video_h = SNK_PSYCHOS_VISIBLE_HEIGHT;
    }
    else
    {
        p->video_w = (sms.console == CONSOLE_GG && !option.extra_gg) ? 160u : 256u;
        p->video_h = (sms.console == CONSOLE_GG && !option.extra_gg) ? 144u : 240u;
    }
    p->video_y4m = fopen(p->opt.video_y4m_path, "wb");
    if (!p->video_y4m) return 0;
    if (sms.display == DISPLAY_PAL)
        fprintf(p->video_y4m, "YUV4MPEG2 W%u H%u F50:1 Ip A1:1 C444\n", p->video_w, p->video_h);
    else
        fprintf(p->video_y4m, "YUV4MPEG2 W%u H%u F60000:1001 Ip A1:1 C444\n", p->video_w, p->video_h);
    return 1;
}

static int y4m_write_frame(multirexz80_headless_platform_t *p)
{
    if (!p->video_y4m) return 1;

    size_t plane = (size_t)p->video_w * p->video_h;
    uint8_t *ybuf = calloc(plane * 3, 1);
    if (!ybuf) return 0;
    uint8_t *ubuf = ybuf + plane;
    uint8_t *vbuf = ubuf + plane;

    uint32_t x0 = (uint32_t)((bitmap.viewport.x < 0) ? 0 : bitmap.viewport.x);
    uint32_t active_w = (bitmap.viewport.w > 0) ? (uint32_t)bitmap.viewport.w : p->video_w;
    uint32_t active_h = (bitmap.viewport.h > 0) ? (uint32_t)bitmap.viewport.h : (uint32_t)vdp.height;
    if (active_w > p->video_w) active_w = p->video_w;
    if (active_h > p->video_h) active_h = p->video_h;

    for (uint32_t yy = 0; yy < p->video_h; yy++)
    {
        for (uint32_t xx = 0; xx < p->video_w; xx++)
        {
            uint8_t r = 0, g = 0, b = 0;
            if (xx < active_w && yy < active_h)
            {
                uint32_t pix = read_native_pixel(x0 + xx, yy);
                pixel_to_rgb(pix, &r, &g, &b);
            }
            yuv_from_rgb(r, g, b, &ybuf[yy * p->video_w + xx], &ubuf[yy * p->video_w + xx], &vbuf[yy * p->video_w + xx]);
        }
    }

    fwrite("FRAME\n", 1, 6, p->video_y4m);
    fwrite(ybuf, 1, plane * 3, p->video_y4m);
    free(ybuf);
    return 1;
}



static int dump_state(const char *prefix)
{
    char path[1024];
    uint8_t mem[0x10000];
    for (uint32_t a = 0; a < 0x10000; a++)
        mem[a] = cpu_readmap[a >> 10][a & 0x03FF];

    snprintf(path, sizeof(path), "%s_mem.bin", prefix);
    if (!write_file_bytes(path, mem, sizeof(mem))) return 0;
    snprintf(path, sizeof(path), "%s_wram.bin", prefix);
    if (!write_file_bytes(path, sms.wram, sizeof(sms.wram))) return 0;
    snprintf(path, sizeof(path), "%s_cart_sram.bin", prefix);
    if (!write_file_bytes(path, cart.sram, sizeof(cart.sram))) return 0;
    snprintf(path, sizeof(path), "%s_vram.bin", prefix);
    if (!write_file_bytes(path, vdp.vram, sizeof(vdp.vram))) return 0;
    snprintf(path, sizeof(path), "%s_cram.bin", prefix);
    if (!write_file_bytes(path, vdp.cram, sizeof(vdp.cram))) return 0;
    snprintf(path, sizeof(path), "%s_vdp_regs.bin", prefix);
    if (!write_file_bytes(path, vdp.reg, sizeof(vdp.reg))) return 0;
    if (sms.console == CONSOLE_SYSTEME || sms.console == CONSOLE_SYSTEM1 || sms.console == CONSOLE_SNKPSYCHOS)
    {
        snprintf(path, sizeof(path), "%s_vdp2_vram.bin", prefix);
        if (!write_file_bytes(path, vdp2.vram, sizeof(vdp2.vram))) return 0;
        snprintf(path, sizeof(path), "%s_vdp2_cram.bin", prefix);
        if (!write_file_bytes(path, vdp2.cram, sizeof(vdp2.cram))) return 0;
        snprintf(path, sizeof(path), "%s_vdp2_regs.bin", prefix);
        if (!write_file_bytes(path, vdp2.reg, sizeof(vdp2.reg))) return 0;
    }

snprintf(path, sizeof(path), "%s_psg_context.bin", prefix);
    if (!write_file_bytes(path, &PSG, sizeof(PSG))) return 0;

    uint32_t fm_size = FM_GetContextSize();
    if (fm_size)
    {
        uint8_t *fm_ctx = malloc(fm_size);
        if (fm_ctx)
        {
            FM_GetContext(fm_ctx);
            snprintf(path, sizeof(path), "%s_ym2413_context.bin", prefix);
            write_file_bytes(path, fm_ctx, fm_size);
            free(fm_ctx);
        }
    }

    snprintf(path, sizeof(path), "%s_cpu.txt", prefix);
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    fprintf(fp,
            "console=%u display=%u territory=%u mapper=%u pages=%u megacart_bankcount=%u megacart_active=%u device0=%u device1=%u lightgun_xy0=%d,%d lightgun_xy1=%d,%d\n"
            "pc=%04X sp=%04X af=%04X bc=%04X de=%04X hl=%04X ix=%04X iy=%04X wz=%04X\n"
            "i=%02X r=%02X iff1=%u iff2=%u im=%u halt=%u cycles=%d line=%d\n"
            "vdp_mode=%u vdp_height=%u vdp_lpf=%u vdp_status=%02X vdp_addr=%04X vdp_code=%u\n",
            sms.console, sms.display, sms.territory, cart.mapper, cart.pages,
            slot.coleco_megacart_bankcount, slot.coleco_megacart_activebank,
            sms.device[0], sms.device[1], input.analog[0][0], input.analog[0][1],
            input.analog[1][0], input.analog[1][1],
            Z80.pc.w.l, Z80.sp.w.l, Z80.af.w.l, Z80.bc.w.l, Z80.de.w.l,
            Z80.hl.w.l, Z80.ix.w.l, Z80.iy.w.l, Z80.wz.w.l,
            Z80.i, (uint8_t)((Z80.r & 0x7f) | (Z80.r2 & 0x80)), Z80.iff1, Z80.iff2,
            Z80.im, Z80.halt, z80_get_elapsed_cycles(), vdp.line,
            vdp.mode, vdp.height, vdp.lpf, vdp.status, vdp.addr, vdp.code);
    fclose(fp);
    return 1;
}

int multirexz80_headless_platform_create(multirexz80_headless_platform_t **out,
                                     const multirexz80_headless_platform_options_t *options)
{
    multirexz80_headless_platform_t *p = calloc(1, sizeof(*p));
    if (!p) return 0;
    if (options) p->opt = *options;

    if (p->opt.input_record_path)
    {
        if (!multirexz80_input_recorder_open(&p->input_record, p->opt.input_record_path, 1)) goto fail;
    }
    if (p->opt.input_playback_path)
    {
        uint32_t tap_frames = p->opt.input_tap_frames ? p->opt.input_tap_frames : MULTIREXZ80_INPUT_TAP_FRAMES_DEFAULT;
        if (!multirexz80_input_script_load(&p->input_playback, p->opt.input_playback_path, tap_frames)) goto fail;
    }
    if (!wav_open(p)) goto fail;
    if (p->opt.trace_path)
    {
        p->trace = fopen(p->opt.trace_path, "wb");
        if (!p->trace) goto fail;
    }
    MULTIREXZ80_TRACE_SET_TRACE(p->trace);
    if (!y4m_open(p)) goto fail;

    *out = p;
    return 1;

fail:
    multirexz80_headless_platform_destroy(p);
    return 0;
}

void multirexz80_headless_platform_destroy(multirexz80_headless_platform_t *p)
{
    if (!p) return;
    wav_close(p);
    multirexz80_input_recorder_close(p->input_record);
    if (p->trace) fclose(p->trace);
    if (p->video_y4m) fclose(p->video_y4m);
    multirexz80_input_script_free(p->input_playback);
    free(p);
}

int multirexz80_headless_platform_begin_frame(multirexz80_headless_platform_t *p, uint64_t frame)
{
    MULTIREXZ80_TRACE_SET_FRAME(frame);
    if (!p) return 1;
    multirexz80_input_script_apply_frame(p->input_playback, frame, &input);
    return 1;
}

int multirexz80_headless_platform_end_frame(multirexz80_headless_platform_t *p, uint64_t frame)
{
    MULTIREXZ80_TRACE_CPU_FRAME(frame);
    if (!p) return 1;

    multirexz80_input_recorder_write_state_changes(p->input_record, frame, &input);

    if (p->audio_wav && snd.output && snd.sample_count > 0)
    {
        size_t bytes = (size_t)snd.sample_count * 2u * sizeof(int16_t);
        fwrite(snd.output, 1, bytes, p->audio_wav);
        p->audio_bytes += bytes;
    }

    if (p->video_y4m && !y4m_write_frame(p)) return 0;

    if (p->opt.screenshot_every && p->opt.screenshot_prefix && ((frame + 1) % p->opt.screenshot_every == 0))
    {
        char path[1024];
        make_frame_path(path, sizeof(path), p->opt.screenshot_prefix, frame + 1, ".ppm");
        save_ppm(path);
    }

    if (p->opt.dump_every && p->opt.dump_prefix && ((frame + 1) % p->opt.dump_every == 0))
    {
        char path[1024];
        make_frame_path(path, sizeof(path), p->opt.dump_prefix, frame + 1, "");
        dump_state(path);
    }

    return 1;
}

int multirexz80_headless_platform_save_final(multirexz80_headless_platform_t *p, uint64_t frame)
{
    int ok = 1;
    if (!p) return 1;
    if (p->opt.screenshot_path)
        ok &= save_ppm(p->opt.screenshot_path);
    if (p->opt.dump_prefix)
    {
        char path[1024];
        make_frame_path(path, sizeof(path), p->opt.dump_prefix, frame, "_final");
        ok &= dump_state(path);
    }
    return ok;
}
