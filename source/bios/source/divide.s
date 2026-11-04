@---------------------------------------------------------------------------------
@ Div and DivArm (SWI 0x06 and 0x07)
@
@ Custom GBA BIOS replacement
@ Copyright (c) 2002-2006 VBA Development Team
@ Copyright (c) 2006-2013 VBA-M Development Team
@ Copyright (c) 2013 Normmatt
@
@ This program is free software; you can redistribute it and/or modify it
@ under the terms of the GNU General Public License as published by the Free
@ Software Foundation; either version 2 of the License, or (at your option)
@ any later version.
@
@ Changed by GBA Rush PSP on 2026-11-05: rewritten so that Div and DivArm
@ return what the official BIOS returns (source/src/bios_hle.h): the
@ remainder with the sign of the numerator, the absolute quotient in r3,
@ and a result for every division by zero and for -2^31 divided by a
@ power of two, which used to loop forever. Div is the routine for C code;
@ swi_Div and swi_DivARM are the SWI entries. scripts/build-open-bios.py
@ assembles this file into the image in source/src/open_bios.h.
@---------------------------------------------------------------------------------
	.arm
	.text

@---------------------------------------------------------------------------------
@ The SWI handler in core.s saves r2, r3 and lr on the system mode stack
@ and restores them when the SWI returns. Div returns a value in r3, so
@ the SWI entries overwrite the saved copy.
@---------------------------------------------------------------------------------
.global swi_DivARM
.type swi_DivARM STT_FUNC
swi_DivARM:
	@ r0 = denominator, r1 = numerator
	mov     r3, r0
	mov     r0, r1
	mov     r1, r3

.global swi_Div
.type swi_Div STT_FUNC
swi_Div:
	stmfd   sp!, {lr}
	bl      Div
	ldmfd   sp!, {lr}
	str     r3, [sp, #4]
	bx      lr

@---------------------------------------------------------------------------------
@ r0 = numerator, r1 = denominator. Returns r0 = quotient, r1 = remainder
@ with the sign of the numerator, r3 = absolute quotient; changes r2 and
@ r12. Dividing by zero returns r0 = 1 or -1 (the sign of the numerator),
@ r1 = numerator and r3 = 1, as the official BIOS does for 0, 1 and -1
@ (it never returns for other numerators).
@---------------------------------------------------------------------------------
.global Div
.type Div STT_FUNC
Div:
	cmp     r1, #0
	beq     div_by_zero

	@ r12: bit 31 set when the quotient is negative, bit 0 when the
	@ numerator is
	eor     r12, r0, r1
	and     r12, r12, #0x80000000
	orr     r12, r12, r0, lsr #31

	@ Divide the absolute values as unsigned numbers (-2^31 stays
	@ 0x80000000)
	cmp     r0, #0
	rsblt   r0, r0, #0
	cmp     r1, #0
	rsblt   r1, r1, #0

	@ Shift the denominator up towards the numerator, never past bit 31
	mov     r2, #0              @ quotient
	mov     r3, #1              @ the quotient bit the denominator stands for
0:
	cmp     r1, #0x80000000
	cmpcc   r1, r0
	movcc   r1, r1, lsl #1
	movcc   r3, r3, lsl #1
	bcc     0b

	@ Grade-school division, one quotient bit per step
1:
	cmp     r0, r1
	subcs   r0, r0, r1
	orrcs   r2, r2, r3
	movs    r3, r3, lsr #1
	movne   r1, r1, lsr #1
	bne     1b

	@ r0 = absolute remainder, r2 = absolute quotient: put the signs back
	mov     r3, r2
	tst     r12, #1
	rsbne   r1, r0, #0
	moveq   r1, r0
	tst     r12, #0x80000000
	rsbne   r0, r2, #0
	moveq   r0, r2
	bx      lr

div_by_zero:
	mov     r1, r0
	mov     r3, #1
	cmp     r0, #0
	movge   r0, #1
	mvnlt   r0, #0
	bx      lr
