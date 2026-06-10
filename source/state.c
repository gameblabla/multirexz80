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

/******************************************************************************
 *  Sega Master System / GameGear Emulator
 *  Copyright (C) 1998-2007  Charles MacDonald
 *
 *  additional code by Eke-Eke (SMS Plus GX)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 ******************************************************************************/
/*
 * See git commit history for more information.
 * - Gameblabla
 * March 15th 2019 : Minor changes.
 * March 14th 2019 : Use fwrite instead of fputc. Also fix an issue with fread which could cause issues.
 * March 13th 2019 : Bring it back again due to regressions. Also i parsed some random shit by accident so i fixed it again.
 * March 9th 2019 : Decomment CrabZ80 related code and removing extra externs.
 * March 7th 2019 : Comment out CrabZ80 related code.
 * Feb 2nd 2019 : Sound function names were changed, fix accordingly.
*/

#include "shared.h"
#include "miniz.h"


#define STATE2_MAGIC "SPGXST2"
#define STATE2_MAGIC_LEN 8
#define STATE2_FLAG_COMPRESSED 0x00000001u
#define STATE2_EXTRA_MAGIC 0x31585453u /* STX1, little-endian. */
#define STATE_RAW_CHUNK_MAGIC "SPGXRAW1"
#define STATE_RAW_CHUNK_MAGIC_LEN 8
#define STATE_RAW_CHUNK_SNK 0x314b4e53u /* SNK1, little-endian. */

typedef struct
{
    char magic[STATE2_MAGIC_LEN];
    uint32_t header_size;
    uint32_t version;
    uint32_t flags;
    uint32_t raw_size;
    uint32_t payload_size;
    uint32_t extra_size;
    uint32_t thumbnail_format;
    uint32_t thumbnail_width;
    uint32_t thumbnail_height;
    uint32_t thumbnail_pitch;
    uint32_t thumbnail_size;
    uint32_t metadata_size;
    uint32_t reserved[8];
} state2_header_t;

typedef struct
{
    uint32_t magic;
    uint32_t size;
    uint32_t console;
    uint32_t mapper;
    uint32_t coleco_megacart_bankcount;
    uint32_t coleco_megacart_activebank;
    uint32_t cart_fcr;
    uint32_t reserved[8];
} state2_extra_v1_t;

typedef struct
{
    char magic[STATE_RAW_CHUNK_MAGIC_LEN];
    uint32_t id;
    uint32_t size;
} state_raw_chunk_header_t;


static int state_write_raw_chunk(FILE *fd, uint32_t id, uint32_t size, int (*writer)(FILE *))
{
    state_raw_chunk_header_t chunk;
    if (!fd || !writer || !size)
        return 0;
    memset(&chunk, 0, sizeof(chunk));
    memcpy(chunk.magic, STATE_RAW_CHUNK_MAGIC, STATE_RAW_CHUNK_MAGIC_LEN);
    chunk.id = id;
    chunk.size = size;
    if (fwrite(&chunk, 1, sizeof(chunk), fd) != sizeof(chunk))
        return 0;
    return writer(fd);
}

static void state_load_raw_chunks(FILE *fd)
{
    state_raw_chunk_header_t chunk;

    if (!fd)
        return;

    while (fread(&chunk, 1, sizeof(chunk), fd) == sizeof(chunk))
    {
        long payload_pos;
        if (memcmp(chunk.magic, STATE_RAW_CHUNK_MAGIC, STATE_RAW_CHUNK_MAGIC_LEN) != 0 ||
            chunk.size > 0x1000000u)
            break;

        payload_pos = ftell(fd);
        if (payload_pos < 0)
            break;

        if (chunk.id == STATE_RAW_CHUNK_SNK && sms.console == CONSOLE_SNKPSYCHOS)
            (void)snk_psychos_load_state(fd, chunk.size);

        if (fseek(fd, payload_pos + (long)chunk.size, SEEK_SET) != 0)
            break;
    }
}

