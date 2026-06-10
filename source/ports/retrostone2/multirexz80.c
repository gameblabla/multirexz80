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

static SDL_Surface* sdl_screen;
static SDL_Surface *backbuffer;
#ifdef SCALE2X_UPSCALER
static SDL_Surface *scale2x_buf;
#endif
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
static uint_fast8_t dpad_input[4] = {0, 0, 0, 0};
uint_fast16_t pixels_shifting_remove = 0;

int update_window_size(int w, int h);


static void retrostone2_put_pixel32(SDL_Surface *dst, int x, int y, uint32_t pixel)
{
	uint8_t *p;
	if (!dst || !dst->pixels || !dst->format) return;
	if (x < 0 || y < 0 || x >= dst->w || y >= dst->h) return;
	p = (uint8_t *)dst->pixels + (size_t)y * dst->pitch + (size_t)x * dst->format->BytesPerPixel;
	if (dst->format->BytesPerPixel == 4)
		*(uint32_t *)p = pixel;
	else if (dst->format->BytesPerPixel == 3)
	{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		p[0] = (uint8_t)((pixel >> 16) & 0xff);
		p[1] = (uint8_t)((pixel >> 8) & 0xff);
		p[2] = (uint8_t)(pixel & 0xff);
#else
		p[0] = (uint8_t)(pixel & 0xff);
		p[1] = (uint8_t)((pixel >> 8) & 0xff);
		p[2] = (uint8_t)((pixel >> 16) & 0xff);
#endif
	}
}

static void retrostone2_blit32_to_screen(SDL_Surface *src, const SDL_Rect *src_rect,
									 SDL_Surface *dst, const SDL_Rect *dst_rect)
{
	SDL_Rect srect, drect;
	int locked_src = 0, locked_dst = 0;
	int x, y;

	if (!src || !dst || !src->pixels || !dst->pixels || !src->format || !dst->format)
		return;

	srect.x = src_rect ? src_rect->x : 0;
	srect.y = src_rect ? src_rect->y : 0;
	srect.w = src_rect ? src_rect->w : src->w;
	srect.h = src_rect ? src_rect->h : src->h;
	drect.x = dst_rect ? dst_rect->x : 0;
	drect.y = dst_rect ? dst_rect->y : 0;
	drect.w = dst_rect ? dst_rect->w : srect.w;
	drect.h = dst_rect ? dst_rect->h : srect.h;

	if (srect.x < 0) { srect.w += srect.x; drect.x -= srect.x; srect.x = 0; }
	if (srect.y < 0) { srect.h += srect.y; drect.y -= srect.y; srect.y = 0; }
	if (srect.x + srect.w > src->w) srect.w = src->w - srect.x;
	if (srect.y + srect.h > src->h) srect.h = src->h - srect.y;
	if (drect.x < 0) { drect.w += drect.x; drect.x = 0; }
	if (drect.y < 0) { drect.h += drect.y; drect.y = 0; }
	if (drect.x + drect.w > dst->w) drect.w = dst->w - drect.x;
	if (drect.y + drect.h > dst->h) drect.h = dst->h - drect.y;
	if (srect.w <= 0 || srect.h <= 0 || drect.w <= 0 || drect.h <= 0)
		return;

	if (SDL_MUSTLOCK(src) && SDL_LockSurface(src) == 0) locked_src = 1;
	if (SDL_MUSTLOCK(dst) && SDL_LockSurface(dst) == 0) locked_dst = 1;

	for (y = 0; y < drect.h; y++)
	{
		int sy = srect.y + (int)((int64_t)y * srect.h / drect.h);
		const uint32_t *srcrow = (const uint32_t *)((const uint8_t *)src->pixels + (size_t)sy * src->pitch);
		for (x = 0; x < drect.w; x++)
		{
			int sx = srect.x + (int)((int64_t)x * srect.w / drect.w);
			retrostone2_put_pixel32(dst, drect.x + x, drect.y + y, srcrow[sx]);
		}
	}

	if (locked_dst) SDL_UnlockSurface(dst);
	if (locked_src) SDL_UnlockSurface(src);
}

static SDL_Surface *retrostone2_create_xrgb8888_surface(int w, int h)
{
	if (w < 1) w = 1;
	if (h < 1) h = 1;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	return SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32,
							 0x0000ff00, 0x00ff0000, 0xff000000, 0x00000000);
#else
	return SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32,
							 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000);
