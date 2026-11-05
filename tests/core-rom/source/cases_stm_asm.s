@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ Block stores over code that has run, and over I/O, for the multiple
@ store cases (cases_stm.c). The routines rewritten return 1 in their old
@ version and 2 in the new one; the new versions are data in the ROM.

	.syntax unified

@ ---- Routines to rewrite ---------------------------------------------------

	.section .iwram, "ax", %progbits
	.arm
	.align	2
	.global	stm_code_low
	.type	stm_code_low, %function
stm_code_low:
	mov	r0, #1
	bx	lr
	.word	0

	.global	stm_code_high
	.type	stm_code_high, %function
stm_code_high:
	mov	r0, #1
	bx	lr
	.word	0

@ Two routines back to back: the first returns 1, the second 3.
	.global	stm_pair
	.type	stm_pair, %function
stm_pair:
	mov	r0, #1
	bx	lr
	mov	r0, #3
	bx	lr

@ Eight words, as CpuFastSet copies.
	.global	stm_fast_code
	.type	stm_fast_code, %function
stm_fast_code:
	mov	r0, #1
	bx	lr
	.word	0, 0, 0, 0, 0, 0

	.thumb
	.align	2
	.global	stm_thumb_iwram
	.type	stm_thumb_iwram, %object
stm_thumb_iwram:
	movs	r0, #1
	bx	lr
	.word	0

@ Entered at its start; the PUSH rewrites its second word.
	.align	2
	.global	stm_push_code
	.type	stm_push_code, %object
stm_push_code:
	nop
	nop
	movs	r0, #1
	bx	lr

	.section .ewram, "ax", %progbits
	.arm
	.align	2
	.global	stm_code_ewram
	.type	stm_code_ewram, %function
stm_code_ewram:
	mov	r0, #1
	bx	lr
	.word	0

	.thumb
	.align	2
	.global	stm_thumb_ewram
	.type	stm_thumb_ewram, %object
stm_thumb_ewram:
	movs	r0, #1
	bx	lr
	.word	0

@ ---- New versions, and the old one copied to VRAM ---------------------------

	.text
	.arm
	.align	2
	.global	stm_arm_old
	.type	stm_arm_old, %object
stm_arm_old:
	mov	r0, #1
	bx	lr
	.word	0
stm_arm_new:
	mov	r0, #2
	bx	lr
stm_pair_new:
	mov	r0, #2
	bx	lr
	mov	r0, #4
	.global	stm_fast_new
	.type	stm_fast_new, %object
stm_fast_new:
	mov	r0, #2
	bx	lr
	.word	0, 0, 0, 0, 0, 0
	.thumb
	.align	2
stm_thumb_new:
	movs	r0, #2
	bx	lr

@ ---- Rewriting -------------------------------------------------------------

	.arm
	.align	2

@ u32 stm_rewrite(u32 *code, u32 lower_sp): runs the ARM routine at code,
@ rewrites it with an STM of three words, the last on the data word after
@ it, and runs it again. With lower_sp set, SP points below the routine
@ during the STM. Returns the first result, and the second in bits 8-15.
	.global	stm_rewrite
	.type	stm_rewrite, %function
stm_rewrite:
	push	{r4-r7, lr}
	mov	r4, r0
	mov	r7, r1
	mov	lr, pc
	bx	r4
	mov	r5, r0
	ldr	r0, =stm_arm_new
	ldm	r0, {r1, r2}
	ldr	r3, =0x5A5A5A5A
	mov	r6, sp
	cmp	r7, #0
	subne	sp, r4, #0x100
	stmia	r4, {r1, r2, r3}
	mov	sp, r6
	mov	lr, pc
	bx	r4
	orr	r0, r5, r0, lsl #8
	pop	{r4-r7, lr}
	bx	lr

@ u32 stm_pair_rewrite(void): runs both routines of stm_pair, rewrites
@ the first one's two words and the second one's first word with one
@ STM, and runs both again. Returns the four results in bytes 0-3.
	.global	stm_pair_rewrite
	.type	stm_pair_rewrite, %function
