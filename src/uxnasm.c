#include <stdio.h>

/*
Copyright (c) 2021-2024 Devine Lu Linvega, Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#define TRIM 0x0100
#define LENGTH 0x10000

typedef unsigned char Uint8;
typedef signed char Sint8;
typedef unsigned short Uint16;

typedef struct {
	char *name, items[0x40][0x40];
	Uint8 len;
} Macro;

typedef struct {
	char *name, rune;
	Uint16 addr, refs;
} Label;

typedef struct {
	int ptr, length;
	Uint8 data[LENGTH];
	Uint8 lambda_stack[0x100], lambda_ptr, lambda_len;
	Uint16 line, label_len, macro_len, refs_len;
	Label labels[0x400], refs[0x1000];
	Macro macros[0x100];
} Program;

static char source[0x40], token[0x40], scope[0x40], sublabel[0x80], lambda[0x05];
static char dict[0x10000], *storenext = dict;

Program p;

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
static char *scat(char *dst, char *src) { char *ptr = dst + slen(dst); while(*src) *ptr++ = *src++; *ptr = '\0'; return dst; } /* str cat */
static char *push(char *s) { char *ptr = storenext; while((*storenext++ = *s++)); *storenext++ = 0; return ptr; } /* save str */

#define isopcode(x) (findopcode(x) || scmp(x, "BRK", 4))
#define writeshort(x) (writebyte(x >> 8) && writebyte(x & 0xff))
#define makesublabel(x) (push(scat(scat(scpy(scope, sublabel, 0x40), "/"), x)))
#define error_top(name, msg) !!fprintf(stderr, "%s: %s\n", name, msg)
#define error_asm(name) !!fprintf(stderr, "%s: %s in @%s, %s:%d.\n", name, token, scope, source, p.line)

/* clang-format on */

static int parse(char *w, FILE *f);

static Macro *
findmacro(char *name)
{
	int i;
	for(i = 0; i < p.macro_len; i++)
		if(scmp(p.macros[i].name, name, 0x40))
			return &p.macros[i];
	return NULL;
}

