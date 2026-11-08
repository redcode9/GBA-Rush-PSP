@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ Stores into the instructions right after them, for the prefetch window
@ cases (cases_prefetch.c). Each routine runs from IWRAM, rewrites code
@ ahead of itself with a version that adds a different number, and
@ returns what the code that ran added up to in r0. The new versions sit
@ after each routine as data.

	.syntax unified

	.macro	function_arm name
	.arm
	.align	2
	.global	\name
	.type	\name, %function
\name:
	.endm

	.macro	function_thumb name
	.thumb
	.align	1
	.global	\name
	.type	\name, %function
	.thumb_func
\name:
	.endm

@ A STR over the instruction \g words after it: 1 if the old one ran, 2
@ if the new one did. The old one is put back, so the routine can run
@ again.
	.macro	prefetch_arm g, tag
	function_arm prefetch_arm_\g\()_\tag
	ldr	r1, 3f
	ldr	r2, 2f
	mov	r0, #0
	str	r1, [pc, #(4 * \g - 8)]
	.rept	\g - 1
	mov	r12, r12
	.endr
2:	mov	r0, #1
	str	r2, 2b
	bx	lr
3:	mov	r0, #2
	.endm

@ The same in Thumb with STRH, \g halfwords ahead.
	.macro	prefetch_thumb g, tag
	function_thumb prefetch_thumb_\g\()_\tag
	movs	r0, #0
	ldr	r1, 3f
	mov	r3, pc
	ldrh	r2, [r3, #(2 * \g)]
	strh	r1, [r3, #(2 * \g)]
	.rept	\g - 1
	nop
	.endr
	movs	r0, #1
	strh	r2, [r3, #(2 * \g)]
	bx	lr
	.align	2
3:	movs	r0, #2
	.endm

	.section .iwram, "ax", %progbits

	prefetch_arm 1, cold
	prefetch_arm 2, cold
	prefetch_arm 3, cold
	prefetch_arm 1, warm
	prefetch_arm 2, warm
	prefetch_arm 3, warm
	prefetch_thumb 1, cold
	prefetch_thumb 2, cold
	prefetch_thumb 3, cold
	prefetch_thumb 1, warm
	prefetch_thumb 2, warm
	prefetch_thumb 3, warm

@ An STM over the next two instructions: 3 if both old ones ran.
	function_arm prefetch_stm
	ldr	r1, 3f
	ldr	r2, 4f
	mov	r0, #0
	mov	r12, pc
	stmia	r12, {r1, r2}
	add	r0, r0, #1
	add	r0, r0, #2
	bx	lr
3:	add	r0, r0, #16
4:	add	r0, r0, #32

@ A Thumb PUSH of one word over the next two instructions, with SP moved
@ there: 3 if both old ones ran.
	function_thumb prefetch_push
	movs	r0, #0
	ldr	r1, 3f
	mov	r12, sp
	.align	2
	mov	r3, pc			@ 6 bytes before the PUSH: r3 = PUSH - 2
	adds	r3, #8
	mov	sp, r3
	push	{r1}
	adds	r0, #1
	adds	r0, #2
	mov	sp, r12
	bx	lr
	.align	2
3:	adds	r0, #16
	adds	r0, #32

@ A SWP over the next instruction: 1 if the old one ran.
	function_arm prefetch_swp
	ldr	r1, 3f
	mov	r0, #0
	add	r2, pc, #0
	swp	r1, r1, [r2]
	add	r0, r0, #1
	bx	lr
3:	add	r0, r0, #16
