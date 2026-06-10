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

/* scaler.c: code for selecting (etc) scalers
 * Copyright (C) 2003 Fredrick Meunier, Philip Kendall
 * Copyright (c) 2015 Sergio Baldoví
 * 
 * $Id: scaler.c 5432 2016-05-01 04:16:09Z fredm $
 *
 * Originally taken from ScummVM - Scumm Interpreter
 * Copyright (C) 2001  Ludvig Strigeus
 * Copyright (C) 2001/2002 The ScummVM project
 *
 * HQ2x and HQ3x scalers taken from HiEnd3D Demos (http://www.hiend3d.com)
 * Copyright (C) 2003 MaxSt ( maxst@hiend3d.com )
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */
#include "scale2x.h"

/* No license ? Scale2x was licensed under the GPLv2 and this code was found among in GPLv2 licensed code.
 * I had to make some minor modifications to it and the code is fairly small but i think we should be good. */

#define SCALE2X_PIXEL(E, B, D, F, H, OUT0, OUT1, OUT2, OUT3) \
	do { \
		(OUT0) = ((D) == (B) && (B) != (F) && (D) != (H)) ? (D) : (E); \
		(OUT1) = ((B) == (F) && (B) != (D) && (F) != (H)) ? (F) : (E); \
		(OUT2) = ((D) == (H) && (D) != (B) && (H) != (F)) ? (D) : (E); \
		(OUT3) = ((H) == (F) && (D) != (H) && (B) != (F)) ? (F) : (E); \
	} while (0)

void scale2x(uint16_t* restrict srcpixels, uint16_t* restrict dstpixels, const uint32_t srcpitch, const uint32_t dstpitch, const uint32_t width, uint32_t height)
{
	const uint32_t src_stride = srcpitch / sizeof(uint16_t);
	const uint32_t dst_stride = dstpitch / sizeof(uint16_t);
	uint32_t x, y;

	if (!srcpixels || !dstpixels || width == 0 || height == 0 || src_stride == 0 || dst_stride == 0)
		return;

	for (y = 0; y < height; y++)
	{
		const uint16_t *row_above = srcpixels + ((y == 0) ? 0 : (y - 1)) * src_stride;
		const uint16_t *row = srcpixels + y * src_stride;
		const uint16_t *row_below = srcpixels + ((y + 1 < height) ? (y + 1) : y) * src_stride;
		uint16_t *dst0 = dstpixels + (y * 2) * dst_stride;
		uint16_t *dst1 = dst0 + dst_stride;

		for (x = 0; x < width; x++)
		{
			const uint16_t B = row_above[x];
			const uint16_t D = row[(x == 0) ? 0 : (x - 1)];
			const uint16_t E = row[x];
			const uint16_t F = row[(x + 1 < width) ? (x + 1) : x];
			const uint16_t H = row_below[x];
			SCALE2X_PIXEL(E, B, D, F, H, dst0[x * 2], dst0[x * 2 + 1], dst1[x * 2], dst1[x * 2 + 1]);
		}
	}
}

void scale2x32(uint32_t* restrict srcpixels, uint32_t* restrict dstpixels, const uint32_t srcpitch, const uint32_t dstpitch, const uint32_t width, uint32_t height)
{
	const uint32_t src_stride = srcpitch / sizeof(uint32_t);
	const uint32_t dst_stride = dstpitch / sizeof(uint32_t);
	uint32_t x, y;

	if (!srcpixels || !dstpixels || width == 0 || height == 0 || src_stride == 0 || dst_stride == 0)
		return;

	for (y = 0; y < height; y++)
	{
		const uint32_t *row_above = srcpixels + ((y == 0) ? 0 : (y - 1)) * src_stride;
		const uint32_t *row = srcpixels + y * src_stride;
		const uint32_t *row_below = srcpixels + ((y + 1 < height) ? (y + 1) : y) * src_stride;
		uint32_t *dst0 = dstpixels + (y * 2) * dst_stride;
		uint32_t *dst1 = dst0 + dst_stride;

		for (x = 0; x < width; x++)
		{
			const uint32_t B = row_above[x];
			const uint32_t D = row[(x == 0) ? 0 : (x - 1)];
			const uint32_t E = row[x];
			const uint32_t F = row[(x + 1 < width) ? (x + 1) : x];
			const uint32_t H = row_below[x];
			SCALE2X_PIXEL(E, B, D, F, H, dst0[x * 2], dst0[x * 2 + 1], dst1[x * 2], dst1[x * 2 + 1]);
		}
	}
}
