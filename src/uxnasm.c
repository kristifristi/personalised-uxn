#include <stdio.h>

/*
Copyright (c) 2021-2024 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#define PAGE 0x0100

typedef unsigned char Uint8;
typedef signed char Sint8;
typedef unsigned short Uint16;

typedef struct {
	char *name, rune, *content;
	Uint16 addr, refs;
} Item;

typedef struct {
	int line;
	char *path;
} Context;

static int ptr, length;
static char token[0x40], scope[0x40], sublabel[0x80], lambda[0x05];
static char dict[0x10000], *dictnext = dict;
static Uint8 data[0x10000], lambda_stack[0x100], lambda_ptr, lambda_len;
static Uint16 label_len, refs_len, macro_len;
static Item labels[0x400], refs[0x1000], macros[0x100];

/* clang-format off */

static char *runes = "|$@&,_.-;=!?#\"%~";
static char *hexad = "0123456789abcdef";
static char ops[][4] = {
	"LIT", "INC", "POP", "NIP", "SWP", "ROT", "DUP", "OVR",
	"EQU", "NEQ", "GTH", "LTH", "JMP", "JCN", "JSR", "STH",
	"LDZ", "STZ", "LDR", "STR", "LDA", "STA", "DEI", "DEO",
	"ADD", "SUB", "MUL", "DIV", "AND", "ORA", "EOR", "SFT"
};

static int   cndx(char *s, char t) { int i = 0; char c; while((c = *s++)) { if(c == t) return i; i++; } return -1; } /* chr in str */
static int   sihx(char *s) { char c; while((c = *s++)) if(cndx(hexad, c) < 0) return 0; return 1; } /* str is hex */
static int   shex(char *s) { int n = 0; char c; while((c = *s++)) { n = n << 4, n |= cndx(hexad, c); } return n; } /* str to num */
static int   scmp(char *a, char *b, int len) { int i = 0; while(a[i] == b[i]) if(!a[i] || ++i >= len) return 1; return 0; } /* str compare */
static int   slen(char *s) { int i = 0; while(s[i]) i++; return i; } /* str length */
static char *scpy(char *src, char *dst, int len) { int i = 0; while((dst[i] = src[i]) && i < len - 2) i++; dst[i + 1] = '\0'; return dst; } /* str copy */
static char *scat(char *dst, char *src) { char *o = dst + slen(dst); while(*src) *o++ = *src++; *o = '\0'; return dst; } /* str concat */
static char *push(char *s, char c) { char *o = dictnext; while((*dictnext++ = *s++) && *s); *dictnext++ = c; return o; } /* save str */

#define isopcode(x) (findopcode(x) || scmp(x, "BRK", 4))
#define writeshort(x) (writebyte(x >> 8, ctx) && writebyte(x & 0xff, ctx))
#define makesublabel(x) push(scat(scat(scpy(scope, sublabel, 0x40), "/"), x), 0)
#define findlabel(x) finditem(x, labels, label_len)
#define findmacro(x) finditem(x, macros, macro_len)
#define error_top(name, msg) !fprintf(stderr, "%s: %s\n", name, msg)
#define error_asm(name) !fprintf(stderr, "%s: %s in @%s, %s:%d.\n", name, token, scope, ctx->path, ctx->line)

/* clang-format on */

static int parse(char *w, FILE *f, Context *ctx);

static Item *
finditem(char *name, Item *list, int len)
{
	int i;
	if(name[0] == '&')
		name = makesublabel(name + 1);
	for(i = 0; i < len; i++)
		if(scmp(list[i].name, name, 0x40))
			return &list[i];
	return NULL;
}

static Uint8
findopcode(char *s)
{
	int i;
	for(i = 0; i < 0x20; i++) {
		int m = 3;
		if(!scmp(ops[i], s, 3))
			continue;
		if(!i)
			i |= (1 << 7);
		while(s[m]) {
			if(s[m] == '2')
				i |= (1 << 5);
			else if(s[m] == 'r')
				i |= (1 << 6);
			else if(s[m] == 'k')
				i |= (1 << 7);
			else
				return 0;
			m++;
		}
		return i;
	}
	return 0;
}

static int
walkcomment(FILE *f, Context *ctx)
{
	char c;
	int depth = 1;
	while(f && fread(&c, 1, 1, f)) {
		if(c == 0xa) ctx->line += 1;
		if(c == '(') depth++;
		if(c == ')' && --depth < 1) return 1;
	}
	return 0;
}

static int
walkmacro(Item *m, Context *ctx)
{
	char c, *contentptr = m->content, *cptr = token;
	while((c = *contentptr++)) {
		if(c < 0x21) {
			*cptr++ = 0x00;
			if(token[0] && !parse(token, NULL, ctx)) return 0;
			cptr = token;
		} else
			*cptr++ = c;
	}
	return 1;
}

static int
walkfile(FILE *f, Context *ctx)
{
	char c, *cptr = token;
	while(f && fread(&c, 1, 1, f)) {
		if(c == 0xa) ctx->line += 1;
		if(c < 0x21) {
			*cptr++ = 0x00;
			if(token[0] && !parse(token, f, ctx))
				return 0;
			cptr = token;
		} else if(cptr - token < 0x3f)
			*cptr++ = c;
		else
			return error_asm("Token too long");
	}
	return 1;
}

