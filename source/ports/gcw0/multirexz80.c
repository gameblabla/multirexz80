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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libgen.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <SDL/SDL.h>

#include "shared.h"
#include "scaler.h"
#include "multirexz80.h"
#include "sdl12_common.h"
#include "font_drawing.h"
#include "sound_output.h"

#ifndef SDL_TRIPLEBUF
#define SDL_TRIPLEBUF SDL_DOUBLEBUF
#endif

#define SDL_FLAGS SDL_HWSURFACE | SDL_TRIPLEBUF

static SDL_Joystick * sdl_joy[3];
#define DEADZONE_JOYSTICK 8192

static int32_t joy_axis[2] = {0, 0};

static gamedata_t gdata;

t_config option;

static char home_path[128];

static int joy_numb = 1;

static SDL_Surface* sdl_screen, *scale2x_buf;
static SDL_Surface *backbuffer;
extern SDL_Surface *font;
extern SDL_Surface *bigfontred;
extern SDL_Surface *bigfontwhite;
SDL_Surface *sms_bitmap;

static uint8_t selectpressed = 0;
static uint8_t save_slot = 0;
static uint8_t quit = 0;

static const int8_t upscalers_available = 1
#ifdef SCALE2X_UPSCALER
+1
#endif
;

static int width_hold = 256;
static int width_remember = 256;
static int width_remove = 0;
static int remember_res_height;

static uint_fast8_t forcerefresh = 0;
static uint_fast8_t video_clear_pending = 1;
static uint_fast8_t dpad_input[4] = {0, 0, 0, 0};
uint_fast16_t pixels_shifting_remove = 0;
static uint8_t virtual_keyboard_requested = 0;

int update_window_size(int w, int h);
static void Virtual_Keyboard(void);

static void Clear_video()
{
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	#ifdef SDL_TRIPLEBUF
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	#endif
}

static void video_update()
{
	SDL_Rect src, dst;
	multirexz80_sdl12_view_t view;
	int target_w, target_h;
	static int last_mode = -1;
	static int last_src_x = -1;
	static int last_src_y = -1;
	static int last_src_w = -1;
	static int last_src_h = -1;
	int locked = 0;
	int screen_pitch;

	multirexz80_sdl12_get_active_view(&view);
	width_hold = view.w;
	width_remove = view.x;

	/* On GCW0/OpenDingux, requesting a source-sized SDL mode is the
	 * hardware/IPU scaling path.  Native output must keep a 320x240 scanout
	 * and blit the source rectangle 1:1 in the centre. */
	target_w = (option.fullscreen == 1) ? view.w : HOST_WIDTH_RESOLUTION;
	target_h = (option.fullscreen == 1) ? view.h : HOST_HEIGHT_RESOLUTION;
	if (remember_res_height != target_h || width_remember != target_w || forcerefresh == 1)
	{
		remember_res_height = target_h;
		width_remember = target_w;
		if (update_window_size(target_w, target_h))
			return;
		video_clear_pending = 1;
		forcerefresh = 0;
	}

	src.x = (Sint16)view.x;
	src.y = (Sint16)view.y;
	src.w = (Uint16)view.w;
	src.h = (Uint16)view.h;
	if (last_mode != option.fullscreen || last_src_x != src.x || last_src_y != src.y ||
	    last_src_w != src.w || last_src_h != src.h)
	{
		video_clear_pending = 1;
		last_mode = option.fullscreen;
		last_src_x = src.x;
		last_src_y = src.y;
		last_src_w = src.w;
		last_src_h = src.h;
	}

	switch(option.fullscreen)
	{
		/* Native 1:1 inside the fixed 320x240 scanout. */
		case 0:
			if (video_clear_pending)
			{
				SDL_FillRect(sdl_screen, NULL, 0);
				video_clear_pending = 0;
			}
			dst.x = (Sint16)((sdl_screen->w - src.w) / 2);
			dst.y = (Sint16)((sdl_screen->h - src.h) / 2);
			if (dst.x < 0) dst.x = 0;
			if (dst.y < 0) dst.y = 0;
			SDL_BlitSurface(sms_bitmap, &src, sdl_screen, &dst);
			break;

		/* Hardware/IPU: source-sized mode, scaled by OpenDingux SDL. */
		case 1:
			dst.x = 0;
			dst.y = 0;
			SDL_BlitSurface(sms_bitmap, &src, sdl_screen, &dst);
			video_clear_pending = 0;
			break;

		/* Software EPX/Scale2x, then fit to the fixed LCD scanout. */
		case 2:
#ifdef SCALE2X_UPSCALER
			if (view.w * 2 <= scale2x_buf->w && view.h * 2 <= scale2x_buf->h)
			{
				if (video_clear_pending)
				{
					SDL_FillRect(sdl_screen, NULL, 0);
					video_clear_pending = 0;
				}
				scale2x((uint16_t *)sms_bitmap->pixels + view.y * view.pitch_pixels + view.x,
				        (uint16_t *)scale2x_buf->pixels,
				        sms_bitmap->pitch,
				        scale2x_buf->pitch,
				        view.w, view.h);
#ifdef MULTIREXZ80_SCALE2X_TARGET_NATIVE
				dst.x = 0;
				dst.y = 0;
				dst.w = (Uint16)sdl_screen->w;
				dst.h = (Uint16)sdl_screen->h;
#else
				multirexz80_sdl12_fit_rect(&dst, sdl_screen->w, sdl_screen->h, view.w * 2, view.h * 2);
#endif
				if (SDL_MUSTLOCK(sdl_screen))
				{
					if (SDL_LockSurface(sdl_screen) < 0)
						break;
					locked = 1;
				}
				screen_pitch = multirexz80_sdl12_surface_pitch_pixels(sdl_screen);
				if (screen_pitch <= 0) screen_pitch = sdl_screen->w;
				bitmap_scale(0, 0, view.w * 2, view.h * 2, dst.w, dst.h,
				             scale2x_buf->pitch >> 1, screen_pitch - dst.w,
				             (uint16_t * restrict)scale2x_buf->pixels,
				             (uint16_t * restrict)sdl_screen->pixels + dst.x + dst.y * screen_pitch);
				if (locked) { SDL_UnlockSurface(sdl_screen); locked = 0; }
			}
#endif
			break;
	}
	SDL_Flip(sdl_screen);
}

