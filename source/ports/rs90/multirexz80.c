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

#ifndef SDL_TRIPLEBUF
#define SDL_TRIPLEBUF SDL_DOUBLEBUF
#endif

#define SDL_FLAGS (SDL_HWSURFACE | SDL_TRIPLEBUF)

static gamedata_t gdata;

t_config option;

static SDL_Surface* sdl_screen, *backbuffer;
#ifdef SCALE2X_UPSCALER
static SDL_Surface *scale2x_buf;
#endif
SDL_Surface *sms_bitmap;

static char home_path[256];

static uint_fast8_t save_slot = 0;
static uint_fast8_t quit = 0;


static uint_fast8_t forcerefresh = 0;
static uint32_t update_window_size(uint32_t w, uint32_t h);

static int screen_w_remember = 0;
static int screen_h_remember = 0;

static void video_update(void)
{
	SDL_Rect src, dst;
	multirexz80_sdl12_view_t view;
	uint32_t target_w;
	uint32_t target_h;
	int locked = 0;
	int screen_pitch;

	multirexz80_sdl12_get_active_view(&view);

	/* RS-90 panel mode is the native 240x160 target.  If Scale2x is enabled
	 * through config/fullscreen mode 2, keep the final software-scaled image on
	 * that native surface instead of asking SDL/IPU for a source-sized mode. */
	target_w = (option.fullscreen == 0) ? (uint32_t)view.w : HOST_WIDTH_RESOLUTION;
	target_h = (option.fullscreen == 0) ? (uint32_t)view.h : HOST_HEIGHT_RESOLUTION;
	if (target_h == 0) target_h = VIDEO_HEIGHT_SMS;

	if (screen_w_remember != (int)target_w || screen_h_remember != (int)target_h || forcerefresh == 1)
	{
		if (update_window_size(target_w, target_h))
			return;
		screen_w_remember = (int)target_w;
		screen_h_remember = (int)target_h;
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
			dst.x = (Sint16)((sdl_screen->w - src.w) / 2);
			dst.y = (Sint16)((sdl_screen->h - src.h) / 2);
			if (dst.x < 0) dst.x = 0;
			if (dst.y < 0) dst.y = 0;
			SDL_BlitSurface(sms_bitmap, &src, sdl_screen, &dst);
			break;

		case 2:
#ifdef SCALE2X_UPSCALER
			if (scale2x_buf && view.w * 2 <= scale2x_buf->w && view.h * 2 <= scale2x_buf->h)
			{
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
					{
						SDL_Flip(sdl_screen);
						return;
					}
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

		case 1:
		default:
			multirexz80_sdl12_fit_rect(&dst, sdl_screen->w, sdl_screen->h, view.w, view.h);
			if (SDL_MUSTLOCK(sdl_screen))
			{
				if (SDL_LockSurface(sdl_screen) < 0)
				{
					SDL_Flip(sdl_screen);
					return;
				}
				locked = 1;
			}
			screen_pitch = multirexz80_sdl12_surface_pitch_pixels(sdl_screen);
			if (screen_pitch <= 0) screen_pitch = sdl_screen->w;
			bitmap_scale(view.x, view.y, view.w, view.h, dst.w, dst.h,
			             view.pitch_pixels, screen_pitch - dst.w,
			             (uint16_t * restrict)sms_bitmap->pixels,
			             (uint16_t * restrict)sdl_screen->pixels + dst.x + dst.y * screen_pitch);
			if (locked) { SDL_UnlockSurface(sdl_screen); locked = 0; }
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

static uint32_t sdl_controls_update_input_down(SDLKey k)
{
	multirexz80_sdl12_keymap_t map;
	multirexz80_sdl12_keymap_from_config(&map, option.config_buttons);
	return multirexz80_sdl12_update_key(k, 1, &map, NULL);
}


static uint32_t sdl_controls_update_input_release(SDLKey k)
{
	multirexz80_sdl12_keymap_t map;
	multirexz80_sdl12_keymap_from_config(&map, option.config_buttons);
	return multirexz80_sdl12_update_key(k, 0, &map, NULL);
}


static void bios_init()
{
	FILE *fd;
	char bios_path[256];
	
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
	unsigned long i;
	// Set paths, create directories
	snprintf(home_path, sizeof(home_path), "%s/.multirexz80/", getenv("HOME"));
	
	if (mkdir(home_path, 0755) && errno != EEXIST) {
		fprintf(stderr, "Failed to create %s: %d\n", home_path, errno);
	}
	
	// Set the game name
	snprintf(gdata.gamename, sizeof(gdata.gamename), "%s", basename(filename));
	
	// Strip the file extension off
	for (i = strlen(gdata.gamename) - 1; i > 0; i--) {
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
		case 273:
			return "DPAD UP";
		/* DOWN button */
		case 274:
			return "DPAD DOWN";
		/* LEFT button */
		case 276:
			return "DPAD LEFT";
		/* RIGHT button */
		case 275:
			return "DPAD RIGHT";
		/* A button */
		case 306:
			return "A button";
		/* B button */
		case 308:
			return "B button";
		/* X button */
		case 304:
			return "X button";
		/* Y button */
		case 32:
			return "Y button";
		/* L button */
		case 9:
			return "L Shoulder";
		/* R button */
		case 8:
			return "R Shoulder";
		/* Start */
		case 13:
			return "Start button";
		case 27:
			return "Select button";
		default:
			return "...";
	}	
}

static void Input_Remapping()
{
	SDL_Event Event;
	char text[50];
	uint32_t pressed = 0;
	int32_t currentselection = 1;
	uint32_t exit_map = 0;
	
	uint8_t menu_config_input = 0;
	
	while(!(currentselection == -2 && pressed == 1))
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
							currentselection = 1;
						}
                        break;
                    case SDLK_DOWN:
                        currentselection++;
                        switch(menu_config_input)
                        {
							case 0:
								if (currentselection > 7)
									currentselection = 7;
							break;
							case 1:
								if (currentselection > 11)
									currentselection = 11;
							break;
						}
                        break;
                    case SDLK_LEFT:
						currentselection -= 8;
						if (currentselection < 1) currentselection = 1;
                    break;
                    case SDLK_RIGHT:
						if (menu_config_input == 1)
						{
							currentselection += 8;
							if (currentselection > 11) currentselection = 10;
						}
                    break;
                    case SDLK_LCTRL:
                    case SDLK_RETURN:
                        pressed = 1;
					break;
                    case SDLK_TAB:
                        menu_config_input = 0;
                        currentselection = 1;
					break;
                    case SDLK_BACKSPACE:
                        if (sms.console == CONSOLE_COLECO) 
                        {
							menu_config_input = 1;
						}
						currentselection = 1;
					break;
                    case SDLK_LALT:
                        pressed = 1;
                        currentselection = -2;
					break;
                    case SDLK_ESCAPE:
						option.config_buttons[currentselection - 1] = 0;
					break;
					default:
					break;
                }
            }
        }

        if (pressed)
        {
            switch(currentselection)
            {
				case -2:
					/* Exits */
				break;
                default:
					SDL_FillRect( backbuffer, NULL, 0 );
					print_string("Press button for mapping", TextWhite, TextBlue, 24, 64, backbuffer->pixels);
					SDL_BlitSurface(backbuffer, NULL, sdl_screen, NULL);
					SDL_Flip(sdl_screen);
					exit_map = 0;
					while( !exit_map )
					{
						while (SDL_PollEvent(&Event))
						{
							if (Event.type == SDL_KEYDOWN)
							{
								option.config_buttons[(currentselection - 1) + (menu_config_input ? 7 : 0) ] = Event.key.keysym.sym;
								exit_map = 1;
							}
						}
					}
				break;
            }
        }

		print_string("[A] : Map, [B] : Exit", TextWhite, TextBlue, 20, 8, backbuffer->pixels);
		
		if (menu_config_input == 0)
		{
			snprintf(text, sizeof(text), "UP : %s\n", Return_Text_Button(option.config_buttons[0]));
			if (currentselection == 1) print_string(text, TextRed, 0, 5, 25, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 25, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "DOWN : %s\n", Return_Text_Button(option.config_buttons[1]));
			if (currentselection == 2) print_string(text, TextRed, 0, 5, 45, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 45, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "LEFT : %s\n", Return_Text_Button(option.config_buttons[2]));
			if (currentselection == 3) print_string(text, TextRed, 0, 5, 65, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 65, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "RIGHT : %s\n", Return_Text_Button(option.config_buttons[3]));
			if (currentselection == 4) print_string(text, TextRed, 0, 5, 85, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 85, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "A : %s\n", Return_Text_Button(option.config_buttons[4]));
			if (currentselection == 5) print_string(text, TextRed, 0, 5, 105, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 105, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "B : %s\n", Return_Text_Button(option.config_buttons[5]));
			if (currentselection == 6) print_string(text, TextRed, 0, 5, 125, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 125, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "START : %s\n", Return_Text_Button(option.config_buttons[6]));
			if (currentselection == 7) print_string(text, TextRed, 0, 5, 145, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 145, backbuffer->pixels);
		}
		else if (menu_config_input == 1)
		{
			snprintf(text, sizeof(text), "[*] : %s\n", Return_Text_Button(option.config_buttons[7]));
			if (currentselection == 1) print_string(text, TextRed, 0, 5, 25, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 25, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[#] : %s\n", Return_Text_Button(option.config_buttons[8]));
			if (currentselection == 2) print_string(text, TextRed, 0, 5, 45, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 45, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[1] : %s\n", Return_Text_Button(option.config_buttons[9]));
			if (currentselection == 3) print_string(text, TextRed, 0, 5, 65, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 65, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[2] : %s\n", Return_Text_Button(option.config_buttons[10]));
			if (currentselection == 4) print_string(text, TextRed, 0, 5, 85, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 85, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[3] : %s\n", Return_Text_Button(option.config_buttons[11]));
			if (currentselection == 5) print_string(text, TextRed, 0, 5, 105, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 105, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[4] : %s\n", Return_Text_Button(option.config_buttons[12]));
			if (currentselection == 6) print_string(text, TextRed, 0, 5, 125, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 125, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[5] : %s\n", Return_Text_Button(option.config_buttons[13]));
			if (currentselection == 7) print_string(text, TextRed, 0, 5, 145, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 145, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[6] : %s\n", Return_Text_Button(option.config_buttons[14]));
			if (currentselection == 8) print_string(text, TextRed, 0, 5, 145, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 5, 145, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[7] : %s\n", Return_Text_Button(option.config_buttons[15]));
			if (currentselection == 9) print_string(text, TextRed, 0, 145, 25, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 145, 25, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[8] : %s\n", Return_Text_Button(option.config_buttons[16]));
			if (currentselection == 10) print_string(text, TextRed, 0, 145, 45, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 145, 45, backbuffer->pixels);
			
			snprintf(text, sizeof(text), "[9] : %s\n", Return_Text_Button(option.config_buttons[17]));
			if (currentselection == 11) print_string(text, TextRed, 0, 145, 65, backbuffer->pixels);
			else print_string(text, TextWhite, 0, 145, 65, backbuffer->pixels);
		}
		SDL_BlitSurface(backbuffer, NULL, sdl_screen, NULL);
		SDL_Flip(sdl_screen);
	}
	
}

static void Menu()
{
	uint_fast8_t i;
	char text[50];
    int16_t pressed = 0;
    int16_t currentselection = 1;
    SDL_Event Event;
    
    update_window_size(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION);
    screen_w_remember = HOST_WIDTH_RESOLUTION;
    screen_h_remember = HOST_HEIGHT_RESOLUTION;
    font_drawing_set_target((uint16_t *)backbuffer->pixels, backbuffer->pitch >> 1, backbuffer->w, backbuffer->h);

	Sound_Pause();
    
    while (((currentselection != 1) && (currentselection != 7)) || (!pressed))
    {
        pressed = 0;
 		SDL_FillRect( backbuffer, NULL, 0 );

		print_string("MultiRexZ80", TextWhite, 0, 72, 15, backbuffer->pixels);
		
		if (currentselection == 1) print_string("Continue", TextBlue, 0, 5, 27, backbuffer->pixels);
		else  print_string("Continue", TextWhite, 0, 5, 27, backbuffer->pixels);
		
		snprintf(text, sizeof(text), "Load State %d", save_slot);
		
		if (currentselection == 2) print_string(text, TextBlue, 0, 5, 39, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 39, backbuffer->pixels);
		
		snprintf(text, sizeof(text), "Save State %d", save_slot);
		
		if (currentselection == 3) print_string(text, TextBlue, 0, 5, 51, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 51, backbuffer->pixels);
		
		if (currentselection == 4) print_string("Input remapping", TextBlue, 0, 5, 63, backbuffer->pixels);
		else print_string("Input remapping", TextWhite, 0, 5, 63, backbuffer->pixels);
		
		snprintf(text, sizeof(text), "FM Sound : %d", option.fm);
		
		if (currentselection == 5) print_string(text, TextBlue, 0, 5, 75, backbuffer->pixels);
		else print_string(text, TextWhite, 0, 5, 75, backbuffer->pixels);
		
		if (currentselection == 6) print_string("Reset", TextBlue, 0, 5, 87, backbuffer->pixels);
		else print_string("Reset", TextWhite, 0, 5, 87, backbuffer->pixels);

        if (currentselection == 7) print_string("Quit", TextBlue, 0, 5, 99, backbuffer->pixels);
		else print_string("Quit", TextWhite, 0, 5, 99, backbuffer->pixels);

        
		print_string("By gameblabla, ekeeke", TextWhite, 0, 5, 145, backbuffer->pixels);

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
                    case SDLK_LCTRL:
                    case SDLK_LALT:
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
							case 5:
								if (option.fm > 0) option.fm--;
							break;
                        }
                        break;
                    case SDLK_RIGHT:
                        switch(currentselection)
                        {
                            case 2:
                            case 3:
                                save_slot++;
								if (save_slot == 10)
									save_slot = 9;
							break;
							case 5:
								if (option.fm < 1) option.fm++;
							break;
                        }
                        break;
					default:
					break;
                }
            }
            else if (Event.type == SDL_QUIT)
            {
				currentselection = 8;
			}
        }

        if (pressed)
        {
            switch(currentselection)
            {
                case 6:
                    //reset
                    Sound_Close();
                    Sound_Init();
                    system_poweron();
                    currentselection = 1;
                    break;
				case 4:
					Input_Remapping();
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

		SDL_BlitSurface(backbuffer, NULL, sdl_screen, NULL);
		SDL_Flip(sdl_screen);
    }
    sms.use_fm = option.fm;
    for(i=0;i<3;i++)
    {
		SDL_FillRect(sdl_screen, NULL, 0);
		SDL_Flip(sdl_screen);
	}
    
    if (currentselection == 7)
        quit = 1;
	else
		Sound_Unpause();
}


static void config_load()
{
	uint_fast8_t i;
	char config_path[256];
	snprintf(config_path, sizeof(config_path), "%sconfig.cfg", home_path);
	FILE* fp;
	
	fp = fopen(config_path, "rb");
	if (fp)
	{
		fread(&option, sizeof(option), sizeof(int8_t), fp);
		fclose(fp);
        printf("Config loaded. >%s\n",config_path);
	}
	else
	{
        printf("Config NOT loaded. >%s\n",config_path);
        
		for (i = 0; i < sizeof(option); i++)
		{
			option.config_buttons[i] = 0;
		}

		/* Default mapping for the Bittboy in case loading configuration file fails */
		option.config_buttons[CONFIG_BUTTON_UP] = SDLK_UP;
		option.config_buttons[CONFIG_BUTTON_DOWN] = SDLK_DOWN;
		option.config_buttons[CONFIG_BUTTON_LEFT] = SDLK_LEFT;
		option.config_buttons[CONFIG_BUTTON_RIGHT] = SDLK_RIGHT;
		
		option.config_buttons[CONFIG_BUTTON_BUTTON1] = SDLK_LCTRL;
		option.config_buttons[CONFIG_BUTTON_BUTTON2] = SDLK_LALT;
		
		option.config_buttons[CONFIG_BUTTON_START] = SDLK_RETURN;
	}
}

static void config_save()
{
	char config_path[256];
	snprintf(config_path, sizeof(config_path), "%sconfig.cfg", home_path);
	FILE* fp;
	
	fp = fopen(config_path, "wb");
	if (fp)
	{
		fwrite(&option, sizeof(option), sizeof(int8_t), fp);
		fclose(fp);
        printf("Config Saved. >%s\n",config_path);
	}
    else
        printf("Config Save Failed! >%s\n",config_path);
}


static void Cleanup(void)
{
	if (sdl_screen) SDL_FreeSurface(sdl_screen);
	if (sms_bitmap) SDL_FreeSurface(sms_bitmap);
	if (backbuffer) SDL_FreeSurface(backbuffer);
#ifdef SCALE2X_UPSCALER
	if (scale2x_buf) SDL_FreeSurface(scale2x_buf);
#endif
	if (bios.rom) free(bios.rom);
	
	// Deinitialize audio and video output
	Sound_Close();
	
	SDL_Quit();

	// Shut down
	system_poweroff();
	system_shutdown();	
}

uint32_t update_window_size(uint32_t w, uint32_t h)
{
	if (w == 0) w = VIDEO_WIDTH_SMS;
	if (h == 0) h = VIDEO_HEIGHT_SMS;

	/* Upstream RS-90 SDL now handles panel scaling for normal 16-bpp modes.
	 * Keep this path like the GCW0/OpenDingux backend: request an RGB565
	 * surface at the desired active output size and render into it. */
	sdl_screen = SDL_SetVideoMode((int)w, (int)h, 16, SDL_FLAGS);
	if (!sdl_screen)
	{
		fprintf(stderr, "SDL_SetVideoMode Initialisation error : %s\n", SDL_GetError());
		fprintf(stderr, "Width %u, Height %u, FLAGS 0x%x\n", w, h, SDL_FLAGS);
		return 1;
	}
	return 0;
}

int main (int argc, char *argv[]) 
{
	uint_fast32_t i;
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
#ifdef SCALE2X_UPSCALER
	/* RS-90 has no display-mode menu, but keep a saved/manual Scale2x mode usable. */
	if (option.fullscreen < 1 || option.fullscreen > 2) option.fullscreen = 1;
#else
	/* RS-90 has no display-mode menu; ignore stale saved values and always
	 * use the 240x160 panel-sized output path. */
	option.fullscreen = 1;
#endif

	option.console = 0;
	
	strcpy(option.game_name, argv[1]);
	
	// Force Colecovision mode if extension is .col
	if (strcmp(strrchr(argv[1], '.'), ".col") == 0) option.console = 6;
	// Sometimes Game Gear games are not properly detected, force them accordingly
	else if (strcmp(strrchr(argv[1], '.'), ".gg") == 0) option.console = 3;

	// Load ROM
	if(!load_rom(argv[1])) {
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

	SDL_Init(SDL_INIT_VIDEO);
	SDL_ShowCursor(0);
	if (update_window_size(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION))
	{
		fprintf(stdout, "Could not create display, exiting\n");	
		Cleanup();
		return 0;
	}
	backbuffer = multirexz80_sdl12_create_rgb565_surface(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION);
	font_drawing_set_target((uint16_t *)backbuffer->pixels, backbuffer->pitch >> 1, backbuffer->w, backbuffer->h);
	sms_bitmap = multirexz80_sdl12_create_rgb565_surface(multirexz80_sdl12_bitmap_width(), multirexz80_sdl12_bitmap_height());
#ifdef SCALE2X_UPSCALER
	scale2x_buf = multirexz80_sdl12_create_rgb565_surface(multirexz80_sdl12_bitmap_width() * 2, multirexz80_sdl12_bitmap_height() * 2);
#endif
	
	SDL_FillRect(sms_bitmap, NULL, 0 );
	for(i=0;i<3;i++)
	{
		SDL_FillRect(sdl_screen, NULL, 0 );
		SDL_Flip(sdl_screen);
	}
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
	{ 
		sms.use_fm = option.fm;
	}
	
	bios_init();
	
	// Initialize all systems and power on
	system_poweron();
	
	Sound_Init();
	
	forcerefresh = 1;
	
	// Loop until the user closes the window
	while (!quit) 
	{
		if (SDL_PollEvent(&event)) 
		{
			switch(event.type) 
			{
				case SDL_KEYUP:
					sdl_controls_update_input_release(event.key.keysym.sym);
				break;
				case SDL_KEYDOWN:
					if (event.key.keysym.sym == SDLK_ESCAPE)
					{
						Menu();
						input.system &= (IS_GG) ? ~INPUT_START : ~INPUT_PAUSE;
						forcerefresh = 1;
					}
					sdl_controls_update_input_down(event.key.keysym.sym);
				break;
				case SDL_QUIT:
					quit = 1;
				break;
			}
		}

		multirexz80_sdl12_frame_update();

		// Execute frame(s)
		system_frame(0);
		
		// Refresh sound data
		Sound_Update(snd.output, snd.sample_count);
		
		// Refresh video data
		video_update();
	}
	
	config_save();
	Cleanup();
	
	return 0;
}