static int
makemacro(char *name, FILE *f, Context *ctx)
{
	char c;
	Item *m;
	if(!slen(name)) return error_asm("Macro is empty");
	if(findmacro(name)) return error_asm("Macro is duplicate");
	if(sihx(name)) return error_asm("Macro is hex number");
	if(isopcode(name)) return error_asm("Macro is opcode");
	if(macro_len == 0x100) return error_asm("Macros limit exceeded");
	m = &macros[macro_len++];
	m->name = push(name, 0);
	m->content = dictnext;
	while(f && fread(&c, 1, 1, f) && c != '{')
		if(c == 0xa) ctx->line += 1;
	while(f && fread(&c, 1, 1, f) && c != '}') {
		if(c == 0xa) ctx->line += 1;
		if(c == '%') return 0;
		if(c == '(') walkcomment(f, ctx);
		*dictnext++ = c;
	}
	*dictnext++ = 0;
	return 1;
}

static int
makelabel(char *name, int setscope, Context *ctx)
{
	Item *l;
	if(name[0] == '&')
		name = makesublabel(name + 1);
	if(!slen(name)) return error_asm("Label is empty");
	if(findlabel(name)) return error_asm("Label is duplicate");
	if(sihx(name)) return error_asm("Label is hex number");
	if(isopcode(name)) return error_asm("Label is opcode");
	if(cndx(runes, name[0]) >= 0) return error_asm("Label name is runic");
	if(label_len == 0x400) return error_asm("Labels limit exceeded");
	l = &labels[label_len++];
	l->name = push(name, 0);
	l->addr = ptr;
	l->refs = 0;
	if(setscope) {
		int i = 0;
		while(name[i] != '/' && i < 0x3e && (scope[i] = name[i])) i++;
		scope[i] = '\0';
	}
	return 1;
}

static char *
makelambda(int id)
{
	lambda[0] = (char)0xce;
	lambda[1] = (char)0xbb;
	lambda[2] = hexad[id >> 0x4];
	lambda[3] = hexad[id & 0xf];
	return lambda;
}

static int
makepad(char *w)
{
	Item *l;
	int rel = w[0] == '$' ? ptr : 0;
	if(sihx(w + 1)) {
		ptr = shex(w + 1) + rel;
		return 1;
	}
	if((l = findlabel(w + 1))) {
		ptr = l->addr + rel;
		return 1;
	}
	return 0;
}

static int
addref(char *label, char rune, Uint16 addr)
{
	Item *r;
	if(refs_len >= 0x1000)
		return error_top("References limit exceeded", label);
	r = &refs[refs_len++];
	if(label[0] == '{') {
		lambda_stack[lambda_ptr++] = lambda_len;
		r->name = push(makelambda(lambda_len++), 0);
	} else if(label[0] == '&' || label[0] == '/') {
		r->name = makesublabel(label + 1);
	} else
		r->name = push(label, 0);
	r->rune = rune;
	r->addr = addr;
	return 1;
}

static int
writebyte(Uint8 b, Context *ctx)
{
	if(ptr < PAGE)
		return error_asm("Writing in zero-page");
	else if(ptr >= 0x10000)
		return error_asm("Writing outside memory");
	else if(ptr < length)
		return error_asm("Writing rewind");
	data[ptr++] = b;
	length = ptr;
	return 1;
}

static int
writehex(char *w, Context *ctx)
{
	if(*w == '#')
		writebyte(findopcode("LIT") | (slen(++w) > 2) << 5, ctx);
	if(slen(w) == 2)
		return writebyte(shex(w), ctx);
	else if(slen(w) == 4)
		return writeshort(shex(w));
	else
		return 0;
}

static int
makeinclude(char *filename)
{
	FILE *f;
	int res = 0;
	Context ctx;
	ctx.line = 0;
	ctx.path = push(filename, 0);
	if(!(f = fopen(filename, "r")))
		return error_top("Invalid source", filename);
	res = walkfile(f, &ctx);
	fclose(f);
	return res;
}

static int
writestring(char *w, Context *ctx)
{
	char c;
	while((c = *(w++)))
		if(!writebyte(c, ctx)) return 0;
	return 1;
}