void smsp_state(uint8_t slot_number, uint8_t mode)
{
	multirexz80_sdl12_state_file(gdata.stdir, gdata.gamename, slot_number, mode);
}

void system_manage_sram(uint8_t *sram, uint8_t slot_number, uint8_t mode)
{
	(void)slot_number;
	multirexz80_sdl12_sram_file(gdata.sramfile, sram, mode);
}

static uint32_t sdl_controls_update_input(SDLKey k, int32_t p)
{
	multirexz80_sdl12_keymap_t map;
	multirexz80_sdl12_keymap_from_config(&map, option.config_buttons);
	return multirexz80_sdl12_update_key(k, p, &map, &selectpressed);
}



static void bios_init()
{
	FILE *fd;
	char bios_path[384];
	
	bios.rom = malloc(0x100000);
	bios.enabled = 0;
	
	snprintf(bios_path, sizeof(bios_path), "%s%s", gdata.biosdir, "BIOS.sms");

	fd = fopen(bios_path, "rb");
	if(fd)
	{
		/* Seek to end of file, and get size */
		fseek(fd, 0, SEEK_END);
		uint32_t size = ftell(fd);
		fseek(fd, 0, SEEK_SET);
		if (size < 0x4000) size = 0x4000;
		fread(bios.rom, size, 1, fd);
		bios.enabled = 2;  
		bios.pages = size / 0x4000;
		fclose(fd);
	}

	snprintf(bios_path, sizeof(bios_path), "%s%s", gdata.biosdir, "BIOS.col");
	
	fd = fopen(bios_path, "rb");
	if(fd)
	{
		/* Seek to end of file, and get size */
		fread(coleco.rom, 0x2000, 1, fd);
		fclose(fd);
	}
}


static void smsp_gamedata_set(char *filename) 
{
	// Set paths, create directories
	snprintf(home_path, sizeof(home_path), "%s/.multirexz80/", getenv("HOME"));
	
	if (mkdir(home_path, 0755) && errno != EEXIST) {
		fprintf(stderr, "Failed to create %s: %d\n", home_path, errno);
	}
	
	// Set the game name
	snprintf(gdata.gamename, sizeof(gdata.gamename), "%s", basename(filename));
	
	// Strip the file extension off
	for (unsigned long i = strlen(gdata.gamename) - 1; i > 0; i--) {
		if (gdata.gamename[i] == '.') {
			gdata.gamename[i] = '\0';
			break;
		}
	}
	
	// Set up the sram directory
	snprintf(gdata.sramdir, sizeof(gdata.sramdir), "%ssram/", home_path);
	if (mkdir(gdata.sramdir, 0755) && errno != EEXIST) {
		fprintf(stderr, "Failed to create %s: %d\n", gdata.sramdir, errno);
	}
	
	// Set up the sram file
	snprintf(gdata.sramfile, sizeof(gdata.sramfile), "%s%s.sav", gdata.sramdir, gdata.gamename);
	
	// Set up the state directory
	snprintf(gdata.stdir, sizeof(gdata.stdir), "%sstate/", home_path);
	if (mkdir(gdata.stdir, 0755) && errno != EEXIST) {
		fprintf(stderr, "Failed to create %s: %d\n", gdata.stdir, errno);
	}
	
	// Set up the screenshot directory
	snprintf(gdata.scrdir, sizeof(gdata.scrdir), "%sscreenshots/", home_path);
	if (mkdir(gdata.scrdir, 0755) && errno != EEXIST) {
		fprintf(stderr, "Failed to create %s: %d\n", gdata.scrdir, errno);
	}
	
	// Set up the sram directory
	snprintf(gdata.biosdir, sizeof(gdata.biosdir), "%sbios/", home_path);
	if (mkdir(gdata.biosdir, 0755) && errno != EEXIST) {
		fprintf(stderr, "Failed to create %s: %d\n", gdata.sramdir, errno);
	}
	
}

