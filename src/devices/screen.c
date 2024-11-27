#include <stdlib.h>

#include "../uxn.h"
#include "screen.h"

/*
Copyright (c) 2021-2024 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

UxnScreen uxn_screen;

#define MAR(x) (x + 0x08)
#define MAR2(x) (x + 0x10)

/* c = !ch ? (color % 5 ? color >> 2 : 0) : color % 4 + ch == 1 ? 0 : (ch - 2 + (color & 3)) % 3 + 1; */

static Uint8 blending[4][16] = {
	{0, 0, 0, 0, 1, 0, 1, 1, 2, 2, 0, 2, 3, 3, 3, 0},
	{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},
	{1, 2, 3, 1, 1, 2, 3, 1, 1, 2, 3, 1, 1, 2, 3, 1},
	{2, 3, 1, 2, 2, 3, 1, 2, 2, 3, 1, 2, 2, 3, 1, 2}};

int
screen_changed(void)
{
	if(uxn_screen.x1 < 0)
		uxn_screen.x1 = 0;
	else if(uxn_screen.x1 >= uxn_screen.width)
		uxn_screen.x1 = uxn_screen.width;
	if(uxn_screen.y1 < 0)
		uxn_screen.y1 = 0;
	else if(uxn_screen.y1 >= uxn_screen.height)
		uxn_screen.y1 = uxn_screen.height;
	if(uxn_screen.x2 < 0)
		uxn_screen.x2 = 0;
	else if(uxn_screen.x2 >= uxn_screen.width)
		uxn_screen.x2 = uxn_screen.width;
	if(uxn_screen.y2 < 0)
		uxn_screen.y2 = 0;
	else if(uxn_screen.y2 >= uxn_screen.height)
		uxn_screen.y2 = uxn_screen.height;
	return uxn_screen.x2 > uxn_screen.x1 && uxn_screen.y2 > uxn_screen.y1;
}

void
screen_change(int x1, int y1, int x2, int y2)
{
	if(x1 < uxn_screen.x1) uxn_screen.x1 = x1;
	if(y1 < uxn_screen.y1) uxn_screen.y1 = y1;
	if(x2 > uxn_screen.x2) uxn_screen.x2 = x2;
	if(y2 > uxn_screen.y2) uxn_screen.y2 = y2;
}

void
screen_fill(Uint8 *layer, int color)
{
	int i, len = MAR2(uxn_screen.width) * uxn_screen.height;
	for(i = 0; i < len; i++)
		layer[i] = color;
}

void
screen_palette(void)
{
	int i, shift;
	for(i = 0, shift = 4; i < 4; ++i, shift ^= 4) {
		Uint8
			r = (uxn.dev[0x8 + i / 2] >> shift) & 0xf,
			g = (uxn.dev[0xa + i / 2] >> shift) & 0xf,
			b = (uxn.dev[0xc + i / 2] >> shift) & 0xf;
		uxn_screen.palette[i] = 0x0f000000 | r << 16 | g << 8 | b;
		uxn_screen.palette[i] |= uxn_screen.palette[i] << 4;
	}
	screen_change(0, 0, uxn_screen.width, uxn_screen.height);
}

void
screen_resize(Uint16 width, Uint16 height, int scale)
{
	Uint8 *bg, *fg;
	Uint32 *pixels = NULL;
	int dim_change = uxn_screen.width != width || uxn_screen.height != height;
	if(width < 0x8 || height < 0x8 || width >= 0x800 || height >= 0x800 || scale < 1 || scale >= 4)
		return;
	if(uxn_screen.width == width && uxn_screen.height == height && uxn_screen.scale == scale)
		return;
	if(dim_change) {
		int wmar = MAR2(width), hmar = MAR2(height);
		bg = malloc(wmar * hmar), fg = malloc(wmar * hmar);
		if(bg && fg)
			pixels = realloc(uxn_screen.pixels, width * height * sizeof(Uint32) * scale * scale);
		if(!bg || !fg || !pixels) {
			free(bg), free(fg);
			return;
		}
		free(uxn_screen.bg), free(uxn_screen.fg);
	} else {
		bg = uxn_screen.bg, fg = uxn_screen.fg;
		pixels = realloc(uxn_screen.pixels, width * height * sizeof(Uint32) * scale * scale);
		if(!pixels)
			return;
	}
	uxn_screen.bg = bg, uxn_screen.fg = fg;
	uxn_screen.pixels = pixels;
	uxn_screen.width = width, uxn_screen.height = height, uxn_screen.scale = scale;
	if(dim_change)
		screen_fill(uxn_screen.bg, 0), screen_fill(uxn_screen.fg, 0);
	emu_resize(width, height);
	screen_change(0, 0, width, height);
}

