/*
 * GBA Rush PSP
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * CPU cases: ARM7TDMI behaviour a recompiler can get wrong. The code under
 * test is hand written in cases_cpu_asm.s; the expected values come from
 * the ARM Architecture Reference Manual, the ARM7TDMI data sheet and
 * GBATEK. Where the manual says UNPREDICTABLE (Rn = PC with a shift by
 * register, ARM state at an address with bit 1 set) the cases expect what
 * the ARM7TDMI does. To confirm in mGBA and on hardware.
 *
 * The IRQ cases get their IRQ from a one-word DMA3 copy that requests one
 * when it ends: it is pending as soon as the copy is done, with no timer
 * to program. cpu_irq_handler, the IRQ vector during those cases,
 * acknowledges it and logs the call (irq_log).
 *
 * cpu_shift_reg: LSL, LSR, ASR and ROR by register, with the results of
 * shift_table.h (gen_shift_table.py), in seven forms: 0 ARM MOVS, 1 MOV
 * (NZCV unchanged), 2 the operand of ADD (Rn = FFFFFFFFh, NZCV unchanged),
 * 3 MOVS with Rm = Rs, 4 MOVS with Rm = PC (against MOVS of the PC + 12 it
 * must read), 5 Thumb with C read after, 6 Thumb with C overwritten
 * unread. Runs nest form, kind, carry in (0, 1), Rm (8 values, 1 for forms
 * 3 and 4) and Rs (32 values): 2048 runs a form, 256 for forms 3 and 4,
 * 10752 in all. Detail: wrong runs in bits 0-15, the index of the first in
 * bits 16-31.
 *
 * cpu_pc12_rn: ADD, SUB, AND and CMP with Rn = PC read PC + 12 with a shift
 * by register and PC + 8 with an immediate shift. Detail: a bit per wrong
 * read (pc12_rn in cases_cpu_asm.s).
 *
 * cpu_msr_c_flags: with N and V in the CPSR and Z and C live from a CMP,
 * MSR CPSR_c must leave Z and C for the next conditional instruction and
 * MRS. Detail: bits 0-11 two per mode (immediate, register) in the order
 * IRQ, SVC, ABT, UND, System, FIQ; bits 12-13 MSR CPSR_x and CPSR_s; bits
 * 16-19 in user mode, where MSR CPSR_c changes nothing: flags lost or CPSR
 * changed by the immediate form, then by the register form.
 *
 * cpu_irq_after_msr: an IRQ already pending, unmasked by MSR CPSR_c in SVC
 * (0), MSR CPSR_c in system mode (1) and MSR CPSR_fc (2). Detail: four
 * bits each from bit 4 * n: handler not run exactly once, the MSR ran
 * again (I clear after the handler set it in the SPSR), the four
 * instructions after the MSR not run once each, SPSR_irq without the live
 * flags and the unmasked mode.
 *
 * cpu_irq_unmask: an IRQ already pending is taken at the end of the
 * instruction that unmasks it, with lr_irq = the next instruction + 4.
 * Detail: bits 0-4 for MSR CPSR_c, MSR CPSR_fc, SUBS pc, lr, LDM {pc}^
 * and a window of one instruction with I clear; bits 8-15 how many words
 * late the first wrong one entered.
 *
 * cpu_irq_latency, the control of cpu_irq_unmask: an IRQ raised with IRQs
 * unmasked enters within the 16 instructions after the store that raises
 * it, after the latency of the IRQ line (a few cycles; GBA Rush counts
 * 9). Detail: the instructions that ran first in bits 0-7, the handler's
 * calls in bits 16-23.
 *
 * cpu_mode_pairs: every ordered pair of IRQ, SVC, ABT, UND and System,
 * switched by MSR CPSR_c immediate and register and, from a mode with an
 * SPSR, by MOVS pc, lr, SUBS pc, lr, #4 and LDM {pc}^ to ARM and to Thumb
 * (mode_switch in cases_cpu_asm.s), with r8-r14 distinct in every bank;
 * 136 runs. cpu_fiq_bank: the same for the 74 runs to or from FIQ, plus an
 * IRQ taken in FIQ mode (the handler must see the shared r8-r12, FIQ mode
 * get its own back), FIQ -> SVC -> FIQ with r8-r12 written in SVC, and SWI
 * 06h from FIQ mode. Detail of both: wrong runs in bits 0-7, the first
 * one's index in bits 8-15 and what went wrong in it in bits 16-25 (r13 or
 * r14 in the new mode, its CPSR, the wrong landing code ran, r8-r12 in the
 * new mode, then a bit per mode whose bank changed, IRQ to FIQ order);
 * cpu_fiq_bank adds bits 26-30: the handler's r8-r12, FIQ registers after
 * the IRQ, FIQ r8-r12 after SVC, shared r8-r12 written in SVC, SWI.
 *
 * cpu_user_bank: STM and LDM with ^ and no PC, and STM ^ with PC, from IRQ,
 * SVC and FIQ mode with base r13, and from FIQ mode with base r8: the
 * address comes from the current mode's register, the registers moved are
 * the user ones. Detail: a bit per wrong run, in that order.
 *
 * cpu_bx_align: BX to a word address + 2 from ARM and from Thumb, MOVS pc,
 * lr with lr = word + 2, and a Thumb BX PC at a word + 2 must go on in ARM
 * state at the word (for BX PC the word after the BX). Detail: 2 bits
 * each in that order, 3 when the word ran, 2 when it was skipped.
 *
 * cpu_cond_run: branches forward and back into the middle of 8 ARM
 * instructions whose condition is false: none may run. Detail: the bits of
 * those that ran, forward in bits 0-7, back in bits 8-15.
 */

