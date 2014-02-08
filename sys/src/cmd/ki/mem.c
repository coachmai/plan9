/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>
#define Extern extern
#include "sparc.h"

extern ulong	textbase;

ulong
ifetch(ulong addr)
{
	uchar *va;

	if(addr&3) {
		Bprint(bioout, "instruction_address_not_aligned [addr %.8lux]\n", addr);
		longjmp(errjmp, 0);
	}

	if(icache.on)
		updateicache(addr);

	va = vaddr(addr);
	iprof[(addr-textbase)/PROFGRAN]++;

	va += addr&(BY2PG-1);

	return va[0]<<24 | va[1]<<16 | va[2]<<8 | va[3];
}

ulong
getmem_4(ulong addr)
{
	ulong val;
	int i;

	val = 0;
	for(i = 0; i < 4; i++)
		val = val<<8 | getmem_b(addr++);
	return val;
}

ulong
getmem_2(ulong addr)
{
	ulong val;

	val = getmem_b(addr);
	val = val<<8 | getmem_b(addr+1);

	return val;
}

ulong
getmem_w(ulong addr)
{
	uchar *va;

	if(addr&3) {
		Bprint(bioout, "mem_address_not_aligned [load addr %.8lux]\n", addr);
		longjmp(errjmp, 0);
	}
	if(membpt)
		brkchk(addr, Read);

	va = vaddr(addr);
	va += addr&(BY2PG-1);

	return va[0]<<24 | va[1]<<16 | va[2]<<8 | va[3];;
}

ushort
getmem_h(ulong addr)
{
	uchar *va;

	if(addr&1) {
		Bprint(bioout, "mem_address_not_aligned [load addr %.8lux]\n", addr);
		longjmp(errjmp, 0);
	}
	if(membpt)
		brkchk(addr, Read);

	va = vaddr(addr);
	va += addr&(BY2PG-1);

	return va[0]<<8 | va[1];
}

uchar
getmem_b(ulong addr)
{
	uchar *va;

	if(membpt)
		brkchk(addr, Read);

	va = vaddr(addr);
	va += addr&(BY2PG-1);
	return va[0];
}

void
putmem_w(ulong addr, ulong data)
{
	uchar *va;

	if(addr&3) {
		Bprint(bioout, "mem_address_not_aligned [store addr %.8lux]\n", addr);
		longjmp(errjmp, 0);
	}

	va = vaddr(addr);
	va += addr&(BY2PG-1);

	va[0] = data>>24;
	va[1] = data>>16;
	va[2] = data>>8;
	va[3] = data;
	if(membpt)
		brkchk(addr, Write);
}
void
putmem_b(ulong addr, uchar data)
{
	uchar *va;

	va = vaddr(addr);
	va += addr&(BY2PG-1);
	va[0] = data;
	if(membpt)
		brkchk(addr, Write);
}

void
putmem_h(ulong addr, short data)
{
	uchar *va;

	if(addr&1) {
		Bprint(bioout, "mem_address_not_aligned [store addr %.8lux]\n", addr);
		longjmp(errjmp, 0);
	}

	va = vaddr(addr);
	va += addr&(BY2PG-1);
	va[0] = data>>8;
	va[1] = data;
	if(membpt)
		brkchk(addr, Write);
}

char *
memio(char *mb, ulong mem, int size, int dir)
{
	int i;
	char *buf, c;

	if(mb == 0)
		mb = emalloc(size);

	buf = mb;
	switch(dir) {
	default:
		fatal(0, "memio");
	case MemRead:
		while(size--)
			*mb++ = getmem_b(mem++);
		break;
	case MemReadstring:
		for(;;) {
			if(size-- == 0) {
				Bprint(bioout, "memio: user/kernel copy too long for mipsim\n");
				longjmp(errjmp, 0);
			}
			c = getmem_b(mem++);
			*mb++ = c;
			if(c == '\0')
				break;
		}
		break;
	case MemWrite:
		for(i = 0; i < size; i++)
			putmem_b(mem++, *mb++);
		break;
	}
	return buf;
}

void *
vaddr(ulong addr)
{
	Segment *s, *es;
	int off, foff, l, n;
	uchar **p, *a;

	es = &memory.seg[Nseg];
	for(s = memory.seg; s < es; s++) {
		if(addr >= s->base && addr < s->end) {
			s->refs++;
			off = (addr-s->base)/BY2PG;
			p = &s->table[off];
			if(*p)
				return *p;
			s->rss++;
			switch(s->type) {
			default:
				fatal(0, "vaddr");
			case Text:
				*p = emalloc(BY2PG);
				if(seek(text, s->fileoff+(off*BY2PG), 0) < 0)
					fatal(1, "vaddr text seek");
				if(read(text, *p, BY2PG) < 0)
					fatal(1, "vaddr text read");
				return *p;
			case Data:
				*p = emalloc(BY2PG);
				foff = s->fileoff+(off*BY2PG);
				if(seek(text, foff, 0) < 0)
					fatal(1, "vaddr text seek");
				n = read(text, *p, BY2PG);
				if(n < 0)
					fatal(1, "vaddr text read");
				if(foff + n > s->fileend) {
					l = BY2PG - (s->fileend-foff);
					a = *p+(s->fileend-foff);
					memset(a, 0, l);
				}
				return *p;
			case Bss:
			case Stack:
				*p = emalloc(BY2PG);
				return *p;
			}
		}
	}
	Bprint(bioout, "data_access_MMU_miss [addr 0x%.8lux]\n", addr);
	longjmp(errjmp, 0);
	return 0;		/*to stop compiler whining*/
}