void
screen_redraw(void)
{
	int i, x, y, k, l, s = uxn_screen.scale;
	Uint8 *fg = uxn_screen.fg, *bg = uxn_screen.bg;
	Uint16 w = uxn_screen.width, h = uxn_screen.height;
	Uint16 x1 = uxn_screen.x1, y1 = uxn_screen.y1;
	Uint16 x2 = uxn_screen.x2 > w ? w : uxn_screen.x2, y2 = uxn_screen.y2 > h ? h : uxn_screen.y2;
	Uint32 palette[16], *pixels = uxn_screen.pixels;
	uxn_screen.x1 = uxn_screen.y1 = 9000;
	uxn_screen.x2 = uxn_screen.y2 = 0;
	for(i = 0; i < 16; i++)
		palette[i] = uxn_screen.palette[(i >> 2) ? (i >> 2) : (i & 3)];
	for(y = y1; y < y2; y++) {
		int ys = y * s;
		for(x = x1, i = MAR(x) + MAR(y) * MAR2(w); x < x2; x++, i++) {
			int c = palette[fg[i] << 2 | bg[i]];
			for(k = 0; k < s; k++) {
				int oo = ((ys + k) * w + x) * s;
				for(l = 0; l < s; l++)
					pixels[oo + l] = c;
			}
		}
	}
}

/* screen registers */

static int rX, rY, rA, rMX, rMY, rMA, rML, rDX, rDY;

Uint8
screen_dei(Uxn *u, Uint8 addr)
{
	switch(addr) {
	case 0x22: return uxn_screen.width >> 8;
	case 0x23: return uxn_screen.width;
	case 0x24: return uxn_screen.height >> 8;
	case 0x25: return uxn_screen.height;
	case 0x28: return rX >> 8;
	case 0x29: return rX;
	case 0x2a: return rY >> 8;
	case 0x2b: return rY;
	case 0x2c: return rA >> 8;
	case 0x2d: return rA;
	default: return u->dev[addr];
	}
}

