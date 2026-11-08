@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ BIOS calls for cases_bios.c, each from ARM and from Thumb code:
@ swi_<name>_arm and swi_<name>_thumb pass their three arguments to the
@ SWI in r0-r2 and return. ARM code puts the function number in bits
@ 16-23 of the SWI comment, Thumb code in bits 0-7 (GBATEK, BIOS
@ Functions).

	.syntax unified
	.text

	.macro	bios_call name, number
	.arm
	.align	2
	.global	swi_\name\()_arm
	.type	swi_\name\()_arm, %function
swi_\name\()_arm:
	swi	\number << 16
	bx	lr

	.thumb
	.global	swi_\name\()_thumb
	.type	swi_\name\()_thumb, %function
	.thumb_func
swi_\name\()_thumb:
	swi	\number
	bx	lr
	.endm

	bios_call	cpuset, 0x0B
	bios_call	cpufastset, 0x0C
	bios_call	bitunpack, 0x10
	bios_call	lz77_wram, 0x11
	bios_call	lz77_vram, 0x12
	bios_call	huffman, 0x13
	bios_call	rl_wram, 0x14
	bios_call	rl_vram, 0x15