#include "result.h"
#include "shift_table.h"

#define MODE_IRQ 0
#define MODE_SVC 1
#define MODE_SYS 4
#define MODE_FIQ 5
#define MODES 6
/* The bank row with spare values. */
#define SPARE 6

#define FLAGS 0xF0000000u
#define FLAG_C 0x20000000u
#define FLAG_V 0x10000000u

extern const u8 mode_bits[MODES];
extern volatile u32 irq_log[9];
void cpu_irq_handler(void);

void shift_run(u32 variant, u32 *io);
u32 pc12_rn(void);
u32 msr_flags(u32 mode);
u32 msr_flags_xs(void);
u32 msr_flags_user(void);
void irq_after_msr(u32 which, u32 *out);
u32 irq_unmask(u32 which);
void mode_switch(const u32 *bank, u32 *out, u32 from, u32 to_method);
void fiq_irq(const u32 *bank, u32 *out);
void fiq_svc_fiq(const u32 *bank, u32 *out);
void swi_fiq(const u32 *bank, u32 *out);
void user_bank(u32 mode, u32 form, const u32 *setup, u32 *out);
u32 bx_arm(void);
u32 bx_movs(void);
u32 bx_thumb_rm(void);
u32 bx_thumb_pc(void);
u32 cond_forward(void);
u32 cond_backward(void);

static u32 out[64];

/* ---- cpu_shift_reg ------------------------------------------------------ */

enum
{
  FORM_MOVS, FORM_MOV, FORM_ADD, FORM_SELF, FORM_PC, FORM_LIVE, FORM_DEAD,
  FORMS
};

/* NZCV before each run with the carry clear and set; every flag differs. */
static const u32 flags_in[2] = { 0x50000000u, 0xA0000000u };

static int cpu_shift_reg(u32 *detail)
{
  u32 index = 0;
  u32 wrong = 0;
  u32 first = 0;
  u32 form, kind, c, m, s;

  for(form = 0; form < FORMS; form++)
    for(kind = 0; kind < 4; kind++)
      for(c = 0; c < 2; c++)
        for(m = 0; m < (form == FORM_SELF || form == FORM_PC ? 1u : 8u); m++)
          for(s = 0; s < 32; s++, index++)
          {
            int self = form == FORM_SELF;
            u32 result = self ? shift_self_result[kind][s] :
             shift_result[kind][m][s];
            u32 carry = ((self ? shift_self_carry[kind][s] :
             shift_carry[kind][m][s]) >> c) & 1;
            u32 flags = (result & 0x80000000u) |
             (result == 0 ? 0x40000000u : 0) | carry << 29 |
             (flags_in[c] & FLAG_V);
            u32 mask = FLAGS;
            u32 io[4];

            io[0] = shift_rm[m];
            io[1] = shift_rs[s];
            io[2] = flags_in[c];
            shift_run(form * 4 + kind, io);
            if(form == FORM_MOV || form == FORM_ADD)
              flags = flags_in[c];
            if(form == FORM_ADD)
              result -= 1;
            if(form == FORM_PC)
            {
              u32 ref[4];

              ref[0] = io[3];
              ref[1] = shift_rs[s];
              ref[2] = flags_in[c];
              shift_run(FORM_MOVS * 4 + kind, ref);
              result = ref[0];
              flags = ref[2];
            }
            if(form == FORM_LIVE)
              mask = FLAG_C;
            if(form == FORM_DEAD)
              mask = 0;
            if(io[0] != result || ((io[2] ^ flags) & mask))
            {
              if(wrong == 0)
                first = index;
              wrong++;
            }
          }
  *detail = (wrong > 0xFFFF ? 0xFFFF : wrong) | first << 16;
  return wrong ? RESULT_FAIL : RESULT_PASS;
}

