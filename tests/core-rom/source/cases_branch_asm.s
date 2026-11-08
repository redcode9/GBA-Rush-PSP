@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ Branches timed by timer 0 at F/1, for the branch cases
@ (cases_branch.c). Each routine gets the address of TM0CNT_L in r0,
@ reads it, runs one branch (or one skipped instruction) that lands on
@ the next instruction, reads it again and returns the difference: the
@ first read plus the branch. Setup that must not be timed, such as the
@ flags or a target register, comes before the first read. The same code
@ is assembled into IWRAM and into EWRAM.

	.syntax unified

@ The second read, then the difference returned to \ret.
	.macro	result_arm ret=lr
	ldrh	r3, [r0]
	sub	r0, r3, r2
	lsl	r0, r0, #16
	lsr	r0, r0, #16
	bx	\ret
	.endm

	.macro	result_thumb ret=lr
	ldrh	r3, [r0]
	subs	r0, r3, r2
	lsls	r0, r0, #16
	lsrs	r0, r0, #16
	bx	\ret
	.endm

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

	.macro	branches where

	function_arm branch_arm_b_\where
	ldrh	r2, [r0]
	b	1f
1:	result_arm

	function_arm branch_arm_bl_\where
	mov	r12, lr
	ldrh	r2, [r0]
	bl	1f
1:	result_arm r12

	function_arm branch_arm_beq_\where
	cmp	r0, r0
	ldrh	r2, [r0]
	beq	1f
1:	result_arm

	function_arm branch_arm_bne_\where
	cmp	r0, r0
	ldrh	r2, [r0]
	bne	1f
1:	result_arm

	function_arm branch_arm_bx_\where
	adr	r1, 1f
	ldrh	r2, [r0]
	bx	r1
1:	result_arm

	function_arm branch_arm_movpc_\where
	adr	r1, 1f
	ldrh	r2, [r0]
	mov	pc, r1
1:	result_arm

	@ The word loaded comes from the same region as the code.
	function_arm branch_arm_ldrpc_\where
	adr	r1, 2f
	ldrh	r2, [r0]
	ldr	pc, [r1]
1:	result_arm
2:	.word	1b

	function_arm branch_arm_ldmpc_\where
	adr	r1, 2f
	ldrh	r2, [r0]
	ldmia	r1, {r12, pc}
1:	result_arm
2:	.word	0, 1b

	function_arm branch_arm_skip_\where
	cmp	r0, r0
	ldrh	r2, [r0]
	movne	r12, #0
	result_arm

	function_thumb branch_thumb_b_\where
	ldrh	r2, [r0]
	b	1f
1:	result_thumb

	function_thumb branch_thumb_beq_\where
	cmp	r0, r0
	ldrh	r2, [r0]
	beq	1f
1:	result_thumb

	function_thumb branch_thumb_bne_\where
	cmp	r0, r0
	ldrh	r2, [r0]
	bne	1f
1:	result_thumb

	function_thumb branch_thumb_bl_\where
	mov	r12, lr
	ldrh	r2, [r0]
	bl	1f
1:	result_thumb r12

	function_thumb branch_thumb_bx_\where
	ldr	r1, 2f
	ldrh	r2, [r0]
	bx	r1
1:	result_thumb
	.align	2
2:	.word	1b + 1

	@ The stack, and so the word popped, is in IWRAM.
	function_thumb branch_thumb_poppc_\where
	ldr	r1, 2f
	push	{r1}
	ldrh	r2, [r0]
	pop	{pc}
1:	result_thumb
	.align	2
2:	.word	1b + 1

	.endm

	.section .iwram, "ax", %progbits
	branches iwram

	.section .ewram, "ax", %progbits
	branches ewram
