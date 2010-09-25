#include <sys/types.h>

#ifndef __I386_THREAD_H__
#define __I386_THREAD_H__

/* Details a 32-bit Task State Segment */
struct TSS {
	uint16_t	link;
	uint16_t	_reserved0;
	uint32_t	esp0;
	uint16_t	ss0;
	uint16_t	_reserved1;
	uint32_t	esp1;
	uint16_t	ss1;
	uint16_t	_reserved2;
	uint32_t	esp2;
	uint16_t	ss2;
	uint16_t	_reserved3;
	uint32_t	cr3;
	uint32_t	eip;
	uint32_t	eflags;
	uint32_t	eax;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	ebx;
	uint32_t	esp;
	uint32_t	ebp;
	uint32_t	esi;
	uint32_t	edi;
	uint16_t	es;
	uint16_t	_reserved4;
	uint16_t	cs;
	uint16_t	_reserved5;
	uint16_t	ss;
	uint16_t	_reserved6;
	uint16_t	ds;
	uint16_t	_reserved7;
	uint16_t	fs;
	uint16_t	_reserved8;
	uint16_t	gs;
	uint16_t	_reserved9;
	uint16_t	ldt;
	uint16_t	flags;
#define TSS_FLAG_T	0x01			/* Debug Trap */
	uint16_t	iomap;
	uint32_t	iobitmap;
} __attribute__((packed));

/* Stored thread context - note that this must match cntx.S ! */
struct CONTEXT {
	uint32_t	/* 00 */ eax;
	uint32_t	/* 04 */ ebx;
	uint32_t	/* 08 */ ecx;
	uint32_t	/* 0c */ edx;
	uint32_t	/* 10 */ esi;
	uint32_t	/* 14 */ edi;
	uint32_t	/* 18 */ ebp;
	uint32_t	/* 1c */ esp;
	uint32_t	/* 20 */ eip;
	uint32_t	/* 24 */ cs;
	uint32_t	/* 28 */ ds;
	uint32_t	/* 2c */ es;
	uint32_t	/* 30 */ fs;
	uint32_t	/* 34 */ gs;
	uint32_t	/* 38 */ ss;
	uint32_t	/* 3c */ cr3;
	uint32_t	/* 40 */ eflags;
	uint32_t	/* 44 */ esp0;
};

struct FPUREGS {
	uint16_t	cw;		/* control word */
	uint16_t	reserved0;
	uint16_t	sw;		/* status word */
	uint16_t	reserved1;
	uint16_t	tw;		/* tag word */
	uint16_t	reserved2;
	uint32_t	ip;		/* instruction pointer */
	uint32_t	op_ip_sel;	/* opcode, ip selector */
	uint32_t	op;		/* operand */
	uint32_t	op_selector;	/* operand selector */
	uint8_t		r0[10];
	uint8_t		r1[10];
	uint8_t		r2[10];
	uint8_t		r3[10];
	uint8_t		r4[10];
	uint8_t		r5[10];
	uint8_t		r6[10];
	uint8_t		r7[10];
};

/* i386-specific thread detais */
#define MD_THREAD_FIELDS \
	struct CONTEXT	md_ctx; \
	struct FPUREGS	md_fpu_ctx __attribute__ ((aligned(16))); \
	void*		md_pagedir; \
	void*		md_stack; \
	void*		md_kstack; \

void md_restore_ctx(struct CONTEXT* ctx);

#endif /* __I386_THREAD_H__ */