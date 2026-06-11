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

#ifndef MULTIREXZ80_SDL12_COMMON_H_
#define MULTIREXZ80_SDL12_COMMON_H_

#include <stdint.h>
#include <SDL/SDL.h>

/*
 * Shared SDL 1.2 input helpers for the legacy SDL/handheld backends.
 * The individual ports keep their menu/video setup, but normal game input,
 * arcade coin/start/service/test handling, Coleco keypad mapping, and the
 * System 1 dial fallback live here so fixes do not have to be copied across
 * a dozen frontends.
 */
typedef struct multirexz80_sdl12_keymap_t
{
    SDLKey up;
    SDLKey down;
    SDLKey left;
    SDLKey right;
    SDLKey button1;     /* core INPUT_BUTTON1 */
    SDLKey button2;     /* core INPUT_BUTTON2 */
    SDLKey rotate_left; /* SNK LS-30 optional rotate */
    SDLKey rotate_right;
    SDLKey aim_up;     /* SNK LS-30 absolute aim, independent from movement */
    SDLKey aim_down;
    SDLKey aim_left;
    SDLKey aim_right;
    SDLKey start;
    SDLKey select;
    SDLKey arcade_coin1;
    SDLKey arcade_coin2;
    SDLKey arcade_start1;
    SDLKey arcade_start2;
    SDLKey arcade_service;
    SDLKey arcade_test;
    SDLKey virtual_keyboard;
    SDLKey keypad[12];  /* 0..9, *, #/$-style aliases */
} multirexz80_sdl12_keymap_t;

typedef struct multirexz80_sdl12_keyboard_key_t
{
    const char *label;
    uint8_t row;
    uint8_t mask;
    SDLKey key;
} multirexz80_sdl12_keyboard_key_t;

void multirexz80_sdl12_keymap_defaults(multirexz80_sdl12_keymap_t *map);
int multirexz80_sdl12_arcade_active(void);
int multirexz80_sdl12_keyboard_active(void);
uint32_t multirexz80_sdl12_update_key(SDLKey key, int32_t pressed,
                                  const multirexz80_sdl12_keymap_t *map,
                                  uint8_t *select_pressed);
void multirexz80_sdl12_update_arcade_from_key_state(const uint8_t *keys);
void multirexz80_sdl12_update_arcade_from_key_state_mapped(const uint8_t *keys,
                                  const multirexz80_sdl12_keymap_t *map);
void multirexz80_sdl12_frame_update(void);
void multirexz80_sdl12_set_arcade_button(uint8_t mask, int32_t pressed);
int multirexz80_sdl12_keyboard_key_count(void);
const multirexz80_sdl12_keyboard_key_t *multirexz80_sdl12_keyboard_key(int index);
void multirexz80_sdl12_keyboard_set_key(int index, int32_t pressed);
int multirexz80_sdl12_keyboard_from_sdl_key(SDLKey key, int32_t pressed);

/* Active source rectangle for the current emulated display.  Legacy SDL 1.2
 * ports used hard-coded 256xvdp.height SMS assumptions; arcade boards expose
 * their exact active raster through bitmap.viewport and can be wider/taller. */
typedef struct multirexz80_sdl12_view_t
{
    int x;
    int y;
    int w;
    int h;
    int pitch_pixels;
    int bytes_per_pixel;
} multirexz80_sdl12_view_t;

void multirexz80_sdl12_get_active_view(multirexz80_sdl12_view_t *view);
void multirexz80_sdl12_fit_rect(SDL_Rect *dst, int dst_w, int dst_h, int src_w, int src_h);
int multirexz80_sdl12_bitmap_width(void);
int multirexz80_sdl12_bitmap_height(void);
int multirexz80_sdl12_surface_pitch_pixels(const SDL_Surface *surface);
SDL_Surface *multirexz80_sdl12_create_rgb565_surface(int width, int height);


void multirexz80_sdl12_state_file(const char *stdir, const char *gamename, uint8_t slot_number, uint8_t mode);
void multirexz80_sdl12_sram_file(const char *sramfile, uint8_t *sram, uint8_t mode);