uint32_t system_save_state(FILE* fd)
{
    /* Save VDP context */
    fwrite(&vdp, sizeof(vdp_t), sizeof(int8_t), fd);

    /* Save SMS context */
    fwrite(&sms, sizeof(sms_t), sizeof(int8_t), fd);

    fwrite(cart.fcr, 4, sizeof(int8_t), fd);

    fwrite(cart.sram, 0x8000, sizeof(int8_t), fd);

    /* Save Z80 context */
    fwrite(Z80_Context, sizeof(z80_t), sizeof(int8_t), fd);

    /* Save YM2413 context */
    fwrite(FM_GetContextPtr(), FM_GetContextSize(), sizeof(int8_t), fd);
    /* Save SN76489 context */
    fwrite(&PSG, sizeof(sn76489_t), sizeof(int8_t), fd);

    if (sms.console == CONSOLE_SNKPSYCHOS)
        state_write_raw_chunk(fd, STATE_RAW_CHUNK_SNK, snk_psychos_state_size(), snk_psychos_save_state);

    return 0;
}

void system_load_state(FILE* fd)
{
    uint8_t *buf;

    /* Initialize everything */
    system_reset();

    /* Load VDP context */
    fread(&vdp, sizeof(vdp_t), sizeof(int8_t), fd);

    /* Load SMS context */
    fread(&sms, sizeof(sms_t), sizeof(int8_t), fd);

    /** restore video & audio settings (needed if timing changed) ***/
    vdp_init();
    if (((sms.console == CONSOLE_SMS) || (sms.console == CONSOLE_SMS2)) && vdp.cram_top_valid)
    {
        vdp.hscroll_top_next_armed |= 0x80;
    }
    MULTIREXZ80_sound_init();

    fread(cart.fcr, 4, sizeof(int8_t), fd);

    fread(cart.sram, 0x8000, sizeof(int8_t), fd);

    /* Load Z80 context */
    fread(Z80_Context, sizeof(z80_t), sizeof(int8_t), fd);
    Z80.irq_callback = sms_irq_callback;

    /* Load YM2413 context */
    buf = malloc(FM_GetContextSize());
    fread(buf, FM_GetContextSize(), sizeof(int8_t), fd);
    FM_SetContext(buf);
    free(buf);
    /* Load SN76489 context */
    buf = malloc(sizeof(sn76489_t));
    fread(buf, sizeof(sn76489_t), sizeof(int8_t), fd);
    memcpy(&PSG, buf, sizeof(sn76489_t));
    free(buf);

    mapper_restore_state();

    /* Force full pattern cache update */
    bg_list_index = 0x200;
    for(uint16_t i = 0; i < 0x200; i++)
    {
        bg_name_list[i] = i;
        bg_name_dirty[i] = 255;
    }

    /* Restore palette */
    for(int32_t i = 0; i < PALETTE_SIZE; i++)
        palette_sync(i);

    state_load_raw_chunks(fd);
}

static int read_file_tail(FILE *fd, uint8_t **out_data, uint32_t *out_size)
{
    long size;
    uint8_t *data;

    if (fseek(fd, 0, SEEK_END) != 0)
        return 0;
    size = ftell(fd);
    if (size <= 0 || size > 0x1000000)
        return 0;
    if (fseek(fd, 0, SEEK_SET) != 0)
        return 0;

    data = (uint8_t *)malloc((size_t)size);
    if (!data)
        return 0;
    if (fread(data, 1, (size_t)size, fd) != (size_t)size)
    {
        free(data);
        return 0;
    }

    *out_data = data;
    *out_size = (uint32_t)size;
    return 1;
}

int system_save_state_buffer(uint8_t **out_data, uint32_t *out_size)
{
    FILE *fd;
    uint8_t *data = NULL;
    uint32_t size = 0;

    if (!out_data || !out_size)
        return 0;
    *out_data = NULL;
    *out_size = 0;

    fd = tmpfile();
    if (!fd)
        return 0;

    system_save_state(fd);
    if (!read_file_tail(fd, &data, &size))
    {
        fclose(fd);
        return 0;
    }

    fclose(fd);
    *out_data = data;
    *out_size = size;
    return 1;
}

int system_load_state_buffer(const uint8_t *data, uint32_t size)
{
    FILE *fd;

    if (!data || !size)
        return 0;

    fd = tmpfile();
    if (!fd)
        return 0;
    if (fwrite(data, 1, size, fd) != size)
    {
        fclose(fd);
        return 0;
    }
    rewind(fd);
    system_load_state(fd);
    fclose(fd);
    return 1;
}

void system_free_state_buffer(void *data)
{
    free(data);
}