void
screen_deo(Uxn *u, Uint8 addr)
{
	switch(addr) {
	case 0x23: screen_resize(PEEK2(&uxn.dev[0x22]), uxn_screen.height, uxn_screen.scale); return;
	case 0x25: screen_resize(uxn_screen.width, PEEK2(&uxn.dev[0x24]), uxn_screen.scale); return;
	case 0x26: rMX = u->dev[0x26] & 0x1, rMY = u->dev[0x26] & 0x2, rMA = u->dev[0x26] & 0x4, rML = u->dev[0x26] >> 4, rDX = rMX << 3, rDY = rMY << 2; return;
	case 0x28:
	case 0x29: rX = (u->dev[0x28] << 8) | u->dev[0x29], rX = twos(rX); return;
	case 0x2a:
	case 0x2b: rY = (u->dev[0x2a] << 8) | u->dev[0x2b], rY = twos(rY); return;
	case 0x2c:
	case 0x2d: rA = (u->dev[0x2c] << 8) | u->dev[0x2d]; return;
	case 0x2e: {
		int ctrl = u->dev[0x2e];
		int color = ctrl & 0x3;
		int len = MAR2(uxn_screen.width);
		Uint8 *layer = ctrl & 0x40 ? uxn_screen.fg : uxn_screen.bg;
		/* fill mode */
		if(ctrl & 0x80) {
			int x1, y1, x2, y2, ax, bx, ay, by, hor, ver;
			if(ctrl & 0x10)
				x1 = MAR(0), x2 = MAR(rX);
			else
				x1 = MAR(rX), x2 = MAR(uxn_screen.width);
			if(ctrl & 0x20)
				y1 = MAR(0), y2 = MAR(rY);
			else
				y1 = MAR(rY), y2 = MAR(uxn_screen.height);
			hor = x2 - x1, ver = y2 - y1;
			for(ay = y1 * len, by = ay + ver * len; ay < by; ay += len)
				for(ax = ay + x1, bx = ax + hor; ax < bx; ax++)
					layer[ax] = color;
			screen_change(x1, y1, x2, y2);
		}
		/* pixel mode */
		else {
			if(rX >= 0 && rY >= 0 && rX < len && rY < uxn_screen.height)
				layer[MAR(rX) + MAR(rY) * len] = color;
			screen_change(rX, rY, rX + 1, rY + 1);
			if(rMX) rX++;
			if(rMY) rY++;
		}
		return;
	}
	case 0x2f: {
		int i;
		int ctrl = u->dev[0x2f];
		int twobpp = !!(ctrl & 0x80);
		int blend = ctrl & 0xf;
		int fx = ctrl & 0x10 ? -1 : 1, fy = ctrl & 0x20 ? -1 : 1;
		int x1, x2, y1, y2, ax, ay, qx, qy, x = rX, y = rY;
		int dxy = rDX * fy, dyx = rDY * fx, addr_incr = rMA << (1 + twobpp);
		int len = MAR2(uxn_screen.width);
		Uint8 *layer = ctrl & 0x40 ? uxn_screen.fg : uxn_screen.bg;
		if(twobpp)
			for(i = 0; i <= rML; i++, x += dyx, y += dxy, rA += addr_incr) {
				Uint16 xx = MAR(x), yy = MAR(y);
				if(xx < MAR(uxn_screen.width) && MAR(yy) < MAR2(uxn_screen.height)) {
					Uint8 *sprite = &u->ram[rA];
					int opaque = blend % 5, by = (yy + 8) * len;
					for(ay = yy * len, qy = fy < 0 ? 7 : 0; ay < by; ay += len, qy += fy) {
						int ch1 = sprite[qy], ch2 = sprite[qy + 8] << 1, bx = xx + 8 + ay;
						for(ax = xx + ay, qx = fx > 0 ? 7 : 0; ax < bx; ax++, qx -= fx) {
							int color = ((ch1 >> qx) & 1) | ((ch2 >> qx) & 2);
							if(opaque || color) layer[ax] = blending[color][blend];
						}
					}
				}
			}
		else
			for(i = 0; i <= rML; i++, x += dyx, y += dxy, rA += addr_incr) {
				Uint16 xx = MAR(x), yy = MAR(y);
				if(xx < MAR(uxn_screen.width) && MAR(yy) < MAR2(uxn_screen.height)) {
					Uint8 *sprite = &u->ram[rA];
					int opaque = blend % 5, by = (yy + 8) * len;
					for(ay = yy * len, qy = fy < 0 ? 7 : 0; ay < by; ay += len, qy += fy) {
						int ch1 = sprite[qy], bx = xx + 8 + ay;
						for(ax = xx + ay, qx = fx > 0 ? 7 : 0; ax < bx; ax++, qx -= fx) {
							int color = (ch1 >> qx) & 1;
							if(opaque || color) layer[ax] = blending[color][blend];
						}
					}
				}
			}
		if(fx < 0)
			x1 = x, x2 = rX;
		else
			x1 = rX, x2 = x;
		if(fy < 0)
			y1 = y, y2 = rY;
		else
			y1 = rY, y2 = y;
		screen_change(x1, y1, x2 + 8, y2 + 8);
		if(rMX) rX += rDX * fx;
		if(rMY) rY += rDY * fy;
		return;
	}
	}
}