#endif
}

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

	multirexz80_sdl12_get_active_view(&view);
	width_hold = view.w;
	width_remove = view.x;

	/* Retrostone2 follows the OpenDingux-style layout used by GCW0/RS97:
	 * mode 0 is true 1:1 native output inside the fixed LCD scanout, mode 1
	 * requests a source-sized mode for hardware/IPU scaling, and mode 2 keeps
	 * the software Scale2x path on the fixed native surface. */
	target_w = (option.fullscreen == 1) ? view.w : HOST_WIDTH_RESOLUTION;
	target_h = (option.fullscreen == 1) ? view.h : HOST_HEIGHT_RESOLUTION;
	if (remember_res_height != target_h || width_remember != target_w || forcerefresh == 1)
	{
		remember_res_height = target_h;
		width_remember = target_w;
		update_window_size(target_w, target_h);
		forcerefresh = 0;
	}

	src.x = (Sint16)view.x;
	src.y = (Sint16)view.y;
	src.w = (Uint16)view.w;
	src.h = (Uint16)view.h;
	SDL_FillRect(sdl_screen, NULL, 0);

	switch (option.fullscreen)
	{
		case 0:
			dst.x = (Sint16)((sdl_screen->w - view.w) / 2);
			dst.y = (Sint16)((sdl_screen->h - view.h) / 2);
			dst.w = (Uint16)view.w;
			dst.h = (Uint16)view.h;
			if (dst.x < 0) dst.x = 0;
			if (dst.y < 0) dst.y = 0;
			retrostone2_blit32_to_screen(sms_bitmap, &src, sdl_screen, &dst);
			break;

		case 1:
			dst.x = 0;
			dst.y = 0;
			dst.w = (Uint16)view.w;
			dst.h = (Uint16)view.h;
			retrostone2_blit32_to_screen(sms_bitmap, &src, sdl_screen, &dst);
			break;

		case 2:
#ifdef SCALE2X_UPSCALER
			if (scale2x_buf && view.w * 2 <= scale2x_buf->w && view.h * 2 <= scale2x_buf->h)
			{
				SDL_Rect scaled_src;
				scale2x32((uint32_t *)sms_bitmap->pixels + view.y * view.pitch_pixels + view.x,
				          (uint32_t *)scale2x_buf->pixels,
				          sms_bitmap->pitch,
				          scale2x_buf->pitch,
				          view.w, view.h);
				scaled_src.x = 0;
				scaled_src.y = 0;
				scaled_src.w = (Uint16)(view.w * 2);
				scaled_src.h = (Uint16)(view.h * 2);
#ifdef MULTIREXZ80_SCALE2X_TARGET_NATIVE
				dst.x = 0;
				dst.y = 0;
				dst.w = (Uint16)sdl_screen->w;
				dst.h = (Uint16)sdl_screen->h;
#else
				multirexz80_sdl12_fit_rect(&dst, sdl_screen->w, sdl_screen->h, view.w * 2, view.h * 2);
#endif
				retrostone2_blit32_to_screen(scale2x_buf, &scaled_src, sdl_screen, &dst);
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

static void Draw_Option(int32_t numb, int32_t selection, const char* drawtext, int32_t x, int32_t y)
{
	char text[18];
	snprintf(text, sizeof(text), drawtext, Return_Text_Button(option.config_buttons[numb]));
	if (selection == numb+1) print_string(text, TextRed, 0, x, y+2, (font_pixel_t *)backbuffer->pixels);
	else print_string(text, TextWhite, 0, x, y+2, (font_pixel_t *)backbuffer->pixels);
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
                        if (currentselection < 1)
                        {
							if (sms.console == CONSOLE_COLECO)
							{
								currentselection = 18;
							}
							else currentselection = 7;
						}
                        break;
                    case SDLK_DOWN:
                        currentselection++;
                        if (currentselection == 19 && sms.console == CONSOLE_COLECO)
                        {
							currentselection = 1;
						}
                        else if (currentselection == 8 && sms.console != CONSOLE_COLECO)
                        {
							currentselection = 1;
						}
                        break;
                    case SDLK_LCTRL:
                    case SDLK_RETURN:
                        pressed = 1;
					break;
                    case SDLK_LALT:
                        exit_input = 1;
					break;
                    case SDLK_LEFT:
						if (sms.console == CONSOLE_COLECO)
						{
							if (currentselection > 8) currentselection -= 9;
						}
					break;
                    case SDLK_RIGHT:
						if (sms.console == CONSOLE_COLECO)
						{
							if (currentselection < 10) currentselection += 9;
						}
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
			retrostone2_blit32_to_screen(backbuffer, NULL, sdl_screen, NULL);
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
							option.config_buttons[currentselection - 1] = Event.key.keysym.sym;
							exit_map = 1;
						}
					}
				}
			}
        }
		
		print_string("Input remapping", TextWhite, 0, 100, 10, backbuffer->pixels);
		
		print_string("Press [A] to map to a button", TextWhite, TextBlue, 50, 210, backbuffer->pixels);
		print_string("Press [B] to Exit", TextWhite, TextBlue, 85, 225, backbuffer->pixels);
		
		Draw_Option(0, currentselection, "  UP  : %s\n", 5, 25);
		Draw_Option(1, currentselection, " DOWN : %s\n", 5, 45);
		Draw_Option(2, currentselection, " LEFT : %s\n", 5, 65);
		Draw_Option(3, currentselection, "RIGHT : %s\n", 5, 85);
		
		Draw_Option(4, currentselection, "BTN 1 : %s\n", 5, 105);
		Draw_Option(5, currentselection, "BTN 2 : %s\n", 5, 125);
		Draw_Option(6, currentselection, "START : %s\n", 5, 145);
		
		if (sms.console == CONSOLE_COLECO)
		{
			Draw_Option(7, currentselection, " [*]  : %s\n", 5, 165);
			Draw_Option(8, currentselection, " [#]  : %s\n", 5, 185);
			
			Draw_Option(9, currentselection, " [1]  : %s\n", 165, 25);
			Draw_Option(10, currentselection, " [2]  : %s\n", 165, 45);
			
			Draw_Option(11, currentselection, " [3]  : %s\n", 165, 65);
			Draw_Option(12, currentselection, " [4]  : %s\n", 165, 85);
			Draw_Option(13, currentselection, " [5]  : %s\n", 165, 105);
			Draw_Option(14, currentselection, " [6]  : %s\n", 165, 125);
			
			Draw_Option(15, currentselection, " [7]  : %s\n", 165, 145);
			Draw_Option(16, currentselection, " [8]  : %s\n", 165, 165);
			Draw_Option(17, currentselection, " [9]  : %s\n", 165, 185);
		}
		retrostone2_blit32_to_screen(backbuffer, NULL, sdl_screen, NULL);
		SDL_Flip(sdl_screen);
	}
	
}

