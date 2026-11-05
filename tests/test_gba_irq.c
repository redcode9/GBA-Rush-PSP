/*
 * Host tests for gba_irq.h: what ends HALT and STOP, and when an interrupt
 * request enters the IRQ exception.
 */

#include <assert.h>
#include <stdio.h>

#include "gba_irq.h"

int main(void)
{
  /* HALT ends on any enabled request, even with IME 0 or CPSR.I set:
     those only decide whether the IRQ exception follows. */
  assert(gba_irq_halt_wakes(0, 0x0001, 0x0001));
  assert(!gba_irq_enters(0x0001, 0x0001, 0, 0x1F));
  assert(!gba_irq_enters(0x0001, 0x0001, 1, 0x9F));
  assert(gba_irq_enters(0x0001, 0x0001, 1, 0x1F));

  /* A request that is not enabled does nothing. */
  assert(!gba_irq_halt_wakes(0, 0x0001, 0x0002));
  assert(!gba_irq_enters(0x0001, 0x0002, 1, 0x1F));
  assert(!gba_irq_halt_wakes(0, 0xC000, 0xC000));
  assert(!gba_irq_enters(0xC000, 0xC000, 1, 0x1F));

  /* STOP ends only on serial (bit 7), keypad (12) and Game Pak (13). */
  assert(!gba_irq_halt_wakes(1, 0x0F7F, 0x0F7F));
  assert(gba_irq_halt_wakes(1, 0x0080, 0x0080));
  assert(gba_irq_halt_wakes(1, 0x1000, 0x1000));
  assert(gba_irq_halt_wakes(1, 0x2000, 0x3FFF));

  /* Without the IRQ exception the CPU goes on after HALT on both cores
     and after STOP on the New core; the Classic core stays in STOP. */
  assert(gba_irq_resumes(0, 0));
  assert(gba_irq_resumes(0, 1));
  assert(gba_irq_resumes(1, 0));
  assert(!gba_irq_resumes(1, 1));

  /* Every IE bit 0-13 ends HALT. */
  for(unsigned bit = 0; bit < 14; bit++)
    assert(gba_irq_halt_wakes(0, 1u << bit, 1u << bit));

  puts("gba_irq tests passed");
  return 0;
}
