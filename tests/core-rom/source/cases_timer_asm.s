@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ Timed code for the timer cases (cases_timer.c). Everything that is
@ timed runs from work RAM, where the cycle counts do not depend on the
@ GamePak prefetch buffer. r0 is the address of a timer's TMxCNT_L.

	.syntax unified

@ u32 timer_loop_*(vu16 *counter, u32 iterations): reads the counter,
@ runs a subs/bne loop of `iterations` turns, reads it again and returns
@ the difference. Between the two reads: the first read (opcode fetch,
@ I/O access, internal cycle) and the loop.
	.macro	loop_arm name
	.arm
	.global	\name
	.type	\name, %function
\name:
	ldrh	r2, [r0]
1:	subs	r1, r1, #1
	bne	1b
	ldrh	r3, [r0]
	sub	r0, r3, r2
	lsl	r0, r0, #16
	lsr	r0, r0, #16
	bx	lr
	.endm

	.section .iwram, "ax", %progbits
	loop_arm timer_loop_arm

	.thumb
	.global	timer_loop_thumb
	.type	timer_loop_thumb, %function
	.thumb_func
timer_loop_thumb:
	ldrh	r2, [r0]
1:	subs	r1, r1, #1
	bne	1b
	ldrh	r3, [r0]
	subs	r0, r3, r2
	lsls	r0, r0, #16
	lsrs	r0, r0, #16
	bx	lr

	.arm
	.align	2

@ u32 timer_start_read(vu16 *counter): starts the stopped timer at F/1
@ and reads it with the very next instruction; then stops it, starts it
@ again and reads it 4 cycles later. Returns the first value, and the
@ second in bits 16-31.
	.global	timer_start_read
	.type	timer_start_read, %function
timer_start_read:
	mov	r1, #0x80
	strh	r1, [r0, #2]
	ldrh	r2, [r0]
	mov	r3, #0
	strh	r3, [r0, #2]
	strh	r1, [r0, #2]
	mov	r3, r3
	mov	r3, r3
	mov	r3, r3
	mov	r3, r3
	ldrh	r3, [r0]
	orr	r0, r2, r3, lsl #16
	bx	lr

@ u32 timer_stop_read(vu16 *counter): starts the stopped timer at F/1,
@ stops it 64 cycles after the start (a mov, a 16-turn loop of 62 cycles
@ and the stop's opcode fetch), then reads it twice, 64 cycles apart.
@ Returns the first value, and the second in bits 16-31.
	.global	timer_stop_read
	.type	timer_stop_read, %function
timer_stop_read:
	mov	r1, #0x80
	mov	r3, #0
	strh	r1, [r0, #2]
	mov	r2, #16
1:	subs	r2, r2, #1
	bne	1b
	strh	r3, [r0, #2]
	ldrh	r1, [r0]
	mov	r2, #16
2:	subs	r2, r2, #1
	bne	2b
	ldrh	r2, [r0]
	orr	r0, r1, r2, lsl #16
	bx	lr

@ u32 timer_irq_on(vu16 *counter): sets the IRQ bit of a running F/1
@ timer between two reads. Returns the difference of the reads: the first
@ read and the store, 5 cycles, when the write leaves the count alone.
	.global	timer_irq_on
	.type	timer_irq_on, %function
timer_irq_on:
	mov	r1, #0xC0
	ldrh	r2, [r0]
	strh	r1, [r0, #2]
	ldrh	r3, [r0]
	sub	r0, r3, r2
	lsl	r0, r0, #16
	lsr	r0, r0, #16
	bx	lr

@ u32 timer_prescale_change(vu16 *counter): starts the stopped timer at
@ F/1, switches it to F/64 513 cycles later without stopping it, reads
@ it, runs the 1024-turn loop and reads it again. Returns the difference
@ of the reads, and the first read in bits 16-31.
	.global	timer_prescale_change
	.type	timer_prescale_change, %function
timer_prescale_change:
	mov	r1, #0x80
	strh	r1, [r0, #2]
	mov	r2, #128
1:	subs	r2, r2, #1
	bne	1b
	mov	r1, #0x81
	strh	r1, [r0, #2]
	ldrh	r2, [r0]
	mov	r1, #1024
2:	subs	r1, r1, #1
	bne	2b
	ldrh	r3, [r0]
	sub	r3, r3, r2
	lsl	r3, r3, #16
	lsr	r3, r3, #16
	orr	r0, r3, r2, lsl #16
	bx	lr

	.section .ewram, "ax", %progbits
	loop_arm timer_loop_ewram