static void Menu()
{
	char text[50];
    int16_t pressed = 0;
    int16_t currentselection = 1;
    SDL_Event Event;
    
    Sound_Pause();

    while (((currentselection != 1) && (currentselection != 7)) || (!pressed))
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
		
        if (currentselection == 4)
        {
			switch(option.fullscreen)
			{
				case 0:
					print_string("Scaling : Native", TextRed, 0, 5, 105, backbuffer->pixels);
				break;
				case 1:
					print_string("Scaling : IPU/Hardware", TextRed, 0, 5, 105, backbuffer->pixels);
				break;
				case 2:
					print_string("Scaling : EPX/Scale2x", TextRed, 0, 5, 105, backbuffer->pixels);
				break;
			}
        }
        else
        {
			switch(option.fullscreen)
			{
				case 0:
					print_string("Scaling : Native", TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
				case 1:
					print_string("Scaling : IPU/Hardware", TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
				case 2:
					print_string("Scaling : EPX/Scale2x", TextWhite, 0, 5, 105, backbuffer->pixels);
				break;
			}
        }

		snprintf(text, sizeof(text), "Sound volume : %s", Return_Volume(option.soundlevel));
		
		if (currentselection == 5) print_string(text, TextRed, 0, 5, 125, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 125, backbuffer->pixels);
		
		if (currentselection == 6) print_string("Input remapping", TextRed, 0, 5, 145, backbuffer->pixels);
		else print_string("Input remapping", TextWhite, 0, 5, 145, backbuffer->pixels);
		
		if (currentselection == 7) print_string("Quit", TextRed, 0, 5, 165, backbuffer->pixels);
		else print_string("Quit", TextWhite, 0, 5, 165, backbuffer->pixels);

		print_string("Build " __DATE__ ", " __TIME__, TextWhite, 0, 5, 195, backbuffer->pixels);
		print_string("Based on SMS Plus GX / SMS Plus", TextWhite, 0, 5, 210, backbuffer->pixels);
		print_string("MultiRexZ80 by gameblabla", TextWhite, 0, 5, 225, backbuffer->pixels);
		
		if (SDL_MUSTLOCK(backbuffer)) SDL_UnlockSurface(backbuffer);

        while (SDL_PollEvent(&Event))
        {
            if (Event.type == SDL_KEYDOWN)
            {
                switch(Event.key.keysym.sym)
                {
                    case SDLK_UP:
                        currentselection--;
                        if (currentselection == 0)
                            currentselection = 7;
                        break;
                    case SDLK_DOWN:
                        currentselection++;
                        if (currentselection == 8)
                            currentselection = 1;
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
							default:
							break;
                            case 2:
                            case 3:
                                if (save_slot > 0) save_slot--;
							break;
                            case 4:
							option.fullscreen--;
							if (option.fullscreen < 0)
								option.fullscreen = upscalers_available;
							break;
							case 5:
								option.soundlevel--;
								if (option.soundlevel > 4)
									option.soundlevel = 4;
							break;
                        }
                        break;
                    case SDLK_RIGHT:
                        switch(currentselection)
                        {
							default:
							break;
                            case 2:
                            case 3:
                                save_slot++;
								if (save_slot == 10)
									save_slot = 9;
							break;
                            case 4:
                                option.fullscreen++;
                                if (option.fullscreen > upscalers_available)
                                    option.fullscreen = 0;
							break;
							case 5:
								option.soundlevel++;
								if (option.soundlevel > 4)
									option.soundlevel = 0;
							break;
                        }
                        break;
					default:
					break;
                }
            }
            else if (Event.type == SDL_QUIT)
            {
				currentselection = 7;
				pressed = 1;
			}
        }

        if (pressed)
        {
            switch(currentselection)
            {
				case 6:
					Input_Remapping();
				break;
				case 5:
					option.soundlevel++;
					if (option.soundlevel > 4)
						option.soundlevel = 1;
				break;
                case 4 :
                    option.fullscreen++;
                    if (option.fullscreen > upscalers_available)
                        option.fullscreen = 0;
                    break;
                case 2 :
                    smsp_state(save_slot, 1);
					currentselection = 1;
                    break;
                case 3 :
					smsp_state(save_slot, 0);
					currentselection = 1;
				break;
				default:
				break;
            }
        }
		retrostone2_blit32_to_screen(backbuffer, NULL, sdl_screen, NULL);
		SDL_Flip(sdl_screen);
    }
    
	sms.use_fm = option.fm;
    
	Clear_video();
    if (currentselection == 7)
        quit = 1;
	else
		Sound_Unpause();
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
	for (i = 7; i < 18; i++)
	{
		option.config_buttons[i] = 0;
	}	
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
#ifdef SCALE2X_UPSCALER
	if (scale2x_buf != NULL)
	{
		SDL_FreeSurface(scale2x_buf);
		scale2x_buf = NULL;
	}
#endif
	
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
	sdl_screen = SDL_SetVideoMode(w, h, 32, SDL_FLAGS);
			
	if (!sdl_screen)
	{
		fprintf(stderr,"SDL_SetVideoMode Initialisation error : %s",SDL_GetError());
		printf("Width %d, Height %d, FLAGS 0x%x\n", w, h, SDL_FLAGS);
		return 1;
	}

	if (vdp.height == 0) remember_res_height = 240;
	
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
	
	/* Force 50 Fps refresh rate for PAL only games */
	if (sms.display == DISPLAY_PAL)
	{
		setenv("SDL_VIDEO_REFRESHRATE", "50", 0);
	}
	else
	{
		setenv("SDL_VIDEO_REFRESHRATE", "60", 0);
	}
	
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
	SDL_ShowCursor(0);
	if (update_window_size(320, 240) == 1)
	{
		fprintf(stderr, "Error: Failed to init video window\n");
		Cleanup();
		return 0;
	}
	
	sms_bitmap = retrostone2_create_xrgb8888_surface(multirexz80_sdl12_bitmap_width(), multirexz80_sdl12_bitmap_height());
	backbuffer = retrostone2_create_xrgb8888_surface(320, 240);
#ifdef SCALE2X_UPSCALER
	scale2x_buf = retrostone2_create_xrgb8888_surface(multirexz80_sdl12_bitmap_width() * 2, multirexz80_sdl12_bitmap_height() * 2);
#endif
	font_drawing_set_target((font_pixel_t *)backbuffer->pixels, backbuffer->pitch / sizeof(font_pixel_t), backbuffer->w, backbuffer->h);
	
	SDL_JoystickEventState(SDL_ENABLE);
	
	joy_numb = SDL_NumJoysticks();
	if (SDL_NumJoysticks() > 3) joy_numb = 3;
	
	for(int i=0;i<joy_numb;i++)
	{
		sdl_joy[i] = SDL_JoystickOpen(i);
	}
	
	fprintf(stdout, "CRC : %08X\n", cart.crc);
	
	// Set parameters for internal bitmap
	bitmap.width = sms_bitmap->w;
	bitmap.height = sms_bitmap->h;
	bitmap.depth = MULTIREXZ80_RENDER_DEPTH;
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
					sdl_controls_update_input(event.key.keysym.sym, 0);
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
					sdl_controls_update_input(event.key.keysym.sym, 1);
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
		
		if (selectpressed == 1)
		{
			update_window_size(320, 240);
            Menu();
            if (option.fullscreen == 0)
            {
				update_window_size(320, 240);
			}
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