static const char* Return_Text_Button(uint32_t button)
{
	switch(button)
	{
		/* UP button */
		case SDLK_UP:
			return "DPAD UP";
		/* DOWN button */
		case SDLK_DOWN:
			return "DPAD DOWN";
		/* LEFT button */
		case SDLK_LEFT:
			return "DPAD LEFT";
		/* RIGHT button */
		case SDLK_RIGHT:
			return "DPAD RIGHT";
		/* A button */
		case SDLK_LCTRL:
			return "A button";
		/* B button */
		case SDLK_LALT:
			return "B button";
		/* Y button */
		case SDLK_LSHIFT:
			return "Y button";
		/* X button */
		case SDLK_SPACE:
			return "X button";
		/* L button */
		case SDLK_TAB:
			return "L1";
		/* R button */
		case SDLK_BACKSPACE:
			return "R1";
		case SDLK_PAGEUP:
			return "L2";
		case SDLK_PAGEDOWN:
			return "R2";
		/* Power button */
		case SDLK_HOME:
			return "POWER";
		/* Brightness */
		case 34:
			return "Brightness";
		/* Volume - */
		case 38:
			return "Volume -";
		/* Volume + */
		case 233:
			return "Volume +";
		/* Start */
		case SDLK_RETURN:
			return "Start button";
		case SDLK_0:
			return "0 key";
		case SDLK_1:
			return "1 key";
		case SDLK_2:
			return "2 key";
		case SDLK_3:
			return "3 key";
		case SDLK_4:
			return "4 key";
		case SDLK_5:
			return "5 key";
		case SDLK_6:
			return "6 key";
		case SDLK_7:
			return "7 key";
		case SDLK_8:
			return "8 key";
		case SDLK_9:
			return "9 key";
		case SDLK_F2:
			return "F2 key";
		/* Select */
		case SDLK_ESCAPE:
			return "Select button";
		case 0:
			return "...";
		default:
			return "Unknown";
	}	
}


static const char* Return_Volume(uint32_t vol)
{
	switch(vol)
	{
		case 0:
			return "Mute";
		case 1:
			return "25 %";
		case 2:
			return "50 %";
		case 3:
			return "75 %";
		case 4:
			return "100 %";
		default:
			return "...";
	}	
}

static void Draw_Option(int32_t config_index, int32_t selection, int32_t selection_slot, const char* drawtext, int32_t x, int32_t y)
{
	char text[28];
	snprintf(text, sizeof(text), drawtext, Return_Text_Button(option.config_buttons[config_index]));
	if (selection == selection_slot) print_string(text, TextRed, 0, x, y+2, (uint16_t*)backbuffer->pixels);
	else print_string(text, TextWhite, 0, x, y+2, (uint16_t*)backbuffer->pixels);
}

static int32_t Remap_Option_Count(void)
{
	int32_t count = 7;
	if (sms.console == CONSOLE_COLECO) count += 11;
	else if (multirexz80_sdl12_arcade_active()) count += 6;
	if (multirexz80_sdl12_keyboard_active()) count += 1;
	return count;
}

static int32_t Remap_Config_Index(int32_t selection)
{
	static const int32_t base_indices[] = {
		CONFIG_BUTTON_UP, CONFIG_BUTTON_DOWN, CONFIG_BUTTON_LEFT, CONFIG_BUTTON_RIGHT,
		CONFIG_BUTTON_BUTTON1, CONFIG_BUTTON_BUTTON2, CONFIG_BUTTON_START
	};
	static const int32_t coleco_indices[] = {
		CONFIG_BUTTON_DOLLARS, CONFIG_BUTTON_ASTERISK, CONFIG_BUTTON_ONE, CONFIG_BUTTON_TWO,
		CONFIG_BUTTON_THREE, CONFIG_BUTTON_FOUR, CONFIG_BUTTON_FIVE, CONFIG_BUTTON_SIX,
		CONFIG_BUTTON_SEVEN, CONFIG_BUTTON_EIGHT, CONFIG_BUTTON_NINE
	};
	static const int32_t arcade_indices[] = {
		CONFIG_BUTTON_ARCADE_COIN1, CONFIG_BUTTON_ARCADE_COIN2,
		CONFIG_BUTTON_ARCADE_START1, CONFIG_BUTTON_ARCADE_START2,
		CONFIG_BUTTON_ARCADE_SERVICE, CONFIG_BUTTON_ARCADE_TEST
	};
	selection--;
	if (selection < 0) return -1;
	if (selection < 7) return base_indices[selection];
	selection -= 7;
	if (sms.console == CONSOLE_COLECO)
	{
		if (selection < 11) return coleco_indices[selection];
		selection -= 11;
	}
	else if (multirexz80_sdl12_arcade_active())
	{
		if (selection < 6) return arcade_indices[selection];
		selection -= 6;
	}
	if (multirexz80_sdl12_keyboard_active() && selection == 0) return CONFIG_BUTTON_VKBD;
	return -1;
}

static int32_t Draw_Remap_Option(int32_t slot, const char *text, int32_t currentselection, int32_t x, int32_t y)
{
	int32_t config_index = Remap_Config_Index(slot);
	if (config_index >= 0) Draw_Option(config_index, currentselection, slot, text, x, y);
	return slot + 1;
}