static int
parse(char *w, FILE *f, Context *ctx)
{
	Item *m;
	switch(w[0]) {
	case '(': return !walkcomment(f, ctx) ? error_asm("Invalid comment") : 1;
	case '~': return !makeinclude(w + 1) ? error_asm("Invalid include") : 1;
	case '%': return !makemacro(w + 1, f, ctx) ? error_asm("Invalid macro") : 1;
	case '@': return !makelabel(w + 1, 1, ctx) ? error_asm("Invalid label") : 1;
	case '&': return !makelabel(w, 0, ctx) ? error_asm("Invalid sublabel") : 1;
	case '}': return !makelabel(makelambda(lambda_stack[--lambda_ptr]), 0, ctx) ? error_asm("Invalid label") : 1;
	case '#': return !sihx(w + 1) || !writehex(w, ctx) ? error_asm("Invalid hexadecimal") : 1;
	case '_': return addref(w + 1, w[0], ptr) && writebyte(0xff, ctx);
	case ',': return addref(w + 1, w[0], ptr + 1) && writebyte(findopcode("LIT"), ctx) && writebyte(0xff, ctx);
	case '-': return addref(w + 1, w[0], ptr) && writebyte(0xff, ctx);
	case '.': return addref(w + 1, w[0], ptr + 1) && writebyte(findopcode("LIT"), ctx) && writebyte(0xff, ctx);
	case ':': fprintf(stderr, "Deprecated rune %s, use =%s\n", w, w + 1); /* fall-through */
	case '=': return addref(w + 1, w[0], ptr) && writeshort(0xffff);
	case ';': return addref(w + 1, w[0], ptr + 1) && writebyte(findopcode("LIT2"), ctx) && writeshort(0xffff);
	case '?': return addref(w + 1, w[0], ptr + 1) && writebyte(0x20, ctx) && writeshort(0xffff);
	case '!': return addref(w + 1, w[0], ptr + 1) && writebyte(0x40, ctx) && writeshort(0xffff);
	case '"': return !writestring(w + 1, ctx) ? error_asm("Invalid string") : 1;
	case '$':
	case '|': return !makepad(w) ? error_asm("Invalid padding") : 1;
	case '[':
	case ']': return 1;
	}
	if(sihx(w))
		return writehex(w, ctx);
	else if(isopcode(w))
		return writebyte(findopcode(w), ctx);
	else if((m = findmacro(w)))
		return walkmacro(m, ctx);
	else
		return addref(w, ' ', ptr + 1) && writebyte(0x60, ctx) && writeshort(0xffff);
	return 1;
}

static int
resolve(void)
{

	int i;
	Uint16 a;
	for(i = 0; i < refs_len; i++) {
		Item *r = &refs[i];
		Uint8 *rom = data + r->addr;
		Item *l = findlabel(r->name);
		switch(r->rune) {
		case '_':
		case ',':
			if(!l) return error_top("Unknown relative reference", r->name);
			*rom = (Sint8)(l->addr - r->addr - 2);
			if((Sint8)data[r->addr] != (l->addr - r->addr - 2))
				return error_top("Relative reference is too far", r->name);
			l->refs++;
			break;
		case '-':
		case '.':
			if(!l) return error_top("Unknown zero-page reference", r->name);
			*rom = l->addr;
			l->refs++;
			break;
		case ':':
		case '=':
		case ';':
			if(!l) return error_top("Unknown absolute reference", r->name);
			*rom++ = l->addr >> 8, *rom = l->addr;
			l->refs++;
			break;
		case '?':
		case '!':
		default:
			if(!l) return error_top("Unknown subroutine reference", r->name);
			a = l->addr - r->addr - 2;
			*rom++ = a >> 8, *rom = a;
			l->refs++;
			break;
		}
	}
	return 1;
}

static void
review(char *filename)
{
	int i;
	for(i = 0; i < label_len; i++)
		if(labels[i].name[0] - 'A' > 25 && !labels[i].refs)
			fprintf(stdout, "-- Unused label: %s\n", labels[i].name);
	fprintf(stdout,
		"Assembled %s in %d bytes(%.2f%% used), %d labels, %d macros.\n",
		filename,
		length - PAGE,
		(length - PAGE) / 652.80,
		label_len,
		macro_len);
}

static void
writesym(char *filename)
{
	int i;
	char symdst[0x60];
	FILE *fp;
	if(slen(filename) > 0x60 - 5)
		return;
	fp = fopen(scat(scpy(filename, symdst, slen(filename) + 1), ".sym"), "w");
	if(fp != NULL) {
		for(i = 0; i < label_len; i++) {
			Uint8 hb = labels[i].addr >> 8, lb = labels[i].addr;
			fwrite(&hb, 1, 1, fp);
			fwrite(&lb, 1, 1, fp);
			fwrite(labels[i].name, slen(labels[i].name) + 1, 1, fp);
		}
	}
	fclose(fp);
}

int
main(int argc, char *argv[])
{
	FILE *dst;
	ptr = PAGE;
	scpy("on-reset", scope, 0x40);
	if(argc == 1) return error_top("usage", "uxnasm [-v] input.tal output.rom");
	if(scmp(argv[1], "-v", 2)) return !fprintf(stdout, "Uxnasm - Uxntal Assembler, 27 Mar 2024.\n");
	if(!makeinclude(argv[1]) || !resolve()) return !error_top("Assembly", "Failed to assemble rom.");
	if(!(dst = fopen(argv[2], "wb"))) return !error_top("Invalid Output", argv[2]);
	if(length <= PAGE) return !error_top("Assembly", "Output rom is empty.");
	review(argv[2]);
	fwrite(data + PAGE, length - PAGE, 1, dst);
	writesym(argv[2]);
	return 0;
}
