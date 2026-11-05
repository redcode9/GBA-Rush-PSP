@ GBA Rush PSP
@
@ Copyright (C) 2026 Piero Carrieri
@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License as
@ published by the Free Software Foundation; either version 2 of
@ the License, or (at your option) any later version.

@ The code under test of the CPU cases (cases_cpu.c). Every function is
@ called from C in system mode and returns there with the CPSR's I and F
@ bits clear. Code that changes mode keeps what it needs in r0-r7, the
@ only registers no mode banks.

	.syntax unified
	.text

@ ---- Shared pieces -------------------------------------------------------

@ CPSR mode bits in the order the bank tables use: IRQ, SVC, ABT, UND,
@ System, FIQ.
	.global mode_bits
	.type mode_bits, %object
mode_bits:
	.byte	0x12, 0x13, 0x17, 0x1B, 0x1F, 0x11
	.align	2

@ Saves the system mode SP and r13-r14 of the five exception modes in
@ mode_saved; ends in system mode with IRQs and FIQs masked. Uses r1.
	.macro save_banks
	ldr	r1, =mode_saved
	str	sp, [r1], #4
	msr	cpsr_c, #0xD2
	stmia	r1!, {sp, lr}
	msr	cpsr_c, #0xD3
	stmia	r1!, {sp, lr}
	msr	cpsr_c, #0xD7
	stmia	r1!, {sp, lr}
	msr	cpsr_c, #0xDB
	stmia	r1!, {sp, lr}
	msr	cpsr_c, #0xD1
	stmia	r1!, {sp, lr}
	msr	cpsr_c, #0xDF
	.endm

@ Puts back what save_banks saved; ends in system mode with I and F clear.
@ Uses r1.
	.macro restore_banks
	ldr	r1, =mode_saved + 4
	msr	cpsr_c, #0xD2
	ldmia	r1!, {sp, lr}
	msr	cpsr_c, #0xD3
	ldmia	r1!, {sp, lr}
	msr	cpsr_c, #0xD7
	ldmia	r1!, {sp, lr}
	msr	cpsr_c, #0xDB
	ldmia	r1!, {sp, lr}
	msr	cpsr_c, #0xD1
	ldmia	r1!, {sp, lr}
	msr	cpsr_c, #0x1F
	ldr	r1, =mode_saved
	ldr	sp, [r1]
	.endm

@ Loads (ldmia) or stores (stmia) r8-r14 of every mode from or to the
@ table at r0, 8 words a mode in the order of mode_bits; the non-FIQ
@ modes share r8-r12. Ends in system mode, IRQs and FIQs masked. Uses r1-r3.
	.macro each_bank op
	ldr	r1, =mode_bits
	mov	r2, #0
1:	ldrb	r3, [r1, r2]
	orr	r3, r3, #0xC0
	msr	cpsr_c, r3
	add	r3, r0, r2, lsl #5
	\op	r3, {r8-r14}
	add	r2, r2, #1
	cmp	r2, #6
	blo	1b
	msr	cpsr_c, #0xDF
	.endm