static void Input_Remapping()
{
	SDL_Event Event;
	uint32_t pressed = 0;
	int32_t currentselection = 1;
	int32_t exit_input = 0;
	uint32_t exit_map = 0;
	
	while(!exit_input)
	{
		pressed = 0;
		SDL_FillRect( backbuffer, NULL, 0 );
		
		while (SDL_PollEvent(&Event))
		{
			if (Event.type == SDL_KEYDOWN)
			{
				switch(Event.key.keysym.sym)
				{
					case SDLK_UP:
						currentselection--;
						if (currentselection < 1) currentselection = Remap_Option_Count();
						break;
					case SDLK_DOWN:
						currentselection++;
						if (currentselection > Remap_Option_Count()) currentselection = 1;
						break;
					case SDLK_LCTRL:
					case SDLK_RETURN:
						pressed = 1;
						break;
					case SDLK_LALT:
						exit_input = 1;
						break;
					case SDLK_LEFT:
						if (sms.console == CONSOLE_COLECO && currentselection > 8) currentselection -= 9;
						else if (multirexz80_sdl12_arcade_active() && currentselection > 7) currentselection -= 6;
						break;
					case SDLK_RIGHT:
						if (sms.console == CONSOLE_COLECO && currentselection < 10) currentselection += 9;
						else if (multirexz80_sdl12_arcade_active() && currentselection < 8) currentselection += 6;
						if (currentselection > Remap_Option_Count()) currentselection = Remap_Option_Count();
						break;
					default:
						break;
				}
			}
		}

		if (pressed)
		{
			SDL_FillRect( backbuffer, NULL, 0 );
			print_string("Please press button for mapping", TextWhite, TextBlue, 37, 108, backbuffer->pixels);
			SDL_SoftStretch(backbuffer, NULL, sdl_screen, NULL);
			SDL_Flip(sdl_screen);
			exit_map = 0;
			while( !exit_map )
			{
				while (SDL_PollEvent(&Event))
				{
					if (Event.type == SDL_KEYDOWN)
					{
						if (Event.key.keysym.sym != SDLK_END && Event.key.keysym.sym != SDLK_HOME && Event.key.keysym.sym != SDLK_RCTRL)
						{
							int32_t config_index = Remap_Config_Index(currentselection);
							if (config_index >= 0) option.config_buttons[config_index] = Event.key.keysym.sym;
							exit_map = 1;
						}
					}
				}
			}
		}
		
		print_string("Input remapping", TextWhite, 0, 100, 10, backbuffer->pixels);
		print_string("Press [A] to map to a button", TextWhite, TextBlue, 50, 210, backbuffer->pixels);
		print_string("Press [B] to Exit", TextWhite, TextBlue, 85, 225, backbuffer->pixels);
		
		{
			int32_t slot = 1;
			slot = Draw_Remap_Option(slot, "  UP  : %s\n", currentselection, 5, 25);
			slot = Draw_Remap_Option(slot, " DOWN : %s\n", currentselection, 5, 45);
			slot = Draw_Remap_Option(slot, " LEFT : %s\n", currentselection, 5, 65);
			slot = Draw_Remap_Option(slot, "RIGHT : %s\n", currentselection, 5, 85);
			slot = Draw_Remap_Option(slot, "BTN 1 : %s\n", currentselection, 5, 105);
			slot = Draw_Remap_Option(slot, "BTN 2 : %s\n", currentselection, 5, 125);
			slot = Draw_Remap_Option(slot, "START : %s\n", currentselection, 5, 145);

			if (sms.console == CONSOLE_COLECO)
			{
				slot = Draw_Remap_Option(slot, " [*]  : %s\n", currentselection, 5, 165);
				slot = Draw_Remap_Option(slot, " [#]  : %s\n", currentselection, 5, 185);
				slot = Draw_Remap_Option(slot, " [1]  : %s\n", currentselection, 165, 25);
				slot = Draw_Remap_Option(slot, " [2]  : %s\n", currentselection, 165, 45);
				slot = Draw_Remap_Option(slot, " [3]  : %s\n", currentselection, 165, 65);
				slot = Draw_Remap_Option(slot, " [4]  : %s\n", currentselection, 165, 85);
				slot = Draw_Remap_Option(slot, " [5]  : %s\n", currentselection, 165, 105);
				slot = Draw_Remap_Option(slot, " [6]  : %s\n", currentselection, 165, 125);
				slot = Draw_Remap_Option(slot, " [7]  : %s\n", currentselection, 165, 145);
				slot = Draw_Remap_Option(slot, " [8]  : %s\n", currentselection, 165, 165);
				slot = Draw_Remap_Option(slot, " [9]  : %s\n", currentselection, 165, 185);
			}
			else if (multirexz80_sdl12_arcade_active())
			{
				slot = Draw_Remap_Option(slot, "COIN1 : %s\n", currentselection, 165, 25);
				slot = Draw_Remap_Option(slot, "COIN2 : %s\n", currentselection, 165, 45);
				slot = Draw_Remap_Option(slot, "STRT1 : %s\n", currentselection, 165, 65);
				slot = Draw_Remap_Option(slot, "STRT2 : %s\n", currentselection, 165, 85);
				slot = Draw_Remap_Option(slot, "SERV  : %s\n", currentselection, 165, 105);
				slot = Draw_Remap_Option(slot, "TEST  : %s\n", currentselection, 165, 125);
			}
			if (multirexz80_sdl12_keyboard_active())
				slot = Draw_Remap_Option(slot, "VKBD  : %s\n", currentselection, 165, 185);
		}
		SDL_SoftStretch(backbuffer, NULL, sdl_screen, NULL);
		SDL_Flip(sdl_screen);
	}
}

