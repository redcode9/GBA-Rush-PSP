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

/* The official BIOS's Div results; see bios_hle.h. */

#include "bios_hle.h"

GbaBiosDiv gba_bios_div(int32_t numerator, int32_t denominator)
{
  GbaBiosDiv result;
  int32_t quotient;

  if(denominator == 0)
  {
    result.r0 = numerator < 0 ? 0xFFFFFFFFu : 1u;
    result.r1 = (uint32_t)numerator;
    result.r3 = 1;
    return result;
  }
  /* The one quotient C cannot represent: it wraps to -2^31. */
  if(denominator == -1 && numerator == INT32_MIN)
  {
    result.r0 = result.r3 = 0x80000000u;
    result.r1 = 0;
    return result;
  }
  quotient = numerator / denominator;
  result.r0 = (uint32_t)quotient;
  result.r1 = (uint32_t)(numerator % denominator);
  result.r3 = quotient < 0 ? 0u - (uint32_t)quotient : (uint32_t)quotient;
  return result;
}