static void build_state_extra(state2_extra_v1_t *extra)
{
    memset(extra, 0, sizeof(*extra));
    extra->magic = STATE2_EXTRA_MAGIC;
    extra->size = (uint32_t)sizeof(*extra);
    extra->console = sms.console;
    extra->mapper = cart.mapper;
    extra->coleco_megacart_bankcount = slot.coleco_megacart_bankcount;
    extra->coleco_megacart_activebank = slot.coleco_megacart_activebank;
    extra->cart_fcr = (uint32_t)cart.fcr[0] | ((uint32_t)cart.fcr[1] << 8) |
                      ((uint32_t)cart.fcr[2] << 16) | ((uint32_t)cart.fcr[3] << 24);
}

static void apply_state_extra(const uint8_t *data, uint32_t size)
{
    const state2_extra_v1_t *extra;

    if (!data || size < sizeof(state2_extra_v1_t))
        return;
    extra = (const state2_extra_v1_t *)data;
    if (extra->magic != STATE2_EXTRA_MAGIC || extra->size < sizeof(state2_extra_v1_t))
        return;

    if (cart.mapper == MAPPER_COLECO_MEGACART)
    {
        slot.coleco_megacart_bankcount = (uint16_t)extra->coleco_megacart_bankcount;
        slot.coleco_megacart_activebank = (uint16_t)extra->coleco_megacart_activebank;
        mapper_restore_state();
    }
}

static int __attribute__((unused)) system_save_state_state2_legacy(const char *path,
                              const void *thumbnail_xrgb8888,
                              uint32_t thumbnail_width,
                              uint32_t thumbnail_height,
                              uint32_t thumbnail_pitch)
{
    FILE *fd;
    uint8_t *raw = NULL;
    uint32_t raw_size = 0;
    uint8_t *payload = NULL;
    uint32_t payload_size = 0;
    mz_ulong bound, compressed_size;
    state2_header_t header;
    state2_extra_v1_t extra;
    char metadata[256];
    uint32_t thumb_size = 0;
    int compressed = 0;

    if (!path)
        return 0;
    if (!system_save_state_buffer(&raw, &raw_size))
        return 0;

    bound = mz_compressBound(raw_size);
    payload = (uint8_t *)malloc((size_t)bound);
    if (payload)
    {
        compressed_size = bound;
        if (mz_compress2(payload, &compressed_size, raw, raw_size, MZ_BEST_SPEED) == MZ_OK)
        {
            payload_size = (uint32_t)compressed_size;
            compressed = 1;
        }
        else
        {
            free(payload);
            payload = NULL;
        }
    }

    if (!payload)
    {
        payload = raw;
        payload_size = raw_size;
        raw = NULL;
    }

    build_state_extra(&extra);
    snprintf(metadata, sizeof(metadata),
             "core=MultiRexZ80\nstate_format=2\nlegacy_payload_version=%04X\nrom_crc=%08X\nconsole=%u\nmapper=%u\n",
             STATE_VERSION, cart.crc, sms.console, cart.mapper);

    if (thumbnail_xrgb8888 && thumbnail_width && thumbnail_height && thumbnail_pitch >= thumbnail_width * 4)
        thumb_size = thumbnail_pitch * thumbnail_height;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, STATE2_MAGIC, STATE2_MAGIC_LEN);
    header.header_size = (uint32_t)sizeof(header);
    header.version = STATE2_VERSION;
    header.flags = compressed ? STATE2_FLAG_COMPRESSED : 0;
    header.raw_size = raw_size;
    header.payload_size = payload_size;
    header.extra_size = (uint32_t)sizeof(extra);
    header.thumbnail_format = thumb_size ? STATE2_THUMB_XRGB8888 : 0;
    header.thumbnail_width = thumb_size ? thumbnail_width : 0;
    header.thumbnail_height = thumb_size ? thumbnail_height : 0;
    header.thumbnail_pitch = thumb_size ? thumbnail_pitch : 0;
    header.thumbnail_size = thumb_size;
    header.metadata_size = (uint32_t)strlen(metadata) + 1;

    fd = fopen(path, "wb");
    if (!fd)
    {
        free(raw);
        free(payload);
        return 0;
    }

    if (fwrite(&header, 1, sizeof(header), fd) != sizeof(header) ||
        fwrite(metadata, 1, header.metadata_size, fd) != header.metadata_size ||
        fwrite(&extra, 1, sizeof(extra), fd) != sizeof(extra) ||
        (thumb_size && fwrite(thumbnail_xrgb8888, 1, thumb_size, fd) != thumb_size) ||
        fwrite(payload, 1, payload_size, fd) != payload_size)
    {
        fclose(fd);
        free(raw);
        free(payload);
        return 0;
    }

    fclose(fd);
    free(raw);
    free(payload);
    return 1;
}