static inline void multirexz80_sdl12_keymap_from_config(multirexz80_sdl12_keymap_t *map,
                                                    const uint32_t *config_buttons)
{
    multirexz80_sdl12_keymap_defaults(map);
    if (!config_buttons) return;
#ifdef CONFIG_BUTTON_UP
    if (config_buttons[CONFIG_BUTTON_UP]) map->up = (SDLKey)config_buttons[CONFIG_BUTTON_UP];
#endif
#ifdef CONFIG_BUTTON_DOWN
    if (config_buttons[CONFIG_BUTTON_DOWN]) map->down = (SDLKey)config_buttons[CONFIG_BUTTON_DOWN];
#endif
#ifdef CONFIG_BUTTON_LEFT
    if (config_buttons[CONFIG_BUTTON_LEFT]) map->left = (SDLKey)config_buttons[CONFIG_BUTTON_LEFT];
#endif
#ifdef CONFIG_BUTTON_RIGHT
    if (config_buttons[CONFIG_BUTTON_RIGHT]) map->right = (SDLKey)config_buttons[CONFIG_BUTTON_RIGHT];
#endif
#ifdef CONFIG_BUTTON_BUTTON1
    /* Legacy remappers named this the physical A button; existing ports map it to SMS button 2. */
    if (config_buttons[CONFIG_BUTTON_BUTTON1]) map->button2 = (SDLKey)config_buttons[CONFIG_BUTTON_BUTTON1];
#endif
#ifdef CONFIG_BUTTON_BUTTON2
    /* Legacy remappers named this the physical B button; existing ports map it to SMS button 1. */
    if (config_buttons[CONFIG_BUTTON_BUTTON2]) map->button1 = (SDLKey)config_buttons[CONFIG_BUTTON_BUTTON2];
#endif
#ifdef CONFIG_BUTTON_START
    if (config_buttons[CONFIG_BUTTON_START]) map->start = (SDLKey)config_buttons[CONFIG_BUTTON_START];
#endif
#ifdef CONFIG_BUTTON_ARCADE_COIN1
    if (config_buttons[CONFIG_BUTTON_ARCADE_COIN1]) map->arcade_coin1 = (SDLKey)config_buttons[CONFIG_BUTTON_ARCADE_COIN1];
#endif
#ifdef CONFIG_BUTTON_ARCADE_COIN2
    if (config_buttons[CONFIG_BUTTON_ARCADE_COIN2]) map->arcade_coin2 = (SDLKey)config_buttons[CONFIG_BUTTON_ARCADE_COIN2];
#endif
#ifdef CONFIG_BUTTON_ARCADE_START1
    if (config_buttons[CONFIG_BUTTON_ARCADE_START1]) map->arcade_start1 = (SDLKey)config_buttons[CONFIG_BUTTON_ARCADE_START1];
#endif
#ifdef CONFIG_BUTTON_ARCADE_START2
    if (config_buttons[CONFIG_BUTTON_ARCADE_START2]) map->arcade_start2 = (SDLKey)config_buttons[CONFIG_BUTTON_ARCADE_START2];
#endif
#ifdef CONFIG_BUTTON_ARCADE_SERVICE
    if (config_buttons[CONFIG_BUTTON_ARCADE_SERVICE]) map->arcade_service = (SDLKey)config_buttons[CONFIG_BUTTON_ARCADE_SERVICE];
#endif
#ifdef CONFIG_BUTTON_ARCADE_TEST
    if (config_buttons[CONFIG_BUTTON_ARCADE_TEST]) map->arcade_test = (SDLKey)config_buttons[CONFIG_BUTTON_ARCADE_TEST];
#endif
#ifdef CONFIG_BUTTON_VKBD
    if (config_buttons[CONFIG_BUTTON_VKBD]) map->virtual_keyboard = (SDLKey)config_buttons[CONFIG_BUTTON_VKBD];
#endif
#ifdef CONFIG_BUTTON_ONE
    if (config_buttons[CONFIG_BUTTON_ONE]) map->keypad[1] = (SDLKey)config_buttons[CONFIG_BUTTON_ONE];
#endif
#ifdef CONFIG_BUTTON_TWO
    if (config_buttons[CONFIG_BUTTON_TWO]) map->keypad[2] = (SDLKey)config_buttons[CONFIG_BUTTON_TWO];
#endif
#ifdef CONFIG_BUTTON_THREE
    if (config_buttons[CONFIG_BUTTON_THREE]) map->keypad[3] = (SDLKey)config_buttons[CONFIG_BUTTON_THREE];
#endif
#ifdef CONFIG_BUTTON_FOUR
    if (config_buttons[CONFIG_BUTTON_FOUR]) map->keypad[4] = (SDLKey)config_buttons[CONFIG_BUTTON_FOUR];
#endif
#ifdef CONFIG_BUTTON_FIVE
    if (config_buttons[CONFIG_BUTTON_FIVE]) map->keypad[5] = (SDLKey)config_buttons[CONFIG_BUTTON_FIVE];
#endif
#ifdef CONFIG_BUTTON_SIX
    if (config_buttons[CONFIG_BUTTON_SIX]) map->keypad[6] = (SDLKey)config_buttons[CONFIG_BUTTON_SIX];
#endif
#ifdef CONFIG_BUTTON_SEVEN
    if (config_buttons[CONFIG_BUTTON_SEVEN]) map->keypad[7] = (SDLKey)config_buttons[CONFIG_BUTTON_SEVEN];
#endif
#ifdef CONFIG_BUTTON_EIGHT
    if (config_buttons[CONFIG_BUTTON_EIGHT]) map->keypad[8] = (SDLKey)config_buttons[CONFIG_BUTTON_EIGHT];
#endif
#ifdef CONFIG_BUTTON_NINE
    if (config_buttons[CONFIG_BUTTON_NINE]) map->keypad[9] = (SDLKey)config_buttons[CONFIG_BUTTON_NINE];
#endif
#ifdef CONFIG_BUTTON_DOLLARS
    if (config_buttons[CONFIG_BUTTON_DOLLARS]) map->keypad[10] = (SDLKey)config_buttons[CONFIG_BUTTON_DOLLARS];
#endif
#ifdef CONFIG_BUTTON_ASTERISK
    if (config_buttons[CONFIG_BUTTON_ASTERISK]) map->keypad[11] = (SDLKey)config_buttons[CONFIG_BUTTON_ASTERISK];
#endif
}

#endif /* MULTIREXZ80_SDL12_COMMON_H_ */
