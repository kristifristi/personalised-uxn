#include <stdio.h>
#include <stdlib.h>

#include "../uxn.h"

#include "system.h"

/*
Copyright (c) 2022-2024 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

char *boot_rom;

static void
system_zero(int soft)
{
	int i;
	for(i = soft ? 0x100 : 0; i < 0x10000; i++)
		uxn.ram[i] = 0;
	for(i = 0x0; i < 0x100; i++)
		uxn.dev[i] = 0;
	uxn.wst.ptr = uxn.rst.ptr = 0;
}

static int
system_load(Uint8 *ram, char *filename)
{
	FILE *f = fopen(filename, "rb");
	if(f) {
		int i = 0, l = fread(ram, 0x10000 - PAGE_PROGRAM, 1, f);
		while(l && ++i < RAM_PAGES)
			l = fread(ram + 0x10000 * i - PAGE_PROGRAM, 0x10000, 1, f);
		fclose(f);
	}
	return !!f;
}

static void
system_print(Stack *s)
{
	Uint8 i;
	for(i = s->ptr - 8; i != (Uint8)(s->ptr); i++)
		fprintf(stderr, "%02x%c", s->dat[i], i == 0xff ? '|' : ' ');
	fprintf(stderr, "< \n");
}

void
system_inspect(void)
{
	fprintf(stderr, "WST "), system_print(&uxn.wst);
	fprintf(stderr, "RST "), system_print(&uxn.rst);
}

void
system_image(void)
{
	int i, len = 0;
	FILE *f = fopen("image.rom", "wb");
	for(i = PAGE_PROGRAM; i < 0x10000; i++)
		if(uxn.ram[i])
			len = i;
	fwrite(uxn.ram + PAGE_PROGRAM, len - PAGE_PROGRAM + 1, 1, f), fclose(f);
	printf("Saved image.rom, %d bytes\n", len);
}

int
system_error(char *msg, const char *err)
{
	fprintf(stderr, "%s: %s\n", msg, err), fflush(stderr);
	return 0;
}

void
system_reboot(int soft)
{
	system_zero(soft);
	if(system_load(&uxn.ram[PAGE_PROGRAM], boot_rom))
		uxn_eval(PAGE_PROGRAM);
}

int
system_boot(Uint8 *ram, char *boot)
{
	uxn.ram = ram, boot_rom = boot;
	return system_load(uxn.ram + PAGE_PROGRAM, boot);
}

/* IO */

Uint8
system_dei(Uint8 addr)
{
	switch(addr) {
	case 0x4: return uxn.wst.ptr;
	case 0x5: return uxn.rst.ptr;
	default: return uxn.dev[addr];
	}
}

void
system_deo(Uint8 port)
{
	switch(port) {
	case 0x3: {
		Uint16 addr = PEEK2(uxn.dev + 2);
		if(uxn.ram[addr] == 0x0) {
			Uint16 i, value = uxn.ram[addr + 7], length = PEEK2(uxn.ram + addr + 1);
			unsigned int dst_page = PEEK2(uxn.ram + addr + 3), dst_addr = PEEK2(uxn.ram + addr + 5);
			unsigned int dst = dst_page * 0x10000;
			if(dst_page < RAM_PAGES)
				for(i = 0; i < length; i++)
					uxn.ram[dst + (Uint16)(dst_addr + i)] = value;
		} else if(uxn.ram[addr] == 0x1) {
			Uint16 i, length = PEEK2(uxn.ram + addr + 1);
			unsigned int a_page = PEEK2(uxn.ram + addr + 3), a_addr = PEEK2(uxn.ram + addr + 5);
			unsigned int b_page = PEEK2(uxn.ram + addr + 7), b_addr = PEEK2(uxn.ram + addr + 9);
			unsigned int src = a_page * 0x10000, dst = b_page * 0x10000;
			if(a_page < RAM_PAGES && b_page < RAM_PAGES)
				for(i = 0; i < length; i++)
					uxn.ram[dst + (Uint16)(b_addr + i)] = uxn.ram[src + (Uint16)(a_addr + i)];
		} else if(uxn.ram[addr] == 0x2) {
			Uint16 i, length = PEEK2(uxn.ram + addr + 1);
			unsigned int a_page = PEEK2(uxn.ram + addr + 3), a_addr = PEEK2(uxn.ram + addr + 5);
			unsigned int b_page = PEEK2(uxn.ram + addr + 7), b_addr = PEEK2(uxn.ram + addr + 9);
			unsigned int src = a_page * 0x10000, dst = b_page * 0x10000;
			if(a_page < RAM_PAGES && b_page < RAM_PAGES)
				for(i = length - 1; i != 0xffff; i--)
					uxn.ram[dst + (Uint16)(b_addr + i)] = uxn.ram[src + (Uint16)(a_addr + i)];
		} else
			fprintf(stderr, "Unknown Expansion Command 0x%02x\n", uxn.ram[addr]);
		break;
	}
	case 0x4:
		uxn.wst.ptr = uxn.dev[4];
		break;
	case 0x5:
		uxn.rst.ptr = uxn.dev[5];
		break;
	case 0xe:
		system_inspect();
		break;
	}
}
