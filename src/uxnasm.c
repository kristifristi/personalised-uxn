#include <stdio.h>

/*
Copyright (c) 2021-2023 Devine Lu Linvega, Andrew Alderwick

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
	char name[0x40], items[0x40][0x40];
	Uint8 len;
} Macro;

typedef struct {
	char name[0x40];
	Uint16 addr, refs;
} Label;

typedef struct {
	char name[0x40], rune;
	Uint16 addr;
} Reference;

typedef struct {
	Uint8 data[LENGTH];
	Uint8 lambda_stack[0x100], lambda_ptr, lambda_len;
	Uint16 ptr, length, label_len, macro_len, refs_len;
	char scope[0x40], lambda_name[0x05], *location;
	Label labels[0x400];
	Macro macros[0x100];
	Reference refs[0x1000];
} Program;

char token[0x40];

Program p;

/* clang-format off */

static char ops[][4] = {
	"LIT", "INC", "POP", "NIP", "SWP", "ROT", "DUP", "OVR",
	"EQU", "NEQ", "GTH", "LTH", "JMP", "JCN", "JSR", "STH",
	"LDZ", "STZ", "LDR", "STR", "LDA", "STA", "DEI", "DEO",
	"ADD", "SUB", "MUL", "DIV", "AND", "ORA", "EOR", "SFT"
};

static char *runes = "|$@&,_.-;=!?#\"%~";
static char *hexad = "0123456789abcdef";