#define PNG_STATE_MAGIC "SMSPGPNG"
#define PNG_STATE_VERSION 1u

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int write_png_chunk(FILE *fd, const char type[4], const void *data, uint32_t size)
{
    uint8_t len_be[4];
    uint32_t crc;
    put_be32(len_be, size);
    crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)type, 4);
    if (data && size) crc = (uint32_t)mz_crc32(crc, (const unsigned char *)data, size);
    if (fwrite(len_be, 1, 4, fd) != 4 || fwrite(type, 1, 4, fd) != 4) return 0;
    if (size && fwrite(data, 1, size, fd) != size) return 0;
    put_be32(len_be, crc);
    return fwrite(len_be, 1, 4, fd) == 4;
}

int system_save_state_png(const char *path,
                          const void *thumbnail_xrgb8888,
                          uint32_t thumbnail_width,
                          uint32_t thumbnail_height,
                          uint32_t thumbnail_pitch)
{
    FILE *fd = NULL;
    uint8_t *raw = NULL, *payload = NULL, *state_chunk = NULL, *image = NULL, *idat = NULL;
    uint32_t raw_size = 0, payload_size = 0, image_size = 0, idat_size = 0;
    mz_ulong bound, zsize;
    state2_extra_v1_t extra;
    uint8_t ihdr[13];
    uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    int ok = 0;
    uint32_t x, y;

    if (!path || !system_save_state_buffer(&raw, &raw_size)) return 0;
    if (!thumbnail_xrgb8888 || !thumbnail_width || !thumbnail_height || thumbnail_pitch < thumbnail_width * 4)
    {
        thumbnail_width = 160;
        thumbnail_height = 120;
        thumbnail_pitch = thumbnail_width * 4;
        image = (uint8_t *)calloc((size_t)thumbnail_height, 1 + (size_t)thumbnail_width * 3);
    }
    else
    {
        image = (uint8_t *)malloc((size_t)thumbnail_height * (1 + (size_t)thumbnail_width * 3));
    }
    if (!image) goto done;

    for (y = 0; y < thumbnail_height; y++)
    {
        uint8_t *dst = image + (size_t)y * (1 + (size_t)thumbnail_width * 3);
        const uint8_t *src = thumbnail_xrgb8888 ? ((const uint8_t *)thumbnail_xrgb8888 + (size_t)y * thumbnail_pitch) : NULL;
        dst[0] = 0; /* PNG filter: None */
        dst++;
        for (x = 0; x < thumbnail_width; x++)
        {
            uint32_t p = src ? ((const uint32_t *)src)[x] : 0xff000000u;
            dst[x * 3 + 0] = (uint8_t)(p >> 16);
            dst[x * 3 + 1] = (uint8_t)(p >> 8);
            dst[x * 3 + 2] = (uint8_t)p;
        }
    }
    image_size = thumbnail_height * (1 + thumbnail_width * 3);
    bound = mz_compressBound(image_size);
    idat = (uint8_t *)malloc((size_t)bound);
    if (!idat) goto done;
    zsize = bound;
    if (mz_compress2(idat, &zsize, image, image_size, MZ_BEST_SPEED) != MZ_OK) goto done;
    idat_size = (uint32_t)zsize;

    bound = mz_compressBound(raw_size);
    payload = (uint8_t *)malloc((size_t)bound);
    if (!payload) goto done;
    zsize = bound;
    if (mz_compress2(payload, &zsize, raw, raw_size, MZ_BEST_SPEED) != MZ_OK) goto done;
    payload_size = (uint32_t)zsize;

    build_state_extra(&extra);
    state_chunk = (uint8_t *)malloc(32 + sizeof(extra) + payload_size);
    if (!state_chunk) goto done;
    memcpy(state_chunk + 0, PNG_STATE_MAGIC, 8);
    put_be32(state_chunk + 8, PNG_STATE_VERSION);
    put_be32(state_chunk + 12, raw_size);
    put_be32(state_chunk + 16, payload_size);
    put_be32(state_chunk + 20, (uint32_t)sizeof(extra));
    put_be32(state_chunk + 24, cart.crc);
    put_be32(state_chunk + 28, sms.console);
    memcpy(state_chunk + 32, &extra, sizeof(extra));
    memcpy(state_chunk + 32 + sizeof(extra), payload, payload_size);

    memset(ihdr, 0, sizeof(ihdr));
    put_be32(ihdr + 0, thumbnail_width);
    put_be32(ihdr + 4, thumbnail_height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* RGB */

    fd = fopen(path, "wb");
    if (!fd) goto done;
    if (fwrite(sig, 1, sizeof(sig), fd) != sizeof(sig)) goto done;
    if (!write_png_chunk(fd, "IHDR", ihdr, sizeof(ihdr))) goto done;
    if (!write_png_chunk(fd, "IDAT", idat, idat_size)) goto done;
    /* Keep the emulator state in a private ancillary chunk after IDAT.
     * This keeps the file as a normal previewable PNG first, with the save
     * payload carried like PICO-8-style PNG cartridges rather than replacing
     * the visible image. */
    if (!write_png_chunk(fd, "stAt", state_chunk, 32 + (uint32_t)sizeof(extra) + payload_size)) goto done;
    if (!write_png_chunk(fd, "IEND", NULL, 0)) goto done;
    ok = 1;

done:
    if (fd) fclose(fd);
    free(raw); free(payload); free(state_chunk); free(image); free(idat);
    return ok;
}

static int png_try_load_state(FILE *fd)
{
    uint8_t sig[8], lenbuf[4], type[4];
    int ok = 0;
    if (fread(sig, 1, 8, fd) != 8 || memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) return 0;
    while (fread(lenbuf, 1, 4, fd) == 4 && fread(type, 1, 4, fd) == 4)
    {
        uint32_t len = get_be32(lenbuf);
        long data_pos = ftell(fd);
        if (len > 0x2000000u) return 0;
        if (memcmp(type, "stAt", 4) == 0)
        {
            uint8_t *chunk = (uint8_t *)malloc(len);
            uint8_t *payload, *raw;
            mz_ulong raw_len;
            uint32_t raw_size, payload_size, extra_size;
            if (!chunk) return 0;
            if (fread(chunk, 1, len, fd) != len) { free(chunk); return 0; }
            if (len >= 32 && memcmp(chunk, PNG_STATE_MAGIC, 8) == 0 && get_be32(chunk + 8) <= PNG_STATE_VERSION)
            {
                raw_size = get_be32(chunk + 12);
                payload_size = get_be32(chunk + 16);
                extra_size = get_be32(chunk + 20);
                if (raw_size && payload_size && raw_size <= 0x1000000u && payload_size <= len && 32u + extra_size + payload_size <= len)
                {
                    payload = chunk + 32 + extra_size;
                    raw = (uint8_t *)malloc(raw_size);
                    if (raw)
                    {
                        raw_len = raw_size;
                        if (mz_uncompress(raw, &raw_len, payload, payload_size) == MZ_OK && raw_len == raw_size)
                        {
                            ok = system_load_state_buffer(raw, raw_size);
                            if (ok) apply_state_extra(chunk + 32, extra_size);
                        }
                        free(raw);
                    }
                }
            }
            free(chunk);
            return ok;
        }
        if (fseek(fd, data_pos + (long)len + 4, SEEK_SET) != 0) return 0; /* skip data + CRC */
        if (memcmp(type, "IEND", 4) == 0) break;
    }
    return 0;
}

int system_state_png_read_thumbnail(const char *path,
                                    uint32_t **out_xrgb8888,
                                    uint32_t *out_width,
                                    uint32_t *out_height)
{
    FILE *fd;
    uint8_t sig[8], lenbuf[4], type[4], ihdr[13];
    uint8_t *idat = NULL, *new_idat, *raw = NULL;
    uint32_t idat_size = 0, width = 0, height = 0;
    int ok = 0;
    if (!path || !out_xrgb8888 || !out_width || !out_height) return 0;
    *out_xrgb8888 = NULL; *out_width = 0; *out_height = 0;
    fd = fopen(path, "rb");
    if (!fd) return 0;
    if (fread(sig, 1, 8, fd) != 8 || memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) goto done;
    while (fread(lenbuf, 1, 4, fd) == 4 && fread(type, 1, 4, fd) == 4)
    {
        uint32_t len = get_be32(lenbuf);
        if (len > 0x2000000u) goto done;
        if (memcmp(type, "IHDR", 4) == 0 && len == 13)
        {
            if (fread(ihdr, 1, 13, fd) != 13) goto done;
            width = get_be32(ihdr); height = get_be32(ihdr + 4);
            if (ihdr[8] != 8 || ihdr[9] != 2 || width == 0 || height == 0 || width > 2048 || height > 2048) goto done;
            if (fseek(fd, 4, SEEK_CUR) != 0) goto done;
            continue;
        }
        if (memcmp(type, "IDAT", 4) == 0)
        {
            new_idat = (uint8_t *)realloc(idat, idat_size + len);
            if (!new_idat) goto done;
            idat = new_idat;
            if (fread(idat + idat_size, 1, len, fd) != len) goto done;
            idat_size += len;
            if (fseek(fd, 4, SEEK_CUR) != 0) goto done;
            continue;
        }
        if (fseek(fd, (long)len + 4, SEEK_CUR) != 0) goto done;
        if (memcmp(type, "IEND", 4) == 0) break;
    }
    if (width && height && idat_size)
    {
        mz_ulong raw_size = (mz_ulong)height * (1 + (mz_ulong)width * 3);
        uint32_t *pix;
        uint32_t x, y;
        raw = (uint8_t *)malloc((size_t)raw_size);
        pix = (uint32_t *)malloc((size_t)width * height * 4);
        if (!raw || !pix) { free(pix); goto done; }
        if (mz_uncompress(raw, &raw_size, idat, idat_size) != MZ_OK) { free(pix); goto done; }
        for (y = 0; y < height; y++)
        {
            const uint8_t *row = raw + (size_t)y * (1 + (size_t)width * 3);
            if (row[0] != 0) { free(pix); goto done; }
            row++;
            for (x = 0; x < width; x++) pix[(size_t)y * width + x] = 0xff000000u | ((uint32_t)row[x*3+0] << 16) | ((uint32_t)row[x*3+1] << 8) | row[x*3+2];
        }
        *out_xrgb8888 = pix; *out_width = width; *out_height = height; ok = 1;
    }
done:
    fclose(fd); free(idat); free(raw); return ok;
}

int system_load_state_file(const char *path)
{
    FILE *fd;
    state2_header_t header;
    uint8_t *metadata = NULL;
    uint8_t *extra = NULL;
    uint8_t *thumb = NULL;
    uint8_t *payload = NULL;
    uint8_t *raw = NULL;
    int ok = 0;

    if (!path)
        return 0;

    fd = fopen(path, "rb");
    if (!fd)
        return 0;

    if (fread(&header, 1, sizeof(header), fd) != sizeof(header) ||
        memcmp(header.magic, STATE2_MAGIC, STATE2_MAGIC_LEN) != 0)
    {
        rewind(fd);
        if (png_try_load_state(fd))
        {
            fclose(fd);
            return 1;
        }
        rewind(fd);
        system_load_state(fd);
        fclose(fd);
        return 1;
    }

    if (header.header_size != sizeof(header) || header.version > STATE2_VERSION ||
        header.raw_size == 0 || header.payload_size == 0 ||
        header.raw_size > 0x1000000 || header.payload_size > 0x1000000 ||
        header.metadata_size > 0x10000 || header.extra_size > 0x10000 ||
        header.thumbnail_size > 0x400000)
    {
        fclose(fd);
        return 0;
    }

    if (header.metadata_size)
    {
        metadata = (uint8_t *)malloc(header.metadata_size);
        if (!metadata || fread(metadata, 1, header.metadata_size, fd) != header.metadata_size) goto done;
    }
    if (header.extra_size)
    {
        extra = (uint8_t *)malloc(header.extra_size);
        if (!extra || fread(extra, 1, header.extra_size, fd) != header.extra_size) goto done;
    }
    if (header.thumbnail_size)
    {
        thumb = (uint8_t *)malloc(header.thumbnail_size);
        if (!thumb || fread(thumb, 1, header.thumbnail_size, fd) != header.thumbnail_size) goto done;
    }

    payload = (uint8_t *)malloc(header.payload_size);
    if (!payload || fread(payload, 1, header.payload_size, fd) != header.payload_size) goto done;

    if (header.flags & STATE2_FLAG_COMPRESSED)
    {
        mz_ulong raw_len = header.raw_size;
        raw = (uint8_t *)malloc(header.raw_size);
        if (!raw) goto done;
        if (mz_uncompress(raw, &raw_len, payload, header.payload_size) != MZ_OK || raw_len != header.raw_size)
            goto done;
    }
    else
    {
        raw = payload;
        payload = NULL;
    }

    if (!system_load_state_buffer(raw, header.raw_size)) goto done;
    apply_state_extra(extra, header.extra_size);
    ok = 1;

done:
    fclose(fd);
    free(metadata);
    free(extra);
    free(thumb);
    free(payload);
    free(raw);
    return ok;
}
