@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ Start-up code of the test ROMs: the cartridge header, the stacks, the
@ copies to work RAM, the default IRQ handler, then main() in system mode
@ with the CPSR's I bit clear and IME still 0. No BIOS call is made here,
@ so the first protected BIOS read still sees the value the BIOS left at
@ boot.

	.syntax unified
	.arm
	.section .crt0, "ax"
	.global _start
_start:
	b	reset
	@ The rest of the 192-byte header. tools/gba_header.py fills in the
	@ title, game code, maker code, fixed byte and complement check after
	@ the link; the logo stays blank (see tests/gba-rom.mk).
	.fill	0xBC, 1, 0

reset:
	mov	r0, #0xD2		@ IRQ mode, IRQ and FIQ masked
	msr	cpsr_c, r0
	ldr	sp, =0x03007FA0
	mov	r0, #0xD3		@ supervisor mode, for the BIOS's SWIs
	msr	cpsr_c, r0
	ldr	sp, =0x03007FE0
	mov	r0, #0x1F		@ system mode, I and F clear
	msr	cpsr_c, r0
	ldr	sp, =0x03007F00

	ldr	r0, =__iwram_load
	ldr	r1, =__iwram_start
	ldr	r2, =__iwram_end
	bl	copy_words
	ldr	r0, =__ewram_load
	ldr	r1, =__ewram_start
	ldr	r2, =__ewram_end
	bl	copy_words
	ldr	r1, =__bss_start
	ldr	r2, =__bss_end
	mov	r0, #0
1:	cmp	r1, r2
	strlo	r0, [r1], #4
	blo	1b

	ldr	r0, =irq_default
	ldr	r1, =0x03007FFC
	str	r0, [r1]

	ldr	r0, =main
	mov	lr, pc
	bx	r0
2:	b	2b

@ Copies words from r0 to r1 until r1 reaches r2.
copy_words:
	cmp	r1, r2
	ldrlo	r3, [r0], #4
	strlo	r3, [r1], #4
	blo	copy_words
	bx	lr

	.text
	.arm

@ Called by the BIOS's IRQ handler, in IRQ mode, which saves r0-r3, r12
@ and lr for it.
	.global irq_default
	.type irq_default, %function
irq_default:
	mov	r0, #0x04000000
	add	r0, r0, #0x200
	ldrh	r1, [r0]		@ IE
	ldrh	r2, [r0, #2]		@ IF
	and	r1, r1, r2
	strh	r1, [r0, #2]		@ acknowledged
	ldr	r0, =0x03007FF8
	ldrh	r2, [r0]
	orr	r2, r2, r1
	strh	r2, [r0]
	ldr	r0, =irq_seen
	ldr	r2, [r0]
	orr	r2, r2, r1
	str	r2, [r0]
	ldr	r0, =irq_count
	ldr	r2, [r0]
	add	r2, r2, #1
	str	r2, [r0]
	bx	lr

	.global cpsr_irq_off
	.type cpsr_irq_off, %function
cpsr_irq_off:
	mrs	r0, cpsr
	orr	r0, r0, #0x80
	msr	cpsr_c, r0
	bx	lr

	.global cpsr_irq_on
	.type cpsr_irq_on, %function
cpsr_irq_on:
	mrs	r0, cpsr
	bic	r0, r0, #0x80
	msr	cpsr_c, r0
	bx	lr

	.pool

	.bss
	.align	2
	.global irq_count
irq_count:
	.space	4
	.global irq_seen
irq_seen:
	.space	4
