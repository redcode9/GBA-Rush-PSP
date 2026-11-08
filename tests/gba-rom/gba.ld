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
 * Memory layout of the test ROMs. Code and constants run from the ROM;
 * .iwram and .data are copied to the on-chip work RAM, .ewram to the
 * on-board one, and .bss is cleared, all by crt0.s. The top 1280 bytes
 * of the on-chip RAM hold the stacks (user from 03007F00h down, IRQ from
 * 03007FA0h, supervisor from 03007FE0h) and the BIOS's words.
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
  rom (rx)    : ORIGIN = 0x08000000, LENGTH = 32M
  ewram (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
  iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 0x7B00
}

SECTIONS
{
  .text :
  {
    KEEP(*(.crt0))
    *(.text .text.* .glue_7 .glue_7t .v4_bx .vfp11_veneer)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > rom

  .iwram :
  {
    *(.iwram .iwram.*)
    *(.data .data.*)
    . = ALIGN(4);
  } > iwram AT > rom
  __iwram_start = ADDR(.iwram);
  __iwram_end = ADDR(.iwram) + SIZEOF(.iwram);
  __iwram_load = LOADADDR(.iwram);

  .bss (NOLOAD) :
  {
    *(.bss .bss.* COMMON)
    . = ALIGN(4);
  } > iwram
  __bss_start = ADDR(.bss);
  __bss_end = ADDR(.bss) + SIZEOF(.bss);

  .ewram :
  {
    *(.ewram .ewram.*)
    . = ALIGN(4);
  } > ewram AT > rom
  __ewram_start = ADDR(.ewram);
  __ewram_end = ADDR(.ewram) + SIZEOF(.ewram);
  __ewram_load = LOADADDR(.ewram);

  .ewram_bss (NOLOAD) :
  {
    *(.ewram_bss .ewram_bss.*)
  } > ewram

  /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note*) }
}