@ Starts a one-word DMA3 copy that raises its IRQ when it ends, with
@ IE = DMA3 and IME = 1; the starting store is the last instruction.
@ Uses r0-r1.
	.macro dma3_irq_start
	mov	r0, #0x04000000
	ldr	r1, =dma_word
	str	r1, [r0, #0xD4]
	str	r1, [r0, #0xD8]
	add	r0, r0, #0x200
	mov	r1, #0x800
	strh	r1, [r0]
	mov	r1, #1
	strh	r1, [r0, #8]
	sub	r0, r0, #0x200
	ldr	r1, =0xC4000001		@ enable, IRQ, 32-bit, one unit
	str	r1, [r0, #0xDC]
	.endm

@ Waits until IF shows the DMA3 IRQ, for a bounded time. Uses r0-r2.
	.macro dma3_irq_wait_if
	mov	r0, #0x04000000
	add	r0, r0, #0x200
	mov	r1, #0x4000
1:	ldrh	r2, [r0, #2]
	tst	r2, #0x800
	bne	2f
	subs	r1, r1, #1
	bne	1b
2:
	.endm

@ Waits until cpu_irq_handler has run, for a bounded time, without
@ changing the flags, so an IRQ that comes late still saves the flags
@ the code under test left. Uses r0-r3.
	.macro wait_irq
	mov	r0, #0x4000
	ldr	r1, =irq_log
1:	ldr	r2, [r1]		@ handler calls
	sub	r0, r0, #1
	sub	r3, r0, #1
	orr	r2, r2, r3, lsr #31	@ or 1 once the time is up
	rsb	r2, r2, #0
	mov	r2, r2, lsr #31		@ 1 to stop waiting, 0 to wait on
	add	pc, pc, r2, lsl #2
	nop
	b	1b
	.endm

@ Clears the handler log; spsr_or is ORed into the SPSR the handler
@ returns with. Uses r0-r1.
	.macro irq_log_reset spsr_or
	ldr	r0, =irq_log
	mov	r1, #0
	str	r1, [r0]
	mov	r1, #\spsr_or
	str	r1, [r0, #32]
	.endm

@ IME = IE = 0 and every IF bit acknowledged. Uses r0-r1.
	.macro irq_off
	mov	r0, #0x04000000
	add	r0, r0, #0x200
	mov	r1, #0
	strh	r1, [r0, #8]
	strh	r1, [r0]
	mvn	r1, #0
	strh	r1, [r0, #2]
	.endm

@ The IRQ handler of the IRQ cases, called by the BIOS in IRQ mode with
@ r0-r3, r12 and lr_irq pushed on the IRQ stack (lr_irq at [sp, #20]).
@ Acknowledges every pending IRQ and logs the call in irq_log: calls,
@ SPSR, lr_irq, r8-r12 as IRQ mode sees them. It returns with the SPSR
@ ORed with irq_log[8]: 80h masks IRQs after the return, so an MSR that
@ runs again (and clears I) shows; 8Fh also turns user mode into system
@ mode, the way out of user mode.
	.arm
	.global cpu_irq_handler
	.type cpu_irq_handler, %function
cpu_irq_handler:
	mov	r0, #0x04000000
	add	r0, r0, #0x200
	ldrh	r1, [r0, #2]
	strh	r1, [r0, #2]
	ldr	r0, =irq_log
	ldr	r1, [r0]
	add	r1, r1, #1
	str	r1, [r0]
	mrs	r1, spsr
	str	r1, [r0, #4]
	ldr	r2, [sp, #20]
	str	r2, [r0, #8]
	add	r2, r0, #12
	stmia	r2, {r8-r12}
	ldr	r2, [r0, #32]
	orr	r1, r1, r2
	msr	spsr_fc, r1
	bx	lr
	.pool

@ ---- cpu_shift_reg --------------------------------------------------------

@ void shift_run(u32 variant, u32 *io): io[0] Rm, io[1] Rs, io[2] NZCV in;
@ returns io[0] result, io[2] CPSR after (Thumb: only C, in bit 29),
@ io[3] the PC value the Rm = PC form read. variant = form * 4 + kind.
	.global shift_run
	.type shift_run, %function
shift_run:
	push	{r4, r5, lr}
	mov	r4, r1
	ldr	r5, =shift_variants
	ldr	r5, [r5, r0, lsl #2]
	ldmia	r4, {r0-r2}
	msr	cpsr_f, r2
	mov	lr, pc
	bx	r5
	str	r0, [r4]
	str	r1, [r4, #8]
	str	r3, [r4, #12]
	pop	{r4, r5, lr}
	bx	lr
	.pool

	.macro shift_arm kind, index
shift_movs_\kind:
	movs	r0, r0, \kind r1
	mrs	r1, cpsr
	bx	lr
shift_mov_\kind:
	mov	r0, r0, \kind r1
	mrs	r1, cpsr
	bx	lr
shift_add_\kind:
	mvn	r3, #0
	add	r0, r3, r0, \kind r1
	mrs	r1, cpsr
	bx	lr
shift_self_\kind:
	movs	r0, r1, \kind r1
	mrs	r1, cpsr
	bx	lr
shift_pc_\kind:
	@ movs r0, pc, <kind> r1: reads its own address + 12 (UNPREDICTABLE
	@ in the manual, written by hand to keep the assembler quiet).
	.inst	0xE1B0011F | (\index << 5)
	mov	r3, pc			@ the same address + 12
	mrs	r1, cpsr
	bx	lr
	.endm

	shift_arm lsl, 0
	shift_arm lsr, 1
	shift_arm asr, 2
	shift_arm ror, 3

	.thumb
	.macro shift_thumb kind
shift_live_\kind:
	\kind\()s r0, r0, r1
	movs	r2, #0
	adcs	r2, r2			@ C, read after the shift
	lsls	r1, r2, #29
	bx	lr
shift_dead_\kind:
	\kind\()s r0, r0, r1
	cmp	r0, r0			@ overwrites C unread
	bx	lr
	.endm

	shift_thumb lsl
	shift_thumb lsr
	shift_thumb asr
	shift_thumb ror

	.align	2
	.arm
shift_variants:
	.word	shift_movs_lsl, shift_movs_lsr, shift_movs_asr, shift_movs_ror
	.word	shift_mov_lsl, shift_mov_lsr, shift_mov_asr, shift_mov_ror
	.word	shift_add_lsl, shift_add_lsr, shift_add_asr, shift_add_ror
	.word	shift_self_lsl, shift_self_lsr, shift_self_asr, shift_self_ror
	.word	shift_pc_lsl, shift_pc_lsr, shift_pc_asr, shift_pc_ror
	.word	shift_live_lsl + 1, shift_live_lsr + 1
	.word	shift_live_asr + 1, shift_live_ror + 1
	.word	shift_dead_lsl + 1, shift_dead_lsr + 1
	.word	shift_dead_asr + 1, shift_dead_ror + 1


@ ---- cpu_pc12_rn ----------------------------------------------------------

@ u32 pc12_rn(void): ADD, SUB, AND and CMP with Rn = PC, each with a shift
@ by register (Rs = 0; expected PC + 12, the instruction's address + 12)
@ and with an immediate shift (expected PC + 8). Returns a bit per wrong
@ read: ADD, ADD, SUB, SUB, AND, AND, CMP, CMP, register form first. The
@ register forms are UNPREDICTABLE in the manual, so written by hand.
	.global pc12_rn
	.type pc12_rn, %function
pc12_rn:
	push	{r4, lr}
	mov	r0, #0
	mov	r1, #0			@ Rm
	mov	r2, #0			@ Rs: a shift by 0
	mvn	r3, #0			@ all ones, for AND
pc_add_reg:
	.inst	0xE08F4211		@ add r4, pc, r1, lsl r2
	adr	r12, pc_add_reg + 12
	cmp	r4, r12
	orrne	r0, r0, #1
pc_add_imm:
	add	r4, pc, r1
	adr	r12, pc_add_imm + 8
	cmp	r4, r12
	orrne	r0, r0, #2
pc_sub_reg:
	.inst	0xE04F4211		@ sub r4, pc, r1, lsl r2
	adr	r12, pc_sub_reg + 12
	cmp	r4, r12
	orrne	r0, r0, #4
pc_sub_imm:
	sub	r4, pc, r1
	adr	r12, pc_sub_imm + 8
	cmp	r4, r12
	orrne	r0, r0, #8
pc_and_reg:
	.inst	0xE00F4213		@ and r4, pc, r3, lsl r2
	adr	r12, pc_and_reg + 12
	cmp	r4, r12
	orrne	r0, r0, #16
pc_and_imm:
	and	r4, pc, r3
	adr	r12, pc_and_imm + 8
	cmp	r4, r12
	orrne	r0, r0, #32
	adr	r12, pc_cmp_reg + 12
pc_cmp_reg:
	.inst	0xE15F021C		@ cmp pc, r12, lsl r2
	orrne	r0, r0, #64
	adr	r12, pc_cmp_imm + 8
pc_cmp_imm:
	cmp	pc, r12
	orrne	r0, r0, #128
	pop	{r4, lr}
	bx	lr

@ ---- cpu_msr_c_flags ------------------------------------------------------

@ In the mode of bits: N and V written to the CPSR by MSR CPSR_f, then Z
@ and C live from a CMP, then an MSR CPSR_c that stays in the mode, first
@ with an immediate, then with a register. A conditional instruction and
@ MRS right after each must see Z and C. Sets r0 bit 0 (immediate) or
@ bit 1 (register) when they do not, and returns.
	.macro msr_flags_in bits
	msr	cpsr_c, #(0xC0 | \bits)
	mov	r0, #0
	msr	cpsr_f, #0x90000000
	cmp	r0, r0
	msr	cpsr_c, #(0xC0 | \bits)
	orrne	r0, r0, #1
	mrs	r1, cpsr
	and	r1, r1, #0xF0000000
	cmp	r1, #0x60000000
	orrne	r0, r0, #1
	mov	r2, #(0xC0 | \bits)
	msr	cpsr_f, #0x90000000
	cmp	r2, r2
	msr	cpsr_c, r2
	orrne	r0, r0, #2
	mrs	r1, cpsr
	and	r1, r1, #0xF0000000
	cmp	r1, #0x60000000
	orrne	r0, r0, #2
	msr	cpsr_c, #0x1F
	bx	lr
	.endm

@ u32 msr_flags(u32 mode): mode indexes mode_bits.
	.global msr_flags
	.type msr_flags, %function
msr_flags:
	add	pc, pc, r0, lsl #2
	nop
	b	msr_flags_irq
	b	msr_flags_svc
	b	msr_flags_abt
	b	msr_flags_und
	b	msr_flags_sys
	b	msr_flags_fiq
msr_flags_irq:
	msr_flags_in 0x12
msr_flags_svc:
	msr_flags_in 0x13
msr_flags_abt:
	msr_flags_in 0x17
msr_flags_und:
	msr_flags_in 0x1B
msr_flags_sys:
	msr_flags_in 0x1F
msr_flags_fiq:
	msr_flags_in 0x11

@ u32 msr_flags_xs(void): the same with MSR CPSR_x (bit 0) and MSR CPSR_s
@ (bit 1), which write no flags, in system mode.
	.global msr_flags_xs
	.type msr_flags_xs, %function
msr_flags_xs:
	mov	r0, #0
	mov	r2, #0
	msr	cpsr_f, #0x90000000
	cmp	r2, r2
	msr	cpsr_x, r2
	orrne	r0, r0, #1
	mrs	r1, cpsr
	and	r1, r1, #0xF0000000
	cmp	r1, #0x60000000
	orrne	r0, r0, #1
	msr	cpsr_f, #0x90000000
	cmp	r2, r2
	msr	cpsr_s, r2
	orrne	r0, r0, #2
	mrs	r1, cpsr
	and	r1, r1, #0xF0000000
	cmp	r1, #0x60000000
	orrne	r0, r0, #2
	bx	lr

@ u32 msr_flags_user(void): in user mode, MSR CPSR_c towards system mode
@ with I and F set, immediate then register, must change nothing: bit 0
@ (immediate) or 2 (register) when the flags of the CMP before it are
@ lost, bit 1 or 3 when the low byte of the CPSR is no longer 10h. User
@ mode is left through an IRQ whose handler returns to system mode
@ (cpu_irq_handler must be the IRQ vector); if no IRQ comes, this hangs.
	.global msr_flags_user
	.type msr_flags_user, %function
msr_flags_user:
	push	{r4, lr}
	irq_log_reset 0x8F
	mov	r0, #0
	msr	cpsr_c, #0x10
	msr	cpsr_f, #0x90000000
	cmp	r0, r0
	msr	cpsr_c, #0xDF
	orrne	r0, r0, #1
	mrs	r1, cpsr
	and	r2, r1, #0xF0000000
	cmp	r2, #0x60000000
	orrne	r0, r0, #1
	and	r2, r1, #0xFF
	cmp	r2, #0x10
	orrne	r0, r0, #2
	mov	r3, #0xDF
	msr	cpsr_f, #0x90000000
	cmp	r3, r3
	msr	cpsr_c, r3
	orrne	r0, r0, #4
	mrs	r1, cpsr
	and	r2, r1, #0xF0000000
	cmp	r2, #0x60000000
	orrne	r0, r0, #4
	and	r2, r1, #0xFF
	cmp	r2, #0x10
	orrne	r0, r0, #8
	mov	r4, r0
	dma3_irq_start
1:	mrs	r1, cpsr
	and	r1, r1, #0x1F
	cmp	r1, #0x1F
	bne	1b
	irq_off
	msr	cpsr_c, #0x1F
	mov	r0, r4
	pop	{r4, lr}
	bx	lr
	.pool

@ ---- cpu_irq_after_msr ----------------------------------------------------

@ After the unmasking MSR: four counted instructions, then a wait that
@ keeps the flags. Records the count and the CPSR in out[0] and out[1].
	.macro after_msr
	add	r4, r4, #1
	add	r4, r4, #1
	add	r4, r4, #1
	add	r4, r4, #1
	wait_irq
	b	irq_after_msr_done
	.endm

@ void irq_after_msr(u32 which, u32 *out): with the DMA3 IRQ pending and
@ IRQs masked, N and V written to the CPSR, Z and C live from a CMP, then
@ an MSR that unmasks: which 0 MSR CPSR_c in SVC, 1 MSR CPSR_c in system
@ mode, 2 MSR CPSR_fc in system mode with N and C. cpu_irq_handler must
@ be the IRQ vector.
	.global irq_after_msr
	.type irq_after_msr, %function
irq_after_msr:
	push	{r4-r7, lr}
	mov	r6, r0
	mov	r7, r1
	irq_log_reset 0x80
	cmp	r6, #0
	msreq	cpsr_c, #0xD3
	msrne	cpsr_c, #0xDF
	dma3_irq_start
	dma3_irq_wait_if
	mov	r4, #0
	ldr	r2, =0xA000001F
	cmp	r6, #1
	beq	irq_after_msr_sys
	bhi	irq_after_msr_fc
	msr	cpsr_f, #0x90000000
	cmp	r0, r0
	msr	cpsr_c, #0x13
	after_msr
irq_after_msr_sys:
	msr	cpsr_f, #0x90000000
	cmp	r0, r0
	msr	cpsr_c, #0x1F
	after_msr
irq_after_msr_fc:
	msr	cpsr_f, #0x90000000
	cmp	r0, r0
	msr	cpsr_fc, r2
	after_msr
irq_after_msr_done:
	mrs	r5, cpsr
	msr	cpsr_c, #0xDF
	stmia	r7, {r4, r5}
	irq_off
	msr	cpsr_c, #0x1F
	pop	{r4-r7, lr}
	bx	lr
	.pool

@ ---- cpu_irq_unmask -------------------------------------------------------

@ u32 irq_unmask(u32 which): with the DMA3 IRQ pending and IRQs masked,
@ unmasks them with which 0 MSR CPSR_c, 1 MSR CPSR_fc, 2 SUBS pc, lr, #4
@ from SVC, 3 LDM {pc}^ from SVC, 4 MSR CPSR_c followed by an MSR that
@ masks again; returns the lr_irq expected, the address of the next
@ instruction + 4. which 5 (cpu_irq_latency) raises the IRQ with IRQs
@ unmasked, before 16 counted instructions, and returns the address of the
@ first of them.
@ cpu_irq_handler must be the IRQ vector.
	.global irq_unmask
	.type irq_unmask, %function
irq_unmask:
	push	{r4, r5, lr}
	mov	r5, r0
	irq_log_reset 0x80
	mov	r4, #0
	add	pc, pc, r5, lsl #2
	nop
	b	unmask_msr_c
	b	unmask_msr_fc
	b	unmask_subs
	b	unmask_ldm
	b	unmask_window
	b	unmask_control
unmask_msr_c:
	msr	cpsr_c, #0xDF
	dma3_irq_start
	dma3_irq_wait_if
	msr	cpsr_c, #0x1F
unmask_msr_c_next:
	add	r4, r4, #1
	wait_irq
	ldr	r4, =unmask_msr_c_next + 4
	b	unmask_done
unmask_msr_fc:
	msr	cpsr_c, #0xDF
	dma3_irq_start
	dma3_irq_wait_if
	mov	r1, #0x1F
	msr	cpsr_fc, r1
unmask_msr_fc_next:
	add	r4, r4, #1
	wait_irq
	ldr	r4, =unmask_msr_fc_next + 4
	b	unmask_done
unmask_subs:
	msr	cpsr_c, #0xD3
	dma3_irq_start
	dma3_irq_wait_if
	msr	spsr_fc, #0x1F
	ldr	lr, =unmask_subs_next + 4
	subs	pc, lr, #4
unmask_subs_next:
	add	r4, r4, #1
	wait_irq
	ldr	r4, =unmask_subs_next + 4
	b	unmask_done
unmask_ldm:
	msr	cpsr_c, #0xD3
	dma3_irq_start
	dma3_irq_wait_if
	msr	spsr_fc, #0x1F
	ldr	r0, =unmask_ldm_target
	ldmia	r0, {pc}^
unmask_ldm_next:
	add	r4, r4, #1
	wait_irq
	ldr	r4, =unmask_ldm_next + 4
	b	unmask_done
unmask_window:
	msr	cpsr_c, #0xDF
	dma3_irq_start
	dma3_irq_wait_if
	msr	cpsr_c, #0x1F
unmask_window_next:
	msr	cpsr_c, #0x9F
	ldr	r4, =unmask_window_next + 4
	b	unmask_done
unmask_control:
	dma3_irq_start
unmask_control_first:
	.rept	16
	add	r4, r4, #1
	.endr
	wait_irq
	ldr	r4, =unmask_control_first
unmask_done:
	irq_off
	msr	cpsr_c, #0x1F
	mov	r0, r4
	pop	{r4, r5, lr}
	bx	lr
unmask_ldm_target:
	.word	unmask_ldm_next
	.pool

@ ---- cpu_mode_pairs and cpu_fiq_bank --------------------------------------

@ void mode_switch(const u32 *bank, u32 *out, u32 from, u32 to_method):
@ loads r8-r14 of every mode from bank (7 words of 8 a mode, in the order
@ of mode_bits), enters mode from by MSR, then switches to mode
@ to_method & FFh with method to_method >> 8:
@   0 MSR CPSR_c immediate      1 MSR CPSR_c register
@   2 MOVS pc, lr to ARM        3 MOVS pc, lr to Thumb
@   4 SUBS pc, lr, #4 to ARM    5 SUBS pc, lr, #4 to Thumb
@   6 LDM {pc}^ to ARM          7 LDM {pc}^ to Thumb
@ IRQs and FIQs stay masked. out[0..1] r13-r14 seen there, out[2] the CPSR
@ (back in ARM state), out[3..7] r8-r12, out[8] the lr or memory word the
@ return used, out[9] 0Ah if the ARM landing ran, 7 for the Thumb one,
@ out[16..63] r8-r14 of every mode at the end, laid out like bank.
	.global mode_switch
	.type mode_switch, %function
mode_switch:
	push	{r4-r11, lr}
	mov	r4, r1
	mov	r5, r2
	and	r6, r3, #0xFF
	mov	r7, r3, lsr #8
	save_banks
	each_bank ldmia
	ldr	r1, =mode_bits
	ldrb	r2, [r1, r5]
	orr	r2, r2, #0xC0
	msr	cpsr_c, r2
	ldrb	r3, [r1, r6]
	orr	r3, r3, #0xC0
	add	pc, pc, r7, lsl #2
	nop
	b	switch_msr_imm
	b	switch_msr_reg
	b	switch_movs_arm
	b	switch_movs_thumb
	b	switch_subs_arm
	b	switch_subs_thumb
	b	switch_ldm_arm
	b	switch_ldm_thumb
switch_msr_imm:
	add	pc, pc, r6, lsl #3
	nop
	msr	cpsr_c, #0xD2
	b	switch_arm
	msr	cpsr_c, #0xD3
	b	switch_arm
	msr	cpsr_c, #0xD7
	b	switch_arm
	msr	cpsr_c, #0xDB
	b	switch_arm
	msr	cpsr_c, #0xDF
	b	switch_arm
	msr	cpsr_c, #0xD1
	b	switch_arm
switch_msr_reg:
	msr	cpsr_c, r3
	b	switch_arm
switch_movs_thumb:
	orr	r3, r3, #0x20
	ldr	lr, =switch_thumb
	b	1f
switch_movs_arm:
	ldr	lr, =switch_arm
1:	msr	spsr_fc, r3
	str	lr, [r4, #32]
	movs	pc, lr
switch_subs_thumb:
	orr	r3, r3, #0x20
	ldr	lr, =switch_thumb + 4
	b	1f
switch_subs_arm:
	ldr	lr, =switch_arm + 4
1:	msr	spsr_fc, r3
	str	lr, [r4, #32]
	subs	pc, lr, #4
switch_ldm_thumb:
	orr	r3, r3, #0x20
	ldr	r0, =switch_thumb
	b	1f
switch_ldm_arm:
	ldr	r0, =switch_arm
1:	msr	spsr_fc, r3
	str	r0, [r4, #32]
	add	r0, r4, #32
	ldmia	r0, {pc}^
	.pool

	.thumb
switch_thumb:
	mov	r0, sp
	str	r0, [r4]
	mov	r0, lr
	str	r0, [r4, #4]
	mov	r0, r8
	str	r0, [r4, #12]
	mov	r0, r9
	str	r0, [r4, #16]
	mov	r0, r10
	str	r0, [r4, #20]
	mov	r0, r11
	str	r0, [r4, #24]
	mov	r0, r12
	str	r0, [r4, #28]
	movs	r0, #7
	str	r0, [r4, #36]
	ldr	r0, =switch_record
	bx	r0
	.align	2
	.pool

	.arm
switch_arm:
	str	sp, [r4]
	str	lr, [r4, #4]
	add	r0, r4, #12
	stmia	r0, {r8-r12}
	mov	r0, #0x0A
	str	r0, [r4, #36]
switch_record:
	mrs	r0, cpsr
	str	r0, [r4, #8]
	msr	cpsr_c, #0xDF
	add	r0, r4, #64
	each_bank stmia
	restore_banks
	pop	{r4-r11, lr}
	bx	lr
	.pool

@ void fiq_irq(const u32 *bank, u32 *out): shared r8-r12 from bank row 4,
@ FIQ r8-r14 from row 5, then the DMA3 IRQ taken in FIQ mode with IRQs
@ unmasked. out[0..6] FIQ r8-r14 after it, out[8..12] the shared r8-r12;
@ irq_log has what the handler saw. cpu_irq_handler must be the IRQ
@ vector.
	.global fiq_irq
	.type fiq_irq, %function
fiq_irq:
	push	{r4-r11, lr}
	mov	r4, r1
	mov	r5, r0
	irq_log_reset 0x80
	add	r1, r5, #4 * 32
	ldmia	r1, {r8-r12}
	add	r1, r5, #5 * 32
	msr	cpsr_c, #0xD1
	ldmia	r1, {r8-r14}
	dma3_irq_start
	dma3_irq_wait_if
	msr	cpsr_c, #0x51
	wait_irq
	stmia	r4, {r8-r14}
	msr	cpsr_c, #0xDF
	add	r0, r4, #32
	stmia	r0, {r8-r12}
	irq_off
	msr	cpsr_c, #0x1F
	pop	{r4-r11, lr}
	bx	lr
	.pool

@ void fiq_svc_fiq(const u32 *bank, u32 *out): shared r8-r12 from bank
@ row 4, FIQ r8-r12 from row 5, then FIQ -> SVC, r8-r12 from row 6 there,
@ SVC -> FIQ. out[0..4] FIQ r8-r12 at the end, out[8..12] the shared
@ r8-r12.
	.global fiq_svc_fiq
	.type fiq_svc_fiq, %function
fiq_svc_fiq:
	push	{r4-r11, lr}
	mov	r4, r1
	add	r1, r0, #4 * 32
	ldmia	r1, {r8-r12}
	add	r1, r0, #5 * 32
	msr	cpsr_c, #0xD1
	ldmia	r1, {r8-r12}
	add	r1, r0, #6 * 32
	msr	cpsr_c, #0xD3
	ldmia	r1, {r8-r12}
	msr	cpsr_c, #0xD1
	stmia	r4, {r8-r12}
	msr	cpsr_c, #0xDF
	add	r0, r4, #32
	stmia	r0, {r8-r12}
	msr	cpsr_c, #0x1F
	pop	{r4-r11, lr}
	bx	lr

@ void swi_fiq(const u32 *bank, u32 *out): shared r8-r12 from bank row 4,
@ FIQ r8-r14 from row 5, then SWI 06h (Div, 7 / 2) from FIQ mode.
@ out[0..6] FIQ r8-r14 after it, out[7] r0, out[8..12] the shared r8-r12,
@ out[13] the CPSR after the SWI.
	.global swi_fiq
	.type swi_fiq, %function
swi_fiq:
	push	{r4-r11, lr}
	mov	r4, r1
	add	r1, r0, #4 * 32
	ldmia	r1, {r8-r12}
	add	r1, r0, #5 * 32
	msr	cpsr_c, #0xD1
	ldmia	r1, {r8-r14}
	mov	r0, #7
	mov	r1, #2
	swi	0x60000
	stmia	r4, {r8-r14}
	str	r0, [r4, #28]
	mrs	r1, cpsr
	str	r1, [r4, #52]
	msr	cpsr_c, #0xDF
	add	r0, r4, #32
	stmia	r0, {r8-r12}
	msr	cpsr_c, #0x1F
	pop	{r4-r11, lr}
	bx	lr

@ ---- cpu_user_bank --------------------------------------------------------

@ void user_bank(u32 mode, u32 form, const u32 *setup, u32 *out): loads
@ the user r8-r14 from setup[0..6] and r8-r14 of mode (CPSR mode bits)
@ from setup[8..14], then in that mode runs form:
@   0 STMIA r13, {r8-r14}^      1 LDMIA r13, {r8-r14}^
@   2 STMIA r13, {r8-r15}^      3-5 the same with base r8 (FIQ mode)
@ out[0..6] r8-r14 of the mode after it, out[8..14] the user r8-r14.
	.global user_bank
	.type user_bank, %function
user_bank:
	push	{r4-r11, lr}
	mov	r4, r3
	mov	r5, r1
	orr	r6, r0, #0xC0
	save_banks
	ldmia	r2, {r8-r14}
	add	r2, r2, #32
	msr	cpsr_c, r6
	ldmia	r2, {r8-r14}
	add	pc, pc, r5, lsl #2
	nop
	b	user_stm
	b	user_ldm
	b	user_stm_pc
	b	user_stm_r8
	b	user_ldm_r8
	b	user_stm_pc_r8
user_stm:
	stmia	sp, {r8-r14}^
	b	user_done
user_ldm:
	ldmia	sp, {r8-r14}^
	nop				@ no banked register right after
	b	user_done
user_stm_pc:
	stmia	sp, {r8-pc}^
	b	user_done
user_stm_r8:
	stmia	r8, {r8-r14}^
	b	user_done
user_ldm_r8:
	ldmia	r8, {r8-r14}^
	nop
	b	user_done
user_stm_pc_r8:
	stmia	r8, {r8-pc}^
user_done:
	stmia	r4, {r8-r14}
	msr	cpsr_c, #0xDF
	add	r0, r4, #32
	stmia	r0, {r8-r14}
	restore_banks
	pop	{r4-r11, lr}
	bx	lr
	.pool

@ ---- cpu_bx_align ---------------------------------------------------------

@ The landing word for jumps to its address + 2: running from the word
@ adds 1 and 2 to r0, skipping it adds only 2.
	.align	2
bx_pad:
	add	r0, r0, #1
	add	r0, r0, #2
	bx	lr

@ u32 bx_arm(void): BX to bx_pad + 2 from ARM state.
	.global bx_arm
	.type bx_arm, %function
bx_arm:
	mov	r0, #0
	adr	r1, bx_pad + 2
	bx	r1

@ u32 bx_movs(void): MOVS pc, lr from SVC with lr = bx_pad + 2 and an
@ SPSR of system mode in ARM state.
	.global bx_movs
	.type bx_movs, %function
bx_movs:
	mov	r0, #0
	msr	cpsr_c, #0xD3
	msr	spsr_fc, #0x1F
	adr	lr, bx_pad + 2
	movs	pc, lr

	.thumb
@ u32 bx_thumb_rm(void): BX to bx_pad + 2 from Thumb state.
	.global bx_thumb_rm
	.type bx_thumb_rm, %function
	.thumb_func
bx_thumb_rm:
	movs	r0, #0
	ldr	r1, =bx_pad + 2
	bx	r1
	.align	2
	.pool

@ u32 bx_thumb_pc(void): a Thumb BX PC at a word address + 2, which reads
@ PC = that + 4 + 2 and goes on in ARM state at the next word.
	.global bx_thumb_pc
	.type bx_thumb_pc, %function
	.align	2
	.thumb_func
bx_thumb_pc:
	movs	r0, #0
	bx	pc
	.arm
	add	r0, r0, #1
	add	r0, r0, #2
	bx	lr

@ ---- cpu_cond_run ---------------------------------------------------------

@ u32 cond_forward(void): a branch forward into the middle of 8 ADDEQ with
@ Z clear. Returns the bits of those that ran: none should.
	.global cond_forward
	.type cond_forward, %function
cond_forward:
	mov	r0, #0
	cmp	r0, #1
	b	1f
	addeq	r0, r0, #1
	addeq	r0, r0, #2
	addeq	r0, r0, #4
	addeq	r0, r0, #8
1:	addeq	r0, r0, #16
	addeq	r0, r0, #32
	addeq	r0, r0, #64
	addeq	r0, r0, #128
	bx	lr

@ u32 cond_backward(void): runs the 8 ADDEQ from the top, then branches
@ back into their middle; Z stays clear, C ends the second pass.
	.global cond_backward
	.type cond_backward, %function
cond_backward:
	mov	r0, #0
	msr	cpsr_f, #0x20000000
	addeq	r0, r0, #1
	addeq	r0, r0, #2
	addeq	r0, r0, #4
1:	addeq	r0, r0, #8
	addeq	r0, r0, #16
	addeq	r0, r0, #32
	addeq	r0, r0, #64
	addeq	r0, r0, #128
	bxcc	lr
	msr	cpsr_f, #0
	b	1b

@ ---- Work RAM -------------------------------------------------------------

	.bss
	.align	2
@ calls, SPSR, lr_irq, r8-r12, the bits ORed into the SPSR on return
	.global irq_log
irq_log:
	.space	9 * 4
dma_word:
	.space	4
mode_saved:
	.space	11 * 4