/* ---- cpu_pc12_rn, cpu_msr_c_flags ---------------------------------------- */

static int cpu_pc12_rn(u32 *detail)
{
  *detail = pc12_rn();
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

static int cpu_msr_c_flags(u32 *detail)
{
  u32 m;

  for(m = 0; m < MODES; m++)
    *detail |= msr_flags(m) << (m * 2);
  *detail |= msr_flags_xs() << 12;
  IRQ_VECTOR = cpu_irq_handler;
  *detail |= msr_flags_user() << 16;
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

/* ---- cpu_irq_after_msr, cpu_irq_unmask ---------------------------------- */

static int cpu_irq_after_msr(u32 *detail)
{
  /* NZCV and low byte of the CPSR the IRQ interrupts. */
  static const u32 spsr[3] = { 0x60000013u, 0x6000001Fu, 0xA000001Fu };
  u32 i;

  IRQ_VECTOR = cpu_irq_handler;
  for(i = 0; i < 3; i++)
  {
    u32 after[2];
    u32 wrong = 0;

    irq_after_msr(i, after);
    if(irq_log[0] != 1)
      wrong |= 1;
    if(!(after[1] & 0x80))
      wrong |= 2;
    if(after[0] != 4)
      wrong |= 4;
    if((irq_log[1] & 0xF00000FFu) != spsr[i])
      wrong |= 8;
    *detail |= wrong << (i * 4);
  }
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

static int cpu_irq_unmask(u32 *detail)
{
  u32 i;

  IRQ_VECTOR = cpu_irq_handler;
  for(i = 0; i < 5; i++)
  {
    u32 expected = irq_unmask(i);

    if(irq_log[0] != 1 || irq_log[2] != expected)
    {
      if(!(*detail & 0x1F))
        *detail |= ((irq_log[2] - expected) >> 2 & 0xFF) << 8;
      *detail |= 1u << i;
    }
  }
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

static int cpu_irq_latency(u32 *detail)
{
  u32 first, ran;

  IRQ_VECTOR = cpu_irq_handler;
  first = irq_unmask(5);
  ran = (irq_log[2] - 4 - first) >> 2;
  *detail = (ran > 0xFF ? 0xFF : ran) | (irq_log[0] & 0xFF) << 16;
  return irq_log[0] == 1 && ran < 16 ? RESULT_PASS : RESULT_FAIL;
}

/* ---- cpu_mode_pairs, cpu_fiq_bank ---------------------------------------- */

/* r8-r14 of every mode, rows in the order of mode_bits, then the spare
   row: distinct everywhere except the shared r8-r12 of the non-FIQ modes. */
static u32 bank[7][8];

static void bank_fill(void)
{
  u32 m, r;

  for(m = 0; m < 7; m++)
    for(r = 0; r < 7; r++)
      bank[m][r] = r < 5 && m != MODE_FIQ && m != SPARE ?
       0x5A5A0008u + r : 0xB0000008u + (m << 16) + r;
}

/* One switch; returns the detail bits of what went wrong. */
static u32 switch_run(u32 from, u32 to, u32 method)
{
  int lr_used = method >= 2 && method <= 5;
  u32 landing = method >= 2 && (method & 1) ? 7 : 0x0A;
  u32 wrong = 0;
  u32 m, r;

  out[9] = 0;
  mode_switch(&bank[0][0], out, from, to | method << 8);
  if(out[0] != bank[to][5] || out[1] != bank[to][6])
    wrong |= 1;
  if((out[2] & 0xFF) != (0xC0u | mode_bits[to]))
    wrong |= 2;
  if(out[9] != landing)
    wrong |= 4;
  for(r = 0; r < 5; r++)
    if(out[3 + r] != bank[to][r])
      wrong |= 8;
  for(m = 0; m < MODES; m++)
    for(r = 0; r < 7; r++)
    {
      /* The return through lr leaves its target in lr of the first mode. */
      u32 expected = lr_used && m == from && r == 6 ? out[8] : bank[m][r];

      if(out[16 + m * 8 + r] != expected)
        wrong |= 16u << m;
    }
  return wrong;
}

/* Every ordered pair of different modes that includes FIQ (with_fiq) or
   not, with every method the first mode allows. */
static u32 switch_pairs(int with_fiq)
{
  u32 failed = 0;
  u32 first = 0;
  u32 index = 0;
  u32 from, to, method;

  for(from = 0; from < MODES; from++)
    for(to = 0; to < MODES; to++)
    {
      if(from == to || (from == MODE_FIQ || to == MODE_FIQ) != with_fiq)
        continue;
      for(method = 0; method < (from == MODE_SYS ? 2u : 8u);
       method++, index++)
      {
        u32 wrong = switch_run(from, to, method);

        if(wrong)
        {
          if(failed == 0)
            first = index | wrong << 8;
          failed++;
        }
      }
    }
  return (failed > 0xFF ? 0xFF : failed) | first << 8;
}

static int cpu_mode_pairs(u32 *detail)
{
  bank_fill();
  *detail = switch_pairs(0);
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

static int cpu_fiq_bank(u32 *detail)
{
  u32 extra = 0;
  u32 r;

  bank_fill();
  *detail = switch_pairs(1);
  IRQ_VECTOR = cpu_irq_handler;

  fiq_irq(&bank[0][0], out);
  for(r = 0; r < 7; r++)
  {
    if(r < 5 && (irq_log[0] != 1 || irq_log[3 + r] != bank[MODE_SYS][r]))
      extra |= 1;
    if(out[r] != bank[MODE_FIQ][r] ||
     (r < 5 && out[8 + r] != bank[MODE_SYS][r]))
      extra |= 2;
  }

  fiq_svc_fiq(&bank[0][0], out);
  for(r = 0; r < 5; r++)
  {
    if(out[r] != bank[MODE_FIQ][r])
      extra |= 4;
    if(out[8 + r] != bank[SPARE][r])
      extra |= 8;
  }

  swi_fiq(&bank[0][0], out);
  if(out[7] != 3 || (out[13] & 0xFF) != 0xD1)
    extra |= 16;
  for(r = 0; r < 7; r++)
    if(out[r] != bank[MODE_FIQ][r] ||
     (r < 5 && out[8 + r] != bank[MODE_SYS][r]))
      extra |= 16;

  *detail |= extra << 26;
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

/* ---- cpu_user_bank ------------------------------------------------------ */

static int cpu_user_bank(u32 *detail)
{
  static const u8 modes[3] = { MODE_IRQ, MODE_SVC, MODE_FIQ };
  /* setup[0..6] user r8-r14, setup[8..14] r8-r14 of the mode. */
  static u32 setup[16];
  /* What the base points at, and what the user r13 points at. */
  u32 *base_area = &out[16];
  u32 *user_area = &out[24];
  u32 run = 0;
  u32 i, form;

  for(i = 0; i < 3; i++)
  {
    u32 fiq = modes[i] == MODE_FIQ;

    for(form = 0; form < (fiq ? 6u : 3u); form++, run++)
    {
      int ldm = form % 3 == 1;
      u32 wrong = 0;
      u32 r;

      for(r = 0; r < 8; r++)
      {
        setup[r] = 0x05E00008u + r;
        setup[8 + r] = fiq || r >= 5 ? 0x0D000008u + r : setup[r];
        base_area[r] = ldm ? 0x1D000008u + r : 0;
        user_area[r] = 0xBAD00008u + r;
      }
      setup[5] = (u32)user_area;
      setup[form >= 3 ? 8 : 13] = (u32)base_area;
      user_bank(mode_bits[modes[i]], form, setup, out);
      for(r = 0; r < 7; r++)
      {
        u32 user = ldm ? 0x1D000008u + r : setup[r];
        u32 mode = !fiq && r < 5 ? user : setup[8 + r];

        if(out[8 + r] != user || out[r] != mode ||
         base_area[r] != (ldm ? 0x1D000008u + r : setup[r]) ||
         user_area[r] != 0xBAD00008u + r)
          wrong = 1;
      }
      *detail |= wrong << run;
    }
  }
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

/* ---- cpu_bx_align, cpu_cond_run ----------------------------------------- */

static int cpu_bx_align(u32 *detail)
{
  *detail = bx_arm() | bx_thumb_rm() << 2 | bx_movs() << 4 |
   bx_thumb_pc() << 6;
  return *detail == 0xFF ? RESULT_PASS : RESULT_FAIL;
}

static int cpu_cond_run(u32 *detail)
{
  *detail = cond_forward() | cond_backward() << 8;
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

const TestCase cases_cpu[] =
{
  { "cpu_shift_reg", cpu_shift_reg },
  { "cpu_pc12_rn", cpu_pc12_rn },
  { "cpu_msr_c_flags", cpu_msr_c_flags },
  { "cpu_irq_after_msr", cpu_irq_after_msr },
  { "cpu_irq_unmask", cpu_irq_unmask },
  { "cpu_irq_latency", cpu_irq_latency },
  { "cpu_mode_pairs", cpu_mode_pairs },
  { "cpu_fiq_bank", cpu_fiq_bank },
  { "cpu_user_bank", cpu_user_bank },
  { "cpu_bx_align", cpu_bx_align },
  { "cpu_cond_run", cpu_cond_run },
  { NULL, NULL }
};
