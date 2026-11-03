/*
 * What the Media Engine runs when it leaves reset. main.c copies it to the
 * Media Engine's reset address in the boot page, 0xBFC00040, and fills in
 * the words at its end. It sets the Media Engine's own system registers as
 * Sony's start-up code does, empties both caches without writing anything
 * back (after a sleep they hold garbage), copies the program (renderer.c)
 * from main memory into eDRAM, writes it back there and jumps to it.
 * It runs uncached from the boot page and uses no memory but its own.
 */

	.set noreorder
	.set noat

/* The parameter words start this far into the code, which is copied to
   0xBFC00040; PARAMETER(n) is word n's place from the boot page's start. */
#define PARAMETERS 0xC0
#define PARAMETER(n) (0x40 + PARAMETERS + 4 * (n))
#define RAM_CONFIG  0
#define SOURCE      1
#define DESTINATION 2
#define WORDS       3
#define ENTRY       4
#define ARGUMENT    5

	.text
	.globl	media_engine_boot
	.globl	media_engine_boot_end
	.globl	media_engine_boot_parameters

media_engine_boot:
	lui	$27, 0xBFC0		/* k1 = the boot page */
	lui	$26, 0xBC10		/* k0 = the Media Engine's system registers */
	lw	$8, PARAMETER(RAM_CONFIG)($27)
	sw	$8, 0x40($26)		/* memory size, as the main processor has it */
	li	$8, 7
	sw	$8, 0x50($26)		/* bus clocks */
	li	$8, -1
	sw	$8, 0x04($26)		/* clear the interrupt flags */
	sync
	mtc0	$0, $13			/* Cause */
	lui	$8, 0x2000
	mtc0	$8, $12			/* Status: no interrupts, out of the reset state */
	mtc0	$0, $28			/* TagLo */
	mtc0	$0, $29			/* TagHi */
	sync

	/* Index store tag with zero tags: every line of both 16 KB caches
	   (two ways of 8 KB, the way picked by bit 13) becomes invalid. */
	li	$9, 0x4000
1:	addiu	$9, $9, -64
	cache	0x01, 0($9)		/* instruction cache */
	bnez	$9, 1b
	cache	0x11, 0($9)		/* data cache */
	sync

	/* Copy the program, a whole number of words, then write the data
	   cache back to eDRAM by index, for the instruction fetches. */
	lw	$8, PARAMETER(SOURCE)($27)
	lw	$9, PARAMETER(DESTINATION)($27)
	lw	$10, PARAMETER(WORDS)($27)
2:	lw	$11, 0($8)
	addiu	$8, $8, 4
	addiu	$10, $10, -1
	sw	$11, 0($9)
	bnez	$10, 2b
	addiu	$9, $9, 4
	li	$9, 0x4000
3:	addiu	$9, $9, -64
	lui	$8, 0x8000
	addu	$8, $8, $9
	bnez	$9, 3b
	cache	0x14, 0($8)		/* index writeback invalidate */
	sync

	lw	$4, PARAMETER(ARGUMENT)($27)
	lw	$8, PARAMETER(ENTRY)($27)
	lui	$29, 0x8020		/* stack at the top of eDRAM's first 2 MB */
	jr	$8
	nop

	.org	media_engine_boot + PARAMETERS
media_engine_boot_parameters:
	.word	0, 0, 0, 0, 0, 0	/* filled in by main.c, in the order above */
media_engine_boot_end:

/* The kernel's list of system event handlers, by a NID that is the same
   on every firmware. */
#include "pspimport.s"

	IMPORT_START "sceSysEventForKernel", 0x00010000
	IMPORT_FUNC  "sceSysEventForKernel", 0x68D55505, sceKernelReferSysEventHandler