static void Virtual_Keyboard(void)
{
	SDL_Event Event;
	int32_t current = 0;
	int32_t exit_keyboard = 0;
	int32_t pressed_index = -1;
	int32_t cols = 8;
	int32_t count = multirexz80_sdl12_keyboard_key_count();

	if (!multirexz80_sdl12_keyboard_active() || count <= 0)
		return;

	update_window_size(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION);
	font_drawing_set_target((uint16_t *)backbuffer->pixels, backbuffer->pitch >> 1, backbuffer->w, backbuffer->h);

	while (!exit_keyboard)
	{
		int32_t i;
		SDL_FillRect(backbuffer, NULL, 0);
		print_string("Sord M5 virtual keyboard", TextWhite, 0, 56, 8, backbuffer->pixels);
		for (i = 0; i < count; i++)
		{
			const multirexz80_sdl12_keyboard_key_t *key = multirexz80_sdl12_keyboard_key(i);
			char label[16];
			int32_t x = 4 + (i % cols) * 39;
			int32_t y = 34 + (i / cols) * 22;
			if (!key) continue;
			snprintf(label, sizeof(label), "[%s]", key->label);
			print_string(label, (i == current) ? TextRed : TextWhite, 0, x, y, backbuffer->pixels);
		}
		print_string("D-pad move  A hold key  B exit", TextWhite, TextBlue, 28, 218, backbuffer->pixels);
		SDL_SoftStretch(backbuffer, NULL, sdl_screen, NULL);
		SDL_Flip(sdl_screen);

		while (SDL_PollEvent(&Event))
		{
			if (Event.type == SDL_KEYDOWN)
			{
				switch (Event.key.keysym.sym)
				{
					case SDLK_LEFT:
						if (current > 0) current--;
						break;
					case SDLK_RIGHT:
						if (current < count - 1) current++;
						break;
					case SDLK_UP:
						if (current >= cols) current -= cols;
						break;
					case SDLK_DOWN:
						if (current + cols < count) current += cols;
						break;
					case SDLK_LCTRL:
					case SDLK_RETURN:
						if (pressed_index != current)
						{
							if (pressed_index >= 0) multirexz80_sdl12_keyboard_set_key(pressed_index, 0);
							pressed_index = current;
							multirexz80_sdl12_keyboard_set_key(pressed_index, 1);
						}
						break;
					case SDLK_LALT:
					case SDLK_ESCAPE:
						if (pressed_index >= 0) multirexz80_sdl12_keyboard_set_key(pressed_index, 0);
						pressed_index = -1;
						exit_keyboard = 1;
						break;
					default:
						multirexz80_sdl12_keyboard_from_sdl_key(Event.key.keysym.sym, 1);
						break;
				}
			}
			else if (Event.type == SDL_KEYUP)
			{
				switch (Event.key.keysym.sym)
				{
					case SDLK_LCTRL:
					case SDLK_RETURN:
						if (pressed_index >= 0) multirexz80_sdl12_keyboard_set_key(pressed_index, 0);
						pressed_index = -1;
						break;
					default:
						multirexz80_sdl12_keyboard_from_sdl_key(Event.key.keysym.sym, 0);
						break;
				}
			}
			else if (Event.type == SDL_QUIT)
			{
				exit_keyboard = 1;
			}
		}

		/* Run a frame while a virtual key is held so the emulated keyboard state is sampled. */
		if (pressed_index >= 0)
		{
			system_frame(0);
			Sound_Update(snd.output, snd.sample_count);
		}
		else
		{
			SDL_Delay(16);
		}
	}
	if (pressed_index >= 0)
		multirexz80_sdl12_keyboard_set_key(pressed_index, 0);
	forcerefresh = 1;
}

