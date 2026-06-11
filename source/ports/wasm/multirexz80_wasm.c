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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "shared.h"

#define WASM_BITMAP_WIDTH  400
#define WASM_BITMAP_HEIGHT 400

#if defined(__clang__)
#define WASM_EXPORT __attribute__((visibility("default")))
#else
#define WASM_EXPORT
#endif

t_config option;

static uint32_t wasm_pixels[WASM_BITMAP_WIDTH * WASM_BITMAP_HEIGHT];
static uint8_t wasm_sram[0x8000];
static uint8_t wasm_powered;

static void wasm_defaults(void)
{
    memset(&option, 0, sizeof(option));
    option.fullspeed = 1;
    option.fm = 1;
    option.spritelimit = 1;
    option.soundlevel = 1;
    option.use_bios = 0;
    option.lcd_persistence = 1;
    option.lightgun_cursor = 1;
    option.lightgun_dpad_speed = 3;
}

static void wasm_init_bitmap(void)
{
    memset(wasm_pixels, 0, sizeof(wasm_pixels));
    bitmap.width = WASM_BITMAP_WIDTH;
    bitmap.height = WASM_BITMAP_HEIGHT;
    bitmap.depth = 32;
    bitmap.data = (uint8_t *)(void *)wasm_pixels;
    bitmap.pitch = WASM_BITMAP_WIDTH * 4;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.w = 256;
    bitmap.viewport.h = 192;
    bitmap.viewport.changed = 1;
}

WASM_EXPORT uint32_t wasm_core_init(void)
{
    wasm_defaults();
    wasm_init_bitmap();
    return 1;
}

WASM_EXPORT void *wasm_alloc(uint32_t size)
{
    return malloc(size);
}

WASM_EXPORT void wasm_release(void *ptr)
{
    free(ptr);
}

WASM_EXPORT uint32_t wasm_load_rom(const uint8_t *data, uint32_t size)
{
    if (wasm_powered)
    {
        system_poweroff();
        wasm_powered = 0;
    }
    if (!load_rom_buffer(data, size))
        return 0;
    wasm_init_bitmap();
    system_poweron();
    wasm_powered = 1;
    return 1;
}

WASM_EXPORT void wasm_reset(void)
{
    if (wasm_powered)
        system_reset();
}

WASM_EXPORT void wasm_frame(void)
{
    if (wasm_powered)
        system_frame(0);
}

WASM_EXPORT uint32_t wasm_framebuffer(void)
{
    return (uint32_t)(uintptr_t)wasm_pixels;
}

WASM_EXPORT uint32_t wasm_pitch(void) { return bitmap.pitch; }
WASM_EXPORT uint32_t wasm_width(void) { return bitmap.width; }
WASM_EXPORT uint32_t wasm_height(void) { return bitmap.height; }
WASM_EXPORT uint32_t wasm_view_x(void) { return (uint32_t)((bitmap.viewport.x < 0) ? 0 : bitmap.viewport.x); }
WASM_EXPORT uint32_t wasm_view_y(void) { return (uint32_t)((bitmap.viewport.y < 0) ? 0 : bitmap.viewport.y); }
WASM_EXPORT uint32_t wasm_view_w(void) { return (uint32_t)((bitmap.viewport.w > 0) ? bitmap.viewport.w : 256); }
WASM_EXPORT uint32_t wasm_view_h(void) { return (uint32_t)((bitmap.viewport.h > 0) ? bitmap.viewport.h : vdp.height); }
WASM_EXPORT uint32_t wasm_crc(void) { return cart.crc; }
WASM_EXPORT uint32_t wasm_console(void) { return sms.console; }

WASM_EXPORT void wasm_set_pad(uint32_t port, uint32_t mask)
{
    if (port < 2)
        input.pad[port] = (uint8_t)mask;
}

WASM_EXPORT void wasm_set_system(uint32_t mask)
{
    input.system = (uint8_t)mask;
}

WASM_EXPORT void wasm_set_arcade(uint32_t mask)
{
    input.arcade = (uint8_t)mask;
}

WASM_EXPORT void wasm_set_analog(uint32_t port, int32_t x, int32_t y)
{
    if (port < 2)
    {
        input.analog[port][0] = x;
        input.analog[port][1] = y;
    }
}

WASM_EXPORT uint32_t wasm_sram_ptr(void) { return (uint32_t)(uintptr_t)wasm_sram; }
WASM_EXPORT uint32_t wasm_sram_size(void) { return sizeof(wasm_sram); }

void smsp_state(uint8_t slot_number, uint8_t mode)
{
    (void)slot_number;
    (void)mode;
}

void system_manage_sram(uint8_t *sram, uint8_t slot_number, uint8_t mode)
{
    (void)slot_number;
    if (!sram) return;
    if (mode == SRAM_LOAD)
        memcpy(sram, wasm_sram, sizeof(wasm_sram));
    else if (mode == SRAM_SAVE)
        memcpy(wasm_sram, sram, sizeof(wasm_sram));
}
