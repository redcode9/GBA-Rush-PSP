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
 * Interrupt and BIOS cases.
 *
 * A protected BIOS read (code outside the BIOS reading 0000h-3FFFh)
 * returns the last opcode the BIOS fetched. GBATEK gives the official
 * BIOS's values: E129F000h after boot ([00DCh+8]), E55EC002h after an IRQ
 * ([013Ch+8]), E3A02004h after a SWI ([0188h+8]). The built-in BIOS of
 * GBA Rush returns from different addresses, so with it only the boot
 * value applies. Each case reads right after its event, with no other
 * BIOS code in between; the detail is the value read, 0 if the VBlank IRQ
 * they wait for never came. bios_openbus_swi runs the same SWI and read
 * twice with an IRQ in between, so a core that sets the value when it
 * translates the SWI, not each time it runs, reads the IRQ's value.
 *
 * HALT (SWI 02h) ends when IE & IF becomes non-zero, whatever IME and the
 * CPSR's I bit say (GBATEK, BIOS Halt Functions); the IRQ itself is taken
 * only if both allow it. The detail is IF after waking, plus the IRQs
 * taken while halted in bits 16-23 and, for halt_cpsr_i, those taken once
 * the I bit clears in bits 24-31. A core that keeps a halted CPU waiting
 * for an IRQ it may not take never wakes: the case hangs, and the next
 * boot records it as hung.
 *
 * mGBA 0.10.3 passes every case but bios_openbus_boot: it starts the
 * ROM without running the BIOS and leaves the latch at 0 (E59E1000h on
 * its own BIOS). To confirm on hardware.
 */

#include "result.h"

#define BIOS_WORD (*(vu32 *)0x00000000)
/* Spins of a polling loop before giving up, a few frames. */
#define WAIT_LIMIT 100000u

static int bios_openbus_boot(u32 *detail)
{
  *detail = BIOS_WORD;
  return *detail == 0xE129F000u ? RESULT_PASS : RESULT_FAIL;
}

/* Lets one VBlank IRQ through the BIOS; returns 0 if none came. Nothing
   but ROM code runs after the IRQ returns. */
static int wait_irq(void)
{
  u32 count = irq_count;
  u32 n;

  REG_DISPSTAT = DISPSTAT_VBLANK_IRQ;
  REG_IE = IRQ_VBLANK;
  REG_IME = 1;
  for(n = 0; n < WAIT_LIMIT && irq_count == count; n++)
    ;
  REG_IME = 0;
  return irq_count != count;
}

static int bios_openbus_irq(u32 *detail)
{
  if(!wait_irq())
    return RESULT_FAIL;
  *detail = BIOS_WORD;
  return *detail == 0xE55EC002u ? RESULT_PASS : RESULT_FAIL;
}

__attribute__((noinline)) static u32 read_after_swi(void)
{
  swi_div(7, 2);
  return BIOS_WORD;
}

static int bios_openbus_swi(u32 *detail)
{
  read_after_swi();
  if(!wait_irq())
    return RESULT_FAIL;
  *detail = read_after_swi();
  return *detail == 0xE3A02004u ? RESULT_PASS : RESULT_FAIL;
}

static int halt_ime0(u32 *detail)
{
  u32 count = irq_count;
  u32 woke_if;

  REG_DISPSTAT = DISPSTAT_VBLANK_IRQ;
  REG_IE = IRQ_VBLANK;
  REG_IF = IRQ_VBLANK;
  swi_halt();
  woke_if = REG_IF;
  *detail = woke_if | (irq_count - count) << 16;
  return (woke_if & IRQ_VBLANK) && irq_count == count ?
   RESULT_PASS : RESULT_FAIL;
}

static int halt_cpsr_i(u32 *detail)
{
  u32 count = irq_count;
  u32 woke_if;
  u32 halted;
  u32 n;

  REG_DISPSTAT = DISPSTAT_VBLANK_IRQ;
  REG_IE = IRQ_VBLANK;
  cpsr_irq_off();
  REG_IME = 1;
  REG_IF = IRQ_VBLANK;
  swi_halt();
  woke_if = REG_IF;
  halted = irq_count - count;
  cpsr_irq_on();
  for(n = 0; n < WAIT_LIMIT && irq_count == count + halted; n++)
    ;
  REG_IME = 0;
  *detail = woke_if | halted << 16 | (irq_count - count - halted) << 24;
  return (woke_if & IRQ_VBLANK) && halted == 0 && irq_count == count + 1 ?
   RESULT_PASS : RESULT_FAIL;
}

const TestCase cases_irq[] =
{
  { "bios_openbus_boot", bios_openbus_boot },
  { "bios_openbus_irq", bios_openbus_irq },
  { "bios_openbus_swi", bios_openbus_swi },
  { "halt_ime0", halt_ime0 },
  { "halt_cpsr_i", halt_cpsr_i },
  { NULL, NULL }
};
