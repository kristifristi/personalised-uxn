#include <stdio.h>
#include <stdlib.h>

#include "uxn.h"
#include "devices/system.h"
#include "devices/console.h"
#include "devices/file.h"
#include "devices/datetime.h"

/*
Copyright (c) 2021-2024 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

Uxn uxn;
Uint8 ram[0x10000 * RAM_PAGES];

Uint8
emu_dei(Uint8 addr)
{
	switch(addr & 0xf0) {
	case 0x00: return system_dei(addr);
	case 0xc0: return datetime_dei(addr);
	}
	return uxn.dev[addr];
}

void
emu_deo(Uint8 addr, Uint8 value)
{
	uxn.dev[addr] = value;
	switch(addr & 0xf0) {
	case 0x00: system_deo(addr); break;
	case 0x10: console_deo(addr); break;
	case 0xa0: file_deo(addr); break;
	case 0xb0: file_deo(addr); break;
	}
}

static void
emu_run(void)
{
	while(!uxn.dev[0x0f]) {
		int c = fgetc(stdin);
		if(c == EOF) {
			console_input(0x00, CONSOLE_END);
			break;
		}
		console_input(c, CONSOLE_STD);
	}
}

int
main(int argc, char **argv)
{
	if(argc > 1 && argv[1][0] == '-' && argv[1][1] == 'v')
		fprintf(stdout, "Uxncli - Varvara Emulator, 11 Nov 2024.\n");
	else if(argc == 1 || !system_boot(ram, argv[1]))
		return !fprintf(stdout, "usage: %s [-v] file.rom [args..]\n", argv[0]);
	else {
		uxn.dev[0x17] = argc - 2;
		if(uxn_eval(PAGE_PROGRAM) && PEEK2(uxn.dev + 0x10))
			console_listen(2, argc, argv), emu_run();
	}
	return uxn.dev[0x0f] & 0x7f;
}