stm_pair_rewrite:
	push	{r4, r5, lr}
	ldr	r4, =stm_pair
	mov	lr, pc
	bx	r4
	mov	r5, r0
	add	r1, r4, #8
	mov	lr, pc
	bx	r1
	orr	r5, r5, r0, lsl #8
	ldr	r0, =stm_pair_new
	ldm	r0, {r1-r3}
	stmia	r4, {r1-r3}
	mov	lr, pc
	bx	r4
	orr	r5, r5, r0, lsl #16
	add	r1, r4, #8
	mov	lr, pc
	bx	r1
	orr	r0, r5, r0, lsl #24
	pop	{r4, r5, lr}
	bx	lr
	.pool

	.thumb

@ u32 stm_thumb_rewrite(u16 *code): runs the Thumb routine at code,
@ rewrites it with an STMIA of two words, the last on the data word after
@ it, and runs it again. Returns the first result, and the second in bits
@ 8-15.
	.global	stm_thumb_rewrite
	.type	stm_thumb_rewrite, %function
	.thumb_func
stm_thumb_rewrite:
	push	{r4, r5, lr}
	adds	r4, r0, #1
	bl	call_r4
	movs	r5, r0
	subs	r2, r4, #1
	ldr	r0, =stm_thumb_new
	ldr	r0, [r0]
	ldr	r1, =0x5A5A5A5A
	stmia	r2!, {r0, r1}
	bl	call_r4
	lsls	r0, r0, #8
	orrs	r0, r5
	pop	{r4, r5}
	pop	{r1}
	bx	r1
	.thumb_func
call_r4:
	bx	r4

@ u32 stm_push_rewrite(void): runs stm_push_code, then, with SP moved to
@ its end, rewrites it with PUSH {r4, lr}: r4 holds its first word as it
@ is, LR the new second word. Runs it again; returns the first result,
@ and the second in bits 8-15.
	.global	stm_push_rewrite
	.type	stm_push_rewrite, %function
	.thumb_func
stm_push_rewrite:
	push	{r4-r7, lr}
	ldr	r7, =stm_push_code
	adds	r6, r7, #1
	bl	call_r6
	movs	r5, r0
	ldr	r4, [r7]
	ldr	r1, =stm_thumb_new
	ldr	r1, [r1]
	mov	r2, sp
	adds	r3, r7, #4
	adds	r3, #4
	mov	sp, r3
	mov	lr, r1
	push	{r4, lr}
	mov	sp, r2
	bl	call_r6
	lsls	r0, r0, #8
	orrs	r0, r5
	pop	{r4-r7}
	pop	{r1}
	bx	r1
	.thumb_func
call_r6:
	bx	r6
	.align	2
	.pool

@ ---- I/O -------------------------------------------------------------------

	.arm
	.align	2

@ The IRQ handler of stm_irq: records the r4 of the code it interrupted,
@ then acknowledges and counts like the default one.
	.global	stm_irq_handler
	.type	stm_irq_handler, %function
stm_irq_handler:
	ldr	r0, =irq_mark
	str	r4, [r0]
	b	irq_default
	.pool

	.section .iwram, "ax", %progbits
	.arm
	.align	2

@ u32 stm_timer_start(vu32 *tm0cnt): starts timer 0 at F/1 from 0 with
@ the first word of an STM (the second leaves timer 1 stopped) and reads
@ it with the next instruction; stops it, starts it with a single STR and
@ reads it the same way. Returns the first read, and the second in bits
@ 16-31.
	.global	stm_timer_start
	.type	stm_timer_start, %function
stm_timer_start:
	mov	r1, #0x00800000
	mov	r2, #0
	stmia	r0, {r1, r2}
	ldrh	r3, [r0]
	str	r2, [r0]
	str	r1, [r0]
	ldrh	r1, [r0]
	orr	r0, r3, r1, lsl #16
	bx	lr

@ void stm_irq_enable_<stm|str>(vu32 *ime): sets IME with the first
@ word of an STM (the second goes to the unused 0400020Ch) or with a
@ single STR, then counts eight instructions in r4 and clears IME.
	.macro	irq_enable name, store:vararg
	.global	\name
	.type	\name, %function
\name:
	push	{r4, lr}
	mov	r1, #1
	mov	r2, #0
	mov	r4, #0
	\store
	.rept	8
	add	r4, r4, #1
	.endr
	str	r2, [r0]
	pop	{r4, lr}
	bx	lr
	.endm

	irq_enable stm_irq_enable_stm, stmia r0, {r1, r2}
	irq_enable stm_irq_enable_str, str r1, [r0]
