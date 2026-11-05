@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ BIOS calls for cases_bios.c, each from ARM and from Thumb code:
@ swi_<name>_arm and swi_<name>_thumb pass their three arguments to the
@ SWI in r0-r2 and return. ARM code puts the function number in bits
@ 16-23 of the SWI comment, Thumb code in bits 0-7 (GBATEK, BIOS
@ Functions).

	.syntax unified
	.text

	.macro	bios_call name, number
	.arm
	.align	2
	.global	swi_\name\()_arm
	.type	swi_\name\()_arm, %function
swi_\name\()_arm:
	swi	\number << 16
	bx	lr

	.thumb
	.global	swi_\name\()_thumb
	.type	swi_\name\()_thumb, %function
	.thumb_func
swi_\name\()_thumb:
	swi	\number
	bx	lr
	.endm

	bios_call	cpuset, 0x0B
	bios_call	cpufastset, 0x0C
	bios_call	bitunpack, 0x10
	bios_call	lz77_wram, 0x11
	bios_call	lz77_vram, 0x12
	bios_call	huffman, 0x13
	bios_call	rl_wram, 0x14
	bios_call	rl_vram, 0x15

@ Div (SWI 06h) and DivArm (07h) for cases_bios.c:
@ div_<name>_<arm|thumb>(const u32 in[3], u32 out[6]) calls the SWI from
@ ARM or Thumb code with r0 and r1 from in[0] and in[1], r2 = 22222222h,
@ r12 = CCCCCCCCh and the condition flags from bits 28-31 of in[2], and
@ stores the r0, r1, r2, r3, r12 and CPSR it returns with. The Thumb
@ version sets everything up in ARM code, where MSR can set the flags,
@ and switches with BX, which leaves them alone.

	.macro	div_call name, number
	.arm
	.align	2
	.global	div_\name\()_arm
	.type	div_\name\()_arm, %function
div_\name\()_arm:
	push	{r4, r5, lr}
	mov	r4, r1
	ldmia	r0, {r0, r1, r5}
	ldr	r2, =0x22222222
	ldr	r12, =0xCCCCCCCC
	msr	cpsr_f, r5
	swi	\number << 16
	b	swi_store

	.global	div_\name\()_thumb
	.type	div_\name\()_thumb, %function
div_\name\()_thumb:
	push	{r4, r5, lr}
	mov	r4, r1
	ldmia	r0, {r0, r1, r5}
	ldr	r2, =0x22222222
	ldr	r12, =0xCCCCCCCC
	adr	r3, 1f
	orr	r3, r3, #1
	msr	cpsr_f, r5
	bx	r3
	.thumb
	.align	2
1:	swi	\number
	nop
	@ Word aligned: back to ARM code at the next word.
	bx	pc
	nop
	.arm
	b	swi_store
	.endm

	div_call	div, 0x06
	div_call	divarm, 0x07

@ Sqrt (SWI 08h), ArcTan (09h), ArcTan2 (0Ah), BgAffineSet (0Eh) and
@ ObjAffineSet (0Fh) for cases_bios.c: math_<name>(const u32 in[4],
@ u32 out[6]) calls the SWI from ARM code with r0-r3 from in,
@ r12 = CCCCCCCCh and the flags at 6h, and stores the r0, r1, r2, r3, r12
@ and CPSR it returns with.

	.macro	math_call name, number
	.arm
	.align	2
	.global	math_\name
	.type	math_\name, %function
math_\name:
	push	{r4, r5, lr}
	mov	r4, r1
	ldmia	r0, {r0-r3}
	ldr	r12, =0xCCCCCCCC
	msr	cpsr_f, #0x60000000
	swi	\number << 16
	b	swi_store
	.endm

	math_call	sqrt, 0x08
	math_call	arctan, 0x09
	math_call	arctan2, 0x0A
	math_call	bg_affine_set, 0x0E
	math_call	obj_affine_set, 0x0F

	.arm
	.align	2
swi_store:
	mrs	r5, cpsr
	stmia	r4, {r0, r1, r2, r3, r12}
	str	r5, [r4, #20]
	pop	{r4, r5, lr}
	bx	lr
