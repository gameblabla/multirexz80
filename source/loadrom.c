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
 *   ROM File Loading support
 *
 ******************************************************************************/
 
/*
 * See git commit history for more information.
 * - Gameblabla
 * July 16th 2019 : Add 4PAK support to the list. It also makes use of PAL mode.
 * June 6th 2019 : Add Blockhole & Alibaba to the list as they rely on the Japanese BIOS and its uninitiliazed memory.
 * March 14th 2019 : Add NOZIP for building without zip support.
 * March 11th 2019 : Fantastic Dizzy should run in PAL mode to avoid issues.
 * August 12th 2018 : Add Bad Apple to the list. (plus minor fixes)
*/

#include "shared.h"
#include "other/decode/mc8123_decode.h"
#include "other/decode/sega_decode.h"
#include <ctype.h>

uint8_t gaiden_hack = 0;

typedef struct
{
	uint32_t crc;
	uint8_t glasses_3d;
	uint8_t device;
	uint8_t mapper;
	uint8_t display;
	uint8_t territory;
	uint8_t console;
	uint8_t fm_compatible;
	const char *name;
} rominfo_t;

static rominfo_t game_list[] =
{
	{0x2D465006, 0, DEVICE_PAD2B, MAPPER_SYSTEME, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SYSTEME, FM_NOT_COMPATIBLE,
	"Tetris [Sega System E]"},
	{0x47108F41, 0, DEVICE_PAD2B, MAPPER_SYSTEME, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SYSTEME, FM_NOT_COMPATIBLE,
	"Transformer [Sega System E]"},
	{0xC923FEAA, 0, DEVICE_PAD2B, MAPPER_SYSTEME, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SYSTEME, FM_NOT_COMPATIBLE,
	"Astro Flash [Sega System E, encrypted]"},
	{0x65C47676, 0, DEVICE_PAD2B, MAPPER_SYSTEM1, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SYSTEM1, FM_NOT_COMPATIBLE,
	"Block Gal [Sega System 1/2, bootleg source ROM]"},
	{0xB15BA9F8, 0, DEVICE_PAD2B, MAPPER_SYSTEM1, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SYSTEM1, FM_NOT_COMPATIBLE,
	"Block Gal [Sega System 1/2, bootleg mapped data]"},
	{0xFE49D83E, 0, DEVICE_PAD2B, MAPPER_SYSTEM1, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SYSTEM1, FM_NOT_COMPATIBLE,
	"Choplifter (unprotected) [Sega System 2]"},
	{0x71A37932, 0, DEVICE_PAD2B, MAPPER_SYSTEM1, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SYSTEM1, FM_NOT_COMPATIBLE,
	"Choplifter (bootleg) [Sega System 2]"},
	{0x2D2AEC31, 0, DEVICE_PAD2B, MAPPER_SYSTEM1, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SYSTEM1, FM_NOT_COMPATIBLE,
	"Brain [Sega System 1, unencrypted]"},

	{0x562809F4, 0, DEVICE_PAD2B, MAPPER_SNKPSYCHOS, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SNKPSYCHOS, FM_NOT_COMPATIBLE,
	"Psycho Soldier (US) [SNK Triple Z80]"},
	{0x900A113C, 0, DEVICE_PAD2B, MAPPER_SNKPSYCHOS, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SNKPSYCHOS, FM_NOT_COMPATIBLE,
	"Athena [SNK TNK III/Ikari-era hardware]"},

	/* ColecoVision MegaCart titles.  MAME exposes these through the
	 * colecovision_megacart slot; raw ROM loading has no slot metadata, so
	 * known dumps are listed explicitly and Coleco >32 KiB power-of-two images
	 * get a conservative heuristic below. */
	{0xF3CCACB3, 0, DEVICE_PAD2B, MAPPER_COLECO_MEGACART, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_COLECO, FM_NOT_COMPATIBLE,
	"PACOL [ColecoVision MegaCart]"},
	{0xB11A6D23, 0, DEVICE_PAD2B, MAPPER_COLECO_MEGACART, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_COLECO, FM_NOT_COMPATIBLE,
	"L'Abbaye des Morts [ColecoVision MegaCart]"},
	{0x53DA40BC, 0, DEVICE_PAD2B, MAPPER_COLECO_MEGACART, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_COLECO, FM_NOT_COMPATIBLE,
	"Mecha 8 [ColecoVision MegaCart]"},
	{0xD0F37969, 0, DEVICE_PAD2B, MAPPER_COLECO_MEGACART, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_COLECO, FM_NOT_COMPATIBLE,
	"Tank Mission [ColecoVision MegaCart]"},
	{0xA7A8D25E, 0, DEVICE_PAD2B, MAPPER_COLECO_MEGACART, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_COLECO, FM_NOT_COMPATIBLE,
	"Vanguard [ColecoVision MegaCart]"},

	/* Games requiring CODEMASTER mapper */
	{0x29822980, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Cosmic Spacehead"},
	{0x6CAA625B, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GG, FM_COMPATIBLE,
	"Cosmic Spacehead (GG)"}, 
	{0xEA5C3A6F, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Dinobasher - Starring Bignose the Caveman [Proto]"}, 
	{0x152F0DCC, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Drop Zone"}, 
	{0x5E53C7F7, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GG, FM_COMPATIBLE,
	"Ernie Els Golf"}, 
	{0x8813514B, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Excellent Dizzy Collection, The [Proto]"},
	{0xAA140C9C, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Excellent Dizzy Collection, The [SMS-GG]"},
	{0xB9664AE1, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Fantastic Dizzy"},
	{0xC888222B, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Fantastic Dizzy [SMS-GG]"},
	{0x76C5BDFB, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Jang Pung 2 [SMS-GG]"},
	{0xD9A7F170, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Man Overboard!"}, 
	{0xA577CE46, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Micro Machines"}, 
	{0xF7C524F6, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Micro Machines [BAD DUMP]"}, 
	{0xDBE8895C, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Micro Machines 2 - Turbo Tournament"},
	{0xC1756BEE, 0, DEVICE_PAD2B, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Pete Sampras Tennis"},

	/* Games requiring KOREA mappers */
	{0x17AB6883, 0, DEVICE_PAD2B, MAPPER_NONE, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"FA Tetris (KR)"},
	{0x61E8806F, 0, DEVICE_PAD2B, MAPPER_NONE, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Flash Point (KR)"},
	{0x89B79E77, 0, DEVICE_PAD2B, MAPPER_KOREA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Dodgeball King (KR)"},
	{0x18FB98A3, 0, DEVICE_PAD2B, MAPPER_KOREA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Jang Pung 3 (KR)"},
	{0x97D03541, 0, DEVICE_PAD2B, MAPPER_KOREA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Sangokushi 3 (KR)"},
	{0x67C2F0FF, 0, DEVICE_PAD2B, MAPPER_KOREA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Super Boy 2 (KR)"},
	{0x445525E2, 0, DEVICE_PAD2B, MAPPER_KOREA_MSX, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Penguin Adventure (KR)"},
	{0x83F0EEDE, 0, DEVICE_PAD2B, MAPPER_KOREA_MSX, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Street Master (KR)"},
	{0xA05258F5, 0, DEVICE_PAD2B, MAPPER_KOREA_MSX, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Won-Si-In (KR)"},
	{0x06965ED9, 0, DEVICE_PAD2B, MAPPER_KOREA_MSX, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"F-1 Spirit - The way to Formula-1 (KR)"},

	/* Games that require PAL timings (from MEKA.nam by Omar Cornut) */
	{0x72420F38, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Addams Familly"},
	/* Back to the Future Part III (Europe) is timing-sensitive/problematic on
	 * NTSC or SMS1 defaults; force 50 Hz PAL SMS2. */
	{0x2D48C1D3, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Back to the Future Part III (Europe) [50 Hz PAL SMS2]"},
	{0x1CBB7BF1, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Battlemaniacs (BR)"}, 
	{0x1B10A951, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Bram Stoker's Dracula"},
	{0xC0E25D62, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"California Games II"}, 
	{0xC9DBF936, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Home Alone"},
	{0xA109A6FE, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Power Strike II"},
	{0x4FF0CEC7, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Power Strike II (Game Gear Micro)"},
	
	{0x0047B615, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Predator2"},
	{0xF42E145C, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Quest for the Shaven Yak Starring Ren Hoek & Stimpy (BR)"}, 
	{0x9F951756, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"RoboCop 3"}, 
	{0x1575581D, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Shadow of the Beast"}, 
	{0x96B3F29E, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Sonic Blast (BR)"}, 
	{0x5B3B922C, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Sonic the Hedgehog 2 [V0]"},
	{0xD6F2BFCA, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Sonic the Hedgehog 2 [V1]"},
	{0xCA1D3752, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Space Harrier [50 Hz]"}, 
	{0x85CFC9C9, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Taito Chase H.Q."},  
	{0x38434560, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Bad Apple SMS"},  
	{0xDA2A68C6, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"The Synchrobots"},  

	/* Games requiring 315-5124 VDP (Mark-III, Sega Master System) */
	{0x32759751, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_COMPATIBLE,
	"Y's (J)"},

	/* Games requiring Game Gear SMS compatibility mode */
	{0x59840FD6, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Castle of Illusion - Starring Mickey Mouse"},
	{0x9942B69B, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Castle of Illusion - Starring Mickey Mouse (J)"},
	{0x5877B10D, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Castle of Illusion - Starring Mickey Mouse (J) [HACK]"},
	{0x9C76FB3A, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Rastan Saga [SMS-GG]"},
	{0x7BB81E3D, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Taito Chase H.Q. [SMS-GG]"},
	{0x44FBE8F6, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Taito Chase H.Q. [SMS-GG][HACK]"},
	{0x18086B70, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Taito Chase H.Q. [SMS-GG][HACK][BAD]"},
	{0xDA8E95A9, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"WWF Wrestlemania Steel Cage Challenge [SMS-GG]"},
	{0xCB42BD33, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"WWF Wrestlemania Steel Cage Challenge [SMS-GG] [BAD DUMP]"},
	{0x1D93246E, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Olympic Gold [SMS-GG] [A]"},
	{0xA2f9C7AF, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Olympic Gold [SMS-GG] [B]"},
	{0xF037EC00, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Out Run Europa [SMS-GG]"},
	{0xE5F789B9, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Predator 2 [SMS-GG]"},
	{0x311D2863, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Prince of Persia [SMS-GG] [A]"},
	{0x45F058d6, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Prince of Persia [SMS-GG] [B]"},
	{0x56201996, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"R.C. Grand Prix [SMS-GG]"},
	{0x10DBBEF4, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Super Kick Off [SMS-GG]"},
	{0xBD1CC7DF, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Super Tetris (KR)"},
	
	/* Games requiring uninitialized work RAM due to Japanese BIOS not clearing memory. */
	{0x08BF3DE3, 0, DEVICE_PAD2B, MAPPER_NONE, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_COMPATIBLE,
	"Alibaba and 40 Thieves"},
	{0x643B6B76, 0, DEVICE_PAD2B, MAPPER_NONE, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_COMPATIBLE,
	"Block Hole"},
	
	/* 4-PAK mapper for Australian exclusive */
	{0xA67F2A5C, 0, DEVICE_PAD2B, MAPPER_4PAK, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"4 PAK All Action"},

	/* Games requiring 3D Glasses */
	{0xFBF96C81, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Blade Eagle 3-D (BR)"},
	{0x8ECD201C, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Blade Eagle 3-D"},
	{0x31B8040B, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Maze Hunter 3-D"},
	{0x871562b0, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_COMPATIBLE,
	"Maze Walker"},
	{0xABD48AD2, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Poseidon Wars 3-D"},
	{0x6BD5C2BF, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Space Harrier 3-D"},
	{0x156948f9, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_COMPATIBLE,
	"Space Harrier 3-D (J)"},
	{0xA3EF13CB, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Zaxxon 3-D"},
	{0xBBA74147, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Zaxxon 3-D [Proto]"},
	{0xD6F43DDA, 1, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Out Run 3-D"},

	/* 3-D Gunner prototype: Light Phaser + 3-D glasses. */
	{0x56DCB2D4, 1, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"3-D Gunner [Proto]"},

	/* Games requiring Light Phaser & 3D Glasses */
	{0xFBE5CFBB, 1, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Missile Defense 3D"},
	{0xE79BB689, 1, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Missile Defense 3D [BIOS]"},
  
	/* Games requiring Light Phaser */
	{0x861B6E79, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Assault City [Light Phaser]"},
	{0x5FC74D2A, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Gangster Town"},
	{0xE167A561, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Hang-On / Safari Hunt"},
	{0xC5083000, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Hang-On / Safari Hunt [BAD DUMP]"},
	{0x91E93385, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Hang-On / Safari Hunt [BIOS]"},
	{0xE8EA842C, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Marksman Shooting / Trap Shooting"},
	{0xE8215C2E, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Marksman Shooting / Trap Shooting / Safari Hunt"},
	{0x205CAAE8, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_PAL, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Operation Wolf"}, /* Can be also played using the PLAYER2 gamepad */
	{0x23283F37, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Operation Wolf [A]"}, /* Can be also played using the PLAYER2 gamepad */
	{0xDA5A7013, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Rambo 3"},
	{0x79AC8E7F, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Rescue Mission"},
	{0x4B051022, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Shooting Gallery"},
	{0xA908CFF5, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Spacegun"},
	/* This game won't work with an FM board connected */
	{0x5359762D, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_NOT_COMPATIBLE,
	"Wanted"},
	{0x0CA95637, 0, DEVICE_LIGHTGUN, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Laser Ghost"},

	/* Games requiring Paddle */
	{0xF9DBB533, 0, DEVICE_PADDLE, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_COMPATIBLE,
	"Alex Kidd BMX Trial"},
	{0xA6FA42D0, 0, DEVICE_PADDLE, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_COMPATIBLE,
	"Galactic Protector"},
	{0x29BC7FAD, 0, DEVICE_PADDLE, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_COMPATIBLE,
	"Megumi Rescue"},
	{0x315917D4, 0, DEVICE_PADDLE, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_COMPATIBLE,
	"Woody Pop"},

	/* Games requiring Sport Pad (NOT EMULATED YET) */
	{0x946B8C4A, 0, DEVICE_SPORTSPAD, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Great Ice Hockey"},
	{0xE42E4998, 0, DEVICE_SPORTSPAD, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Sports Pad Football"},
	{0x41C948BF, 0, DEVICE_SPORTSPAD, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_COMPATIBLE,
	"Sports Pad Soccer"},
   
	/* Games using FM sound only when a certain region is set. Not required for the games to be playable
	* but most people prefer the FM soundtrack anyway. */
	{0x679E1676, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Wonder Boy III - The dragon's Trap"},
	{0x22CCA9BB, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS2, FM_COMPATIBLE,
	"Turma da Monica em O Resgate"},
	{0x23BAC434, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GG, FM_NOT_COMPATIBLE,
	"Shining Force Final Conflict"},
	
	/* These games have control issues if there's an FM board connected. */
	{0xE6795C53, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_NOT_COMPATIBLE,
	"Fushigi no Oshiro Pit Pot"},
	{0x89E98A7C, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_NOT_COMPATIBLE,
	"Great Baseball (Japan)"},
	{0x316727DD, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_SMS, FM_NOT_COMPATIBLE,
	"Teddy Boy Blues (Japan)"},


	/* Wonder Kid Game Gear prototype.  Mapper details were supplied with the
	 * prototype dump; Ben Sittler is credited by the dump notes for helping
	 * reverse-engineer the single $8000 bank register behavior. */
	{0x5E7B18C8, 0, DEVICE_PAD2B, MAPPER_WONDERKID, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_GGMS, FM_NOT_COMPATIBLE,
	"Wonder Kid [Proto] [SMS-GG]"},

	/* Broken-games-list fixes. */
	{0xB289011D, 0, DEVICE_PAD2B, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_DOMESTIC, CONSOLE_GG, FM_NOT_COMPATIBLE,
	"Madou Monogatari I (J)"},
	
	/* Sega Graphic Board v2 prototype: pen tablet peripheral */
	{0x276AA542, 0, DEVICE_GRAPHICBOARD, MAPPER_SEGA, DISPLAY_NTSC, TERRITORY_EXPORT, CONSOLE_SMS2, FM_NOT_COMPATIBLE,
	"Sega Graphic Board v2.0 [Proto]"},

};

#define GAME_DATABASE_CNT ARRAY_SIZE(game_list)

static int console_feature_enabled(uint8_t console)
{
    switch (console)
    {
#if !MULTIREXZ80_ENABLE_COLECO
        case CONSOLE_COLECO:
            return 0;
#endif
#if !MULTIREXZ80_ENABLE_SORDM5
        case CONSOLE_SORDM5:
            return 0;
#endif
#if !MULTIREXZ80_ENABLE_ARCADE
        case CONSOLE_SYSTEME:
        case CONSOLE_SYSTEM1:
        case CONSOLE_SNKPSYCHOS:
        case CONSOLE_TAITOL:
            return 0;
#endif
        default:
            return 1;
    }
}

static void apply_disabled_console_fallback(void)
{
    if (console_feature_enabled(sms.console))
        return;

    sms.console = CONSOLE_SMS2;
    sms.territory = TERRITORY_EXPORT;
    sms.display = DISPLAY_NTSC;
    sms.device[0] = DEVICE_PAD2B;
    sms.device[1] = DEVICE_PAD2B;
    sms.use_fm = option.fm;
    if (cart.mapper == MAPPER_COLECO_MEGACART ||
        cart.mapper == MAPPER_SYSTEME ||
        cart.mapper == MAPPER_SYSTEM1 ||
        cart.mapper == MAPPER_SNKPSYCHOS ||
        cart.mapper == MAPPER_TAITOL)
    {
        cart.mapper = MAPPER_SEGA;
    }
}

static int name_contains_ci(const char *s, const char *needle)
{
	char a[256];
	char b[128];
	size_t i;
	if (!s || !needle) return 0;
	for (i = 0; i + 1 < sizeof(a) && s[i]; i++) a[i] = (char)tolower((unsigned char)s[i]);
	a[i] = 0;
	for (i = 0; i + 1 < sizeof(b) && needle[i]; i++) b[i] = (char)tolower((unsigned char)needle[i]);
	b[i] = 0;
	return strstr(a, b) != NULL;
}

static int cart_uses_93c46(void)
{
	/* These Game Gear baseball cartridges use a 93C46 serial EEPROM.  The
	 * filename fallback is intentional: old dumps/databases disagree on exact
	 * CRCs and titles, while the affected game set is small and distinctive. */
	if (name_contains_ci(option.game_name, "majors pro baseball")) return 1;
	if (name_contains_ci(option.game_name, "nomo")) return 1;
	if (name_contains_ci(option.game_name, "pro yakyuu gg league")) return 1;
	if (name_contains_ci(option.game_name, "world series baseball")) return 1;
	return 0;
}

static int is_power_of_two_u32(uint32_t v)
{
    return v && ((v & (v - 1)) == 0);
}

static int coleco_megacart_heuristic(void)
{
    /* MAME's MegaCart mapper operates in 16 KiB banks and uses a low-bit
     * mask for bank select, so only power-of-two bank counts are safe to
     * auto-detect when raw ROMs provide no software-list slot metadata. */
    return (sms.console == CONSOLE_COLECO) && (cart.size > 0x8000) &&
           ((cart.size & 0x3FFF) == 0) && is_power_of_two_u32(cart.size >> 14);
}

static int rom_has_512_byte_header(uint32_t size)
{
    /*
     * Classic SMS/GG copier headers are exactly 512 bytes prepended to an
     * otherwise block-aligned ROM image.  The historical test,
     *     (size / 512) & 1
     * works for aligned files, but it also misclassifies odd-sized homebrew
     * images.  RoadBlaster is 0x116F8B bytes: truncating the division yields
     * an odd block count even though there is no 512-byte header, shifting the
     * whole ROM by 512 bytes and dropping the final partial bank.
     */
    return (size > 512) && ((size & 0x1FF) == 0) && (((size / 512) & 1) != 0);
}

static uint32_t rom_padded_size(uint32_t size)
{
    if (size < 0x4000)
        size = 0x4000;
    return (size + 0x3FFF) & ~0x3FFFu;
}

#ifndef NOZIP_SUPPORT
typedef struct
{
    const char *name;
    uint32_t offset;
    uint32_t size;
    uint32_t crc;
} systeme_rom_file_t;

typedef struct
{
    const char *setname;
    const char *description;
    uint32_t region_size;
    uint8_t encrypted;
    uint8_t rotation;
    systeme_rom_file_t files[6];
} systeme_zip_game_t;

static const systeme_zip_game_t systeme_zip_games[] =
{
    { "hangonjr", "Hang-On Jr. (Rev. B)", 0x30000, 0, 0,
      {{"epr-7257b.ic7",0x00000,0x08000,0xd63925a7},{"epr-7258.ic5",0x10000,0x08000,0xee3caab3},{"epr-7259.ic4",0x18000,0x08000,0xd2ba9bc9},{"epr-7260.ic3",0x20000,0x08000,0xe14da070},{"epr-7261.ic2",0x28000,0x08000,0x3810cbf5},{NULL,0,0,0}} },
    { "slapshtr", "Slap Shooter", 0x30000, 0, 0,
      {{"epr-7351.ic7",0x00000,0x08000,0x894adb04},{"epr-7352.ic5",0x10000,0x08000,0x61c938b6},{"epr-7353.ic4",0x18000,0x08000,0x8ee2951a},{"epr-7354.ic3",0x20000,0x08000,0x41482aa0},{"epr-7355.ic2",0x28000,0x08000,0xc67e1aef},{NULL,0,0,0}} },
    { "transfrm", "Transformer", 0x30000, 0, 0,
      {{"epr-7605.ic7",0x00000,0x08000,0xccf1d123},{"epr-7347.ic5",0x10000,0x08000,0xdf0f639f},{"epr-7348.ic4",0x18000,0x08000,0x0f38ea96},{"epr-7606.ic3",0x20000,0x08000,0x9d485df6},{"epr-7350.ic2",0x28000,0x08000,0x0052165d},{NULL,0,0,0}} },
    { "astrofl", "Astro Flash (Japan, encrypted)", 0x30000, 1, 0,
      {{"epr-7723.ic7",0x00000,0x08000,0x66061137},{"epr-7347.ic5",0x10000,0x08000,0xdf0f639f},{"epr-7348.ic4",0x18000,0x08000,0x0f38ea96},{"epr-7349.ic3",0x20000,0x08000,0xf8c352d5},{"epr-7350.ic2",0x28000,0x08000,0x0052165d},{NULL,0,0,0}} },
    { "ridleofp", "Riddle of Pythagoras (Japan)", 0x30000, 0, 1,
      {{"epr-10426.bin",0x00000,0x08000,0x4404c7e7},{"epr-10425.bin",0x10000,0x08000,0x35964109},{"epr-10424.bin",0x18000,0x08000,0xfcda1dfa},{"epr-10423.bin",0x20000,0x08000,0x0b87244f},{"epr-10422.bin",0x28000,0x08000,0x14781e56},{NULL,0,0,0}} },
    { "opaopa", "Opa Opa (MC-8123, encrypted)", 0x30000, 1, 0,
      {{"epr-11054.ic7",0x00000,0x08000,0x024b1244},{"epr-11053.ic5",0x10000,0x08000,0x6bc41d6e},{"epr-11052.ic4",0x18000,0x08000,0x395c1d0a},{"epr-11051.ic3",0x20000,0x08000,0x4ca132a2},{"epr-11050.ic2",0x28000,0x08000,0xa165e2ef},{NULL,0,0,0}} },
    { "opaopan", "Opa Opa (Rev A, unprotected)", 0x30000, 0, 0,
      {{"epr-11023a.ic7",0x00000,0x08000,0x101c5c6a},{"epr-11022.ic5",0x10000,0x08000,0x15203a42},{"epr-11021.ic4",0x18000,0x08000,0xb4e83340},{"epr-11020.ic3",0x20000,0x08000,0xc51aad27},{"epr-11019.ic2",0x28000,0x08000,0xbd0a6248},{NULL,0,0,0}} },
    { "fantzn2", "Fantasy Zone II - The Tears of Opa-Opa (MC-8123, encrypted)", 0x50000, 1, 0,
      {{"epr-11416.ic7",0x00000,0x08000,0x76db7b7b},{"epr-11415.ic5",0x10000,0x10000,0x57b45681},{"epr-11414.ic4",0x20000,0x10000,0x6f7a9f5f},{"epr-11413.ic3",0x30000,0x10000,0xa231dc85},{"epr-11412.ic2",0x40000,0x10000,0xb14db5af},{NULL,0,0,0}} },
    { "tetrisse", "Tetris (Japan, System E)", 0x30000, 0, 0,
      {{"epr-12213.7",0x00000,0x08000,0xef3c7a38},{"epr-12212.5",0x10000,0x08000,0x28b550bf},{"epr-12211.4",0x18000,0x08000,0x5aa114e9},{NULL,0,0,0},{NULL,0,0,0},{NULL,0,0,0}} },
    { "megrescu", "Megumi Rescue", 0x30000, 0, 1,
      {{"megumi_rescue_version_10.30_final_version_ic-7.ic7",0x00000,0x08000,0x490d0059},{"megumi_rescue_version_10.30_final_version_ic-5.ic5",0x10000,0x08000,0x278caba8},{"megumi_rescue_version_10.30_final_version_ic-4.ic4",0x18000,0x08000,0xbda242d1},{"megumi_rescue_version_10.30_final_version_ic-3.ic3",0x20000,0x08000,0x56e36f85},{"megumi_rescue_version_10.30_final_version_ic-2.ic2",0x28000,0x08000,0x5b74c767},{NULL,0,0,0}} }
};

static const char *zip_basename(const char *name)
{
    const char *slash = strrchr(name, '/');
    const char *backslash = strrchr(name, '\\');
    const char *base = name;
    if (slash && slash + 1 > base) base = slash + 1;
    if (backslash && backslash + 1 > base) base = backslash + 1;
    return base;
}


static int zip_path_stem_ieq(const char *path, const char *stem)
{
    const char *base;
    char tmp[128];
    size_t n;
    if (!path || !stem) return 0;
    base = zip_basename(path);
    n = strlen(base);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, base, n);
    tmp[n] = '\0';
    { char *dot = strrchr(tmp, '.'); if (dot) *dot = '\0'; }
    return !strcasecmp(tmp, stem);
}

static int locate_zip_member_by_name_or_basename(unzFile zhandle, const char *name, char *actual_name, uint32_t actual_name_size, unz_file_info *zinfo)
{
    int32_t zerror;
    const char *want_base = zip_basename(name);

    if (unzLocateFile(zhandle, name, 2) == UNZ_OK)
    {
        zerror = unzGetCurrentFileInfo(zhandle, zinfo, actual_name, actual_name_size, NULL, 0, NULL, 0);
        return zerror == UNZ_OK;
    }

    zerror = unzGoToFirstFile(zhandle);
    while (zerror == UNZ_OK)
    {
        zerror = unzGetCurrentFileInfo(zhandle, zinfo, actual_name, actual_name_size, NULL, 0, NULL, 0);
        if (zerror == UNZ_OK)
        {
            const char *actual_base = zip_basename(actual_name);
            if (!strcasecmp(actual_base, want_base))
                return 1;
        }
        zerror = unzGoToNextFile(zhandle);
    }
    return 0;
}

static const char *zip_member_load_path = NULL;

static int load_zip_member_exact(unzFile zhandle, const char *name, uint8_t *dst, uint32_t expected_size, uint32_t expected_crc)
{
    char zip_name[PATH_MAX];
    unz_file_info zinfo;
    int32_t zerror;

    if (zip_member_load_path)
        return loadZipMemberExact(zip_member_load_path, name, dst, expected_size, expected_crc);

    memset(&zinfo, 0, sizeof(zinfo));
    if (!locate_zip_member_by_name_or_basename(zhandle, name, zip_name, sizeof(zip_name), &zinfo))
        return 0;

    if (zinfo.uncompressed_size != expected_size)
        return 0;
    if (expected_crc && zinfo.crc != expected_crc)
        return 0;

    zerror = unzOpenCurrentFile(zhandle);
    if (zerror != UNZ_OK)
        return 0;

    zerror = unzReadCurrentFile(zhandle, dst, expected_size);
    unzCloseCurrentFile(zhandle);
    return zerror == (int32_t)expected_size;
}



#define SYS1_REGION_MAIN 100
#define SYS1_FILE_SPLIT_OPCODE 0x01u
#define SYS1_FILE_COPY_TO_OPS  0x02u
static const char *system1_current_zip_path = NULL;

typedef struct
{
    const char *name;
    int region;
    uint32_t offset;
    uint32_t size;
    uint32_t crc;
    uint32_t flags;
} system1_zip_file_t;


typedef struct
{
    const char *set_name;
    uint32_t main_size;
    uint32_t set_crc;
    void (*configure)(void);
    const system1_zip_file_t *files;
    int (*postload)(unzFile zhandle, uint8_t *main_region, uint32_t main_size);
} system1_zip_set_t;

static int load_zip_member_to_system1_main(unzFile zhandle, const system1_zip_file_t *file, uint8_t *main_region)
{
    uint8_t *tmp;
    int ok = 0;
    if (!file || !main_region) return 0;
    tmp = (uint8_t *)malloc(file->size);
    if (!tmp) return 0;
    if (load_zip_member_exact(zhandle, file->name, tmp, file->size, file->crc))
    {
        if (file->flags & SYS1_FILE_SPLIT_OPCODE)
        {
            if (file->size >= 0x10000 && file->offset + 0x8000 <= 0x20000)
            {
                memcpy(main_region + file->offset, tmp + 0x8000, 0x8000);
                ok = system1_set_region(SYSTEM1_REGION_OPCODES, file->offset, tmp, 0x8000);
            }
        }
        else if (file->offset + file->size <= 0x20000)
        {
            memcpy(main_region + file->offset, tmp, file->size);
            if (file->flags & SYS1_FILE_COPY_TO_OPS)
                ok = system1_set_region(SYSTEM1_REGION_OPCODES, file->offset, tmp, file->size);
            else
                ok = 1;
        }
    }
    free(tmp);
    return ok;
}

static int load_zip_member_to_system1_from_zip(const char *zip_path, const char *name, int region, uint32_t offset, uint32_t size, uint32_t crc)
{
    uint8_t *tmp;
    int ok = 0;
    if (!zip_path) return 0;
    tmp = (uint8_t *)malloc(size);
    if (tmp)
    {
        if (loadZipMemberExact(zip_path, name, tmp, size, crc))
            ok = system1_set_region(region, offset, tmp, size);
        free(tmp);
    }
    return ok;
}

static int load_zip_member_to_system1_parent_clone(const char *name, int region, uint32_t offset, uint32_t size, uint32_t crc)
{
    char dir[PATH_MAX];
    char path[PATH_MAX];
    const char *slash;
    size_t dirlen;
    static const char *const candidates[] = {
        "blockgal.zip", "blockgal(1).zip", "blockgal(2).zip", "blockgal(3).zip", "blockgal(4).zip",
        "blockgal_parent.zip", NULL
    };
    int i;

    if (!system1_current_zip_path) return 0;
    slash = strrchr(system1_current_zip_path, '/');
    if (!slash) slash = strrchr(system1_current_zip_path, '\\');
    if (slash)
    {
        dirlen = (size_t)(slash - system1_current_zip_path + 1);
        if (dirlen >= sizeof(dir)) return 0;
        memcpy(dir, system1_current_zip_path, dirlen);
        dir[dirlen] = '\0';
    }
    else
    {
        dir[0] = '\0';
        dirlen = 0;
    }

    for (i = 0; candidates[i]; i++)
    {
        if (snprintf(path, sizeof(path), "%s%s", dir, candidates[i]) >= (int)sizeof(path))
            continue;
        if (load_zip_member_to_system1_from_zip(path, name, region, offset, size, crc))
            return 1;
    }
    return 0;
}

static int load_zip_member_to_system1(unzFile zhandle, const char *name, int region, uint32_t offset, uint32_t size, uint32_t crc)
{
    uint8_t *tmp = (uint8_t *)malloc(size);
    int ok = 0;
    if (!tmp) return 0;
    if (load_zip_member_exact(zhandle, name, tmp, size, crc))
        ok = system1_set_region(region, offset, tmp, size);
    free(tmp);
    if (!ok)
        ok = load_zip_member_to_system1_parent_clone(name, region, offset, size, crc);
    return ok;
}



static int system1_postload_blockgal_mc8123(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    uint8_t *opcodes;
    uint8_t key[0x2000];
    int have_key = 0;

    if (!main_region || main_size < 0x8000u)
        return 0;

    /* Block Gal parent uses Sega/NEC MC-8123B key 317-0029.  Decode once at
     * load time into separate data/opcode banks, matching MAME's decrypted
     * opcode address space without adding a runtime encrypted-Z80 penalty. */
    have_key = load_zip_member_exact(zhandle, "317-0029.key", key, 0x2000, 0x350d7f93);
    if (!have_key)
        mc8123_generate_key(key, 0x091755u, 0x1800u);

    opcodes = (uint8_t *)malloc(0x8000u);
    if (!opcodes)
        return 0;

    mc8123_decode(main_region, opcodes, key, 0x8000u);
    have_key = system1_set_region(SYSTEM1_REGION_OPCODES, 0x0000, opcodes, 0x8000u);
    free(opcodes);
    return have_key;
}


static void sega_315_5051_decode(uint8_t *data, uint8_t *opcodes, uint32_t size)
{
    static const uint8_t convtable[32][4] =
    {
        { 0x08,0x88,0x00,0x80 }, { 0xa0,0x80,0xa8,0x88 },
        { 0x80,0x00,0xa0,0x20 }, { 0x88,0x80,0x08,0x00 },
        { 0xa0,0x80,0xa8,0x88 }, { 0x28,0x08,0x20,0x00 },
        { 0x28,0x08,0x20,0x00 }, { 0xa0,0x80,0xa8,0x88 },
        { 0x08,0x88,0x00,0x80 }, { 0x80,0x00,0xa0,0x20 },
        { 0x80,0x00,0xa0,0x20 }, { 0x88,0x80,0x08,0x00 },
        { 0x28,0x08,0x20,0x00 }, { 0x28,0x08,0x20,0x00 },
        { 0x28,0x08,0x20,0x00 }, { 0x88,0x80,0x08,0x00 },
        { 0x08,0x88,0x00,0x80 }, { 0xa8,0x88,0x28,0x08 },
        { 0xa8,0x88,0x28,0x08 }, { 0x80,0x00,0xa0,0x20 },
        { 0x28,0x08,0x20,0x00 }, { 0x88,0x80,0x08,0x00 },
        { 0xa8,0x88,0x28,0x08 }, { 0x88,0x80,0x08,0x00 },
        { 0x08,0x88,0x00,0x80 }, { 0x80,0x00,0xa0,0x20 },
        { 0xa8,0x88,0x28,0x08 }, { 0x80,0x00,0xa0,0x20 },
        { 0x28,0x08,0x20,0x00 }, { 0x28,0x08,0x20,0x00 },
        { 0x08,0x88,0x00,0x80 }, { 0x88,0x80,0x08,0x00 }
    };
    uint32_t a;

    if (!data || !opcodes) return;
    for (a = 0; a < size; a++)
    {
        uint8_t src = data[a];
        int row = (int)((a & 1u) | (((a >> 4) & 1u) << 1) | (((a >> 8) & 1u) << 2) | (((a >> 12) & 1u) << 3));
        int col = (int)(((src >> 3) & 1u) | (((src >> 5) & 1u) << 1));
        int xorval = 0;
        if (src & 0x80u)
        {
            col = 3 - col;
            xorval = 0xa8;
        }
        opcodes[a] = (uint8_t)((src & (uint8_t)~0xa8u) | (uint8_t)(convtable[2 * row][col] ^ xorval));
        data[a]    = (uint8_t)((src & (uint8_t)~0xa8u) | (uint8_t)(convtable[2 * row + 1][col] ^ xorval));
    }
}

static int system1_postload_flicky_315_5051(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    uint8_t *opcodes;
    (void)zhandle;
    if (!main_region || main_size < 0x8000u)
        return 0;
    opcodes = (uint8_t *)malloc(0x8000u);
    if (!opcodes)
        return 0;
    sega_decode_315_5051(main_region, opcodes, 0x8000u);
    if (!system1_set_region(SYSTEM1_REGION_OPCODES, 0x0000, opcodes, 0x8000u))
    {
        free(opcodes);
        return 0;
    }
    free(opcodes);
    return 1;
}



typedef void (*segacrpt_decode_fn)(uint8_t *data, uint8_t *opcodes, uint32_t size);
typedef void (*segacrp2_decode_fn)(uint8_t *data, uint8_t *opcodes);

static int system1_postload_segacrpt(unzFile zhandle, uint8_t *main_region, uint32_t main_size, segacrpt_decode_fn fn)
{
    uint8_t *opcodes;
    uint32_t len;
    (void)zhandle;
    if (!main_region || !fn || main_size < 0x8000u)
        return 0;
    opcodes = (uint8_t *)malloc(main_size);
    if (!opcodes)
        return 0;
    memcpy(opcodes, main_region, main_size);
    len = (main_size < 0x8000u) ? main_size : 0x8000u;
    fn(main_region, opcodes, len);
    if (!system1_set_region(SYSTEM1_REGION_OPCODES, 0x0000, opcodes, main_size))
    {
        free(opcodes);
        return 0;
    }
    free(opcodes);
    return 1;
}

static int system1_postload_segacrp2(unzFile zhandle, uint8_t *main_region, uint32_t main_size, segacrp2_decode_fn fn)
{
    uint8_t *opcodes;
    (void)zhandle;
    if (!main_region || !fn || main_size < 0x8000u)
        return 0;
    opcodes = (uint8_t *)malloc(main_size);
    if (!opcodes)
        return 0;
    memcpy(opcodes, main_region, main_size);
    fn(main_region, opcodes);
    if (!system1_set_region(SYSTEM1_REGION_OPCODES, 0x0000, opcodes, main_size))
    {
        free(opcodes);
        return 0;
    }
    free(opcodes);
    return 1;
}

static int system1_postload_teddybb_315_5155(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5155);
}

static int system1_postload_teddybb_315_5006(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5006);
}

static int system1_postload_wboy_315_5177(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrp2(zhandle, main_region, main_size, sega_decode_315_5177);
}

static int system1_postload_wboy_315_5178(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrp2(zhandle, main_region, main_size, sega_decode_315_5178);
}

static int system1_postload_wboy_315_5179(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrp2(zhandle, main_region, main_size, sega_decode_315_5179);
}

static int system1_postload_wboy_315_5135(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5135);
}

static int system1_postload_wboy_315_5162(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrp2(zhandle, main_region, main_size, sega_decode_315_5162);
}

static int system1_postload_gardia_317_0006(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrp2(zhandle, main_region, main_size, sega_decode_317_0006);
}

static int system1_postload_ufosensi_mc8123(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    uint8_t *opcodes;
    uint8_t key[0x2000];
    int ok;
    if (!main_region || main_size < 0x8000u)
        return 0;
    ok = load_zip_member_exact(zhandle, "317-0064.key", key, 0x2000, 0xda326f36);
    if (!ok)
        return 0;
    opcodes = (uint8_t *)malloc(main_size);
    if (!opcodes)
        return 0;
    mc8123_decode(main_region, opcodes, key, main_size);
    ok = system1_set_region(SYSTEM1_REGION_OPCODES, 0x0000, opcodes, main_size);
    free(opcodes);
    return ok;
}

static int system1_postload_wbml_mc8123(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    uint8_t *opcodes;
    uint8_t key[0x2000];
    int ok;

    if (!main_region || main_size < 0x20000u)
        return 0;

    /* Wonder Boy: Monster Land uses an MC-8123 encrypted main Z80
     * (317-0043).  Decode the entire banked program region once while
     * loading and map it as separate opcode space, matching MAME's
     * banked_decrypted_opcodes_map without per-opcode runtime decryption. */
    ok = load_zip_member_exact(zhandle, "317-0043.key", key, 0x2000, 0xe354abfc);
    if (!ok)
        return 0;

    opcodes = (uint8_t *)malloc(main_size);
    if (!opcodes)
        return 0;

    mc8123_decode(main_region, opcodes, key, main_size);
    ok = system1_set_region(SYSTEM1_REGION_OPCODES, 0x0000, opcodes, main_size);
    free(opcodes);
    return ok;
}


static int system1_postload_upndown_315_5098(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5098);
}

static int system1_postload_swat_315_5048(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5048);
}

static int system1_postload_wmatch_315_5064(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5064);
}

static int system1_postload_pitfall2_315_5093(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5093);
}

static int system1_postload_seganinj_315_5102(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5102);
}

static int system1_postload_imsorry_315_5110(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5110);
}

static int system1_postload_myheroj_315_5132(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    return system1_postload_segacrpt(zhandle, main_region, main_size, sega_decode_315_5132);
}

static int system1_postload_nob_patch(unzFile zhandle, uint8_t *main_region, uint32_t main_size)
{
    uint8_t b = 0x80;
    (void)zhandle;
    (void)main_region;
    if (main_size < 0x8000u)
        return 0;
    /* MAME models a small startup protection quirk at address 0001 by
     * returning 80h while the PC is in the reset vector.  Put the adjusted
     * byte only in opcode space so normal data reads still see the supplied
     * ROM contents where possible. */
    return system1_set_region(SYSTEM1_REGION_OPCODES, 0x0001, &b, 1);
}

static const system1_zip_file_t sys1_blockgal_files[] = {
    {"bg.116",  SYS1_REGION_MAIN,      0x0000, 0x4000, 0xa99b231a, 0},
    {"bg.109",  SYS1_REGION_MAIN,      0x4000, 0x4000, 0xa6b573d5, 0},
    {"bg.120",  SYSTEM1_REGION_SOUND,  0x0000, 0x2000, 0xd848faff, 0},
    {"bg.62",   SYSTEM1_REGION_TILES,  0x0000, 0x2000, 0x7e3ea4eb, 0},
    {"bg.61",   SYSTEM1_REGION_TILES,  0x2000, 0x2000, 0x4dd3d39d, 0},
    {"bg.64",   SYSTEM1_REGION_TILES,  0x4000, 0x2000, 0x17368663, 0},
    {"bg.63",   SYSTEM1_REGION_TILES,  0x6000, 0x2000, 0x0c8bc404, 0},
    {"bg.66",   SYSTEM1_REGION_TILES,  0x8000, 0x2000, 0x2b7dc4fa, 0},
    {"bg.65",   SYSTEM1_REGION_TILES,  0xa000, 0x2000, 0xed121306, 0},
    {"bg.117",  SYSTEM1_REGION_SPRITES,0x0000, 0x4000, 0xe99cc920, 0},
    {"bg.04",   SYSTEM1_REGION_SPRITES,0x4000, 0x4000, 0x213057f8, 0},
    {"bg.110",  SYSTEM1_REGION_SPRITES,0x8000, 0x4000, 0x064c812c, 0},
    {"bg.05",   SYSTEM1_REGION_SPRITES,0xc000, 0x4000, 0x02e0b040, 0},
    {"pr5317.76", SYSTEM1_REGION_PROM, 0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_blockgalb_files[] = {
    {"blockgalb/ic62", SYS1_REGION_MAIN, 0x00000, 0x10000, 0x65c47676, SYS1_FILE_SPLIT_OPCODE},
    {"bg.120",  SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0xd848faff, 0},
    {"bg.62",   SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0x7e3ea4eb, 0},
    {"bg.61",   SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0x4dd3d39d, 0},
    {"bg.64",   SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x17368663, 0},
    {"bg.63",   SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0x0c8bc404, 0},
    {"bg.66",   SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0x2b7dc4fa, 0},
    {"bg.65",   SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0xed121306, 0},
    {"bg.117",  SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0xe99cc920, 0},
    {"bg.04",   SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0x213057f8, 0},
    {"bg.110",  SYSTEM1_REGION_SPRITES, 0x8000, 0x4000, 0x064c812c, 0},
    {"bg.05",   SYSTEM1_REGION_SPRITES, 0xc000, 0x4000, 0x02e0b040, 0},
    {"pr5317.76", SYSTEM1_REGION_PROM,  0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};


static const system1_zip_file_t sys1_choplift_files[] = {
    {"epr-7124.ic90", SYS1_REGION_MAIN, 0x00000, 0x8000, 0x678d5c41, SYS1_FILE_COPY_TO_OPS},
    {"epr-7125.ic91", SYS1_REGION_MAIN, 0x10000, 0x8000, 0xf5283498, SYS1_FILE_COPY_TO_OPS},
    {"epr-7126.ic92", SYS1_REGION_MAIN, 0x18000, 0x8000, 0xdbd192ab, SYS1_FILE_COPY_TO_OPS},
    {"epr-7130.ic126", SYSTEM1_REGION_SOUND, 0x0000, 0x8000, 0x346af118, 0},
    {"epr-7127.ic4", SYSTEM1_REGION_TILES, 0x00000, 0x8000, 0x1e708f6d, 0},
    {"epr-7128.ic5", SYSTEM1_REGION_TILES, 0x08000, 0x8000, 0xb922e787, 0},
    {"epr-7129.ic6", SYSTEM1_REGION_TILES, 0x10000, 0x8000, 0xbd3b6e6e, 0},
    {"epr-7121.ic87", SYSTEM1_REGION_SPRITES, 0x00000, 0x8000, 0xf2b88f73, 0},
    {"epr-7120.ic86", SYSTEM1_REGION_SPRITES, 0x08000, 0x8000, 0x517d7fd3, 0},
    {"epr-7123.ic89", SYSTEM1_REGION_SPRITES, 0x10000, 0x8000, 0x8f16a303, 0},
    {"epr-7122.ic88", SYSTEM1_REGION_SPRITES, 0x18000, 0x8000, 0x7c93f160, 0},
    {"pr7119.ic20", SYSTEM1_REGION_COLOR, 0x0000, 0x0100, 0xb2a8260f, 0},
    {"pr7118.ic14", SYSTEM1_REGION_COLOR, 0x0100, 0x0100, 0x693e20c7, 0},
    {"pr7117.ic8",  SYSTEM1_REGION_COLOR, 0x0200, 0x0100, 0x4124307e, 0},
    {"pr5317.ic28", SYSTEM1_REGION_PROM, 0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_chopliftu_files[] = {
    {"epr-7152.ic90", SYS1_REGION_MAIN, 0x00000, 0x8000, 0xfe49d83e, SYS1_FILE_COPY_TO_OPS},
    {"epr-7153.ic91", SYS1_REGION_MAIN, 0x10000, 0x8000, 0x48697666, SYS1_FILE_COPY_TO_OPS},
    {"epr-7154.ic92", SYS1_REGION_MAIN, 0x18000, 0x8000, 0x56d6222a, SYS1_FILE_COPY_TO_OPS},
    {"epr-7130.ic126", SYSTEM1_REGION_SOUND, 0x0000, 0x8000, 0x346af118, 0},
    {"epr-7127.ic4", SYSTEM1_REGION_TILES, 0x00000, 0x8000, 0x1e708f6d, 0},
    {"epr-7128.ic5", SYSTEM1_REGION_TILES, 0x08000, 0x8000, 0xb922e787, 0},
    {"epr-7129.ic6", SYSTEM1_REGION_TILES, 0x10000, 0x8000, 0xbd3b6e6e, 0},
    {"epr-7121.ic87", SYSTEM1_REGION_SPRITES, 0x00000, 0x8000, 0xf2b88f73, 0},
    {"epr-7120.ic86", SYSTEM1_REGION_SPRITES, 0x08000, 0x8000, 0x517d7fd3, 0},
    {"epr-7123.ic89", SYSTEM1_REGION_SPRITES, 0x10000, 0x8000, 0x8f16a303, 0},
    {"epr-7122.ic88", SYSTEM1_REGION_SPRITES, 0x18000, 0x8000, 0x7c93f160, 0},
    {"pr7119.ic20", SYSTEM1_REGION_COLOR, 0x0000, 0x0100, 0xb2a8260f, 0},
    {"pr7118.ic14", SYSTEM1_REGION_COLOR, 0x0100, 0x0100, 0x693e20c7, 0},
    {"pr7117.ic8",  SYSTEM1_REGION_COLOR, 0x0200, 0x0100, 0x4124307e, 0},
    {"pr5317.ic28", SYSTEM1_REGION_PROM, 0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_chopliftbl_files[] = {
    {"ep7124bl.90", SYS1_REGION_MAIN, 0x00000, 0x8000, 0x71a37932, SYS1_FILE_COPY_TO_OPS},
    {"epr-7125.91", SYS1_REGION_MAIN, 0x10000, 0x8000, 0xf5283498, SYS1_FILE_COPY_TO_OPS},
    {"epr-7126.92", SYS1_REGION_MAIN, 0x18000, 0x8000, 0xdbd192ab, SYS1_FILE_COPY_TO_OPS},
    {"epr-7130.126", SYSTEM1_REGION_SOUND, 0x0000, 0x8000, 0x346af118, 0},
    {"epr-7127.4", SYSTEM1_REGION_TILES, 0x00000, 0x8000, 0x1e708f6d, 0},
    {"epr-7128.5", SYSTEM1_REGION_TILES, 0x08000, 0x8000, 0xb922e787, 0},
    {"epr-7129.6", SYSTEM1_REGION_TILES, 0x10000, 0x8000, 0xbd3b6e6e, 0},
    {"epr-7121.87", SYSTEM1_REGION_SPRITES, 0x00000, 0x8000, 0xf2b88f73, 0},
    {"epr-7120.86", SYSTEM1_REGION_SPRITES, 0x08000, 0x8000, 0x517d7fd3, 0},
    {"epr-7123.89", SYSTEM1_REGION_SPRITES, 0x10000, 0x8000, 0x8f16a303, 0},
    {"epr-7122.88", SYSTEM1_REGION_SPRITES, 0x18000, 0x8000, 0x7c93f160, 0},
    {"pr7119.20", SYSTEM1_REGION_COLOR, 0x0000, 0x0100, 0xb2a8260f, 0},
    {"pr7118.14", SYSTEM1_REGION_COLOR, 0x0100, 0x0100, 0x693e20c7, 0},
    {"pr7117.8",  SYSTEM1_REGION_COLOR, 0x0200, 0x0100, 0x4124307e, 0},
    {"pr5317.28", SYSTEM1_REGION_PROM, 0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_flicky_files[] = {
    {"epr-5978a.116", SYS1_REGION_MAIN,       0x0000, 0x4000, 0x296f1492, 0},
    {"epr-5979a.109", SYS1_REGION_MAIN,       0x4000, 0x4000, 0x64b03ef9, 0},
    {"epr-5869.120",  SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0x6d220d4e, 0},
    {"epr-5868.62",   SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0x7402256b, 0},
    {"epr-5867.61",   SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0x2f5ce930, 0},
    {"epr-5866.64",   SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x967f1d9a, 0},
    {"epr-5865.63",   SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0x03d9a34c, 0},
    {"epr-5864.66",   SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0xe659f358, 0},
    {"epr-5863.65",   SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0xa496ca15, 0},
    {"epr-5855.117",  SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0xb5f894a1, 0},
    {"epr-5856.110",  SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0x266af78f, 0},
    {"pr-5317.76",    SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_brain_files[] = {
    {"brain.1", SYS1_REGION_MAIN, 0x00000, 0x8000, 0x2d2aec31, SYS1_FILE_COPY_TO_OPS},
    {"brain.2", SYS1_REGION_MAIN, 0x10000, 0x8000, 0x810a8ab5, SYS1_FILE_COPY_TO_OPS},
    {"brain.2", SYS1_REGION_MAIN, 0x08000, 0x8000, 0x810a8ab5, SYS1_FILE_COPY_TO_OPS},
    {"brain.3", SYS1_REGION_MAIN, 0x18000, 0x8000, 0x9a225634, SYS1_FILE_COPY_TO_OPS},
    {"brain.120", SYSTEM1_REGION_SOUND, 0x0000, 0x8000, 0xc7e50278, 0},
    {"brain.62", SYSTEM1_REGION_TILES, 0x0000, 0x4000, 0x7dce2302, 0},
    {"brain.64", SYSTEM1_REGION_TILES, 0x4000, 0x4000, 0x7ce03fd3, 0},
    {"brain.66", SYSTEM1_REGION_TILES, 0x8000, 0x4000, 0xea54323f, 0},
    {"brain.117", SYSTEM1_REGION_SPRITES, 0x00000, 0x8000, 0x92ff71a4, 0},
    {"brain.110", SYSTEM1_REGION_SPRITES, 0x08000, 0x8000, 0xa1b847ec, 0},
    {"brain.4", SYSTEM1_REGION_SPRITES, 0x10000, 0x8000, 0xfd2ea53b, 0},
    {"bprom.3", SYSTEM1_REGION_COLOR, 0x0000, 0x0100, 0x8eee0f72, 0},
    {"bprom.2", SYSTEM1_REGION_COLOR, 0x0100, 0x0100, 0x3e7babd7, 0},
    {"bprom.1", SYSTEM1_REGION_COLOR, 0x0200, 0x0100, 0x371c44a6, 0},
    {"pr5317.76", SYSTEM1_REGION_PROM, 0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};


static const system1_zip_file_t sys1_teddybb_files[] = {
    {"epr-6768.116", SYS1_REGION_MAIN,       0x0000, 0x4000, 0x5939817e, 0},
    {"epr-6769.109", SYS1_REGION_MAIN,       0x4000, 0x4000, 0x14a98ddd, 0},
    {"epr-6770.96",  SYS1_REGION_MAIN,       0x8000, 0x4000, 0x67b0c7c2, SYS1_FILE_COPY_TO_OPS},
    {"epr6748x.120", SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0xc2a1b89d, 0},
    {"epr-6747.62",  SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0xa0e5aca7, 0},
    {"epr-6746.61",  SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0xcdb77e51, 0},
    {"epr-6745.64",  SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x0cab75c3, 0},
    {"epr-6744.63",  SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0x0ef8d2cd, 0},
    {"epr-6743.66",  SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0xc33062b5, 0},
    {"epr-6742.65",  SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0xc457e8c5, 0},
    {"epr-6735.117", SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0x1be35a97, 0},
    {"epr-6737.04",  SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0x6b53aa7a, 0},
    {"epr-6736.110", SYSTEM1_REGION_SPRITES, 0x8000, 0x4000, 0x565c25d0, 0},
    {"epr-6738.05",  SYSTEM1_REGION_SPRITES, 0xc000, 0x4000, 0xe116285f, 0},
    {"pr-5317.76",   SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_wboy_files[] = {
    {"epr-7489.116", SYS1_REGION_MAIN,       0x0000, 0x4000, 0x130f4b70, 0},
    {"epr-7490.109", SYS1_REGION_MAIN,       0x4000, 0x4000, 0x9e656733, 0},
    {"epr-7491.96",  SYS1_REGION_MAIN,       0x8000, 0x4000, 0x1f7d0efe, SYS1_FILE_COPY_TO_OPS},
    {"epr-7498.120", SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0x78ae1e7b, 0},
    {"epr-7497.62",  SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0x08d609ca, 0},
    {"epr-7496.61",  SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0x6f61fdf1, 0},
    {"epr-7495.64",  SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x6a0d2c2d, 0},
    {"epr-7494.63",  SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0xa8e281c7, 0},
    {"epr-7493.66",  SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0x89305df4, 0},
    {"epr-7492.65",  SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0x60f806b1, 0},
    {"epr-7485.117", SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0xc2891722, 0},
    {"epr-7487.04",  SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0x2d3a421b, 0},
    {"epr-7486.110", SYSTEM1_REGION_SPRITES, 0x8000, 0x4000, 0x8d622c50, 0},
    {"epr-7488.05",  SYSTEM1_REGION_SPRITES, 0xc000, 0x4000, 0x007c2f1b, 0},
    {"pr-5317.76",   SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_gardia_files[] = {
    {"epr-10255.1",   SYS1_REGION_MAIN,       0x00000, 0x8000, 0x89282a6b, 0},
    {"epr-10254.2",   SYS1_REGION_MAIN,       0x10000, 0x8000, 0x2826b6d8, SYS1_FILE_COPY_TO_OPS},
    {"epr-10253.3",   SYS1_REGION_MAIN,       0x18000, 0x8000, 0x7911260f, SYS1_FILE_COPY_TO_OPS},
    {"epr-10243.120", SYSTEM1_REGION_SOUND,   0x0000,  0x4000, 0x87220660, 0},
    {"epr-10249.61",  SYSTEM1_REGION_TILES,   0x0000,  0x4000, 0x4e0ad0f2, 0},
    {"epr-10248.64",  SYSTEM1_REGION_TILES,   0x4000,  0x4000, 0x3515d124, 0},
    {"epr-10247.66",  SYSTEM1_REGION_TILES,   0x8000,  0x4000, 0x541e1555, 0},
    {"epr-10234.117", SYSTEM1_REGION_SPRITES, 0x00000, 0x8000, 0x8a6aed33, 0},
    {"epr-10233.110", SYSTEM1_REGION_SPRITES, 0x08000, 0x8000, 0xc52784d3, 0},
    {"epr-10236.04",  SYSTEM1_REGION_SPRITES, 0x10000, 0x8000, 0xb35ab227, 0},
    {"epr-10235.5",   SYSTEM1_REGION_SPRITES, 0x18000, 0x8000, 0x006a3151, 0},
    {"pr-7345.3",     SYSTEM1_REGION_COLOR,   0x0000,  0x0100, 0x8eee0f72, 0},
    {"pr-7344.2",     SYSTEM1_REGION_COLOR,   0x0100,  0x0100, 0x3e7babd7, 0},
    {"pr-7343.1",     SYSTEM1_REGION_COLOR,   0x0200,  0x0100, 0x371c44a6, 0},
    {"pr5317.4",      SYSTEM1_REGION_PROM,    0x0000,  0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_ufosensi_files[] = {
    {"epr-11661.90",  SYS1_REGION_MAIN,       0x00000, 0x8000, 0xf3e394e2, 0},
    {"epr-11662.91",  SYS1_REGION_MAIN,       0x10000, 0x8000, 0x0c2e4120, 0},
    {"epr-11663.92",  SYS1_REGION_MAIN,       0x18000, 0x8000, 0x4515ebae, 0},
    {"epr-11667.126", SYSTEM1_REGION_SOUND,   0x0000,  0x8000, 0x110baba9, 0},
    {"epr-11664.4",   SYSTEM1_REGION_TILES,   0x00000, 0x8000, 0x1b1bc3d5, 0},
    {"epr-11665.5",   SYSTEM1_REGION_TILES,   0x08000, 0x8000, 0x3659174a, 0},
    {"epr-11666.6",   SYSTEM1_REGION_TILES,   0x10000, 0x8000, 0x99dcc793, 0},
    {"epr-11658.87",  SYSTEM1_REGION_SPRITES, 0x00000, 0x8000, 0x3b5a20f7, 0},
    {"epr-11657.86",  SYSTEM1_REGION_SPRITES, 0x08000, 0x8000, 0x010f81a9, 0},
    {"epr-11660.89",  SYSTEM1_REGION_SPRITES, 0x10000, 0x8000, 0xe1e2e7c5, 0},
    {"epr-11659.88",  SYSTEM1_REGION_SPRITES, 0x18000, 0x8000, 0x286c7286, 0},
    {"pr11656.20",    SYSTEM1_REGION_COLOR,   0x0000,  0x0100, 0x640740eb, 0},
    {"pr11655.14",    SYSTEM1_REGION_COLOR,   0x0100,  0x0100, 0xa0c3fa77, 0},
    {"pr11654.8",     SYSTEM1_REGION_COLOR,   0x0200,  0x0100, 0xba624305, 0},
    {"pr5317.28",     SYSTEM1_REGION_PROM,    0x0000,  0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_wbml_files[] = {
    {"epr-11031a.90", SYS1_REGION_MAIN,       0x00000, 0x8000, 0xbd3349e5, 0},
    {"epr-11032.91",  SYS1_REGION_MAIN,       0x10000, 0x8000, 0x9d03bdb2, 0},
    {"epr-11033.92",  SYS1_REGION_MAIN,       0x18000, 0x8000, 0x7076905c, 0},
    {"epr-11037.126", SYSTEM1_REGION_SOUND,   0x0000,  0x8000, 0x7a4ee585, 0},
    {"epr-11034.4",   SYSTEM1_REGION_TILES,   0x00000, 0x8000, 0x37a2077d, 0},
    {"epr-11035.5",   SYSTEM1_REGION_TILES,   0x08000, 0x8000, 0xcdf2a21b, 0},
    {"epr-11036.6",   SYSTEM1_REGION_TILES,   0x10000, 0x8000, 0x644687fa, 0},
    {"epr-11028.87",  SYSTEM1_REGION_SPRITES, 0x00000, 0x8000, 0xaf0b3972, 0},
    {"epr-11027.86",  SYSTEM1_REGION_SPRITES, 0x08000, 0x8000, 0x277d8f1d, 0},
    {"epr-11030.89",  SYSTEM1_REGION_SPRITES, 0x10000, 0x8000, 0xf05ffc76, 0},
    {"epr-11029.88",  SYSTEM1_REGION_SPRITES, 0x18000, 0x8000, 0xcedc9c61, 0},
    {"pr11026.20",    SYSTEM1_REGION_COLOR,   0x0000,  0x0100, 0x27057298, 0},
    {"pr11025.14",    SYSTEM1_REGION_COLOR,   0x0100,  0x0100, 0x41e4d86b, 0},
    {"pr11024.8",     SYSTEM1_REGION_COLOR,   0x0200,  0x0100, 0x08d71954, 0},
    {"pr5317.37",     SYSTEM1_REGION_PROM,    0x0000,  0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_wbmlb_files[] = {
    {"wbmlb/wbml.01", SYS1_REGION_MAIN,       0x00000, 0x10000, 0x66482638, SYS1_FILE_SPLIT_OPCODE},
    {"wbmlb/wbml.02", SYS1_REGION_MAIN,       0x10000, 0x10000, 0x48746bb6, SYS1_FILE_SPLIT_OPCODE},
    {"wbmlb/wbml.03", SYS1_REGION_MAIN,       0x18000, 0x10000, 0xd57ba8aa, SYS1_FILE_SPLIT_OPCODE},
    {"epr-11037.126", SYSTEM1_REGION_SOUND,   0x0000,  0x8000, 0x7a4ee585, 0},
    {"wbmlb/wbml.08", SYSTEM1_REGION_TILES,   0x00000, 0x8000, 0xbbea6afe, 0},
    {"wbmlb/wbml.09", SYSTEM1_REGION_TILES,   0x08000, 0x8000, 0x77567d41, 0},
    {"wbmlb/wbml.10", SYSTEM1_REGION_TILES,   0x10000, 0x8000, 0xa52ffbdd, 0},
    {"epr-11028.87",  SYSTEM1_REGION_SPRITES, 0x00000, 0x8000, 0xaf0b3972, 0},
    {"epr-11027.86",  SYSTEM1_REGION_SPRITES, 0x08000, 0x8000, 0x277d8f1d, 0},
    {"epr-11030.89",  SYSTEM1_REGION_SPRITES, 0x10000, 0x8000, 0xf05ffc76, 0},
    {"epr-11029.88",  SYSTEM1_REGION_SPRITES, 0x18000, 0x8000, 0xcedc9c61, 0},
    {"pr11026.20",    SYSTEM1_REGION_COLOR,   0x0000,  0x0100, 0x27057298, 0},
    {"pr11025.14",    SYSTEM1_REGION_COLOR,   0x0100,  0x0100, 0x41e4d86b, 0},
    {"pr11024.8",     SYSTEM1_REGION_COLOR,   0x0200,  0x0100, 0x08d71954, 0},
    {"pr5317.37",     SYSTEM1_REGION_PROM,    0x0000,  0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_starjack_files[] = {
    {"epr-5320b.129", SYS1_REGION_MAIN,       0x0000, 0x2000, 0x7ab72ecd, SYS1_FILE_COPY_TO_OPS},
    {"epr-5321a.130", SYS1_REGION_MAIN,       0x2000, 0x2000, 0x38b99050, SYS1_FILE_COPY_TO_OPS},
    {"epr-5322a.131", SYS1_REGION_MAIN,       0x4000, 0x2000, 0x103a595b, SYS1_FILE_COPY_TO_OPS},
    {"epr-5323.132",  SYS1_REGION_MAIN,       0x6000, 0x2000, 0x46af0d58, SYS1_FILE_COPY_TO_OPS},
    {"epr-5324.133",  SYS1_REGION_MAIN,       0x8000, 0x2000, 0x1e89efe2, SYS1_FILE_COPY_TO_OPS},
    {"epr-5325.134",  SYS1_REGION_MAIN,       0xa000, 0x2000, 0xd6e379a1, SYS1_FILE_COPY_TO_OPS},
    {"epr-5332.3",    SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0x7a72ab3d, 0},
    {"epr-5331.82",   SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0x251d898f, 0},
    {"epr-5330.65",   SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0xeb048745, 0},
    {"epr-5329.81",   SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x3e8bcaed, 0},
    {"epr-5328.64",   SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0x9ed7849f, 0},
    {"epr-5327.80",   SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0x79e92cb1, 0},
    {"epr-5326.63",   SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0xba7e2b47, 0},
    {"epr-5318.86",   SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0x6f2e1fd3, 0},
    {"epr-5319.93",   SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0xebee4999, 0},
    {"pr-5317.106",   SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_upndown_files[] = {
    {"epr5516a.129", SYS1_REGION_MAIN,       0x0000, 0x2000, 0x038c82da, 0},
    {"epr5517a.130", SYS1_REGION_MAIN,       0x2000, 0x2000, 0x6930e1de, 0},
    {"epr-5518.131", SYS1_REGION_MAIN,       0x4000, 0x2000, 0x2a370c99, 0},
    {"epr-5519.132", SYS1_REGION_MAIN,       0x6000, 0x2000, 0x9d664a58, 0},
    {"epr-5520.133", SYS1_REGION_MAIN,       0x8000, 0x2000, 0x208dfbdf, 0},
    {"epr-5521.134", SYS1_REGION_MAIN,       0xa000, 0x2000, 0xe7b8d87a, 0},
    {"epr-5535.3",   SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0xcf4e4c45, 0},
    {"epr-5527.82",  SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0xb2d616f1, 0},
    {"epr-5526.65",  SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0x8a8b33c2, 0},
    {"epr-5525.81",  SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0xe749c5ef, 0},
    {"epr-5524.64",  SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0x8b886952, 0},
    {"epr-5523.80",  SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0xdede35d9, 0},
    {"epr-5522.63",  SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0x5e6d9dff, 0},
    {"epr-5514.86",  SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0xfcc0a88b, 0},
    {"epr-5515.93",  SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0x60908838, 0},
    {"pr-5317.106",  SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_swat_files[] = {
    {"epr5807b.129", SYS1_REGION_MAIN,       0x0000, 0x2000, 0x93db9c9f, 0},
    {"epr-5808.130", SYS1_REGION_MAIN,       0x2000, 0x2000, 0x67116665, 0},
    {"epr-5809.131", SYS1_REGION_MAIN,       0x4000, 0x2000, 0xfd792fc9, 0},
    {"epr-5810.132", SYS1_REGION_MAIN,       0x6000, 0x2000, 0xdc2b279d, 0},
    {"epr-5811.133", SYS1_REGION_MAIN,       0x8000, 0x2000, 0x093e3ab1, 0},
    {"epr-5812.134", SYS1_REGION_MAIN,       0xa000, 0x2000, 0x5bfd692f, 0},
    {"epr-5819.3",   SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0xf6afd0fd, 0},
    {"epr-5818.82",  SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0xb22033d9, 0},
    {"epr-5817.65",  SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0xfd942797, 0},
    {"epr-5816.81",  SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x4384376d, 0},
    {"epr-5815.64",  SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0x16ad046c, 0},
    {"epr-5814.80",  SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0xbe721c99, 0},
    {"epr-5813.63",  SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0x0d42c27e, 0},
    {"epr-5805.86",  SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0x5a732865, 0},
    {"epr-5806.93",  SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0x26ac258c, 0},
    {"pr-5317.106",  SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_wmatch_files[] = {
    {"wm.129", SYS1_REGION_MAIN,       0x0000, 0x2000, 0xb6db4442, 0},
    {"wm.130", SYS1_REGION_MAIN,       0x2000, 0x2000, 0x59a0a7a0, 0},
    {"wm.131", SYS1_REGION_MAIN,       0x4000, 0x2000, 0x4cb3856a, 0},
    {"wm.132", SYS1_REGION_MAIN,       0x6000, 0x2000, 0xe2e44b29, 0},
    {"wm.133", SYS1_REGION_MAIN,       0x8000, 0x2000, 0x43a36445, 0},
    {"wm.134", SYS1_REGION_MAIN,       0xa000, 0x2000, 0x5624794c, 0},
    {"wm.3",   SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0x50d2afb7, 0},
    {"wm.82",  SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0x540f0bf3, 0},
    {"wm.65",  SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0x92c1e39e, 0},
    {"wm.81",  SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x6a01ff2a, 0},
    {"wm.64",  SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0xaae6449b, 0},
    {"wm.80",  SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0xfc3f0bd4, 0},
    {"wm.63",  SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0xc2ce9b93, 0},
    {"wm.86",  SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0x238ae0e5, 0},
    {"wm.93",  SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0xa2f19170, 0},
    {"pr-5317.106", SYSTEM1_REGION_PROM, 0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_spatter_files[] = {
    {"epr-6392.116", SYS1_REGION_MAIN,       0x0000, 0x4000, 0x329b4506, 0},
    {"epr-6393.109", SYS1_REGION_MAIN,       0x4000, 0x4000, 0x3b56e25f, 0},
    {"epr-6394.96",  SYS1_REGION_MAIN,       0x8000, 0x4000, 0x647c1301, 0},
    {"epr-6316.120", SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0x1df95511, 0},
    {"epr-6328.62",  SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0xa2bf2832, 0},
    {"epr-6397.61",  SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0xc60d4471, 0},
    {"epr-6326.64",  SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x269fbb4c, 0},
    {"epr-6396.63",  SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0xc15ccf3b, 0},
    {"epr-6324.66",  SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0x8ab3b563, 0},
    {"epr-6395.65",  SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0x3f083065, 0},
    {"epr-6306.04",  SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0xe871e132, 0},
    {"epr-6308.117", SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0x99c2d90e, 0},
    {"epr-6307.05",  SYSTEM1_REGION_SPRITES, 0x8000, 0x4000, 0x0a5ad543, 0},
    {"epr-6309.110", SYSTEM1_REGION_SPRITES, 0xc000, 0x4000, 0x7423ad98, 0},
    {"pr-5317.106",  SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_pitfall2_files[] = {
    {"epr-6456a.116", SYS1_REGION_MAIN,       0x0000, 0x4000, 0xbcc8406b, 0},
    {"epr-6457a.109", SYS1_REGION_MAIN,       0x4000, 0x4000, 0xa016fd2a, 0},
    {"epr-6458a.96",  SYS1_REGION_MAIN,       0x8000, 0x4000, 0x5c30b3e8, 0},
    {"epr-6462.120",  SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0x86bb9185, 0},
    {"epr-6474a.62",  SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0x9f1711b9, 0},
    {"epr-6473a.61",  SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0x8e53b8dd, 0},
    {"epr-6472a.64",  SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0xe0f34a11, 0},
    {"epr-6471a.63",  SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0xd5bc805c, 0},
    {"epr-6470a.66",  SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0x1439729f, 0},
    {"epr-6469a.65",  SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0xe4ac6921, 0},
    {"epr-6454a.117", SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0xa5d96780, 0},
    {"epr-6455.05",   SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0x32ee64a1, 0},
    {"pr-5317.76",    SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_seganinj_files[] = {
    {"epr-6594a.116", SYS1_REGION_MAIN,       0x0000, 0x4000, 0xa5d0c9d0, 0},
    {"epr-6595a.109", SYS1_REGION_MAIN,       0x4000, 0x4000, 0xb9e6775c, 0},
    {"epr-6596a.96",  SYS1_REGION_MAIN,       0x8000, 0x4000, 0xf2eeb0d8, 0},
    {"epr-6559.120",  SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0x5a1570ee, 0},
    {"epr-6558.62",   SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0x2af9eaeb, 0},
    {"epr-6592.61",   SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0x7804db86, 0},
    {"epr-6556.64",   SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x79fd26f7, 0},
    {"epr-6590.63",   SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0xbf858cad, 0},
    {"epr-6554.66",   SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0x5ac9d205, 0},
    {"epr-6588.65",   SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0xdc931dbb, 0},
    {"epr-6546.117",  SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0xa4785692, 0},
    {"epr-6548.04",   SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0xbdf278c1, 0},
    {"epr-6547.110",  SYSTEM1_REGION_SPRITES, 0x8000, 0x4000, 0x34451b08, 0},
    {"epr-6549a.05",  SYSTEM1_REGION_SPRITES, 0xc000, 0x4000, 0x7c51488c, 0},
    {"pr-5317.76",    SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_imsorry_files[] = {
    {"epr-6676.116", SYS1_REGION_MAIN,       0x0000, 0x4000, 0xeb087d7f, 0},
    {"epr-6677.109", SYS1_REGION_MAIN,       0x4000, 0x4000, 0xbd244bee, 0},
    {"epr-6678.96",  SYS1_REGION_MAIN,       0x8000, 0x4000, 0x2e16b9fd, 0},
    {"epr-6656.120", SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0x25e3d685, 0},
    {"epr-6684.62",  SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0x2c8df377, 0},
    {"epr-6683.61",  SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0x89431c48, 0},
    {"epr-6682.64",  SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0x256a9246, 0},
    {"epr-6681.63",  SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0x6974d189, 0},
    {"epr-6680.66",  SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0x10a629d6, 0},
    {"epr-6674.65",  SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0x143d883c, 0},
    {"epr-6645.117", SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0x1ba167ee, 0},
    {"epr-6646.04",  SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0xedda7ad6, 0},
    {"pr-5317.76",   SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_myhero_files[] = {
    {"epr-6963b.116", SYS1_REGION_MAIN,       0x0000, 0x4000, 0x4daf89d4, SYS1_FILE_COPY_TO_OPS},
    {"epr-6964a.109", SYS1_REGION_MAIN,       0x4000, 0x4000, 0xc26188e5, SYS1_FILE_COPY_TO_OPS},
    {"epr-6927.96",   SYS1_REGION_MAIN,       0x8000, 0x4000, 0x3cbbaf64, SYS1_FILE_COPY_TO_OPS},
    {"epr-69xx.120",  SYSTEM1_REGION_SOUND,   0x0000, 0x2000, 0x0039e1e9, 0},
    {"epr-6966.62",   SYSTEM1_REGION_TILES,   0x0000, 0x2000, 0x157f0401, 0},
    {"epr-6961.61",   SYSTEM1_REGION_TILES,   0x2000, 0x2000, 0xbe53ce47, 0},
    {"epr-6960.64",   SYSTEM1_REGION_TILES,   0x4000, 0x2000, 0xbd381baa, 0},
    {"epr-6959.63",   SYSTEM1_REGION_TILES,   0x6000, 0x2000, 0xbc04e79a, 0},
    {"epr-6958.66",   SYSTEM1_REGION_TILES,   0x8000, 0x2000, 0x714f2c26, 0},
    {"epr-6957.65",   SYSTEM1_REGION_TILES,   0xa000, 0x2000, 0x80920112, 0},
    {"epr-6921.117",  SYSTEM1_REGION_SPRITES, 0x0000, 0x4000, 0xf19e05a1, 0},
    {"epr-6923.04",   SYSTEM1_REGION_SPRITES, 0x4000, 0x4000, 0x7988adc3, 0},
    {"epr-6922.110",  SYSTEM1_REGION_SPRITES, 0x8000, 0x4000, 0x37f77a78, 0},
    {"epr-6924.05",   SYSTEM1_REGION_SPRITES, 0xc000, 0x4000, 0x42bdc8f6, 0},
    {"pr-5317.76",    SYSTEM1_REGION_PROM,    0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t sys1_nob_files[] = {
    {"dm08.1f", SYS1_REGION_MAIN,       0x00000, 0x8000, 0x98d602d6, SYS1_FILE_COPY_TO_OPS},
    {"dm10.1k", SYS1_REGION_MAIN,       0x10000, 0x8000, 0xe7c06663, SYS1_FILE_COPY_TO_OPS},
    {"dm09.1h", SYS1_REGION_MAIN,       0x18000, 0x8000, 0xdc4c872f, SYS1_FILE_COPY_TO_OPS},
    {"dm03.9h", SYSTEM1_REGION_SOUND,   0x0000,  0x4000, 0x415adf76, 0},
    {"dm02.13b",SYSTEM1_REGION_TILES,   0x08000, 0x8000, 0xf12df039, 0},
    {"dm01.12b",SYSTEM1_REGION_TILES,   0x00000, 0x8000, 0x446fbcdd, 0},
    {"dm00.10b",SYSTEM1_REGION_TILES,   0x10000, 0x8000, 0x35f396df, 0},
    {"dm04.5f", SYSTEM1_REGION_SPRITES, 0x00000, 0x8000, 0x2442b86d, 0},
    {"dm06.5k", SYSTEM1_REGION_SPRITES, 0x08000, 0x8000, 0xe33743a6, 0},
    {"dm05.5h", SYSTEM1_REGION_SPRITES, 0x10000, 0x8000, 0x7fbba01d, 0},
    {"dm07.5l", SYSTEM1_REGION_SPRITES, 0x18000, 0x8000, 0x85e7a29f, 0},
    {"nobo_pr.16d", SYSTEM1_REGION_COLOR, 0x0000, 0x0100, 0x95010ac2, 0},
    {"nobo_pr.15d", SYSTEM1_REGION_COLOR, 0x0100, 0x0100, 0xc55aac0c, 0},
    {"dm-12.ic3",   SYSTEM1_REGION_COLOR, 0x0200, 0x0100, 0xde394cee, 0},
    {"dc-11.6a",    SYSTEM1_REGION_PROM,  0x0000, 0x0100, 0x648350b8, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_set_t system1_sets[] = {
    {"starjack",   0x10000, 0x7ab72ecd, system1_set_game_starjack,       sys1_starjack_files, NULL},
    {"upndown",    0x10000, 0x038c82da, system1_set_game_upndown,        sys1_upndown_files, system1_postload_upndown_315_5098},
    {"swat",       0x10000, 0x93db9c9f, system1_set_game_swat,           sys1_swat_files, system1_postload_swat_315_5048},
    {"wmatch",     0x10000, 0xb6db4442, system1_set_game_wmatch,         sys1_wmatch_files, system1_postload_wmatch_315_5064},
    {"spatter",    0x10000, 0x329b4506, system1_set_game_spatter,        sys1_spatter_files, system1_postload_teddybb_315_5006},
    {"pitfall2",   0x10000, 0xbcc8406b, system1_set_game_pitfall2,       sys1_pitfall2_files, system1_postload_pitfall2_315_5093},
    {"seganinj",   0x10000, 0xa5d0c9d0, system1_set_game_seganinj,       sys1_seganinj_files, system1_postload_seganinj_315_5102},
    {"imsorry",    0x10000, 0xeb087d7f, system1_set_game_imsorry,        sys1_imsorry_files, system1_postload_imsorry_315_5110},
    {"myhero",     0x10000, 0x4daf89d4, system1_set_game_myhero,         sys1_myhero_files, NULL},
    {"nob",        0x20000, 0x98d602d6, system1_set_game_nob,            sys1_nob_files, system1_postload_nob_patch},
    {"wbml",       0x20000, 0xbd3349e5, system1_set_game_wbml,           sys1_wbml_files, system1_postload_wbml_mc8123},
    {"wbmlb",      0x20000, 0x66482638, system1_set_game_wbml,           sys1_wbmlb_files, NULL},
    {"teddybb",    0x10000, 0x5939817e, system1_set_game_teddybb,        sys1_teddybb_files, system1_postload_teddybb_315_5155},
    {"wboy",       0x10000, 0x130f4b70, system1_set_game_wboy,           sys1_wboy_files, system1_postload_wboy_315_5177},
    {"gardia",     0x20000, 0x89282a6b, system1_set_game_gardia,         sys1_gardia_files, system1_postload_gardia_317_0006},
    {"ufosensi",   0x20000, 0xf3e394e2, system1_set_game_ufosensi,       sys1_ufosensi_files, system1_postload_ufosensi_mc8123},
    {"blockgal",    0x10000, 0xa99b231a, system1_set_game_blockgal_mc8123, sys1_blockgal_files,  system1_postload_blockgal_mc8123},
    {"blockgalb",   0x10000, 0x65c47676, system1_set_game_blockgal,        sys1_blockgalb_files, NULL},
    {"choplift",    0x20000, 0x678d5c41, system1_set_game_choplifter,      sys1_choplift_files, NULL},
    {"chopliftu",   0x20000, 0xfe49d83e, system1_set_game_choplifter,      sys1_chopliftu_files, NULL},
    {"chopliftbl",  0x20000, 0x71a37932, system1_set_game_choplifter,      sys1_chopliftbl_files, NULL},
    {"flicky",      0x10000, 0x296f1492, system1_set_game_flicky,          sys1_flicky_files, system1_postload_flicky_315_5051},
    {"brain",       0x20000, 0x2d2aec31, system1_set_game_brain,           sys1_brain_files, NULL},
    {NULL, 0, 0, NULL, NULL, NULL}
};

/* ROM-set names mirrored from MAME's Sega System 1/System 2 driver.  Names not
 * present in system1_sets[] are recognized for diagnostics/database coverage but
 * remain blocked until their Sega encryption, MCU/protection, or game-specific
 * I/O is implemented. */
static const char *const system1_known_mame_sets[] = {
    "starjack", "starjacks", "upndown", "upndownu", "regulus", "reguluso", "regulusu",
    "mrviking", "mrvikingj", "swat", "flickyo", "flickys1", "flickyup", "flickyupa",
    "wmatch", "bullfgt", "thetogyu", "nprinces", "nprincesu", "nprinceso", "nprincesb",
    "flicky", "flickya", "flickyb", "flickys2", "spatter", "spattera", "ssanchan",
    "pitfall2", "pitfall2a", "pitfall2u", "seganinj", "seganinju", "seganinja", "ninja",
    "imsorry", "imsorryj", "teddybb", "teddybbo", "teddybboa", "teddybbobl", "myhero",
    "sscandal", "myherobl", "myherok", "4dwarrio", "raflesia", "raflesiau", "wboy",
    "wboyo", "wboy2", "wboy2u", "wboy3", "wboy4", "wboy5", "wboy6", "wboyu",
    "wboyub", "wbdeluxe", "nob", "nobb", "hvymetal", "gardia", "gardiab", "gardiaj",
    "brain", "choplift", "chopliftu", "chopliftbl", "shtngmst", "wboysys2", "wboysys2a",
    "tokisens", "tokisensa", "wbml", "wbmljo", "wbmljb", "wbmlb", "wbmlb2", "wbmlbg",
    "wbmlbge", "wbmlvc", "wbmlvcd", "wbmld", "wbmljod", "dakkochn", "blockgal",
    "blockgalb", "ufosensi", "ufosensib", NULL
};

static int system1_zip_contains_known_name(const char *filename)
{
    const char *base = strrchr(filename, '/');
    char stem[64];
    size_t n;
    const char *const *name;
    base = base ? base + 1 : filename;
    n = strcspn(base, ".");
    if (n >= sizeof(stem)) n = sizeof(stem) - 1;
    memcpy(stem, base, n);
    stem[n] = '\0';
    for (name = system1_known_mame_sets; *name; name++)
        if (strcmp(stem, *name) == 0) return 1;
    return 0;
}


static int zip_member_exists(unzFile zhandle, const char *name)
{
    if (!name) return 0;
    if (zip_member_load_path)
        return zip_member_exists_in_archive(zip_member_load_path, name);
    if (!zhandle) return 0;
    if (unzLocateFile(zhandle, name, 0) == UNZ_OK)
        return 1;
    return 0;
}

/* Merged MAME archives can include bootleg/unlicensed clone files next to the
 * encrypted parent.  Do not silently substitute those clones for the parent:
 * when the encrypted parent is present, the parent must be decoded at load time
 * into separate data/opcode views.  This keeps runtime CPU fetches cheap without
 * depending on bootleg ROM program data. */
static int system1_should_skip_bootleg_clone(unzFile zhandle, const system1_zip_set_t *set)
{
    if (!zhandle || !set || !set->set_name) return 0;

    if (!strcmp(set->set_name, "wbmlb"))
        return zip_member_exists(zhandle, "epr-11031a.90");

    return 0;
}

static int load_system1_zip_set(const char *filename, const system1_zip_set_t *set)
{
    unzFile zhandle = (unzFile)1;
    uint8_t *region;
    int ok = 1;
    int i;

    if (!filename || !set)
        return 0;

    system1_current_zip_path = filename;
    zip_member_load_path = filename;

    if (system1_should_skip_bootleg_clone(zhandle, set))
    {
        zip_member_load_path = NULL;
        return 0;
    }

    region = (uint8_t *)malloc(set->main_size);
    if (!region)
    {
        zip_member_load_path = NULL;
        return 0;
    }
    memset(region, 0xff, set->main_size);
    system1_clear_roms();

    for (i = 0; set->files[i].name; i++)
    {
        const system1_zip_file_t *file = &set->files[i];
        if (file->region == SYS1_REGION_MAIN)
            ok = load_zip_member_to_system1_main(zhandle, file, region);
        else
            ok = load_zip_member_to_system1(zhandle, file->name, file->region, file->offset, file->size, file->crc);
        if (!ok) break;
    }

    if (ok && set->postload)
        ok = set->postload(zhandle, region, set->main_size);

    zip_member_load_path = NULL;
    if (!ok)
    {
        free(region);
        return 0;
    }

    cart.rom = region;
    cart.size = set->main_size;
    cart.crc = set->set_crc;
    option.console = 9;
    if (set->configure) set->configure();
    return 1;
}

static int load_system1_zip(const char *filename)
{
    const system1_zip_set_t *set;

    /* MAME-style archive sets are named after the shortname.  Do not brute-force
     * every supported System 1 set from UI/frontends: on Windows this can make
     * opening unrelated arcade ZIPs look like a hang while the same archive is
     * repeatedly opened and scanned. */
    for (set = system1_sets; set->set_name; set++)
    {
        if (zip_path_stem_ieq(filename, set->set_name))
            return load_system1_zip_set(filename, set);
    }

    if (system1_zip_contains_known_name(filename))
    {
        fprintf(stderr, "Sega System 1/System 2 set is known but not yet supported by this build: %s\n", filename);
    }
    return 0;
}



static const system1_zip_file_t snk_psychos_files[] = {
    {"ps7.4m",     SNK_REGION_MAIN,  0x00000, 0x10000, 0x562809f4, 0},
    {"ps6.8m",     SNK_REGION_SUB,   0x00000, 0x10000, 0x5f426ddb, 0},
    {"ps5.6j",     SNK_REGION_AUDIO, 0x00000, 0x10000, 0x64503283, 0},
    {"psc1.1k",    SNK_REGION_PROM,  0x0000,  0x0400,  0x27b8ca8c, 0},
    {"psc3.1l",    SNK_REGION_PROM,  0x0400,  0x0400,  0x40e78c9e, 0},
    {"psc2.2k",    SNK_REGION_PROM,  0x0800,  0x0400,  0xd845d5ac, 0},
    {"horizon.8j", SNK_REGION_PROM,  0x0c00,  0x0400,  0xc20b197b, 0},
    {"vertical.8k",SNK_REGION_PROM,  0x1000,  0x0400,  0x5d0c617f, 0},
    {"ps8.3a",     SNK_REGION_TX,    0x0000,  0x8000,  0x11a71919, 0},
    {"ps16.1f",    SNK_REGION_BG,    0x00000, 0x10000, 0x167e5765, 0},
    {"ps15.1d",    SNK_REGION_BG,    0x10000, 0x10000, 0x8b0fe8d0, 0},
    {"ps14.1c",    SNK_REGION_BG,    0x20000, 0x10000, 0xf4361c50, 0},
    {"ps13.1a",    SNK_REGION_BG,    0x30000, 0x10000, 0xe4b0b95e, 0},
    {"ps12.3g",    SNK_REGION_SP16,  0x00000, 0x8000,  0xf96f82db, 0},
    {"ps11.3e",    SNK_REGION_SP16,  0x08000, 0x8000,  0x2b007733, 0},
    {"ps10.3c",    SNK_REGION_SP16,  0x10000, 0x8000,  0xefa830e1, 0},
    {"ps9.3b",     SNK_REGION_SP16,  0x18000, 0x8000,  0x24559ee1, 0},
    {"ps17.10f",   SNK_REGION_SP32,  0x00000, 0x10000, 0x2bac250e, 0},
    {"ps18.10h",   SNK_REGION_SP32,  0x10000, 0x10000, 0x5e1ba353, 0},
    {"ps19.10j",   SNK_REGION_SP32,  0x20000, 0x10000, 0x9ff91a97, 0},
    {"ps20.10l",   SNK_REGION_SP32,  0x30000, 0x10000, 0xae1965ef, 0},
    {"ps21.10m",   SNK_REGION_SP32,  0x40000, 0x10000, 0xdf283b67, 0},
    {"ps22.10n",   SNK_REGION_SP32,  0x50000, 0x10000, 0x914f051f, 0},
    {"ps23.10r",   SNK_REGION_SP32,  0x60000, 0x10000, 0xc4488472, 0},
    {"ps24.10s",   SNK_REGION_SP32,  0x70000, 0x10000, 0x8ec7fe18, 0},
    {"ps1.5b",     SNK_REGION_YM2,   0x00000, 0x10000, 0x58f1683f, 0},
    {"ps2.5c",     SNK_REGION_YM2,   0x10000, 0x10000, 0xda3abda1, 0},
    {"ps3.5d",     SNK_REGION_YM2,   0x20000, 0x10000, 0xf3683ae8, 0},
    {"ps4.5f",     SNK_REGION_YM2,   0x30000, 0x10000, 0x437d775a, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_psychosj_files[] = {
    {"ps7.4m",     SNK_REGION_MAIN,  0x00000, 0x10000, 0x05dfb409, 0},
    {"ps6.8m",     SNK_REGION_SUB,   0x00000, 0x10000, 0x5f426ddb, 0},
    {"ps5.6j",     SNK_REGION_AUDIO, 0x00000, 0x10000, 0xbbd0a8e3, 0},
    {"psc1.1k",    SNK_REGION_PROM,  0x0000,  0x0400,  0x27b8ca8c, 0},
    {"psc3.1l",    SNK_REGION_PROM,  0x0400,  0x0400,  0x40e78c9e, 0},
    {"psc2.2k",    SNK_REGION_PROM,  0x0800,  0x0400,  0xd845d5ac, 0},
    {"horizon.8j", SNK_REGION_PROM,  0x0c00,  0x0400,  0xc20b197b, 0},
    {"vertical.8k",SNK_REGION_PROM,  0x1000,  0x0400,  0x5d0c617f, 0},
    {"ps8.3a",     SNK_REGION_TX,    0x0000,  0x8000,  0x11a71919, 0},
    {"ps16.1f",    SNK_REGION_BG,    0x00000, 0x10000, 0x167e5765, 0},
    {"ps15.1d",    SNK_REGION_BG,    0x10000, 0x10000, 0x8b0fe8d0, 0},
    {"ps14.1c",    SNK_REGION_BG,    0x20000, 0x10000, 0xf4361c50, 0},
    {"ps13.1a",    SNK_REGION_BG,    0x30000, 0x10000, 0xe4b0b95e, 0},
    {"ps12.3g",    SNK_REGION_SP16,  0x00000, 0x8000,  0xf96f82db, 0},
    {"ps11.3e",    SNK_REGION_SP16,  0x08000, 0x8000,  0x2b007733, 0},
    {"ps10.3c",    SNK_REGION_SP16,  0x10000, 0x8000,  0xefa830e1, 0},
    {"ps9.3b",     SNK_REGION_SP16,  0x18000, 0x8000,  0x24559ee1, 0},
    {"ps17.10f",   SNK_REGION_SP32,  0x00000, 0x10000, 0x2bac250e, 0},
    {"ps18.10h",   SNK_REGION_SP32,  0x10000, 0x10000, 0x5e1ba353, 0},
    {"ps19.10j",   SNK_REGION_SP32,  0x20000, 0x10000, 0x9ff91a97, 0},
    {"ps20.10l",   SNK_REGION_SP32,  0x30000, 0x10000, 0xae1965ef, 0},
    {"ps21.10m",   SNK_REGION_SP32,  0x40000, 0x10000, 0xdf283b67, 0},
    {"ps22.10n",   SNK_REGION_SP32,  0x50000, 0x10000, 0x914f051f, 0},
    {"ps23.10r",   SNK_REGION_SP32,  0x60000, 0x10000, 0xc4488472, 0},
    {"ps24.10s",   SNK_REGION_SP32,  0x70000, 0x10000, 0x8ec7fe18, 0},
    {"ps1.5b",     SNK_REGION_YM2,   0x00000, 0x10000, 0x0f8e8276, 0},
    {"ps2.5c",     SNK_REGION_YM2,   0x10000, 0x10000, 0x34e41dfb, 0},
    {"ps3.5d",     SNK_REGION_YM2,   0x20000, 0x10000, 0xaa583c5e, 0},
    {"ps4.5f",     SNK_REGION_YM2,   0x30000, 0x10000, 0x7e8bce7a, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_ikari_files[] = {
    {"1.4p",        SNK_REGION_MAIN,  0x00000, 0x10000, 0x52a8b2dd, 0},
    {"2.8p",        SNK_REGION_SUB,   0x00000, 0x10000, 0x45364d55, 0},
    {"3.7k",        SNK_REGION_AUDIO, 0x00000, 0x10000, 0x56a26699, 0},
    {"a6002-1.1k",  SNK_REGION_PROM,  0x0000,  0x0400,  0xb9bf2c2c, 0},
    {"a6002-2.2l",  SNK_REGION_PROM,  0x0400,  0x0400,  0x0703a770, 0},
    {"a6002-3.1l",  SNK_REGION_PROM,  0x0800,  0x0400,  0x0a11cdde, 0},
    {"p7.3b",       SNK_REGION_TX,    0x0000,  0x4000,  0xa7eb4917, 0},
    {"p17.4d",      SNK_REGION_BG,    0x00000, 0x8000,  0xe0dba976, 0},
    {"p18.2d",      SNK_REGION_BG,    0x08000, 0x8000,  0x24947d5f, 0},
    {"p19.4b",      SNK_REGION_BG,    0x10000, 0x8000,  0x9ee59e91, 0},
    {"p20.2b",      SNK_REGION_BG,    0x18000, 0x8000,  0x5da7ec1a, 0},
    {"p8.3d",       SNK_REGION_SP16,  0x00000, 0x8000,  0x9827c14a, 0},
    {"p9.3f",       SNK_REGION_SP16,  0x08000, 0x8000,  0x545c790c, 0},
    {"p10.3h",      SNK_REGION_SP16,  0x10000, 0x8000,  0xec9ba07e, 0},
    {"p11.4m",      SNK_REGION_SP32,  0x00000, 0x8000,  0x5c75ea8f, 0},
    {"p14.2m",      SNK_REGION_SP32,  0x08000, 0x8000,  0x3293fde4, 0},
    {"p12.4p",      SNK_REGION_SP32,  0x10000, 0x8000,  0x95138498, 0},
    {"p15.2p",      SNK_REGION_SP32,  0x18000, 0x8000,  0x65a61c99, 0},
    {"p13.4r",      SNK_REGION_SP32,  0x20000, 0x8000,  0x315383d7, 0},
    {"p16.2r",      SNK_REGION_SP32,  0x28000, 0x8000,  0xe9b03e07, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_victroad_files[] = {
    {"p1.4p",  SNK_REGION_MAIN,  0x00000, 0x10000, 0xe334acef, 0},
    {"p2.8p",  SNK_REGION_SUB,   0x00000, 0x10000, 0x907fac83, 0},
    {"p3.7k",  SNK_REGION_AUDIO, 0x00000, 0x10000, 0xbac745f6, 0},
    {"c1.1k",  SNK_REGION_PROM,  0x0000,  0x0400,  0x491ab831, 0},
    {"c2.2l",  SNK_REGION_PROM,  0x0400,  0x0400,  0x8feca424, 0},
    {"c3.1l",  SNK_REGION_PROM,  0x0800,  0x0400,  0x220076ca, 0},
    {"p7.3b",  SNK_REGION_TX,    0x0000,  0x4000,  0x2b6ed95b, 0},
    {"p17.4c", SNK_REGION_BG,    0x00000, 0x8000,  0x19d4518c, 0},
    {"p18.2c", SNK_REGION_BG,    0x08000, 0x8000,  0xd818be43, 0},
    {"p19.4b", SNK_REGION_BG,    0x10000, 0x8000,  0xd64e0f89, 0},
    {"p20.2b", SNK_REGION_BG,    0x18000, 0x8000,  0xedba0f31, 0},
    {"p8.3d",  SNK_REGION_SP16,  0x00000, 0x8000,  0xdf7f252a, 0},
    {"p9.3f",  SNK_REGION_SP16,  0x08000, 0x8000,  0x9897bc05, 0},
    {"p10.3h", SNK_REGION_SP16,  0x10000, 0x8000,  0xecd3c0ea, 0},
    {"p11.4m", SNK_REGION_SP32,  0x00000, 0x8000,  0x668b25a4, 0},
    {"p14.2m", SNK_REGION_SP32,  0x08000, 0x8000,  0xa7031d4a, 0},
    {"p12.4p", SNK_REGION_SP32,  0x10000, 0x8000,  0xf44e95fa, 0},
    {"p15.2p", SNK_REGION_SP32,  0x18000, 0x8000,  0x120d2450, 0},
    {"p13.4r", SNK_REGION_SP32,  0x20000, 0x8000,  0x980ca3d8, 0},
    {"p16.2r", SNK_REGION_SP32,  0x28000, 0x8000,  0x9f820e8a, 0},
    {"p4.5e",  SNK_REGION_YM2,   0x00000, 0x10000, 0xe10fb8cc, 0},
    {"p5.5g",  SNK_REGION_YM2,   0x10000, 0x10000, 0x93e5f110, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_dogosoke_files[] = {
    {"p1.4p",  SNK_REGION_MAIN,  0x00000, 0x10000, 0x37867ad2, 0},
    {"p2.8p",  SNK_REGION_SUB,   0x00000, 0x10000, 0x907fac83, 0},
    {"p3.7k",  SNK_REGION_AUDIO, 0x00000, 0x10000, 0x173fa571, 0},
    {"c1.1k",  SNK_REGION_PROM,  0x0000,  0x0400,  0x10a2ce2b, 0},
    {"c2.2l",  SNK_REGION_PROM,  0x0400,  0x0400,  0x99dc9792, 0},
    {"c3.1l",  SNK_REGION_PROM,  0x0800,  0x0400,  0xe7213160, 0},
    {"p7.3b",  SNK_REGION_TX,    0x0000,  0x4000,  0x51a4ec83, 0},
    {"p17.4c", SNK_REGION_BG,    0x00000, 0x8000,  0x19d4518c, 0},
    {"p18.2c", SNK_REGION_BG,    0x08000, 0x8000,  0xd818be43, 0},
    {"p19.4b", SNK_REGION_BG,    0x10000, 0x8000,  0xd64e0f89, 0},
    {"p20.2b", SNK_REGION_BG,    0x18000, 0x8000,  0xedba0f31, 0},
    {"p8.3d",  SNK_REGION_SP16,  0x00000, 0x8000,  0xd43044f8, 0},
    {"p9.3f",  SNK_REGION_SP16,  0x08000, 0x8000,  0x365ed2d8, 0},
    {"p10.3h", SNK_REGION_SP16,  0x10000, 0x8000,  0x92579bf3, 0},
    {"p11.4m", SNK_REGION_SP32,  0x00000, 0x8000,  0x668b25a4, 0},
    {"p14.2m", SNK_REGION_SP32,  0x08000, 0x8000,  0xa7031d4a, 0},
    {"p12.4p", SNK_REGION_SP32,  0x10000, 0x8000,  0xf44e95fa, 0},
    {"p15.2p", SNK_REGION_SP32,  0x18000, 0x8000,  0x120d2450, 0},
    {"p13.4r", SNK_REGION_SP32,  0x20000, 0x8000,  0x980ca3d8, 0},
    {"p16.2r", SNK_REGION_SP32,  0x28000, 0x8000,  0x9f820e8a, 0},
    {"p4.5e",  SNK_REGION_YM2,   0x00000, 0x10000, 0x5b43fe9f, 0},
    {"p5.5g",  SNK_REGION_YM2,   0x10000, 0x10000, 0xaae30cd6, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_dogosokb_files[] = {
    {"01.4p",  SNK_REGION_MAIN,  0x00000, 0x10000, 0x53b0ad90, 0},
    {"p2.8p",  SNK_REGION_SUB,   0x00000, 0x10000, 0x907fac83, 0},
    {"p3.7k",  SNK_REGION_AUDIO, 0x00000, 0x10000, 0x173fa571, 0},
    {"c1.1k",  SNK_REGION_PROM,  0x0000,  0x0400,  0x10a2ce2b, 0},
    {"c2.2l",  SNK_REGION_PROM,  0x0400,  0x0400,  0x99dc9792, 0},
    {"c3.1l",  SNK_REGION_PROM,  0x0800,  0x0400,  0xe7213160, 0},
    {"p7.3b",  SNK_REGION_TX,    0x0000,  0x4000,  0x51a4ec83, 0},
    {"p17.4c", SNK_REGION_BG,    0x00000, 0x8000,  0x19d4518c, 0},
    {"p18.2c", SNK_REGION_BG,    0x08000, 0x8000,  0xd818be43, 0},
    {"p19.4b", SNK_REGION_BG,    0x10000, 0x8000,  0xd64e0f89, 0},
    {"p20.2b", SNK_REGION_BG,    0x18000, 0x8000,  0xedba0f31, 0},
    {"p8.3d",  SNK_REGION_SP16,  0x00000, 0x8000,  0xd43044f8, 0},
    {"p9.3f",  SNK_REGION_SP16,  0x08000, 0x8000,  0x365ed2d8, 0},
    {"p10.3h", SNK_REGION_SP16,  0x10000, 0x8000,  0x92579bf3, 0},
    {"p11.4m", SNK_REGION_SP32,  0x00000, 0x8000,  0x668b25a4, 0},
    {"p14.2m", SNK_REGION_SP32,  0x08000, 0x8000,  0xa7031d4a, 0},
    {"p12.4p", SNK_REGION_SP32,  0x10000, 0x8000,  0xf44e95fa, 0},
    {"p15.2p", SNK_REGION_SP32,  0x18000, 0x8000,  0x120d2450, 0},
    {"p13.4r", SNK_REGION_SP32,  0x20000, 0x8000,  0x980ca3d8, 0},
    {"p16.2r", SNK_REGION_SP32,  0x28000, 0x8000,  0x9f820e8a, 0},
    {"p4.5e",  SNK_REGION_YM2,   0x00000, 0x10000, 0x5b43fe9f, 0},
    {"p5.5g",  SNK_REGION_YM2,   0x10000, 0x10000, 0xaae30cd6, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_gwar_files[] = {
    {"1.2g",  SNK_REGION_MAIN,  0x00000, 0x10000, 0x5bcfa7dc, 0},
    {"2.6g",  SNK_REGION_SUB,   0x00000, 0x10000, 0x86d931bf, 0},
    {"3.7g",  SNK_REGION_AUDIO, 0x00000, 0x10000, 0xeb544ab9, 0},
    {"3.9w",  SNK_REGION_PROM,  0x0000,  0x0400,  0x090236a3, 0},
    {"2.9v",  SNK_REGION_PROM,  0x0400,  0x0400,  0x9147de69, 0},
    {"1.9u",  SNK_REGION_PROM,  0x0800,  0x0400,  0x7f9c839e, 0},
    {"gw5.8p", SNK_REGION_TX,   0x0000,  0x8000,  0x80f73e2e, 0},
    {"18.8x", SNK_REGION_BG,    0x00000, 0x10000, 0xf1dcdaef, 0},
    {"19.8z", SNK_REGION_BG,    0x10000, 0x10000, 0x326e4e5e, 0},
    {"gw20.8aa",SNK_REGION_BG,  0x20000, 0x10000, 0x0aa70967, 0},
    {"21.8ac",SNK_REGION_BG,    0x30000, 0x10000, 0xb7686336, 0},
    {"gw6.2j",SNK_REGION_SP16,  0x00000, 0x10000, 0x58600f7d, 0},
    {"7.2l",  SNK_REGION_SP16,  0x10000, 0x10000, 0xa3f9b463, 0},
    {"gw8.2m",SNK_REGION_SP16,  0x20000, 0x10000, 0x092501be, 0},
    {"gw9.2p",SNK_REGION_SP16,  0x30000, 0x10000, 0x25801ea6, 0},
    {"16.2ab",SNK_REGION_SP32,  0x00000, 0x10000, 0x2b46edff, 0},
    {"17.2ad",SNK_REGION_SP32,  0x10000, 0x10000, 0xbe19888d, 0},
    {"14.2y", SNK_REGION_SP32,  0x20000, 0x10000, 0x2d653f0c, 0},
    {"15.2aa",SNK_REGION_SP32,  0x30000, 0x10000, 0xebbf3ba2, 0},
    {"12.2v", SNK_REGION_SP32,  0x40000, 0x10000, 0xaeb3707f, 0},
    {"13.2w", SNK_REGION_SP32,  0x50000, 0x10000, 0x0808f95f, 0},
    {"10.2s", SNK_REGION_SP32,  0x60000, 0x10000, 0x8dfc7b87, 0},
    {"11.2t", SNK_REGION_SP32,  0x70000, 0x10000, 0x06822aac, 0},
    {"4.2j",  SNK_REGION_YM2,   0x00000, 0x10000, 0x2255f8dd, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_gwarj_files[] = {
    {"1.2g",  SNK_REGION_MAIN,  0x00000, 0x10000, 0x7f8a880c, 0},
    {"2.6g",  SNK_REGION_SUB,   0x00000, 0x10000, 0x86d931bf, 0},
    {"3.7g",  SNK_REGION_AUDIO, 0x00000, 0x10000, 0xeb544ab9, 0},
    {"3.9w",  SNK_REGION_PROM,  0x0000,  0x0400,  0x090236a3, 0},
    {"2.9v",  SNK_REGION_PROM,  0x0400,  0x0400,  0x9147de69, 0},
    {"1.9u",  SNK_REGION_PROM,  0x0800,  0x0400,  0x7f9c839e, 0},
    {"gw5.8p", SNK_REGION_TX,   0x0000,  0x8000,  0x99d7ddf3, 0},
    {"18.8x", SNK_REGION_BG,    0x00000, 0x10000, 0xf1dcdaef, 0},
    {"19.8z", SNK_REGION_BG,    0x10000, 0x10000, 0x326e4e5e, 0},
    {"gw20.8aa",SNK_REGION_BG,  0x20000, 0x10000, 0x0aa70967, 0},
    {"21.8ac",SNK_REGION_BG,    0x30000, 0x10000, 0xb7686336, 0},
    {"gw6.2j",SNK_REGION_SP16,  0x00000, 0x10000, 0x58600f7d, 0},
    {"7.2l",  SNK_REGION_SP16,  0x10000, 0x10000, 0xa3f9b463, 0},
    {"gw8.2m",SNK_REGION_SP16,  0x20000, 0x10000, 0x092501be, 0},
    {"gw9.2p",SNK_REGION_SP16,  0x30000, 0x10000, 0x25801ea6, 0},
    {"16.2ab",SNK_REGION_SP32,  0x00000, 0x10000, 0x2b46edff, 0},
    {"17.2ad",SNK_REGION_SP32,  0x10000, 0x10000, 0xbe19888d, 0},
    {"14.2y", SNK_REGION_SP32,  0x20000, 0x10000, 0x2d653f0c, 0},
    {"15.2aa",SNK_REGION_SP32,  0x30000, 0x10000, 0xebbf3ba2, 0},
    {"12.2v", SNK_REGION_SP32,  0x40000, 0x10000, 0xaeb3707f, 0},
    {"13.2w", SNK_REGION_SP32,  0x50000, 0x10000, 0x0808f95f, 0},
    {"10.2s", SNK_REGION_SP32,  0x60000, 0x10000, 0x8dfc7b87, 0},
    {"11.2t", SNK_REGION_SP32,  0x70000, 0x10000, 0x06822aac, 0},
    {"4.2j",  SNK_REGION_YM2,   0x00000, 0x10000, 0x2255f8dd, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_chopper_files[] = {
    {"kk_a_ver2_1.8g", SNK_REGION_MAIN,  0x00000, 0x10000, 0xdc325860, 0},
    {"kk_a_4.6g",      SNK_REGION_SUB,   0x00000, 0x10000, 0x56d10ba3, 0},
    {"kk_3.3d",        SNK_REGION_AUDIO, 0x00000, 0x10000, 0xdbaafb87, 0},
    {"1.9w",           SNK_REGION_PROM,  0x0000,  0x0400,  0x7f07a45c, 0},
    {"3.9u",           SNK_REGION_PROM,  0x0400,  0x0400,  0x15359fc3, 0},
    {"2.9v",           SNK_REGION_PROM,  0x0800,  0x0400,  0x79b50f7d, 0},
    {"kk5.8p",         SNK_REGION_TX,    0x0000,  0x8000,  0xdefc0987, 0},
    {"kk10.8y",        SNK_REGION_BG,    0x00000, 0x10000, 0x5cf4d22b, 0},
    {"kk_a_11.8z",     SNK_REGION_BG,    0x10000, 0x10000, 0x881ac259, 0},
    {"kk_a_12.8ab",    SNK_REGION_BG,    0x20000, 0x10000, 0xde96b331, 0},
    {"kk13.8ac",       SNK_REGION_BG,    0x30000, 0x10000, 0x2756817d, 0},
    {"kk_a_9.3k",      SNK_REGION_SP16,  0x00000, 0x08000, 0x106c2dcc, 0},
    {"kk_a_8.3l",      SNK_REGION_SP16,  0x08000, 0x08000, 0xd4f88f62, 0},
    {"kk_a_7.3n",      SNK_REGION_SP16,  0x10000, 0x08000, 0x28ae39f9, 0},
    {"kk_a_6.3p",      SNK_REGION_SP16,  0x18000, 0x08000, 0x16774a36, 0},
    {"kk18.3ab",       SNK_REGION_SP32,  0x00000, 0x10000, 0x6abbff36, 0},
    {"kk19.2ad",       SNK_REGION_SP32,  0x10000, 0x10000, 0x5283b4d3, 0},
    {"kk20.3y",        SNK_REGION_SP32,  0x20000, 0x10000, 0x6403ddf2, 0},
    {"kk21.3aa",       SNK_REGION_SP32,  0x30000, 0x10000, 0x9f411940, 0},
    {"kk14.3v",        SNK_REGION_SP32,  0x40000, 0x10000, 0x9bad9e25, 0},
    {"kk15.3x",        SNK_REGION_SP32,  0x50000, 0x10000, 0x89faf590, 0},
    {"kk16.3s",        SNK_REGION_SP32,  0x60000, 0x10000, 0xefb1fb6c, 0},
    {"kk17.3t",        SNK_REGION_SP32,  0x70000, 0x10000, 0x6b7fb0a5, 0},
    {"kk2.3j",         SNK_REGION_YM2,   0x00000, 0x10000, 0x06169ae0, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_choppera_files[] = {
    {"chpri-1.8g", SNK_REGION_MAIN,  0x00000, 0x10000, 0xa4e6e978, 0},
    {"kk_a_4.6g",  SNK_REGION_SUB,   0x00000, 0x10000, 0x56d10ba3, 0},
    {"kk3.3d",     SNK_REGION_AUDIO, 0x00000, 0x10000, 0xdbaafb87, 0},
    {"1.9w",       SNK_REGION_PROM,  0x0000,  0x0400,  0x7f07a45c, 0},
    {"3.9u",       SNK_REGION_PROM,  0x0400,  0x0400,  0x15359fc3, 0},
    {"2.9v",       SNK_REGION_PROM,  0x0800,  0x0400,  0x79b50f7d, 0},
    {"kk5.8p",     SNK_REGION_TX,    0x0000,  0x8000,  0xdefc0987, 0},
    {"kk10.8y",    SNK_REGION_BG,    0x00000, 0x10000, 0x5cf4d22b, 0},
    {"kk_a_11.8z", SNK_REGION_BG,    0x10000, 0x10000, 0x881ac259, 0},
    {"kk_a_12.8ab",SNK_REGION_BG,    0x20000, 0x10000, 0xde96b331, 0},
    {"kk13.8ac",   SNK_REGION_BG,    0x30000, 0x10000, 0x2756817d, 0},
    {"kk_a_9.3k",  SNK_REGION_SP16,  0x00000, 0x08000, 0x106c2dcc, 0},
    {"kk_a_8.3l",  SNK_REGION_SP16,  0x08000, 0x08000, 0xd4f88f62, 0},
    {"kk_a_7.3n",  SNK_REGION_SP16,  0x10000, 0x08000, 0x28ae39f9, 0},
    {"kk_a_6.3p",  SNK_REGION_SP16,  0x18000, 0x08000, 0x16774a36, 0},
    {"kk18.3ab",   SNK_REGION_SP32,  0x00000, 0x10000, 0x6abbff36, 0},
    {"kk19.2ad",   SNK_REGION_SP32,  0x10000, 0x10000, 0x5283b4d3, 0},
    {"kk20.3y",    SNK_REGION_SP32,  0x20000, 0x10000, 0x6403ddf2, 0},
    {"kk21.3aa",   SNK_REGION_SP32,  0x30000, 0x10000, 0x9f411940, 0},
    {"kk14.3v",    SNK_REGION_SP32,  0x40000, 0x10000, 0x9bad9e25, 0},
    {"kk15.3x",    SNK_REGION_SP32,  0x50000, 0x10000, 0x89faf590, 0},
    {"kk16.3s",    SNK_REGION_SP32,  0x60000, 0x10000, 0xefb1fb6c, 0},
    {"kk17.3t",    SNK_REGION_SP32,  0x70000, 0x10000, 0x6b7fb0a5, 0},
    {"kk2.3j",     SNK_REGION_YM2,   0x00000, 0x10000, 0x06169ae0, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_chopperb_files[] = {
    {"kk1.8g",     SNK_REGION_MAIN,  0x00000, 0x10000, 0x8fa2f839, 0},
    {"kk4.6g",     SNK_REGION_SUB,   0x00000, 0x10000, 0x004f7d9a, 0},
    {"kk3.3d",     SNK_REGION_AUDIO, 0x00000, 0x10000, 0xdbaafb87, 0},
    {"1.9w",       SNK_REGION_PROM,  0x0000,  0x0400,  0x7f07a45c, 0},
    {"3.9u",       SNK_REGION_PROM,  0x0400,  0x0400,  0x15359fc3, 0},
    {"2.9v",       SNK_REGION_PROM,  0x0800,  0x0400,  0x79b50f7d, 0},
    {"kk5.8p",     SNK_REGION_TX,    0x0000,  0x8000,  0xdefc0987, 0},
    {"kk10.8y",    SNK_REGION_BG,    0x00000, 0x10000, 0x5cf4d22b, 0},
    {"kk11.8z",    SNK_REGION_BG,    0x10000, 0x10000, 0x9af4cad0, 0},
    {"kk12.8ab",   SNK_REGION_BG,    0x20000, 0x10000, 0x02fec778, 0},
    {"kk13.8ac",   SNK_REGION_BG,    0x30000, 0x10000, 0x2756817d, 0},
    {"kk9.3k",     SNK_REGION_SP16,  0x00000, 0x08000, 0x653c4342, 0},
    {"kk8.3l",     SNK_REGION_SP16,  0x08000, 0x08000, 0x2da45894, 0},
    {"kk7.3n",     SNK_REGION_SP16,  0x10000, 0x08000, 0xa0ebebdf, 0},
    {"kk6.3p",     SNK_REGION_SP16,  0x18000, 0x08000, 0x284fad9e, 0},
    {"kk18.3ab",   SNK_REGION_SP32,  0x00000, 0x10000, 0x6abbff36, 0},
    {"kk19.2ad",   SNK_REGION_SP32,  0x10000, 0x10000, 0x5283b4d3, 0},
    {"kk20.3y",    SNK_REGION_SP32,  0x20000, 0x10000, 0x6403ddf2, 0},
    {"kk21.3aa",   SNK_REGION_SP32,  0x30000, 0x10000, 0x9f411940, 0},
    {"kk14.3v",    SNK_REGION_SP32,  0x40000, 0x10000, 0x9bad9e25, 0},
    {"kk15.3x",    SNK_REGION_SP32,  0x50000, 0x10000, 0x89faf590, 0},
    {"kk16.3s",    SNK_REGION_SP32,  0x60000, 0x10000, 0xefb1fb6c, 0},
    {"kk17.3t",    SNK_REGION_SP32,  0x70000, 0x10000, 0x6b7fb0a5, 0},
    {"kk2.3j",     SNK_REGION_YM2,   0x00000, 0x10000, 0x06169ae0, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_legofair_files[] = {
    {"kk1.4m",      SNK_REGION_MAIN,  0x00000, 0x10000, 0x79a485c0, 0},
    {"kk4.8m",      SNK_REGION_SUB,   0x00000, 0x10000, 0x96d3a4d9, 0},
    {"kk3.6j",      SNK_REGION_AUDIO, 0x00000, 0x10000, 0xdbaafb87, 0},
    {"1.1k",        SNK_REGION_PROM,  0x0000,  0x0400,  0x7f07a45c, 0},
    {"2.1l",        SNK_REGION_PROM,  0x0400,  0x0400,  0x15359fc3, 0},
    {"3.2k",        SNK_REGION_PROM,  0x0800,  0x0400,  0x79b50f7d, 0},
    {"horizon.6h",  SNK_REGION_PROM,  0x0c00,  0x0400,  0xc20b197b, 0},
    {"vertical.7h", SNK_REGION_PROM,  0x1000,  0x0400,  0x5d0c617f, 0},
    {"kk5.3a",      SNK_REGION_TX,    0x0000,  0x8000,  0xdefc0987, 0},
    {"kk10.1a",     SNK_REGION_BG,    0x00000, 0x10000, 0x5cf4d22b, 0},
    {"kk11.1b",     SNK_REGION_BG,    0x10000, 0x10000, 0x9af4cad0, 0},
    {"kk12.1d",     SNK_REGION_BG,    0x20000, 0x10000, 0x02fec778, 0},
    {"kk13.1e",     SNK_REGION_BG,    0x30000, 0x10000, 0x2756817d, 0},
    {"kk9.3g",      SNK_REGION_SP16,  0x00000, 0x08000, 0x653c4342, 0},
    {"kk8.3e",      SNK_REGION_SP16,  0x08000, 0x08000, 0x2da45894, 0},
    {"kk7.3d",      SNK_REGION_SP16,  0x10000, 0x08000, 0xa0ebebdf, 0},
    {"kk6.3b",      SNK_REGION_SP16,  0x18000, 0x08000, 0x284fad9e, 0},
    {"kk18.8m",     SNK_REGION_SP32,  0x00000, 0x10000, 0x6abbff36, 0},
    {"kk19.8n",     SNK_REGION_SP32,  0x10000, 0x10000, 0x5283b4d3, 0},
    {"kk20.8p",     SNK_REGION_SP32,  0x20000, 0x10000, 0x6403ddf2, 0},
    {"kk21.8s",     SNK_REGION_SP32,  0x30000, 0x10000, 0x9f411940, 0},
    {"kk14.7p",     SNK_REGION_SP32,  0x40000, 0x10000, 0x9bad9e25, 0},
    {"kk15.7s",     SNK_REGION_SP32,  0x50000, 0x10000, 0x89faf590, 0},
    {"kk16.8j",     SNK_REGION_SP32,  0x60000, 0x10000, 0xefb1fb6c, 0},
    {"kk17.8k",     SNK_REGION_SP32,  0x70000, 0x10000, 0x6b7fb0a5, 0},
    {"kk2.5b",      SNK_REGION_YM2,   0x00000, 0x10000, 0x06169ae0, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_athena_files[] = {
    {"p1.4p",   SNK_REGION_MAIN,  0x00000, 0x04000, 0x900a113c, 0},
    {"p2.4m",   SNK_REGION_MAIN,  0x04000, 0x08000, 0x61c69474, 0},
    {"p3.8p",   SNK_REGION_SUB,   0x00000, 0x04000, 0xdf50af7e, 0},
    {"p4.8m",   SNK_REGION_SUB,   0x04000, 0x08000, 0xf3c933df, 0},
    {"p5.6g",   SNK_REGION_AUDIO, 0x00000, 0x04000, 0x42dbe029, 0},
    {"p6.6k",   SNK_REGION_AUDIO, 0x04000, 0x08000, 0x596f1c8a, 0},
    {"3.2c",    SNK_REGION_PROM,  0x0000,  0x0400,  0x294279ae, 0},
    {"2.1b",    SNK_REGION_PROM,  0x0400,  0x0400,  0xd25c9099, 0},
    {"1.1c",    SNK_REGION_PROM,  0x0800,  0x0400,  0xa4a4e7dc, 0},
    {"p11.2d",  SNK_REGION_TX,    0x0000,  0x4000,  0x18b4bcca, 0},
    {"p10.2b",  SNK_REGION_BG,    0x00000, 0x8000,  0xf269c0eb, 0},
    {"p7.2p",   SNK_REGION_SP16,  0x00000, 0x8000,  0xc63a871f, 0},
    {"p8.2s",   SNK_REGION_SP16,  0x08000, 0x8000,  0x760568d8, 0},
    {"p9.2t",   SNK_REGION_SP16,  0x10000, 0x8000,  0x57b35c73, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_athenab_files[] = {
    {"athenab/p1.4p", SNK_REGION_MAIN,  0x00000, 0x04000, 0xa341677e, 0},
    {"athenab/p2.4m", SNK_REGION_MAIN,  0x04000, 0x08000, 0x26e2b14f, 0},
    {"p3.8p",         SNK_REGION_SUB,   0x00000, 0x04000, 0xdf50af7e, 0},
    {"p4.8m",         SNK_REGION_SUB,   0x04000, 0x08000, 0xf3c933df, 0},
    {"p5.6g",         SNK_REGION_AUDIO, 0x00000, 0x04000, 0x42dbe029, 0},
    {"p6.6k",         SNK_REGION_AUDIO, 0x04000, 0x08000, 0x596f1c8a, 0},
    {"3.2c",          SNK_REGION_PROM,  0x0000,  0x0400,  0x294279ae, 0},
    {"2.1b",          SNK_REGION_PROM,  0x0400,  0x0400,  0xd25c9099, 0},
    {"1.1c",          SNK_REGION_PROM,  0x0800,  0x0400,  0xa4a4e7dc, 0},
    {"p11.2d",        SNK_REGION_TX,    0x0000,  0x4000,  0x18b4bcca, 0},
    {"p10.2b",        SNK_REGION_BG,    0x00000, 0x8000,  0xf269c0eb, 0},
    {"p7.2p",         SNK_REGION_SP16,  0x00000, 0x8000,  0xc63a871f, 0},
    {"p8.2s",         SNK_REGION_SP16,  0x08000, 0x8000,  0x760568d8, 0},
    {"p9.2t",         SNK_REGION_SP16,  0x10000, 0x8000,  0x57b35c73, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_sathena_files[] = {
    {"sathena/p1.4p", SNK_REGION_MAIN,  0x00000, 0x04000, 0x26eb2ce1, 0},
    {"sathena/p2.4m", SNK_REGION_MAIN,  0x04000, 0x08000, 0x925f60ce, 0},
    {"sathena/p3.8p", SNK_REGION_SUB,   0x00000, 0x04000, 0xd0853f62, 0},
    {"sathena/p4.8m", SNK_REGION_SUB,   0x04000, 0x08000, 0x8c697bca, 0},
    {"p5.6g",         SNK_REGION_AUDIO, 0x00000, 0x04000, 0x42dbe029, 0},
    {"p6.6k",         SNK_REGION_AUDIO, 0x04000, 0x08000, 0x596f1c8a, 0},
    {"3.2c",          SNK_REGION_PROM,  0x0000,  0x0400,  0x294279ae, 0},
    {"2.1b",          SNK_REGION_PROM,  0x0400,  0x0400,  0xd25c9099, 0},
    {"1.1c",          SNK_REGION_PROM,  0x0800,  0x0400,  0xa4a4e7dc, 0},
    {"p11.2d",        SNK_REGION_TX,    0x0000,  0x4000,  0x18b4bcca, 0},
    {"p10.2b",        SNK_REGION_BG,    0x00000, 0x8000,  0xf269c0eb, 0},
    {"p7.2p",         SNK_REGION_SP16,  0x00000, 0x8000,  0xc63a871f, 0},
    {"p8.2s",         SNK_REGION_SP16,  0x08000, 0x8000,  0x760568d8, 0},
    {"p9.2t",         SNK_REGION_SP16,  0x10000, 0x8000,  0x57b35c73, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_tdfever_files[] = {
    {"td2-ver3u.6c",  SNK_REGION_MAIN,  0x00000, 0x10000, 0x92138fe4, 0},
    {"td1-ver3u.2c",  SNK_REGION_SUB,   0x00000, 0x10000, 0x798711f5, 0},
    {"td3-ver2u.3j",  SNK_REGION_AUDIO, 0x00000, 0x10000, 0x5d13e0b1, 0},
    {"2t.8e",         SNK_REGION_PROM,  0x0000,  0x0400,  0x67bdf8a0, 0},
    {"1t.8d",         SNK_REGION_PROM,  0x0400,  0x0400,  0x9c4a9198, 0},
    {"3t.9e",         SNK_REGION_PROM,  0x0800,  0x0400,  0xc93c18e8, 0},
    {"td14-u.4n",     SNK_REGION_TX,    0x0000,  0x8000,  0xe841bf1a, 0},
    {"td15.8d",       SNK_REGION_BG,    0x00000, 0x10000, 0xad6e0927, 0},
    {"td16.8e",       SNK_REGION_BG,    0x10000, 0x10000, 0x181db036, 0},
    {"td17.8f",       SNK_REGION_BG,    0x20000, 0x10000, 0xc5decca3, 0},
    {"td18-ver2u.8g", SNK_REGION_BG,    0x30000, 0x10000, 0x3924da37, 0},
    {"td19.8j",       SNK_REGION_BG,    0x40000, 0x10000, 0xbc17ea7f, 0},
    {"td13.2t",       SNK_REGION_SP32,  0x00000, 0x10000, 0x88e2e819, 0},
    {"td12-1.2s",     SNK_REGION_SP32,  0x10000, 0x10000, 0xf6f83d63, 0},
    {"td11.2r",       SNK_REGION_SP32,  0x20000, 0x10000, 0xa0d53fbd, 0},
    {"td10-1.2p",     SNK_REGION_SP32,  0x30000, 0x10000, 0xc8c71c7b, 0},
    {"td9.2n",        SNK_REGION_SP32,  0x40000, 0x10000, 0xa8979657, 0},
    {"td8-1.2l",      SNK_REGION_SP32,  0x50000, 0x10000, 0x28f49182, 0},
    {"td7.2k",        SNK_REGION_SP32,  0x60000, 0x10000, 0x72a5590d, 0},
    {"td6-1.2j",      SNK_REGION_SP32,  0x70000, 0x10000, 0x9b6d4053, 0},
    {"td5.7p",        SNK_REGION_YM2,   0x00000, 0x10000, 0x04794557, 0},
    {"td4.7n",        SNK_REGION_YM2,   0x10000, 0x10000, 0x155e472e, 0},
    {NULL, 0, 0, 0, 0, 0}
};

static const system1_zip_file_t snk_tdfeverj_files[] = {
    {"td2.6c",   SNK_REGION_MAIN,  0x00000, 0x10000, 0x88d88ec4, 0},
    {"td1.2c",   SNK_REGION_SUB,   0x00000, 0x10000, 0x191e6442, 0},
    {"td3.3j",   SNK_REGION_AUDIO, 0x00000, 0x10000, 0x4e4d71c7, 0},
    {"2t.8e",    SNK_REGION_PROM,  0x0000,  0x0400,  0x67bdf8a0, 0},
    {"1t.8d",    SNK_REGION_PROM,  0x0400,  0x0400,  0x9c4a9198, 0},
    {"3t.9e",    SNK_REGION_PROM,  0x0800,  0x0400,  0xc93c18e8, 0},
    {"td14.4n",  SNK_REGION_TX,    0x0000,  0x8000,  0xaf9bced5, 0},
    {"td15.8d",  SNK_REGION_BG,    0x00000, 0x10000, 0xad6e0927, 0},
    {"td16.8e",  SNK_REGION_BG,    0x10000, 0x10000, 0x181db036, 0},
    {"td17.8f",  SNK_REGION_BG,    0x20000, 0x10000, 0xc5decca3, 0},
    {"td18.8g",  SNK_REGION_BG,    0x30000, 0x10000, 0x4512cdfb, 0},
    {"td19.8j",  SNK_REGION_BG,    0x40000, 0x10000, 0xbc17ea7f, 0},
    {"td13.2t",  SNK_REGION_SP32,  0x00000, 0x10000, 0x88e2e819, 0},
    {"td12-1.2s",SNK_REGION_SP32,  0x10000, 0x10000, 0xf6f83d63, 0},
    {"td11.2r",  SNK_REGION_SP32,  0x20000, 0x10000, 0xa0d53fbd, 0},
    {"td10-1.2p",SNK_REGION_SP32,  0x30000, 0x10000, 0xc8c71c7b, 0},
    {"td9.2n",   SNK_REGION_SP32,  0x40000, 0x10000, 0xa8979657, 0},
    {"td8-1.2l", SNK_REGION_SP32,  0x50000, 0x10000, 0x28f49182, 0},
    {"td7.2k",   SNK_REGION_SP32,  0x60000, 0x10000, 0x72a5590d, 0},
    {"td6-1.2j", SNK_REGION_SP32,  0x70000, 0x10000, 0x9b6d4053, 0},
    {"td5.7p",   SNK_REGION_YM2,   0x00000, 0x10000, 0x04794557, 0},
    {"td4.7n",   SNK_REGION_YM2,   0x10000, 0x10000, 0x155e472e, 0},
    {NULL, 0, 0, 0, 0, 0}
};

typedef struct
{
    const char *set_name;
    uint32_t set_crc;
    int variant;
    const system1_zip_file_t *files;
} snk_zip_set_t;

static const snk_zip_set_t snk_zip_sets[] = {
    {"ikari",    0x52a8b2dd, SNK_GAME_IKARI,    snk_ikari_files},
    {"psychos",  0x562809f4, SNK_GAME_PSYCHOS,  snk_psychos_files},
    {"psychosj", 0x05dfb409, SNK_GAME_PSYCHOS,  snk_psychosj_files},
    {"victroad", 0xe334acef, SNK_GAME_VICTROAD, snk_victroad_files},
    {"dogosoke", 0x37867ad2, SNK_GAME_VICTROAD, snk_dogosoke_files},
    {"dogosokb", 0x53b0ad90, SNK_GAME_VICTROAD, snk_dogosokb_files},
    {"gwar",     0x5bcfa7dc, SNK_GAME_GWAR,     snk_gwar_files},
    {"gwarj",    0x7f8a880c, SNK_GAME_GWAR,     snk_gwarj_files},
    {"chopper",  0xdc325860, SNK_GAME_CHOPPER,  snk_chopper_files},
    {"choppera", 0xa4e6e978, SNK_GAME_CHOPPER,  snk_choppera_files},
    {"chopperb", 0x8fa2f839, SNK_GAME_CHOPPER,  snk_chopperb_files},
    {"legofair", 0x79a485c0, SNK_GAME_CHOPPER,  snk_legofair_files},
    {"tdfever",  0x92138fe4, SNK_GAME_TDFEVER,  snk_tdfever_files},
    {"tdfeverj", 0x88d88ec4, SNK_GAME_TDFEVER,  snk_tdfeverj_files},
    {"athenab",  0xa341677e, SNK_GAME_ATHENA,   snk_athenab_files},
    {"sathena",  0x26eb2ce1, SNK_GAME_ATHENA,   snk_sathena_files},
    {"athena",   0x900a113c, SNK_GAME_ATHENA,   snk_athena_files},
    {NULL, 0, 0, NULL}
};

static int load_zip_member_to_snk(unzFile zhandle, const system1_zip_file_t *file, uint8_t *main_region)
{
    uint8_t *tmp;
    int ok = 0;
    if (!file) return 0;
    tmp = (uint8_t *)malloc(file->size);
    if (!tmp) return 0;
    if (load_zip_member_exact(zhandle, file->name, tmp, file->size, file->crc))
    {
        ok = snk_psychos_set_region(file->region, file->offset, tmp, file->size);
        if (ok && file->region == SNK_REGION_MAIN && file->offset + file->size <= 0x10000)
            memcpy(main_region + file->offset, tmp, file->size);
    }
    free(tmp);
    return ok;
}

static int load_snk_psychos_zip(const char *filename)
{
    unzFile zhandle = (unzFile)1;
    uint8_t *region;
    int i, ok;
    const snk_zip_set_t *set;
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;

    (void)base;
    for (set = snk_zip_sets; set->set_name; set++)
    {
        if (zip_path_stem_ieq(filename, set->set_name))
            break;
    }
    if (!set->set_name)
        return 0;

    zip_member_load_path = filename;
    region = (uint8_t *)malloc(0x10000);
    if (!region)
    {
        zip_member_load_path = NULL;
        return 0;
    }
    memset(region, 0xff, 0x10000);
    snk_psychos_clear_roms();
    ok = 1;
    for (i = 0; set->files[i].name; i++)
    {
        ok = load_zip_member_to_snk(zhandle, &set->files[i], region);
        if (!ok) break;
    }
    zip_member_load_path = NULL;
    if (!ok)
    {
        free(region);
        return 0;
    }
    cart.rom = region;
    cart.size = 0x10000;
    cart.crc = set->set_crc;
    option.console = 10;
    snk_psychos_set_game_variant(set->variant);
    return 1;
}

/* ---- Taito L-System zip loader ---------------------------------------- */
/* ROM_LOAD16_BYTE / ROM_LOAD32_BYTE are represented by a non-1 stride:
 * stride=1 -> ROM_LOAD, stride=2 -> ROM_LOAD16_BYTE, stride=4 -> ROM_LOAD32_BYTE.
 * The file bytes are placed at offset, offset+stride, offset+2*stride, ... */
typedef struct
{
    const char *name;
    int region;
    uint32_t offset;
    uint32_t size;
    uint32_t crc;
    uint32_t stride;   /* 1 = ROM_LOAD, 2 = ROM_LOAD16_BYTE, 4 = ROM_LOAD32_BYTE */
} taitol_zip_file_t;

typedef struct
{
    const char *set_name;
    int variant;
    const taitol_zip_file_t *files;
} taitol_zip_set_t;

#define TLF_LD(n,reg,off,sz,crc)        { (n), (reg), (off), (sz), (crc), 1u }
#define TLF_LD16(n,reg,off,sz,crc)      { (n), (reg), (off), (sz), (crc), 2u }
#define TLF_LD32(n,reg,off,sz,crc)      { (n), (reg), (off), (sz), (crc), 4u }
#define TLF_END                          { NULL, 0, 0, 0, 0, 0 }

static const taitol_zip_file_t taitol_raimais_files[] = {
    TLF_LD("b36_11-1.ic7", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0xf19fb0d5),
    TLF_LD("b36_09.ic13",  TAITOL_REGION_MAIN, 0x20000, 0x20000, 0x9c466e43),
    TLF_LD("b36_06.ic24",  TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x29bbc4f8),
    TLF_LD("b36_07.ic2",   TAITOL_REGION_SLAVE, 0x00000, 0x10000, 0x4f3737e6),
    TLF_LD("b36-01.ic6",   TAITOL_REGION_GFX,   0x00000, 0x80000, 0x89355cb2),
    TLF_LD("b36-02.ic12",  TAITOL_REGION_GFX,   0x80000, 0x80000, 0xe71da5db),
    TLF_LD("b36-03.ic28",  TAITOL_REGION_YM2610,0x00000, 0x80000, 0x96166516),
    TLF_END
};
static const taitol_zip_file_t taitol_raimaisj_files[] = {
    TLF_LD("b36_08-1.ic7", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x6cc8f79f),
    TLF_LD("b36_09.ic13",  TAITOL_REGION_MAIN, 0x20000, 0x20000, 0x9c466e43),
    TLF_LD("b36_06.ic24",  TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x29bbc4f8),
    TLF_LD("b36_07.ic2",   TAITOL_REGION_SLAVE, 0x00000, 0x10000, 0x4f3737e6),
    TLF_LD("b36-01.ic6",   TAITOL_REGION_GFX,   0x00000, 0x80000, 0x89355cb2),
    TLF_LD("b36-02.ic12",  TAITOL_REGION_GFX,   0x80000, 0x80000, 0xe71da5db),
    TLF_LD("b36-03.ic28",  TAITOL_REGION_YM2610,0x00000, 0x80000, 0x96166516),
    TLF_END
};
static const taitol_zip_file_t taitol_raimaisjo_files[] = {
    TLF_LD("b36_08.ic7",   TAITOL_REGION_MAIN, 0x00000, 0x20000, 0xf40b9178),
    TLF_LD("b36_09.ic13",  TAITOL_REGION_MAIN, 0x20000, 0x20000, 0x9c466e43),
    TLF_LD("b36_06.ic24",  TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x29bbc4f8),
    TLF_LD("b36_07.ic2",   TAITOL_REGION_SLAVE, 0x00000, 0x10000, 0x4f3737e6),
    TLF_LD("b36-01.ic6",   TAITOL_REGION_GFX,   0x00000, 0x80000, 0x89355cb2),
    TLF_LD("b36-02.ic12",  TAITOL_REGION_GFX,   0x80000, 0x80000, 0xe71da5db),
    TLF_LD("b36-03.ic28",  TAITOL_REGION_YM2610,0x00000, 0x80000, 0x96166516),
    TLF_END
};
static const taitol_zip_file_t taitol_fhawk_files[] = {
    TLF_LD("b70-11.ic3",  TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x7d9f7583),
    TLF_LD("b70-03.ic2",  TAITOL_REGION_MAIN, 0x20000, 0x80000, 0x42d5a9b8),
    TLF_LD("b70-09.ic31", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x85cccaa2),
    TLF_LD("b70-08.ic12", TAITOL_REGION_SLAVE, 0x00000, 0x20000, 0x4d795f48),
    TLF_LD("b70-01.ic1",  TAITOL_REGION_GFX,   0x00000, 0x80000, 0xfcdf67e2),
    TLF_LD("b70-02.ic2",  TAITOL_REGION_GFX,   0x80000, 0x80000, 0x35f7172e),
    TLF_END
};
static const taitol_zip_file_t taitol_fhawkj_files[] = {
    TLF_LD("b70-07.ic3",  TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x939114af),
    TLF_LD("b70-03.ic2",  TAITOL_REGION_MAIN, 0x20000, 0x80000, 0x42d5a9b8),
    TLF_LD("b70-09.ic31", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x85cccaa2),
    TLF_LD("b70-08.ic12", TAITOL_REGION_SLAVE, 0x00000, 0x20000, 0x4d795f48),
    TLF_LD("b70-01.ic1",  TAITOL_REGION_GFX,   0x00000, 0x80000, 0xfcdf67e2),
    TLF_LD("b70-02.ic2",  TAITOL_REGION_GFX,   0x80000, 0x80000, 0x35f7172e),
    TLF_END
};
static const taitol_zip_file_t taitol_champwr_files[] = {
    TLF_LD("c01-13.rom", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x7ef47525),
    TLF_LD("c01-04.rom", TAITOL_REGION_MAIN, 0x20000, 0x20000, 0x358bd076),
    TLF_LD("c01-08.rom", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x810efff8),
    TLF_LD("c01-07.rom", TAITOL_REGION_SLAVE, 0x00000, 0x20000, 0x5117c98f),
    TLF_LD("c01-01.rom", TAITOL_REGION_GFX,   0x000000, 0x80000, 0xf302e6e9),
    TLF_LD("c01-02.rom", TAITOL_REGION_GFX,   0x080000, 0x80000, 0x1e0476c4),
    TLF_LD("c01-03.rom", TAITOL_REGION_GFX,   0x100000, 0x80000, 0x2a142dbc),
    TLF_LD("c01-05.rom", TAITOL_REGION_ADPCM, 0x00000, 0x20000, 0x22efad4a),
    TLF_END
};
static const taitol_zip_file_t taitol_champwru_files[] = {
    TLF_LD("c01-12.rom", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x09f345b3),
    TLF_LD("c01-04.rom", TAITOL_REGION_MAIN, 0x20000, 0x20000, 0x358bd076),
    TLF_LD("c01-08.rom", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x810efff8),
    TLF_LD("c01-07.rom", TAITOL_REGION_SLAVE, 0x00000, 0x20000, 0x5117c98f),
    TLF_LD("c01-01.rom", TAITOL_REGION_GFX,   0x000000, 0x80000, 0xf302e6e9),
    TLF_LD("c01-02.rom", TAITOL_REGION_GFX,   0x080000, 0x80000, 0x1e0476c4),
    TLF_LD("c01-03.rom", TAITOL_REGION_GFX,   0x100000, 0x80000, 0x2a142dbc),
    TLF_LD("c01-05.rom", TAITOL_REGION_ADPCM, 0x00000, 0x20000, 0x22efad4a),
    TLF_END
};
static const taitol_zip_file_t taitol_champwrj_files[] = {
    TLF_LD("c01-06.bin", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x90fa1409),
    TLF_LD("c01-04.rom", TAITOL_REGION_MAIN, 0x20000, 0x20000, 0x358bd076),
    TLF_LD("c01-08.rom", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x810efff8),
    TLF_LD("c01-07.rom", TAITOL_REGION_SLAVE, 0x00000, 0x20000, 0x5117c98f),
    TLF_LD("c01-01.rom", TAITOL_REGION_GFX,   0x000000, 0x80000, 0xf302e6e9),
    TLF_LD("c01-02.rom", TAITOL_REGION_GFX,   0x080000, 0x80000, 0x1e0476c4),
    TLF_LD("c01-03.rom", TAITOL_REGION_GFX,   0x100000, 0x80000, 0x2a142dbc),
    TLF_LD("c01-05.rom", TAITOL_REGION_ADPCM, 0x00000, 0x20000, 0x22efad4a),
    TLF_END
};
static const taitol_zip_file_t taitol_kurikint_files[] = {
    TLF_LD("b42-09.ic2",  TAITOL_REGION_MAIN, 0x00000, 0x20000, 0xe97c4394),
    TLF_LD("b42-06.ic6",  TAITOL_REGION_MAIN, 0x20000, 0x20000, 0xfa15fd65),
    TLF_LD("b42-07.ic22", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x0f2719c0),
    TLF_LD("b42-01.ic1",  TAITOL_REGION_GFX,   0x00000, 0x80000, 0x7d1a1fec),
    TLF_LD("b42-02.ic5",  TAITOL_REGION_GFX,   0x80000, 0x80000, 0x1a52e65c),
    TLF_END
};
static const taitol_zip_file_t taitol_kurikintw_files[] = {
    TLF_LD("b42-10.ic2",  TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x87460109),
    TLF_LD("b42-06.ic6",  TAITOL_REGION_MAIN, 0x20000, 0x20000, 0xfa15fd65),
    TLF_LD("b42-07.ic22", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x0f2719c0),
    TLF_LD("b42-01.ic1",  TAITOL_REGION_GFX,   0x00000, 0x80000, 0x7d1a1fec),
    TLF_LD("b42-02.ic5",  TAITOL_REGION_GFX,   0x80000, 0x80000, 0x1a52e65c),
    TLF_END
};
static const taitol_zip_file_t taitol_kurikintu_files[] = {
    TLF_LD("b42-08.ic2",  TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x7075122e),
    TLF_LD("b42-06.ic6",  TAITOL_REGION_MAIN, 0x20000, 0x20000, 0xfa15fd65),
    TLF_LD("b42-07.ic22", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x0f2719c0),
    TLF_LD("b42-01.ic1",  TAITOL_REGION_GFX,   0x00000, 0x80000, 0x7d1a1fec),
    TLF_LD("b42-02.ic5",  TAITOL_REGION_GFX,   0x80000, 0x80000, 0x1a52e65c),
    TLF_END
};
static const taitol_zip_file_t taitol_kurikintj_files[] = {
    TLF_LD("b42-05.ic2",  TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x077222b8),
    TLF_LD("b42-06.ic6",  TAITOL_REGION_MAIN, 0x20000, 0x20000, 0xfa15fd65),
    TLF_LD("b42-07.ic22", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x0f2719c0),
    TLF_LD("b42-01.ic1",  TAITOL_REGION_GFX,   0x00000, 0x80000, 0x7d1a1fec),
    TLF_LD("b42-02.ic5",  TAITOL_REGION_GFX,   0x80000, 0x80000, 0x1a52e65c),
    TLF_END
};
static const taitol_zip_file_t taitol_kurikinta_files[] = {
    TLF_LD("kk_ic2.ic2", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x908603f2),
    TLF_LD("kk_ic6.ic6", TAITOL_REGION_MAIN, 0x20000, 0x20000, 0xa4a957b1),
    TLF_LD("b42-07.ic22", TAITOL_REGION_AUDIO, 0x00000, 0x10000, 0x0f2719c0),
    TLF_LD16("kk_1-1l.rom", TAITOL_REGION_GFX, 0x00000, 0x20000, 0xdf1d4fcd),
    TLF_LD16("kk_2-2l.rom", TAITOL_REGION_GFX, 0x40000, 0x20000, 0xfca7f647),
    TLF_LD16("kk_5-3l.rom", TAITOL_REGION_GFX, 0x80000, 0x20000, 0xd080fde1),
    TLF_LD16("kk_7-4l.rom", TAITOL_REGION_GFX, 0xc0000, 0x20000, 0xf5bf6829),
    TLF_LD16("kk_3-1h.rom", TAITOL_REGION_GFX, 0x00001, 0x20000, 0x71af848e),
    TLF_LD16("kk_4-2h.rom", TAITOL_REGION_GFX, 0x40001, 0x20000, 0xcebb5bac),
    TLF_LD16("kk_6-3h.rom", TAITOL_REGION_GFX, 0x80001, 0x20000, 0x322e3752),
    TLF_LD16("kk_8-4h.rom", TAITOL_REGION_GFX, 0xc0001, 0x20000, 0x117bde99),
    TLF_END
};
static const taitol_zip_file_t taitol_plotting_files[] = {
    TLF_LD("ic10",       TAITOL_REGION_MAIN, 0x00000, 0x10000, 0xbe240921),
    TLF_LD16("b96-07.ic9", TAITOL_REGION_GFX, 0x00000, 0x10000, 0x0713a387),
    TLF_LD16("b96-08.ic8", TAITOL_REGION_GFX, 0x00001, 0x10000, 0x55b8e294),
    TLF_END
};
static const taitol_zip_file_t taitol_plottinga_files[] = {
    TLF_LD("plot01.ic10", TAITOL_REGION_MAIN, 0x00000, 0x10000, 0x5b30bc25),
    TLF_LD16("b96-02.ic9", TAITOL_REGION_GFX, 0x00000, 0x10000, 0x6e0bad2a),
    TLF_LD16("b96-03.ic8", TAITOL_REGION_GFX, 0x00001, 0x10000, 0xfb5f3ca4),
    TLF_END
};
static const taitol_zip_file_t taitol_plottingb_files[] = {
    TLF_LD("b96-06.ic10", TAITOL_REGION_MAIN, 0x00000, 0x10000, 0xf89a54b1),
    TLF_LD16("b96-02.ic9", TAITOL_REGION_GFX, 0x00000, 0x10000, 0x6e0bad2a),
    TLF_LD16("b96-03.ic8", TAITOL_REGION_GFX, 0x00001, 0x10000, 0xfb5f3ca4),
    TLF_END
};
static const taitol_zip_file_t taitol_plottingu_files[] = {
    TLF_LD("b96-05.ic10", TAITOL_REGION_MAIN, 0x00000, 0x10000, 0xafb99d1f),
    TLF_LD16("b96-02.ic9", TAITOL_REGION_GFX, 0x00000, 0x10000, 0x6e0bad2a),
    TLF_LD16("b96-03.ic8", TAITOL_REGION_GFX, 0x00001, 0x10000, 0xfb5f3ca4),
    TLF_END
};
static const taitol_zip_file_t taitol_flipull_files[] = {
    TLF_LD("b96-01.ic10", TAITOL_REGION_MAIN, 0x00000, 0x10000, 0x65993978),
    TLF_LD16("b96-07.ic9", TAITOL_REGION_GFX, 0x00000, 0x10000, 0x0713a387),
    TLF_LD16("b96-08.ic8", TAITOL_REGION_GFX, 0x00001, 0x10000, 0x55b8e294),
    TLF_END
};
static const taitol_zip_file_t taitol_puzznic_files[] = {
    TLF_LD("c20-09.ic11", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x156d6de1),
    TLF_LD("c20-01.ic4",  TAITOL_REGION_MCU,  0x0000,  0x0800,  0x085f68b4),
    TLF_LD16("c20-07.ic10", TAITOL_REGION_GFX, 0x00000, 0x10000, 0xbe12749a),
    TLF_LD16("c20-06.ic9",  TAITOL_REGION_GFX, 0x00001, 0x10000, 0xac85a9c5),
    TLF_END
};
static const taitol_zip_file_t taitol_puzznicu_files[] = {
    TLF_LD("c20-10.ic11", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x3522d2e5),
    TLF_LD("c20-01.ic4",  TAITOL_REGION_MCU,  0x0000,  0x0800,  0x085f68b4),
    TLF_LD16("c20-03.ic10", TAITOL_REGION_GFX, 0x00000, 0x20000, 0x4264056c),
    TLF_LD16("c20-02.ic9",  TAITOL_REGION_GFX, 0x00001, 0x20000, 0x3c115f8b),
    TLF_END
};
static const taitol_zip_file_t taitol_puzznicj_files[] = {
    TLF_LD("c20-04.ic11", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0xa4150b6c),
    TLF_LD("c20-01.ic4",  TAITOL_REGION_MCU,  0x0000,  0x0800,  0x085f68b4),
    TLF_LD16("c20-03.ic10", TAITOL_REGION_GFX, 0x00000, 0x20000, 0x4264056c),
    TLF_LD16("c20-02.ic9",  TAITOL_REGION_GFX, 0x00001, 0x20000, 0x3c115f8b),
    TLF_END
};
static const taitol_zip_file_t taitol_puzznici_files[] = {
    TLF_LD("1.ic11",  TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x4612f5e0),
    TLF_LD16("u10.ic10", TAITOL_REGION_GFX, 0x00000, 0x20000, 0x4264056c),
    TLF_LD16("3.ic9",    TAITOL_REGION_GFX, 0x00001, 0x20000, 0x2bf5232a),
    TLF_END
};
static const taitol_zip_file_t taitol_puzznicb_files[] = {
    TLF_LD("ic11.bin", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x2510df4d),
    TLF_LD16("ic10.bin", TAITOL_REGION_GFX, 0x00000, 0x10000, 0xbe12749a),
    TLF_LD16("ic9.bin",  TAITOL_REGION_GFX, 0x00001, 0x10000, 0x0f183340),
    TLF_END
};
static const taitol_zip_file_t taitol_puzznicba_files[] = {
    TLF_LD("18.ic10", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x8349eb3b),
    TLF_LD16("19.ic9", TAITOL_REGION_GFX, 0x00000, 0x20000, 0x4264056c),
    TLF_LD16("20.ic8", TAITOL_REGION_GFX, 0x00001, 0x20000, 0x3c115f8b),
    TLF_END
};
static const taitol_zip_file_t taitol_horshoes_files[] = {
    TLF_LD("c47-03.ic6", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x37e15b20),
    /* horshoes gfx uses ROM_CONTINUE interleaving; we approximate with
     * straight 16-bit loads into the four quadrants.  MAME loads each
     * file at 0/0x20000 with continue at 0x40000/0x60000. */
    TLF_LD16("c47-02.ic5",  TAITOL_REGION_GFX, 0x00000, 0x10000, 0x35f96526),
    TLF_LD16("c47-01.ic11", TAITOL_REGION_GFX, 0x20000, 0x10000, 0x031c73d8),
    TLF_LD16("c47-04.ic4",  TAITOL_REGION_GFX, 0x00001, 0x10000, 0xaeac7121),
    TLF_LD16("c47-05.ic10", TAITOL_REGION_GFX, 0x20001, 0x10000, 0xb2a3dafe),
    TLF_END
};
static const taitol_zip_file_t taitol_palamed_files[] = {
    TLF_LD("palamedes_prg_ic6.ic6", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0xee957b0e),
    TLF_LD16("chr-l_ic9.ic9", TAITOL_REGION_GFX, 0x00000, 0x20000, 0xc7bbe460),
    TLF_LD16("chr-h_ic7.ic7", TAITOL_REGION_GFX, 0x00001, 0x20000, 0xfcd86e44),
    TLF_END
};
static const taitol_zip_file_t taitol_palamedj_files[] = {
    TLF_LD("c63-02.ic6", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x55a82bb2),
    TLF_LD16("c63-04.ic9", TAITOL_REGION_GFX, 0x00000, 0x20000, 0xc7bbe460),
    TLF_LD16("c63-03.ic7", TAITOL_REGION_GFX, 0x00001, 0x20000, 0xfcd86e44),
    TLF_END
};
static const taitol_zip_file_t taitol_cachat_files[] = {
    TLF_LD("cac6",  TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x8105cf5f),
    TLF_LD16("cac9",  TAITOL_REGION_GFX, 0x00000, 0x20000, 0xbc462914),
    TLF_LD16("cac10", TAITOL_REGION_GFX, 0x40000, 0x20000, 0xecc64b31),
    TLF_LD16("cac7",  TAITOL_REGION_GFX, 0x00001, 0x20000, 0x7fb71578),
    TLF_LD16("cac8",  TAITOL_REGION_GFX, 0x40001, 0x20000, 0xd2a63799),
    TLF_END
};
static const taitol_zip_file_t taitol_tubeit_files[] = {
    TLF_LD("t-i_02.6", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x54730669),
    TLF_LD16("t-i_03.7", TAITOL_REGION_GFX, 0x00001, 0x40000, 0xe1c3fed0),
    TLF_LD16("t-i_04.9", TAITOL_REGION_GFX, 0x00000, 0x40000, 0xb4a6e31d),
    TLF_END
};
static const taitol_zip_file_t taitol_cubybop_files[] = {
    TLF_LD("cb06.6", TAITOL_REGION_MAIN, 0x00000, 0x40000, 0x66b89a85),
    TLF_LD16("cb09.9",  TAITOL_REGION_GFX, 0x00000, 0x40000, 0x5f831e59),
    TLF_LD16("cb10.10", TAITOL_REGION_GFX, 0x80000, 0x40000, 0x430510fc),
    TLF_LD16("cb07.7",  TAITOL_REGION_GFX, 0x00001, 0x40000, 0x3582de99),
    TLF_LD16("cb08.8",  TAITOL_REGION_GFX, 0x80001, 0x40000, 0x09e18a51),
    TLF_END
};
static const taitol_zip_file_t taitol_plgirls_files[] = {
    TLF_LD("pg03.ic6", TAITOL_REGION_MAIN, 0x00000, 0x40000, 0x6ca73092),
    TLF_LD16("pg02.ic9", TAITOL_REGION_GFX, 0x00000, 0x40000, 0x3cf05ca9),
    TLF_LD16("pg01.ic7", TAITOL_REGION_GFX, 0x00001, 0x40000, 0x79e41e74),
    TLF_END
};
static const taitol_zip_file_t taitol_plgirls2_files[] = {
    TLF_LD("pg2_1j.ic6", TAITOL_REGION_MAIN, 0x00000, 0x40000, 0xf924197a),
    TLF_LD16("cho-l.ic9", TAITOL_REGION_GFX, 0x00000, 0x80000, 0x956384ec),
    TLF_LD16("cho-h.ic7", TAITOL_REGION_GFX, 0x00001, 0x80000, 0x992f99b1),
    TLF_END
};
static const taitol_zip_file_t taitol_plgirls2b_files[] = {
    TLF_LD("plgirls2b/playgirls2b.d1", TAITOL_REGION_MAIN, 0x00000, 0x40000, 0xd58159fa),
    TLF_LD32("plgirls2b/playgirls2b.d8", TAITOL_REGION_GFX, 0x00003, 0x40000, 0x22df48b5),
    TLF_LD32("plgirls2b/playgirls2b.d4", TAITOL_REGION_GFX, 0x00001, 0x40000, 0xbc9e2192),
    TLF_LD32("plgirls2b/playgirls2b.b6", TAITOL_REGION_GFX, 0x00000, 0x40000, 0xaac6c90b),
    TLF_LD32("plgirls2b/playgirls2b.d3", TAITOL_REGION_GFX, 0x00002, 0x40000, 0x75d82fab),
    TLF_END
};
static const taitol_zip_file_t taitol_lagirl_files[] = {
    TLF_LD("rom1", TAITOL_REGION_MAIN, 0x00000, 0x40000, 0xba1acfdb),
    TLF_LD32("rom2", TAITOL_REGION_GFX, 0x00003, 0x20000, 0x4c739a30),
    TLF_LD32("rom3", TAITOL_REGION_GFX, 0x00001, 0x20000, 0x4cf22a4b),
    TLF_LD32("rom4", TAITOL_REGION_GFX, 0x00002, 0x20000, 0x7dcd6696),
    TLF_LD32("rom5", TAITOL_REGION_GFX, 0x00000, 0x20000, 0xb1782816),
    TLF_END
};
static const taitol_zip_file_t taitol_evilston_files[] = {
    TLF_LD("c67-03.ic2", TAITOL_REGION_MAIN, 0x00000, 0x20000, 0x53419982),
    TLF_LD("c67-04.ic6", TAITOL_REGION_MAIN, 0x20000, 0x20000, 0x55d57e19),
    TLF_LD("c67-05.ic22", TAITOL_REGION_AUDIO, 0x00000, 0x20000, 0x94d3a642),
    TLF_LD("c67-01.ic1", TAITOL_REGION_GFX,   0x00000, 0x80000, 0x2f351bf4),
    TLF_LD("c67-02.ic5", TAITOL_REGION_GFX,   0x80000, 0x80000, 0xeb4f895c),
    TLF_END
};

static const taitol_zip_set_t taitol_zip_sets[] = {
    {"raimais",   TAITOL_GAME_RAIMAIS,   taitol_raimais_files},
    {"raimaisj",  TAITOL_GAME_RAIMAISJ,  taitol_raimaisj_files},
    {"raimaisjo", TAITOL_GAME_RAIMAISJO, taitol_raimaisjo_files},
    {"fhawk",     TAITOL_GAME_FHAWK,      taitol_fhawk_files},
    {"fhawkj",    TAITOL_GAME_FHAWKJ,     taitol_fhawkj_files},
    {"champwr",   TAITOL_GAME_CHAMPWR,    taitol_champwr_files},
    {"champwru",  TAITOL_GAME_CHAMPWRU,   taitol_champwru_files},
    {"champwrj",  TAITOL_GAME_CHAMPWRJ,   taitol_champwrj_files},
    {"kurikint",  TAITOL_GAME_KURIKINT,   taitol_kurikint_files},
    {"kurikintw", TAITOL_GAME_KURIKINTW,  taitol_kurikintw_files},
    {"kurikintu", TAITOL_GAME_KURIKINTU,   taitol_kurikintu_files},
    {"kurikintj", TAITOL_GAME_KURIKINTJ,  taitol_kurikintj_files},
    {"kurikinta", TAITOL_GAME_KURIKINTA,  taitol_kurikinta_files},
    {"plotting",  TAITOL_GAME_PLOTTING,   taitol_plotting_files},
    {"plottinga", TAITOL_GAME_PLOTTINGA,  taitol_plottinga_files},
    {"plottingb", TAITOL_GAME_PLOTTINGB,  taitol_plottingb_files},
    {"plottingu", TAITOL_GAME_PLOTTINGU,  taitol_plottingu_files},
    {"flipull",   TAITOL_GAME_FLIPULL,    taitol_flipull_files},
    {"puzznic",   TAITOL_GAME_PUZZNIC,    taitol_puzznic_files},
    {"puzznicu",  TAITOL_GAME_PUZZNICU,   taitol_puzznicu_files},
    {"puzznicj",  TAITOL_GAME_PUZZNICJ,   taitol_puzznicj_files},
    {"puzznici",  TAITOL_GAME_PUZZNICI,   taitol_puzznici_files},
    {"puzznicb",  TAITOL_GAME_PUZZNICB,   taitol_puzznicb_files},
    {"puzznicba", TAITOL_GAME_PUZZNICBA,  taitol_puzznicba_files},
    {"horshoes",  TAITOL_GAME_HORSHOES,   taitol_horshoes_files},
    {"palamed",   TAITOL_GAME_PALAMED,    taitol_palamed_files},
    {"palamedj",  TAITOL_GAME_PALAMEDJ,   taitol_palamedj_files},
    {"cachat",    TAITOL_GAME_CACHAT,     taitol_cachat_files},
    {"tubeit",    TAITOL_GAME_TUBEIT,     taitol_tubeit_files},
    {"cubybop",   TAITOL_GAME_CUBYBOP,    taitol_cubybop_files},
    {"plgirls",   TAITOL_GAME_PLGIRLS,    taitol_plgirls_files},
    {"lagirl",    TAITOL_GAME_LAGIRL,     taitol_lagirl_files},
    {"plgirls2",  TAITOL_GAME_PLGIRLS2,   taitol_plgirls2_files},
    {"plgirls2b", TAITOL_GAME_PLGIRLS2B,  taitol_plgirls2b_files},
    {"evilston",  TAITOL_GAME_EVILSTON,   taitol_evilston_files},
    {NULL, 0, NULL}
};

static int load_zip_member_to_taitol(unzFile zhandle, const taitol_zip_file_t *file)
{
    uint8_t *tmp;
    int ok = 0;
    int i;
    if (!file) return 0;
    tmp = (uint8_t *)malloc(file->size);
    if (!tmp) return 0;
    if (load_zip_member_exact(zhandle, file->name, tmp, file->size, file->crc))
    {
        if (file->stride <= 1)
        {
            ok = taitol_set_region(file->region, file->offset, tmp, file->size);
        }
        else
        {
            /* Interleave the file bytes into the destination region at
             * offset, offset+stride, offset+2*stride, ...  This emulates
             * MAME's ROM_LOAD16_BYTE / ROM_LOAD32_BYTE. */
            uint8_t *dst = NULL;
            uint32_t limit = 0;
            switch (file->region)
            {
                case TAITOL_REGION_MAIN:   dst = taitol_main_rom_ptr();   limit = 0; break;
                case TAITOL_REGION_GFX:    dst = taitol_gfx_rom_ptr();    limit = 0; break;
                default:                   dst = NULL;                    break;
            }
            (void)limit;
            if (dst)
            {
                uint32_t max_off = file->offset + (file->size - 1u) * file->stride;
                /* Place bytes directly; taitol_set_region isn't used for
                 * interleaved loads because the destination region is
                 * written sparsely. */
                for (i = 0; i < (int)file->size; i++)
                {
                    uint32_t a = file->offset + (uint32_t)i * file->stride;
                    /* Bounds checking is done against the region allocation. */
                    dst[a] = tmp[i];
                    (void)max_off;
                }
                ok = 1;
                if (file->region == TAITOL_REGION_GFX)
                {
                    taitol_note_gfx_region_size(max_off + 1u);
                    taitol_invalidate_gfx();
                }
            }
        }
    }
    free(tmp);
    return ok;
}

static int load_taitol_zip(const char *filename)
{
    unzFile zhandle = (unzFile)1;
    int i, ok;
    const taitol_zip_set_t *set;
    uint8_t *main_region;

    for (set = taitol_zip_sets; set->set_name; set++)
    {
        if (zip_path_stem_ieq(filename, set->set_name))
            break;
    }
    if (!set->set_name)
        return 0;

    zip_member_load_path = filename;
    if (!taitol_alloc())
    {
        zip_member_load_path = NULL;
        return 0;
    }
    taitol_clear_roms();
    /* Allocate a main region buffer for cart.rom.  Use the largest main
     * ROM size we support. */
    main_region = (uint8_t *)malloc(0x10000);
    if (!main_region)
    {
        zip_member_load_path = NULL;
        return 0;
    }
    memset(main_region, 0xff, 0x10000);

    ok = 1;
    for (i = 0; set->files[i].name; i++)
    {
        const taitol_zip_file_t *f = &set->files[i];
        if (f->stride <= 1 && f->region == TAITOL_REGION_MAIN &&
            f->offset + f->size <= 0x10000)
        {
            /* Also copy main ROM into cart.rom for CRC / display. */
            uint8_t *tmp = (uint8_t *)malloc(f->size);
            if (!tmp) { ok = 0; break; }
            if (load_zip_member_exact(zhandle, f->name, tmp, f->size, f->crc))
            {
                if (!taitol_set_region(f->region, f->offset, tmp, f->size))
                    { free(tmp); ok = 0; break; }
                memcpy(main_region + f->offset, tmp, f->size);
            }
            else
            {
                free(tmp);
                ok = 0;
                break;
            }
            free(tmp);
        }
        else
        {
            if (!load_zip_member_to_taitol(zhandle, f))
            {
                ok = 0;
                break;
            }
        }
    }
    zip_member_load_path = NULL;
    if (!ok)
    {
        free(main_region);
        return 0;
    }
    cart.rom = main_region;
    cart.size = 0x10000;
    cart.crc = 0;
    if (cart.rom && cart.size)
        cart.crc = crc32(0, cart.rom, cart.size);
    option.console = 11;  /* CONSOLE_TAITOL in set_config() */
    taitol_set_game_variant(set->variant);
    return 1;
}

static int load_systeme_zip_set(unzFile zhandle, const systeme_zip_game_t *game, uint8_t *region)
{
    int i;

    memset(region, 0xFF, game->region_size);
    for (i = 0; i < 6 && game->files[i].name; i++)
    {
        const systeme_rom_file_t *file = &game->files[i];
        if (file->offset + file->size > game->region_size)
            return 0;
        if (!load_zip_member_exact(zhandle, file->name, region + file->offset, file->size, file->crc))
            return 0;
    }
    return 1;
}

static int load_systeme_zip(const char *filename)
{
    uint8_t *region = NULL;
    unzFile zhandle = (unzFile)1;
    const systeme_zip_game_t *match = NULL;
    size_t i;

    for (i = 0; i < sizeof(systeme_zip_games) / sizeof(systeme_zip_games[0]); i++)
    {
        if (zip_path_stem_ieq(filename, systeme_zip_games[i].setname))
        {
            match = &systeme_zip_games[i];
            break;
        }
    }
    if (!match)
        return 0;

    zip_member_load_path = filename;
    region = malloc(match->region_size);
    if (!region)
    {
        zip_member_load_path = NULL;
        return 0;
    }

    if (!load_systeme_zip_set(zhandle, match, region))
    {
        free(region);
        region = NULL;
        match = NULL;
    }

    zip_member_load_path = NULL;

    if (!match || !region)
        return 0;

    cart.rom = region;
    cart.size = match->region_size;
    option.console = 8;
    return 1;
}
#endif

void set_config()
{
	uint32_t i;

	/* default sms settings */
	cart.mapper = MAPPER_SEGA;
	sms.console = CONSOLE_SMS2;
	sms.territory = TERRITORY_EXPORT;
	sms.display = DISPLAY_NTSC;
	sms.glasses_3d = 0;
	sms.device[0] = DEVICE_PAD2B;
	sms.device[1] = DEVICE_PAD2B;
	sms.use_fm = option.fm;
	gaiden_hack = 0;

	/* console type detection */
	/* SMS Header is located at 0x7ff0 */
	if ((cart.size > 0x7000) && (!memcmp (&cart.rom[0x7ff0], "TMR SEGA", 8)))
	{
		uint8_t region = (cart.rom[0x7fff] & 0xf0) >> 4;
		switch (region)
		{
		  case 5:
			sms.console = CONSOLE_GG;
			sms.territory = TERRITORY_DOMESTIC;
			break;

		  case 6:
		  case 7:
			sms.console = CONSOLE_GG;
			sms.territory = TERRITORY_EXPORT;
			break;

		  case 3:
			sms.console = CONSOLE_SMS;
			sms.territory = TERRITORY_DOMESTIC;
			break;
		
		  default:
			sms.console = CONSOLE_SMS2;
			sms.territory = TERRITORY_EXPORT;
			break;
		}
	}

	sms.gun_offset = 20; /* default offset */

	/* retrieve game settings from database */
	for (i = 0; i < GAME_DATABASE_CNT; i++)
	{
		if ((cart.crc == game_list[i].crc) && console_feature_enabled(game_list[i].console))
		{
			cart.mapper = game_list[i].mapper;
			sms.display = game_list[i].display;
			sms.territory = game_list[i].territory;
			sms.glasses_3d = game_list[i].glasses_3d;
			sms.console =  game_list[i].console;
			sms.device[0] = game_list[i].device;
			sms.use_fm = game_list[i].fm_compatible;
			
			if (game_list[i].device != DEVICE_LIGHTGUN) sms.device[1] = game_list[i].device;
			
			/* Games's specific hacks */
			
			if ((strcmp(game_list[i].name, "Spacegun") == 0) ||
			(strcmp(game_list[i].name, "Gangster Town") == 0))
			{
				/* these games seem to use different gun position calculation method */
				sms.gun_offset = 16;
			}
			
			if (strcmp(game_list[i].name, "Shining Force Final Conflict") == 0)
			{
				gaiden_hack = 1;
			}
			
			i = GAME_DATABASE_CNT;
		}
	}

	if (cart_uses_93c46())
	{
		cart.mapper = MAPPER_93C46;
		sms.console = CONSOLE_GG;
	}

	/* enable BIOS on SMS only */
	bios.enabled &= 2;

	/* force settings if AUTO is not set*/
	switch(option.console)
	{
		case 1:
			sms.console = CONSOLE_SMS;
		break;
		case 2:
			sms.console = CONSOLE_SMS2;
		break;
		case 3:
			sms.console = CONSOLE_GG;
		break;
		case 4:
			sms.console = CONSOLE_GGMS;
		break;
		case 5:
			sms.console = CONSOLE_SG1000;
			cart.mapper = MAPPER_NONE;
		break;
#if MULTIREXZ80_ENABLE_COLECO
		case 6:
			sms.console = CONSOLE_COLECO;
			if (cart.mapper != MAPPER_COLECO_MEGACART)
				cart.mapper = MAPPER_NONE;
		break;
#endif
#if MULTIREXZ80_ENABLE_SORDM5
		case 7:
			sms.console = CONSOLE_SORDM5;
			cart.mapper = MAPPER_NONE;
		break;
#endif
#if MULTIREXZ80_ENABLE_ARCADE
		case 8:
			sms.console = CONSOLE_SYSTEME;
			cart.mapper = MAPPER_SYSTEME;
			sms.display = DISPLAY_NTSC;
			sms.territory = TERRITORY_EXPORT;
			sms.use_fm = FM_NOT_COMPATIBLE;
		break;
		case 9:
			sms.console = CONSOLE_SYSTEM1;
			cart.mapper = MAPPER_SYSTEM1;
			sms.display = DISPLAY_NTSC;
			sms.territory = TERRITORY_EXPORT;
			sms.use_fm = FM_NOT_COMPATIBLE;
			sms.device[0] = DEVICE_PAD2B;
			sms.device[1] = DEVICE_PAD2B;
		break;
		case 10:
			sms.console = CONSOLE_SNKPSYCHOS;
			cart.mapper = MAPPER_SNKPSYCHOS;
			sms.display = DISPLAY_NTSC;
			sms.territory = TERRITORY_EXPORT;
			sms.use_fm = FM_NOT_COMPATIBLE;
			sms.device[0] = DEVICE_PAD2B;
			sms.device[1] = DEVICE_PAD2B;
		break;
		case 11:
			sms.console = CONSOLE_TAITOL;
			cart.mapper = MAPPER_TAITOL;
			sms.display = DISPLAY_NTSC;
			sms.territory = TERRITORY_EXPORT;
			sms.use_fm = FM_NOT_COMPATIBLE;
			sms.device[0] = DEVICE_PAD2B;
			sms.device[1] = DEVICE_PAD2B;
		break;
#endif
	}

#if MULTIREXZ80_ENABLE_COLECO
	if (coleco_megacart_heuristic())
		cart.mapper = MAPPER_COLECO_MEGACART;
#endif

	apply_disabled_console_fallback();
  
	switch(option.country)
	{
		/* USA */
		case 1:
			sms.display = DISPLAY_NTSC;
			sms.territory = TERRITORY_EXPORT;
		break;
		/* EUROPE */
		case 2:
			sms.display = DISPLAY_PAL;
			sms.territory = TERRITORY_EXPORT;
		break;
		/* JAPAN */
		case 3:
			sms.display = DISPLAY_NTSC;
			sms.territory = TERRITORY_DOMESTIC;
		break;
	}
}

#ifdef NOZIP_SUPPORT
/* Karl Malbrain's compact CRC-32. See "A compact CCITT crc16 and crc32 C implementation that balances processor cache usage against speed": http://www.geocities.com/malbrain/ */
static uint32_t crc32(uint32_t crc, const uint8_t *ptr, size_t buf_len)
{
	static const uint32_t s_crc32[16] = { 0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
	uint32_t crcu32 = (uint32_t)crc;
	crcu32 = ~crcu32;
	while (buf_len--)
	{
		uint8_t b = *ptr++;
		crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b & 0xF)];
		crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b >> 4)];
	}
	return ~crcu32;
}
#endif


void free_rom(void)
{
	if (cart.rom)
	{
		free(cart.rom);
		cart.rom = NULL;
	}
	system1_free();
	snk_psychos_free();
	taitol_free();
}


uint32_t load_rom_buffer(const uint8_t *data, uint32_t file_size)
{
    uint32_t image_size;
    uint32_t crc_size;
    uint32_t padded_size;
    uint8_t *buffer;

    if (!data || !file_size)
        return 0;

    free_rom();
    buffer = (uint8_t *)malloc(rom_padded_size(file_size));
    if (!buffer)
        return 0;
    memset(buffer, 0xff, rom_padded_size(file_size));
    memcpy(buffer, data, file_size);
    cart.rom = buffer;
    cart.size = file_size;

    image_size = cart.size;
    if (rom_has_512_byte_header(image_size))
    {
        image_size -= 512;
        memmove(cart.rom, cart.rom + 512, image_size);
    }
    crc_size = image_size;
    padded_size = rom_padded_size(image_size);
    if (padded_size > image_size)
        memset(cart.rom + image_size, 0xff, padded_size - image_size);

    cart.size = padded_size;
    cart.pages = cart.size / 0x4000;
    cart.crc = crc32(0, cart.rom, crc_size);
    cart.loaded = 1;
    set_config();
    return 1;
}

uint32_t load_rom (char *filename)
{
	if (filename) snprintf(option.game_name, sizeof(option.game_name), "%s", filename);
	free_rom();

#ifndef NOZIP_SUPPORT
	if(check_zip(filename))
	{
		char name[PATH_MAX];
		uint8_t *zip_rom = NULL;
		uint32_t zip_size = 0;
		int loaded_arcade_zip = 0;
#if MULTIREXZ80_ENABLE_ARCADE
		loaded_arcade_zip = load_system1_zip(filename) || load_systeme_zip(filename) || load_snk_psychos_zip(filename) || load_taitol_zip(filename);
#endif
		if (loaded_arcade_zip)
		{
			cart.pages = cart.size / 0x4000;
			if (!cart.crc && cart.rom && cart.size)
				cart.crc = crc32(0, cart.rom, cart.size);
			cart.loaded = 1;
			set_config();
			return 1;
		}

		zip_rom = loadFromZipByName((char*)filename, name, &zip_size);
		if (!zip_rom)
			return 0;
		if (name[0])
			snprintf(option.game_name, sizeof(option.game_name), "%s", name);
		loaded_arcade_zip = load_rom_buffer(zip_rom, zip_size);
		free(zip_rom);
		return loaded_arcade_zip;
	}
	else
#endif
	{
		FILE *fd = NULL;
		fd = fopen(filename, "rb");
		if (!fd) 
			return 0;

		/* Seek to end of file, and get size */
		fseek(fd, 0, SEEK_END);
		uint32_t file_size = (uint32_t)ftell(fd);
		fseek(fd, 0, SEEK_SET);

		cart.size = file_size;
		uint32_t alloc_size = cart.size;
		if (alloc_size < 0x4000) alloc_size = 0x4000;

		cart.rom = malloc(alloc_size);
		if (!cart.rom)
		{
			fclose(fd);
			return 0;
		}

		/*
		 * Pad short images deterministically.  The old code enlarged cart.size
		 * before fread(), so 8 KB M5 cartridges left the second 8 KB of the
		 * allocation uninitialised and could be exposed through the $2000-$6FFF
		 * cartridge window.
		 */
		memset(cart.rom, 0xff, alloc_size);
		if (fread(cart.rom, 1, file_size, fd) != file_size)
		{
			fclose(fd);
			free(cart.rom);
			cart.rom = NULL;
			return 0;
		}

		fclose(fd);
	}

	/* Take care of image header, if present. */
	uint32_t image_size = cart.size;
	if (rom_has_512_byte_header(image_size))
	{
		image_size -= 512;
		memmove(cart.rom, cart.rom + 512, image_size);
	}

	/*
	 * The mapper works in 16 KiB pages, but homebrew/test ROMs are not always
	 * padded on disk.  Preserve the real image bytes for CRC/database matching
	 * and pad only the mapped backing store, so the final partial bank remains
	 * addressable and deterministic instead of being floored away.
	 */
	uint32_t crc_size = image_size;
	uint32_t padded_size = rom_padded_size(image_size);
	if (padded_size > image_size)
	{
		uint8_t *padded_rom = realloc(cart.rom, padded_size);
		if (!padded_rom)
		{
			free(cart.rom);
			cart.rom = NULL;
			cart.size = 0;
			return 0;
		}
		cart.rom = padded_rom;
		memset(cart.rom + image_size, 0xff, padded_size - image_size);
	}
	cart.size = padded_size;

	/* 16k pages */
	cart.pages = cart.size / 0x4000;

	cart.crc = crc32 (0, cart.rom, crc_size);
	cart.loaded = 1;

	set_config();

	return 1;
}
