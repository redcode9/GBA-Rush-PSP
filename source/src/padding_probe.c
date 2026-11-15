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

/* make PROFILE=diagnostic PADDING_PROBE_BYTES=N links this file for the
   padding experiment in docs/PERFORMANCE.md: about N bytes of code and N
   bytes of BSS that nothing calls or reads, so only the addresses of the
   code and data placed after them change. With 0 it adds nothing. */

#if PADDING_PROBE_BYTES > 0

#define PADDING_PROBE_TEXT(bytes) #bytes
#define PADDING_PROBE_STRING(bytes) PADDING_PROBE_TEXT(bytes)

/* One 4-byte MIPS nop per 4 bytes, plus the function's own return. */
static void __attribute__((used, noinline, cold)) padding_probe_code(void)
{
  __asm__ volatile(".rept (" PADDING_PROBE_STRING(PADDING_PROBE_BYTES)
   ") / 4\n\tnop\n\t.endr");
}

static unsigned char padding_probe_data[PADDING_PROBE_BYTES]
 __attribute__((used));

#else

/* C wants at least one declaration in a translation unit. */
typedef int padding_probe_none;

#endif