static void Menu()
{
	char text[50];
	int16_t pressed = 0;
	int16_t currentselection = 1;
	int16_t menu_max = multirexz80_sdl12_keyboard_active() ? 8 : 7;
	int16_t quit_selection = menu_max;
	SDL_Event Event;
	
	Sound_Pause();

	while (((currentselection != 1) && (currentselection != quit_selection)) || (!pressed))
	{
		pressed = 0;
		SDL_FillRect( backbuffer, NULL, 0 );
		
		if (SDL_MUSTLOCK(backbuffer)) SDL_LockSurface(backbuffer);

		print_string("SMS PLUS GX", TextWhite, 0, 105, 15, backbuffer->pixels);
		
		if (currentselection == 1) print_string("Continue", TextRed, 0, 5, 45, backbuffer->pixels);
		else  print_string("Continue", TextWhite, 0, 5, 45, backbuffer->pixels);
		
		snprintf(text, sizeof(text), "Load State %d", save_slot);
		if (currentselection == 2) print_string(text, TextRed, 0, 5, 65, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 65, backbuffer->pixels);
		
		snprintf(text, sizeof(text), "Save State %d", save_slot);
		if (currentselection == 3) print_string(text, TextRed, 0, 5, 85, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 85, backbuffer->pixels);
		
		switch(option.fullscreen)
		{
			case 0:
				print_string("Scaling : Native", (currentselection == 4) ? TextRed : TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
			case 1:
				print_string("Scaling : IPU/Hardware", (currentselection == 4) ? TextRed : TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
			case 2:
				print_string("Scaling : EPX/Scale2x", (currentselection == 4) ? TextRed : TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
		}

		snprintf(text, sizeof(text), "Sound volume : %s", Return_Volume(option.soundlevel));
		if (currentselection == 5) print_string(text, TextRed, 0, 5, 125, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 125, backbuffer->pixels);
		
		if (currentselection == 6) print_string("Input remapping", TextRed, 0, 5, 145, backbuffer->pixels);
		else print_string("Input remapping", TextWhite, 0, 5, 145, backbuffer->pixels);
		
		if (multirexz80_sdl12_keyboard_active())
		{
			if (currentselection == 7) print_string("Virtual keyboard", TextRed, 0, 5, 165, backbuffer->pixels);
			else print_string("Virtual keyboard", TextWhite, 0, 5, 165, backbuffer->pixels);
			if (currentselection == 8) print_string("Quit", TextRed, 0, 5, 185, backbuffer->pixels);
			else print_string("Quit", TextWhite, 0, 5, 185, backbuffer->pixels);
		}
		else
		{
			if (currentselection == 7) print_string("Quit", TextRed, 0, 5, 165, backbuffer->pixels);
			else print_string("Quit", TextWhite, 0, 5, 165, backbuffer->pixels);
		}

		print_string("Build " __DATE__ ", " __TIME__, TextWhite, 0, 5, multirexz80_sdl12_keyboard_active() ? 205 : 195, backbuffer->pixels);
		print_string("Based on SMS Plus GX / SMS Plus", TextWhite, 0, 5, multirexz80_sdl12_keyboard_active() ? 220 : 210, backbuffer->pixels);
		print_string("MultiRexZ80 by gameblabla", TextWhite, 0, 5, multirexz80_sdl12_keyboard_active() ? 235 : 225, backbuffer->pixels);
		
		if (SDL_MUSTLOCK(backbuffer)) SDL_UnlockSurface(backbuffer);

		while (SDL_PollEvent(&Event))
		{
			if (Event.type == SDL_KEYDOWN)
			{
				switch(Event.key.keysym.sym)
				{
					case SDLK_UP:
						currentselection--;
						if (currentselection == 0) currentselection = menu_max;
						break;
					case SDLK_DOWN:
						currentselection++;
						if (currentselection > menu_max) currentselection = 1;
						break;
					case SDLK_LALT:
					case SDLK_END:
					case SDLK_ESCAPE:
						pressed = 1;
						currentselection = 1;
						break;
					case SDLK_LCTRL:
					case SDLK_RETURN:
						pressed = 1;
						break;
					case SDLK_LEFT:
						switch(currentselection)
						{
							case 2:
							case 3:
								if (save_slot > 0) save_slot--;
								break;
							case 4:
								option.fullscreen--;
								if (option.fullscreen < 0) option.fullscreen = upscalers_available;
								break;
							case 5:
								option.soundlevel--;
								if (option.soundlevel > 4) option.soundlevel = 4;
								break;
							default:
								break;
						}
						break;
					case SDLK_RIGHT:
						switch(currentselection)
						{
							case 2:
							case 3:
								save_slot++;
								if (save_slot == 10) save_slot = 9;
								break;
							case 4:
								option.fullscreen++;
								if (option.fullscreen > upscalers_available) option.fullscreen = 0;
								break;
							case 5:
								option.soundlevel++;
								if (option.soundlevel > 4) option.soundlevel = 0;
								break;
							default:
								break;
						}
						break;
					default:
						break;
				}
			}
			else if (Event.type == SDL_QUIT)
			{
				currentselection = quit_selection;
				pressed = 1;
			}
		}

		if (pressed)
		{
			switch(currentselection)
			{
				case 7:
					if (multirexz80_sdl12_keyboard_active())
					{
						Virtual_Keyboard();
						pressed = 0;
					}
					break;
				case 6:
					Input_Remapping();
					break;
				case 5:
					option.soundlevel++;
					if (option.soundlevel > 4) option.soundlevel = 1;
					break;
				case 4:
					option.fullscreen++;
					if (option.fullscreen > upscalers_available) option.fullscreen = 0;
					break;
				case 2:
					smsp_state(save_slot, 1);
					currentselection = 1;
					break;
				case 3:
					smsp_state(save_slot, 0);
					currentselection = 1;
					break;
				default:
					break;
			}
		}
		SDL_SoftStretch(backbuffer, NULL, sdl_screen, NULL);
		SDL_Flip(sdl_screen);
	}
	
	sms.use_fm = option.fm;
	
	Clear_video();
	if (currentselection == quit_selection)
		quit = 1;
	else
		Sound_Unpause();
}

static void Ensure_Extended_Mapping(void)
{
	if (option.config_buttons[CONFIG_BUTTON_ARCADE_COIN1] == 0)
		option.config_buttons[CONFIG_BUTTON_ARCADE_COIN1] = SDLK_TAB;
	if (option.config_buttons[CONFIG_BUTTON_ARCADE_COIN2] == 0)
		option.config_buttons[CONFIG_BUTTON_ARCADE_COIN2] = SDLK_BACKSPACE;
	if (option.config_buttons[CONFIG_BUTTON_ARCADE_START1] == 0)
		option.config_buttons[CONFIG_BUTTON_ARCADE_START1] = SDLK_RETURN;
	if (option.config_buttons[CONFIG_BUTTON_ARCADE_START2] == 0)
		option.config_buttons[CONFIG_BUTTON_ARCADE_START2] = SDLK_SPACE;
	if (option.config_buttons[CONFIG_BUTTON_ARCADE_SERVICE] == 0)
		option.config_buttons[CONFIG_BUTTON_ARCADE_SERVICE] = SDLK_PAGEUP;
	if (option.config_buttons[CONFIG_BUTTON_ARCADE_TEST] == 0)
		option.config_buttons[CONFIG_BUTTON_ARCADE_TEST] = SDLK_PAGEDOWN;
	if (option.config_buttons[CONFIG_BUTTON_VKBD] == 0)
		option.config_buttons[CONFIG_BUTTON_VKBD] = SDLK_TAB;
}

static void Reset_Mapping()
{
	uint_fast8_t i;
	option.config_buttons[CONFIG_BUTTON_UP] = SDLK_UP;
	option.config_buttons[CONFIG_BUTTON_DOWN] = SDLK_DOWN;
	option.config_buttons[CONFIG_BUTTON_LEFT] = SDLK_LEFT;
	option.config_buttons[CONFIG_BUTTON_RIGHT] = SDLK_RIGHT;
		
	option.config_buttons[CONFIG_BUTTON_BUTTON1] = SDLK_LCTRL;
	option.config_buttons[CONFIG_BUTTON_BUTTON2] = SDLK_LALT;
		
	option.config_buttons[CONFIG_BUTTON_START] = SDLK_RETURN;
	
	/* This is for the Colecovision buttons. Don't set those to anything */
	for (i = 7; i < 19; i++)
	{
		option.config_buttons[i] = 0;
	}
	Ensure_Extended_Mapping();
}

static void config_load()
{
	FILE* fp;
	char config_path[256];
	
	snprintf(config_path, sizeof(config_path), "%s/config.cfg", home_path);
	
	fp = fopen(config_path, "rb");
	if (fp)
	{
		fread(&option, sizeof(option), sizeof(int8_t), fp);
		fclose(fp);
		
		/* Earlier versions had the config settings set to 0. If so then do reset mapping. */
		if (option.config_buttons[CONFIG_BUTTON_UP] == 0)
		{
			Reset_Mapping();
		}
		else
		{
			Ensure_Extended_Mapping();
		}
	}
	else
	{
		/* Default mapping for the Bittboy in case loading configuration file fails */
		Reset_Mapping();
	}
}

static void config_save()
{
	char config_path[256];
	snprintf(config_path, sizeof(config_path), "%s/config.cfg", home_path);
	FILE* fp;
	
	fp = fopen(config_path, "wb");
	if (fp)
	{
		fwrite(&option, sizeof(option), sizeof(int8_t), fp);
		fclose(fp);
	}
}

static void Cleanup(void)
{
#ifdef SCALE2X_UPSCALER
	if (scale2x_buf != NULL)
	{
		SDL_FreeSurface(scale2x_buf);
		scale2x_buf = NULL;
	}
#endif
	if (sdl_screen != NULL)
	{
		SDL_FreeSurface(sdl_screen);
		sdl_screen = NULL;
	}
	if (backbuffer != NULL)
	{
		SDL_FreeSurface(backbuffer);
		backbuffer = NULL;
	}
	if (sms_bitmap != NULL)
	{
		SDL_FreeSurface(sms_bitmap);
		sms_bitmap = NULL;
	}
	
	if (bios.rom != NULL)
	{
		free(bios.rom);
		bios.rom = NULL;
	}
	
	for(int i=0;i<joy_numb;i++)
	{
		SDL_JoystickClose(sdl_joy[i]);
	}

	SDL_Quit();

	// Shut down
	system_poweroff();
	system_shutdown();	
}

int update_window_size(int w, int h)
{
	if (w <= 0) w = HOST_WIDTH_RESOLUTION;
	if (h <= 0) h = HOST_HEIGHT_RESOLUTION;

	/* GCW0/OpenDingux SDL exposes normal 16-bpp RGB565 modes.  Request the
	 * actual menu/game target size; falling back to a 256-wide fullscreen mode
	 * leaves some devices with a valid SDL surface but no visible scanout. */
	sdl_screen = SDL_SetVideoMode(w, h, 16, SDL_FLAGS);
	if (!sdl_screen)
	{
		fprintf(stderr,"SDL_SetVideoMode Initialisation error : %s",SDL_GetError());
		printf("Width %d, Height %d, FLAGS 0x%x\n", w, h, SDL_FLAGS);
		return 1;
	}

	if (vdp.height == 0) remember_res_height = HOST_HEIGHT_RESOLUTION;
	video_clear_pending = 1;
	
	return 0;
}

int main (int argc, char *argv[]) 
{
	SDL_Event event;
	
	if(argc < 2) 
	{
		fprintf(stderr, "Usage: ./multirexz80 [FILE]\n");
		return 0;
	}
	
	smsp_gamedata_set(argv[1]);
	
	memset(&option, 0, sizeof(option));
	
	option.fullscreen = 1;
	option.fm = 1;
	option.spritelimit = 1;
	option.tms_pal = 2;
	option.console = 0;
	option.nosound = 0;
	option.soundlevel = 2;
	
	config_load();

	option.console = 0;
	
	strcpy(option.game_name, argv[1]);
	
	// Force Colecovision mode if extension is .col
	if (strcmp(strrchr(argv[1], '.'), ".col") == 0) option.console = 6;
	// Sometimes Game Gear games are not properly detected, force them accordingly
	else if (strcmp(strrchr(argv[1], '.'), ".gg") == 0) option.console = 3;
	
	if (option.fullscreen < 0 || option.fullscreen > upscalers_available) option.fullscreen = 1;
	
	// Load ROM
	if(!load_rom(argv[1])) 
	{
		fprintf(stderr, "Error: Failed to load %s.\n", argv[1]);
		Cleanup();
		return 0;
	}
	
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
	SDL_ShowCursor(0);
	if (update_window_size(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION) == 1)
	{
		fprintf(stderr, "Error: Failed to init video window\n");
		Cleanup();
		return 0;
	}
	
	sms_bitmap = multirexz80_sdl12_create_rgb565_surface(multirexz80_sdl12_bitmap_width(), multirexz80_sdl12_bitmap_height());
	backbuffer = multirexz80_sdl12_create_rgb565_surface(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION);
	font_drawing_set_target((uint16_t *)backbuffer->pixels, backbuffer->pitch >> 1, backbuffer->w, backbuffer->h);
	
	SDL_JoystickEventState(SDL_ENABLE);
	
	joy_numb = SDL_NumJoysticks();
	if (SDL_NumJoysticks() > 3) joy_numb = 3;
	
	for(int i=0;i<joy_numb;i++)
	{
		sdl_joy[i] = SDL_JoystickOpen(i);
	}

#ifdef SCALE2X_UPSCALER
	scale2x_buf = multirexz80_sdl12_create_rgb565_surface(multirexz80_sdl12_bitmap_width()*2, multirexz80_sdl12_bitmap_height()*2);
#endif
	
	fprintf(stdout, "CRC : %08X\n", cart.crc);
	
	// Set parameters for internal bitmap
	bitmap.width = sms_bitmap->w;
	bitmap.height = sms_bitmap->h;
	bitmap.depth = 16;
	bitmap.data = (uint8_t *)sms_bitmap->pixels;
	bitmap.pitch = sms_bitmap->pitch;
	bitmap.viewport.w = VIDEO_WIDTH_SMS;
	bitmap.viewport.h = VIDEO_HEIGHT_SMS;
	bitmap.viewport.x = 0x00;
	bitmap.viewport.y = 0x00;
	
	//sms.territory = settings.misc_region;
	if (sms.console == CONSOLE_SMS || sms.console == CONSOLE_SMS2)
		sms.use_fm = option.fm;
	
	bios_init();

	// Initialize all systems and power on
	system_poweron();
	
	Sound_Init();

	// Loop until the user closes the window
	while (!quit) 
	{
		while (SDL_PollEvent(&event)) 
		{
			switch(event.type) 
			{
				default:
				break;
				case SDL_KEYUP:
					{
						multirexz80_sdl12_keymap_t map;
						multirexz80_sdl12_keymap_from_config(&map, option.config_buttons);
						if (multirexz80_sdl12_keyboard_active() && event.key.keysym.sym == map.virtual_keyboard)
							virtual_keyboard_requested = 1;
						else
							sdl_controls_update_input(event.key.keysym.sym, 0);
					}
					switch(event.key.keysym.sym) 
					{
						/*
						 * HOME is for OpenDingux
						 * 3 is for RetroFW
						 * RCTRL is for PocketGo v2
						 * ESCAPE is mapped to Select
						*/
						case SDLK_HOME:
						case SDLK_END:
						case SDLK_RCTRL:
						case SDLK_ESCAPE:
							selectpressed = 1;
						break;
						default:
						break;
					}
				break;
				case SDL_KEYDOWN:
					{
						multirexz80_sdl12_keymap_t map;
						multirexz80_sdl12_keymap_from_config(&map, option.config_buttons);
						if (!(multirexz80_sdl12_keyboard_active() && event.key.keysym.sym == map.virtual_keyboard))
							sdl_controls_update_input(event.key.keysym.sym, 1);
					}
				break;
				case SDL_JOYAXISMOTION:
					switch (event.jaxis.axis)
					{
						case 0: /* X axis */
							joy_axis[0] = event.jaxis.value;
						break;
						case 1: /* Y axis */
							joy_axis[1] = event.jaxis.value;
						break;
						default:
						break;
					}
				break;
				case SDL_QUIT:
					quit = 1;
				break;
			}
		}
		
		if (joy_axis[0] > DEADZONE_JOYSTICK) input.pad[0] |= INPUT_RIGHT;
		else if (joy_axis[0] < -DEADZONE_JOYSTICK) input.pad[0] |= INPUT_LEFT;
		else if (dpad_input[1] == 0 && dpad_input[2] == 0)
		{
			input.pad[0] &= ~INPUT_LEFT;
			input.pad[0] &= ~INPUT_RIGHT;
		}
		
		if (joy_axis[1] > DEADZONE_JOYSTICK) input.pad[0] |= INPUT_DOWN;
		else if (joy_axis[1] < -DEADZONE_JOYSTICK) input.pad[0] |= INPUT_UP;
		else if (dpad_input[0] == 0 && dpad_input[3] == 0)
		{
			input.pad[0] &= ~INPUT_UP;
			input.pad[0] &= ~INPUT_DOWN;
		}

		multirexz80_sdl12_frame_update();

		// Execute frame(s)
		system_frame(0);
		
		// Refresh sound data
		Sound_Update(snd.output, snd.sample_count);
		
		// Refresh video data
		video_update();

		if (virtual_keyboard_requested)
		{
			virtual_keyboard_requested = 0;
			Virtual_Keyboard();
			Clear_video();
			forcerefresh = 1;
		}
		
		if (selectpressed == 1)
		{
			update_window_size(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION);
            font_drawing_set_target((uint16_t *)backbuffer->pixels, backbuffer->pitch >> 1, backbuffer->w, backbuffer->h);
            Menu();
			Clear_video();
            input.system &= (IS_GG) ? ~INPUT_START : ~INPUT_PAUSE;
            selectpressed = 0;
            forcerefresh = 1;
		}
	}
	
	config_save();
	Cleanup();
	
	return 0;
}