static Label *
findlabel(char *name)
{
	int i;
	if(name[0] == '&')
		name = makesublabel(name + 1);
	for(i = 0; i < p.label_len; i++)
		if(scmp(p.labels[i].name, name, 0x40))
			return &p.labels[i];
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
walkcomment(char *w, FILE *f)
{
	int depth = 1;
	char c;
	if(slen(w) == 1)
		while(fread(&c, 1, 1, f)) {
			if(c == '(')
				depth++;
			else if(c == ')' && --depth < 1)
				return 1;
		}
	return 0;
}

static int
makemacro(char *name, FILE *f)
{
	Macro *m;
	char word[0x40];
	if(!slen(name)) return error_asm("Macro is empty");
	if(findmacro(name)) return error_asm("Macro is duplicate");
	if(sihx(name)) return error_asm("Macro is hex number");
	if(isopcode(name)) return error_asm("Macro is opcode");
	if(p.macro_len == 0x100) return error_asm("Macros limit exceeded");
	m = &p.macros[p.macro_len++];
	m->name = push(name);
	while(fscanf(f, "%63s", word) == 1) {
		if(word[0] == '{') continue;
		if(word[0] == '}') break;
		if(word[0] == '%') return error_asm("Macro error");
		if(m->len >= 0x40) return error_asm("Macro size exceeded");
		if(word[0] == '(') {
			if(!walkcomment(word, f)) return error_asm("Comment error");
			continue;
		}
		scpy(word, m->items[m->len++], 0x40);
	}
	return 1;
}

static int
makelabel(char *name, int setscope)
{
	Label *l;
	if(name[0] == '&')
		name = makesublabel(name + 1);
	if(!slen(name)) return error_asm("Label is empty");
	if(findlabel(name)) return error_asm("Label is duplicate");
	if(sihx(name)) return error_asm("Label is hex number");
	if(isopcode(name)) return error_asm("Label is opcode");
	if(cndx(runes, name[0]) >= 0) return error_asm("Label name is runic");
	if(p.label_len == 0x400) return error_asm("Labels limit exceeded");
	l = &p.labels[p.label_len++];
	l->addr = p.ptr;
	l->refs = 0;
	l->name = push(name);
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
	Label *l;
	int rel = w[0] == '$' ? p.ptr : 0;
	if(sihx(w + 1))
		p.ptr = shex(w + 1) + rel;
	else if((l = findlabel(w + 1)))
		p.ptr = l->addr + rel;
	else
		return error_asm("Invalid padding");
	return 1;
}

static int
addref(char *label, char rune, Uint16 addr)
{
	Label *r;
	if(p.refs_len >= 0x1000)
		return error_asm("References limit exceeded");
	r = &p.refs[p.refs_len++];
	if(label[0] == '{') {
		p.lambda_stack[p.lambda_ptr++] = p.lambda_len;
		r->name = push(makelambda(p.lambda_len++));
	} else if(label[0] == '&' || label[0] == '/') {
		r->name = makesublabel(label + 1);
	} else
		r->name = push(label);
	r->rune = rune;
	r->addr = addr;
	return 1;
}

static int
writebyte(Uint8 b)
{
	if(p.ptr < TRIM)
		return error_asm("Writing in zero-page");
	else if(p.ptr >= 0x10000)
		return error_asm("Writing outside memory");
	else if(p.ptr < p.length)
		return error_asm("Writing rewind");
	p.data[p.ptr++] = b;
	p.length = p.ptr;
	return 1;
}

static int
writehex(char *w)
{
	if(*w == '#')
		writebyte(findopcode("LIT") | (slen(++w) > 2) << 5);
	if(slen(w) == 2)
		return writebyte(shex(w));
	else if(slen(w) == 4)
		return writeshort(shex(w));
	else
		return 0;
}

static int
tokenize(FILE *f)
{
	char c;
	char *cptr = token;
	while(fread(&c, 1, 1, f)) {
		if(c < 0x21) {
			*cptr++ = 0x00;
			if(c == 0x0a)
				p.line++;
			if(token[0])
				if(!parse(token, f))
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
makeinclude(char *filename)
{
	FILE *f;
	int res = 0;
	if(!(f = fopen(filename, "r")))
		return error_top("Invalid source", filename);
	scpy(filename, source, 0x40);
	p.line = 0;
	res = tokenize(f);
	fclose(f);
	return res;
}

static int
parse(char *w, FILE *f)
{
	int i;
	char c;
	Macro *m;
	switch(w[0]) {
	case '(': return !walkcomment(w, f) ? error_asm("Invalid comment") : 1;
	case '~': return !makeinclude(w + 1) ? error_asm("Invalid include") : 1;
	case '%': return !makemacro(w + 1, f) ? error_asm("Invalid macro") : 1;
	case '@': return !makelabel(w + 1, 1) ? error_asm("Invalid label") : 1;
	case '&': return !makelabel(w, 0) ? error_asm("Invalid sublabel") : 1;
	case '#': return !sihx(w + 1) || !writehex(w) ? error_asm("Invalid hexadecimal") : 1;
	case '_': return addref(w + 1, w[0], p.ptr) && writebyte(0xff);
	case ',': return addref(w + 1, w[0], p.ptr + 1) && writebyte(findopcode("LIT")) && writebyte(0xff);
	case '-': return addref(w + 1, w[0], p.ptr) && writebyte(0xff);
	case '.': return addref(w + 1, w[0], p.ptr + 1) && writebyte(findopcode("LIT")) && writebyte(0xff);
	case ':': fprintf(stderr, "Deprecated rune %s, use =%s\n", w, w + 1); /* fall-through */
	case '=': return addref(w + 1, w[0], p.ptr) && writeshort(0xffff);
	case ';': return addref(w + 1, w[0], p.ptr + 1) && writebyte(findopcode("LIT2")) && writeshort(0xffff);
	case '?': return addref(w + 1, w[0], p.ptr + 1) && writebyte(0x20) && writeshort(0xffff);
	case '!': return addref(w + 1, w[0], p.ptr + 1) && writebyte(0x40) && writeshort(0xffff);
	case '}': return !makelabel(makelambda(p.lambda_stack[--p.lambda_ptr]), 0) ? error_asm("Invalid label") : 1;
	case '$':
	case '|': return !makepad(w) ? error_asm("Invalid padding") : 1;
	case '[':
	case ']':
		if(slen(w) == 1) break; /* else fallthrough */
	case '"':                   /* raw string */
		while((c = *(++w)))
			if(!writebyte(c)) return 0;
		break;
	default:
		if(sihx(w))
			return writehex(w);
		else if(isopcode(w))
			return writebyte(findopcode(w));
		else if((m = findmacro(w))) {
			for(i = 0; i < m->len; i++)
				if(!parse(m->items[i], f))
					return 0;
			return 1;
		} else
			return addref(w, ' ', p.ptr + 1) && writebyte(0x60) && writeshort(0xffff);
	}
	return 1;
}

static int
resolve(void)
{
	Label *l;
	int i;
	Uint16 a;
	for(i = 0; i < p.refs_len; i++) {
		Label *r = &p.refs[i];
		Uint8 *rom = p.data + r->addr;
		switch(r->rune) {
		case '_':
		case ',':
			if(!(l = findlabel(r->name)))
				return error_top("Unknown relative reference", r->name);
			*rom = (Sint8)(l->addr - r->addr - 2);
			if((Sint8)p.data[r->addr] != (l->addr - r->addr - 2))
				return error_top("Relative reference is too far", r->name);
			l->refs++;
			break;
		case '-':
		case '.':
			if(!(l = findlabel(r->name)))
				return error_top("Unknown zero-page reference", r->name);
			*rom = l->addr;
			l->refs++;
			break;
		case ':':
		case '=':
		case ';':
			if(!(l = findlabel(r->name)))
				return error_top("Unknown absolute reference", r->name);
			*rom++ = l->addr >> 8, *rom = l->addr;
			l->refs++;
			break;
		case '?':
		case '!':
		default:
			if(!(l = findlabel(r->name)))
				return error_top("Unknown subroutine reference", r->name);
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
	for(i = 0; i < p.label_len; i++)
		if(p.labels[i].name[0] - 'A' > 25 && !p.labels[i].refs)
			fprintf(stdout, "-- Unused label: %s\n", p.labels[i].name);
	fprintf(stdout,
		"Assembled %s in %d bytes(%.2f%% used), %d labels, %d macros.\n",
		filename,
		p.length - TRIM,
		(p.length - TRIM) / 652.80,
		p.label_len,
		p.macro_len);
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
		for(i = 0; i < p.label_len; i++) {
			Uint8 hb = p.labels[i].addr >> 8, lb = p.labels[i].addr;
			fwrite(&hb, 1, 1, fp);
			fwrite(&lb, 1, 1, fp);
			fwrite(p.labels[i].name, slen(p.labels[i].name) + 1, 1, fp);
		}
	}
	fclose(fp);
}

int
main(int argc, char *argv[])
{
	FILE *dst;
	p.ptr = 0x100;
	scpy("on-reset", scope, 0x40);
	if(argc == 1) return error_top("usage", "uxnasm [-v] input.tal output.rom");
	if(scmp(argv[1], "-v", 2)) return !fprintf(stdout, "Uxnasm - Uxntal Assembler, 26 Mar 2024.\n");
	if(!makeinclude(argv[1]) || !resolve()) return !error_top("Assembly", "Failed to assemble rom.");
	if(!(dst = fopen(argv[2], "wb"))) return !error_top("Invalid Output", argv[2]);
	if(p.length <= TRIM) return !error_top("Assembly", "Output rom is empty.");
	review(argv[2]);
	fwrite(p.data + TRIM, p.length - TRIM, 1, dst);
	writesym(argv[2]);
	return 0;
}
