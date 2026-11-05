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

#ifndef GBA_IRQ_H
#define GBA_IRQ_H

/* When interrupt requests end HALT or STOP and when they enter the IRQ
 * exception, after GBATEK, for update_gba() in main.c. Host tested in
 * tests/test_gba_irq.c.
 */

#include <stdint.h>

/* An enabled request (IE & IF) ends HALT whatever IME and the CPSR's I
   bit say; STOP ends only for serial, keypad and Game Pak requests. */
static inline int gba_irq_halt_wakes(int stopped, uint32_t ie, uint32_t if_)
{
  return (ie & if_ & (stopped ? 0x3080u : 0x3FFFu)) != 0;
}

/* An enabled request enters the IRQ exception only with IME set (the
   register value, nonzero as everywhere in the emulator) and the CPSR's
   I bit clear. */
static inline int gba_irq_enters(uint32_t ie, uint32_t if_, uint32_t ime,
 uint32_t cpsr)
{
  return ime != 0 && !(cpsr & 0x80u) && (ie & if_ & 0x3FFFu) != 0;
}

/* Whether a request that ends HALT or STOP without entering the IRQ
   exception lets the CPU go on. The Classic core keeps STOP as it always
   has (GBATEK does not say what IME 0 does there) until a test decides;
   the New core goes on from both. */
static inline int gba_irq_resumes(int classic_core, int stopped)
{
  return !(classic_core && stopped);
}

#endif /* GBA_IRQ_H */
