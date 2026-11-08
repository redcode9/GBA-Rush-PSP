@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ Two versions of an ARM and of a Thumb routine, copied into VRAM by the
@ VRAM code cases (cases_vram.c): the old one returns 1, the new one 2.
@ They are data here, run only from their copies.

	.syntax unified
	.text

	.arm
	.align	2
	.global	vram_arm_old
	.type	vram_arm_old, %object
vram_arm_old:
	mov	r0, #1
	bx	lr
	.global	vram_arm_new
	.type	vram_arm_new, %object
vram_arm_new:
	mov	r0, #2
	bx	lr

	.thumb
	.global	vram_thumb_old
	.type	vram_thumb_old, %object
vram_thumb_old:
	movs	r0, #1
	bx	lr
	.global	vram_thumb_new
	.type	vram_thumb_new, %object
vram_thumb_new:
	movs	r0, #2
	bx	lr
