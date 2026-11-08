@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ Jumps to 10000000h above ROM code, for the pc_high case (cases_pc.c).
@ Each returns the bit of its state if the code up there ran, else 0.
@ Up there even the literal loads go through the high addresses.

	.syntax unified
	.text

	.arm
	.global pc_high_arm
	.type pc_high_arm, %function
pc_high_arm:
	mov	r0, #0
	ldr	r1, =arm_high + 0x10000000
	bx	r1
arm_high:
	mov	r0, #1
	ldr	r1, =arm_back
	bx	r1
arm_back:
	bx	lr
	.pool

	.thumb
	.global pc_high_thumb
	.type pc_high_thumb, %function
	.thumb_func
pc_high_thumb:
	movs	r0, #0
	ldr	r1, =thumb_high + 0x10000001
	bx	r1
thumb_high:
	movs	r0, #2
	ldr	r1, =thumb_back + 1
	bx	r1
thumb_back:
	bx	lr
	.align	2
	.pool
