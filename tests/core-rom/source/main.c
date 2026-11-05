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
 * The core test ROM. Each area of behaviour is a table of cases in its
 * own cases_<area>.c; this list sets the order they run in, and so the
 * case numbers in the signature. Expected values come from GBATEK and the
 * ARM7TDMI documentation; only where those leave a timing open does a
 * case take mGBA's measurement, and its comment says so (cases_timer.c).
 */

#include "result.h"

extern const TestCase cases_irq[];
extern const TestCase cases_stack[];
extern const TestCase cases_cpu[];
extern const TestCase cases_timer[];
extern const TestCase cases_branch[];
extern const TestCase cases_prefetch[];
extern const TestCase cases_vram[];
extern const TestCase cases_stm[];
extern const TestCase cases_bios[];
extern const TestCase cases_pc[];

static const TestCase *const areas[] =
{
  /* First: its BIOS open bus case needs a BIOS that has run nothing since
     boot. */
  cases_irq,
  cases_stack,
  cases_cpu,
  cases_timer,
  cases_branch,
  cases_prefetch,
  cases_vram,
  cases_stm,
  cases_bios,
  /* Last: on a core that stops at an unmappable jump the emulator ends
     here, and the next boot marks the case hung and finishes. */
  cases_pc,
  NULL
};

int main(void)
{
  result_run(areas);
  for(;;)
    wait_vblank();
}
