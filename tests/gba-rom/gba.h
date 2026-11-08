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
 * GBA registers and helpers shared by the test ROMs (tests/gba-rom.mk).
 * Register names and values are GBATEK's.
 */

#ifndef GBA_H
#define GBA_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define REG_DISPCNT   (*(vu16 *)0x04000000)
#define REG_DISPSTAT  (*(vu16 *)0x04000004)
#define REG_VCOUNT    (*(vu16 *)0x04000006)
#define REG_DMA3SAD   (*(vu32 *)0x040000D4)
#define REG_DMA3DAD   (*(vu32 *)0x040000D8)
#define REG_DMA3CNT   (*(vu32 *)0x040000DC)
#define REG_KEYINPUT  (*(vu16 *)0x04000130)
#define REG_IE        (*(vu16 *)0x04000200)
#define REG_IF        (*(vu16 *)0x04000202)
#define REG_WAITCNT   (*(vu16 *)0x04000204)
#define REG_IME       (*(vu16 *)0x04000208)

/* The BIOS's IRQ handler calls the ARM function stored here; IntrWait
   reads the IF bits a handler acknowledged from the halfword below it. */
#define IRQ_VECTOR    (*(void (*volatile *)(void))0x03007FFC)
#define BIOS_IF       (*(vu16 *)0x03007FF8)

#define VRAM          ((vu16 *)0x06000000)
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 160

#define DISPCNT_MODE3      0x0003
#define DISPCNT_BG2        0x0400
#define DISPSTAT_VBLANK_IRQ 0x0008

#define IRQ_VBLANK 0x0001
#define IRQ_HBLANK 0x0002
#define IRQ_VCOUNT 0x0004
#define IRQ_TIMER0 0x0008
#define IRQ_DMA3   0x0800

#define DMA_ENABLE 0x80000000u
#define DMA_32BIT  0x04000000u

/* KEYINPUT bits read 0 while the key is held. */
#define KEY_A      0x0001
#define KEY_B      0x0002
#define KEY_SELECT 0x0004
#define KEY_START  0x0008
#define KEY_R      0x0100
#define KEY_L      0x0200

/* Code or data copied to work RAM by crt0.s. Calls into it from the ROM
   are long calls: the regions are too far apart for a BL. */
#define IWRAM_CODE __attribute__((section(".iwram"), long_call, noinline))
#define EWRAM_CODE __attribute__((section(".ewram"), long_call, noinline))
#define IWRAM_DATA __attribute__((section(".iwram")))
#define EWRAM_DATA __attribute__((section(".ewram")))
/* Work RAM that crt0.s neither loads nor clears. */
#define EWRAM_BSS  __attribute__((section(".ewram_bss")))

/* crt0.s points IRQ_VECTOR at irq_default, an ARM handler that
   acknowledges IE & IF, ORs it into BIOS_IF and irq_seen and counts the
   call in irq_count. A case that installs its own handler puts this one
   back. */
extern void irq_default(void);
extern volatile u32 irq_count;
extern volatile u32 irq_seen;

/* The I bit of the CPSR (ARM code in crt0.s, callable from Thumb). */
void cpsr_irq_off(void);
void cpsr_irq_on(void);

/* BIOS calls that change no registers the compiler relies on. */
static inline void swi_halt(void)
{
  __asm__ volatile("swi 0x02" ::: "r0", "r1", "r2", "r3", "r12", "memory");
}

static inline u32 swi_div(u32 numerator, u32 denominator)
{
  register u32 r0 __asm__("r0") = numerator;
  register u32 r1 __asm__("r1") = denominator;

  __asm__ volatile("swi 0x06" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12",
   "memory");
  return r0;
}

/* Returns at the start of the next VBlank, polling VCOUNT (no BIOS call,
   no interrupt). */
static inline void wait_vblank(void)
{
  while(REG_VCOUNT >= SCREEN_HEIGHT)
    ;
  while(REG_VCOUNT < SCREEN_HEIGHT)
    ;
}

#endif
