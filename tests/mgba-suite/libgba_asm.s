@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ The assembly half of the libgba stand-in (libgba.h): the interrupt
@ dispatcher and the BIOS calls, after GBATEK.

	.syntax unified

@ Called by the BIOS's IRQ handler in IRQ mode, with r0-r3, r12 and lr
@ saved for it. Acknowledges the interrupts of the first irq_table slot
@ that matches (every pending enabled one when none does), in IF and in
@ the BIOS's flags at 03007FF8h that IntrWait reads, then jumps to that
@ slot's handler, which returns to the BIOS itself: handlers run in IRQ
@ mode with interrupts off. libgba's dispatcher takes a different number
@ of cycles, so results that time an interrupt (Timer count-up, Timer
@ IRQ) belong to this build.
	.section .iwram.text, "ax", %progbits
	.arm
	.global irq_dispatch
	.type irq_dispatch, %function
irq_dispatch:
	mov	r3, #0x04000000
	ldr	r2, [r3, #0x200]!	@ IE, IF << 16
	and	r1, r2, r2, lsr #16
	ldr	r0, =irq_table
1:	ldmia	r0!, {r2, r12}		@ handler, mask
	cmp	r12, #0
	beq	2f
	ands	r12, r12, r1
	beq	1b
	mov	r1, r12
2:	strh	r1, [r3, #2]		@ IF
	ldr	r0, =0x03007FF8
	ldrh	r12, [r0]
	orr	r12, r12, r1
	strh	r12, [r0]
	cmp	r2, #0
	bxne	r2
	bx	lr
	.pool

@ BIOS calls, called from Thumb code; r0-r3 pass the arguments and r0
@ returns the result, as the BIOS takes and gives them.
	.text
	.thumb
	.macro bios name, number
	.global \name
	.type \name, %function
	.thumb_func
\name:
	swi	\number
	bx	lr
	.endm

	bios Halt, 0x02
	bios IntrWait, 0x04
	bios VBlankIntrWait, 0x05
	bios Div, 0x06
	bios Sqrt, 0x08
	bios ArcTan, 0x09
	bios ArcTan2, 0x0A
	bios CpuSet, 0x0B
	bios CpuFastSet, 0x0C
	bios LZ77UnCompVram, 0x12
