@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ SP-based block transfers with SP moved to the top passed in r0, for
@ the ewram_stack case (cases_stack.c). r1 points at the values to push,
@ followed by room for the values popped.

	.syntax unified
	.text

	.arm
	.global stack_arm
	.type stack_arm, %function
stack_arm:
	push	{r4-r11, lr}
	mov	r12, sp
	mov	sp, r0
	ldm	r1, {r2-r9}
	stmfd	sp!, {r2-r9}		@ to top-32 .. top-1
	sub	sp, sp, #32
	ldmfd	sp!, {r2-r9}		@ from top-64 .. top-33
	add	r1, r1, #32
	stm	r1, {r2-r9}
	mov	sp, r12
	pop	{r4-r11, lr}
	bx	lr

	.thumb
	.global stack_thumb
	.type stack_thumb, %function
	.thumb_func
stack_thumb:
	push	{r4-r7, lr}
	mov	r12, sp
	mov	sp, r0
	ldmia	r1!, {r2-r7}
	push	{r2-r7}			@ to top-24 .. top-1
	sub	sp, #24
	pop	{r2-r7}			@ from top-48 .. top-25
	stmia	r1!, {r2-r7}
	mov	sp, r12
	pop	{r4-r7}
	pop	{r3}
	bx	r3
