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
 * What the official GBA BIOS returns from its Div and DivArm functions
 * (SWI 0x06 and 0x07), as plain C: the reference that the built-in BIOS
 * (source/bios/source/divide.s) is tested against, and the code a CPU core
 * can run instead of the BIOS routine. No PSP headers, so
 * tests/test_bios_hle.c checks it on the host.
 */

#ifndef BIOS_HLE_H
#define BIOS_HLE_H

#include <stdint.h>

/* The registers Div changes; r2, r12 and the flags keep their values. */
typedef struct
{
  uint32_t r0;  /* quotient, rounded towards zero */
  uint32_t r1;  /* remainder, with the sign of the numerator */
  uint32_t r3;  /* absolute value of the quotient */
} GbaBiosDiv;

/* Div with r0 = numerator and r1 = denominator; DivArm is the same with
   the two swapped. The official BIOS also returns for these cases:
     - 0, 1 or -1 divided by 0: r0 = 1 or -1 (the sign of the numerator),
       r1 = the numerator, r3 = 1;
     - -2^31 divided by -1: r0 = r3 = 0x80000000, r1 = 0.
   Any other numerator divided by 0 never returns from the official BIOS;
   here it follows the first rule, so a game stuck there keeps running. */
GbaBiosDiv gba_bios_div(int32_t numerator, int32_t denominator);

#endif /* BIOS_HLE_H */