static int   scmp(char *a, char *b, int len) { int i = 0; while(a[i] == b[i]) if(!a[i] || ++i >= len) return 1; return 0; } /* string compare */
static int   sihx(char *s) { int i = 0; char c; while((c = s[i++])) if(!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return 0; return i > 1; } /* string is hexadecimal */
static int   shex(char *s) { int n = 0, i = 0; char c; while((c = s[i++])) if(c >= '0' && c <= '9') n = n * 16 + (c - '0'); else if(c >= 'a' && c <= 'f') n = n * 16 + 10 + (c - 'a'); return n; } /* string to num */
static int   slen(char *s) { int i = 0; while(s[i]) i++; return i; } /* string length */
static int   spos(char *s, char c) { Uint8 i = 0, j; while((j = s[i++])) if(j == c) return i; return -1; } /* character position */
static char *scpy(char *src, char *dst, int len) { int i = 0; while((dst[i] = src[i]) && i < len - 2) i++; dst[i + 1] = '\0'; return dst; } /* string copy */
static char *scat(char *dst, const char *src) { char *ptr = dst + slen(dst); while(*src) *ptr++ = *src++; *ptr = '\0'; return dst; } /* string cat */

/* clang-format on */

static int parse(char *w, FILE *f);

static int
error_top(const char *name, const char *msg)
{
	fprintf(stderr, "%s: %s\n", name, msg);
	return 0;
}

static int
error_asm(const char *name)
{
	fprintf(stderr, "%s: %s in @%s, %s:%d.\n", name, token, p.scope, p.location, 123);
	return 0;
}

static char *
setlocation(char *name)
{
	p.location = name;
	return name;
}

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
isrune(char c)
{
	char cc, *r = runes;
	while((cc = *r++))
		if(c == cc) return 1;
	return 0;
}

static int
isopcode(char *s)
{
	return findopcode(s) || scmp(s, "BRK", 4);
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
	scpy(name, m->name, 0x40);
	while(fscanf(f, "%63s", word) == 1) {
		if(word[0] == '{') continue;
		if(word[0] == '}') break;
		if(word[0] == '%')
			return error_asm("Macro error");
		if(m->len >= 0x40)
			return error_asm("Macro size exceeded");
		scpy(word, m->items[m->len++], 0x40);
	}
	return 1;
}

static int
makelabel(char *name)
{
	Label *l;
	if(!slen(name)) return error_asm("Label is empty");
	if(findlabel(name)) return error_asm("Label is duplicate");
	if(sihx(name)) return error_asm("Label is hex number");
	if(isopcode(name)) return error_asm("Label is opcode");
	if(isrune(name[0])) return error_asm("Label name is runic");
	if(p.label_len == 0x400) return error_asm("Labels limit exceeded");
	l = &p.labels[p.label_len++];
	l->addr = p.ptr;
	l->refs = 0;
	scpy(name, l->name, 0x40);
	return 1;
}

static char *
makelambda(int id)
{
	p.lambda_name[0] = (char)0xce;
	p.lambda_name[1] = (char)0xbb;
	p.lambda_name[2] = hexad[id >> 0x4];
	p.lambda_name[3] = hexad[id & 0xf];
	return p.lambda_name;
}

static char *
makesublabel(char *src, char *scope, char *name)
{
	if(slen(scope) + slen(name) >= 0x3f) {
		error_asm("Sublabel length too long");
		return NULL;
	}
	return scat(scat(scpy(scope, src, 0x40), "/"), name);
}

static int
addref(char *scope, char *label, char rune, Uint16 addr)
{
	char subw[0x40], parent[0x40];
	Reference *r;
	if(p.refs_len >= 0x1000)
		return error_asm("References limit exceeded");
	r = &p.refs[p.refs_len++];
	if(label[0] == '{') {
		p.lambda_stack[p.lambda_ptr++] = p.lambda_len;
		scpy(makelambda(p.lambda_len++), r->name, 0x40);
	} else if(label[0] == '&' || label[0] == '/') {
		if(!makesublabel(subw, scope, label + 1))
			return error_asm("Invalid sublabel");
		scpy(subw, r->name, 0x40);
	} else {
		int pos = spos(label, '/');
		if(pos > 0) {
			Label *l;
			if((l = findlabel(scpy(label, parent, pos))))
				l->refs++;
		}
		scpy(label, r->name, 0x40);
	}
	r->rune = rune;
	r->addr = addr;
	return 1;
}

static int
writebyte(Uint8 b)
{
	if(p.ptr < TRIM)
		return error_asm("Writing in zero-page");
	else if(p.ptr == 0xffff)
		return error_asm("Writing outside memory");
	else if(p.ptr < p.length)
		return error_asm("Writing rewind");
	p.data[p.ptr++] = b;
	p.length = p.ptr;
	return 1;
}

static int
writeshort(Uint16 s, int lit)
{
	return (lit ? writebyte(findopcode("LIT2")) : 1) && writebyte(s >> 8) && writebyte(s & 0xff);
}

static int
doinclude(char *filename)
{
	FILE *f;
	char w[0x40];
	if(!(f = fopen(setlocation(filename), "r")))
		return error_top("Include missing", filename);
	while(fscanf(f, "%63s", w) == 1)
		if(!parse(w, f))
			return error_top("Unknown token", w);
	fclose(f);
	return 1;
}

static int
parse(char *w, FILE *f)
{
	int i;
	char word[0x40], subw[0x40], c;
	Label *l;
	Macro *m;
	if(slen(w) >= 63)
		return error_asm("Invalid token");
	switch(w[0]) {
	case '(': /* comment */
		if(slen(w) != 1) fprintf(stderr, "-- Malformed comment: %s\n", w);
		i = 1; /* track nested comment depth */
		while(fscanf(f, "%63s", word) == 1) {
			if(slen(word) != 1)
				continue;
			else if(word[0] == '(')
				i++;
			else if(word[0] == ')' && --i < 1)
				break;
		}
		break;
	case '~': /* include */
		if(!doinclude(w + 1))
			return error_asm("Invalid include");
		break;
	case '%': /* macro */
		if(!makemacro(w + 1, f))
			return error_asm("Invalid macro");
		break;
	case '|': /* pad-absolute */
		if(sihx(w + 1))
			p.ptr = shex(w + 1);
		else if(w[1] == '&') {
			if(!makesublabel(subw, p.scope, w + 2) || !(l = findlabel(subw)))
				return error_asm("Invalid sublabel");
			p.ptr = l->addr;
		} else {
			if(!(l = findlabel(w + 1)))
				return error_asm("Invalid label");
			p.ptr = l->addr;
		}
		break;
	case '$': /* pad-relative */
		if(sihx(w + 1))
			p.ptr += shex(w + 1);
		else if(w[1] == '&') {
			if(!makesublabel(subw, p.scope, w + 2) || !(l = findlabel(subw)))
				return error_asm("Invalid sublabel");
			p.ptr += l->addr;
		} else {
			if(!(l = findlabel(w + 1)))
				return error_asm("Invalid label");
			p.ptr += l->addr;
		}
		break;
	case '@': /* label */
		if(!makelabel(w + 1))
			return error_asm("Invalid label");
		i = 0;
		while(w[i + 1] != '/' && i < 0x3e && (p.scope[i] = w[i + 1]))
			i++;
		p.scope[i] = '\0';
		break;
	case '&': /* sublabel */
		if(!makesublabel(subw, p.scope, w + 1) || !makelabel(subw))
			return error_asm("Invalid sublabel");
		break;
	case '#': /* literals hex */
		if(sihx(w + 1) && slen(w) == 3)
			return writebyte(findopcode("LIT")) && writebyte(shex(w + 1));
		else if(sihx(w + 1) && slen(w) == 5)
			return writeshort(shex(w + 1), 1);
		else
			return error_asm("Invalid hex literal");
		break;
	case '_': /* raw byte relative */
		return addref(p.scope, w + 1, w[0], p.ptr) && writebyte(0xff);
	case ',': /* literal byte relative */
		return addref(p.scope, w + 1, w[0], p.ptr + 1) && writebyte(findopcode("LIT")) && writebyte(0xff);
	case '-': /* raw byte absolute */
		return addref(p.scope, w + 1, w[0], p.ptr) && writebyte(0xff);
	case '.': /* literal byte zero-page */
		return addref(p.scope, w + 1, w[0], p.ptr + 1) && writebyte(findopcode("LIT")) && writebyte(0xff);
	case ':': fprintf(stderr, "Deprecated rune %s, use =%s\n", w, w + 1);
	case '=': /* raw short absolute */
		return addref(p.scope, w + 1, w[0], p.ptr) && writeshort(0xffff, 0);
	case ';': /* literal short absolute */
		return addref(p.scope, w + 1, w[0], p.ptr + 1) && writeshort(0xffff, 1);
	case '?': /* JCI */
		return addref(p.scope, w + 1, w[0], p.ptr + 1) && writebyte(0x20) && writeshort(0xffff, 0);
	case '!': /* JMI */
		return addref(p.scope, w + 1, w[0], p.ptr + 1) && writebyte(0x40) && writeshort(0xffff, 0);
	case '"': /* raw string */
		i = 0;
		while((c = w[++i]))
			if(!writebyte(c)) return 0;
		break;
	case '}': /* lambda end */
		if(!makelabel(makelambda(p.lambda_stack[--p.lambda_ptr])))
			return error_asm("Invalid label");
		break;
	case '[':
	case ']':
		if(slen(w) == 1) break; /* else fallthrough */
	default:
		/* opcode */
		if(isopcode(w))
			return writebyte(findopcode(w));
		/* raw byte */
		else if(sihx(w) && slen(w) == 2)
			return writebyte(shex(w));
		/* raw short */
		else if(sihx(w) && slen(w) == 4)
			return writeshort(shex(w), 0);
		/* macro */
		else if((m = findmacro(w))) {
			for(i = 0; i < m->len; i++)
				if(!parse(m->items[i], f))
					return 0;
			return 1;
		} else
			return addref(p.scope, w, ' ', p.ptr + 1) && writebyte(0x60) && writeshort(0xffff, 0);
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
		Reference *r = &p.refs[i];
		switch(r->rune) {
		case '_':
		case ',':
			if(!(l = findlabel(r->name)))
				return error_top("Unknown relative reference", r->name);
			p.data[r->addr] = (Sint8)(l->addr - r->addr - 2);
			if((Sint8)p.data[r->addr] != (l->addr - r->addr - 2))
				return error_top("Relative reference is too far", r->name);
			l->refs++;
			break;
		case '-':
		case '.':
			if(!(l = findlabel(r->name)))
				return error_top("Unknown zero-page reference", r->name);
			p.data[r->addr] = l->addr & 0xff;
			l->refs++;
			break;
		case ':':
		case '=':
		case ';':
			if(!(l = findlabel(r->name)))
				return error_top("Unknown absolute reference", r->name);
			p.data[r->addr] = l->addr >> 0x8;
			p.data[r->addr + 1] = l->addr & 0xff;
			l->refs++;
			break;
		case '?':
		case '!':
		default:
			if(!(l = findlabel(r->name)))
				return error_top("Unknown absolute reference", r->name);
			a = l->addr - r->addr - 2;
			p.data[r->addr] = a >> 0x8;
			p.data[r->addr + 1] = a & 0xff;
			l->refs++;
			break;
		}
	}
	return 1;
}

static int
assemble(FILE *f)
{
	p.ptr = 0x100;
	scpy("on-reset", p.scope, 0x40);
	while(fscanf(f, "%62s", token) == 1)
		if(slen(token) > 0x3d || !parse(token, f))
			return error_asm("Invalid token");
	return resolve();
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
			Uint8 hb = p.labels[i].addr >> 8, lb = p.labels[i].addr & 0xff;
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
	FILE *src, *dst;
	if(argc == 1) return error_top("usage", "uxnasm [-v] input.tal output.rom");
	if(scmp(argv[1], "-v", 2)) return !fprintf(stdout, "Uxnasm - Uxntal Assembler, 25 Mar 2024.\n");
	if(!(src = fopen(setlocation(argv[1]), "r"))) return !error_top("Invalid input", argv[1]);
	if(!assemble(src)) return !error_top("Assembly", "Failed to assemble rom.");
	if(!(dst = fopen(argv[2], "wb"))) return !error_top("Invalid Output", argv[2]);
	if(p.length <= TRIM) return !error_top("Assembly", "Output rom is empty.");
	review(argv[2]);
	fwrite(p.data + TRIM, p.length - TRIM, 1, dst);
	writesym(argv[2]);
	return 0;
}
