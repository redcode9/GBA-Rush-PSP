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
 * The part of libgba's interface that mGBA's test suite uses, written
 * from GBATEK, so the suite builds with arm-none-eabi-gcc alone (see the
 * Makefile). Every gba_*.h header the suite includes is this file.
 * Register names and bit values follow libgba's; nothing here is taken
 * from libgba itself.
 */

#ifndef LIBGBA_SHIM_H
#define LIBGBA_SHIM_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;

#define BIT(n) (1 << (n))

/* tests/gba-rom/gba.ld copies .iwram* and .ewram* to work RAM. */
#define IWRAM_CODE __attribute__((section(".iwram.text"), long_call))
#define EWRAM_CODE __attribute__((section(".ewram.text"), long_call))
#define IWRAM_DATA __attribute__((section(".iwram.data")))
#define EWRAM_DATA __attribute__((section(".ewram.data")))

#define REG_BASE 0x04000000
#define EWRAM 0x02000000
#define IWRAM 0x03000000
#define VRAM 0x06000000
#define SRAM 0x0E000000

#define REG16(offset) (*(vu16 *)(REG_BASE + (offset)))
#define REG32(offset) (*(vu32 *)(REG_BASE + (offset)))

/* Video */
#define REG_DISPCNT REG16(0x000)
#define REG_DISPSTAT REG16(0x004)
#define REG_VCOUNT REG16(0x006)
#define REG_BG0CNT REG16(0x008)
#define REG_BG1CNT REG16(0x00A)
#define REG_BG2CNT REG16(0x00C)
#define REG_BG3CNT REG16(0x00E)
#define REG_BG0HOFS REG16(0x010)
#define REG_BG0VOFS REG16(0x012)
#define REG_BG1HOFS REG16(0x014)
#define REG_BG1VOFS REG16(0x016)
#define BG_PALETTE ((u16 *)0x05000000)
#define BG_COLORS BG_PALETTE
#define OBJ_COLORS ((u16 *)0x05000200)
#define SPRITE_PALETTE OBJ_COLORS
#define MODE_0 0
#define MODE_1 1
#define MODE_2 2
#define MODE_3 3
#define MODE_4 4
#define MODE_5 5
#define OBJ_1D_MAP BIT(6)
#define LCDC_OFF BIT(7)
#define BG0_ON BIT(8)
#define BG1_ON BIT(9)
#define BG2_ON BIT(10)
#define BG3_ON BIT(11)
#define OBJ_ON BIT(12)
#define WIN0_ON BIT(13)
#define WIN1_ON BIT(14)
#define LCDC_VBL BIT(3)
#define LCDC_HBL BIT(4)
#define LCDC_VCNT BIT(5)
#define VCOUNT(line) ((line) << 8)
#define CHAR_BASE(n) ((n) << 2)
#define SCREEN_BASE(n) ((n) << 8)
#define TILE_BASE_ADR(n) (VRAM + (n) * 0x4000)
#define SCREEN_BASE_BLOCK(n) (VRAM + (n) * 0x800)

/* Sound and serial */
#define REG_SOUNDCNT_X REG16(0x084)
#define REG_SIOCNT REG16(0x128)
#define REG_RCNT REG16(0x134)
#define R_GPIO 0x8000

/* DMA */
#define REG_DMA0SAD REG32(0x0B0)
#define REG_DMA0DAD REG32(0x0B4)
#define REG_DMA0CNT REG32(0x0B8)
#define REG_DMA1SAD REG32(0x0BC)
#define REG_DMA1DAD REG32(0x0C0)
#define REG_DMA1CNT REG32(0x0C4)
#define REG_DMA2SAD REG32(0x0C8)
#define REG_DMA2DAD REG32(0x0CC)
#define REG_DMA2CNT REG32(0x0D0)
#define REG_DMA3SAD REG32(0x0D4)
#define REG_DMA3DAD REG32(0x0D8)
#define REG_DMA3CNT REG32(0x0DC)
#define DMA_ENABLE (1u << 31)
#define DMA_IRQ (1 << 30)
#define DMA_IMMEDIATE 0
#define DMA_VBLANK (1 << 28)
#define DMA_HBLANK (2 << 28)
#define DMA_SPECIAL (3 << 28)
#define DMA16 0
#define DMA32 (1 << 26)
#define DMA_REPEAT (1 << 25)
#define DMA_SRC_INC 0
#define DMA_SRC_DEC (1 << 23)
#define DMA_SRC_FIXED (2 << 23)
#define DMA_DST_INC 0
#define DMA_DST_DEC (1 << 21)
#define DMA_DST_FIXED (2 << 21)
#define DMA_DST_RELOAD (3 << 21)
#define DMA_COPY(ch, src, dst, mode) do { \
    REG_DMA##ch##SAD = (u32)(src); \
    REG_DMA##ch##DAD = (u32)(dst); \
    REG_DMA##ch##CNT = DMA_ENABLE | (mode); \
  } while(0)
#define DMA0COPY(src, dst, mode) DMA_COPY(0, src, dst, mode)
#define DMA1COPY(src, dst, mode) DMA_COPY(1, src, dst, mode)
#define DMA2COPY(src, dst, mode) DMA_COPY(2, src, dst, mode)
#define DMA3COPY(src, dst, mode) DMA_COPY(3, src, dst, mode)

/* Timers */
#define REG_TM0CNT REG32(0x100)
#define REG_TM0CNT_L REG16(0x100)
#define REG_TM0CNT_H REG16(0x102)
#define REG_TM1CNT REG32(0x104)
#define REG_TM1CNT_L REG16(0x104)
#define REG_TM1CNT_H REG16(0x106)
#define TIMER_COUNT BIT(2)
#define TIMER_IRQ BIT(6)
#define TIMER_START BIT(7)

/* Keys: see keysDownRepeat() in libgba.c */
#define KEY_A BIT(0)
#define KEY_B BIT(1)
#define KEY_SELECT BIT(2)
#define KEY_START BIT(3)
#define KEY_RIGHT BIT(4)
#define KEY_LEFT BIT(5)
#define KEY_UP BIT(6)
#define KEY_DOWN BIT(7)
#define KEY_R BIT(8)
#define KEY_L BIT(9)
void scanKeys(void);
u16 keysDownRepeat(void);
void setRepeat(int delay, int repeat);

/* Interrupts: a table of handlers that libgba_asm.s dispatches from */
#define REG_IE REG16(0x200)
#define REG_IF REG16(0x202)
#define REG_IME REG16(0x208)
enum
{
  IRQ_VBLANK = BIT(0), IRQ_HBLANK = BIT(1), IRQ_VCOUNT = BIT(2),
  IRQ_TIMER0 = BIT(3), IRQ_TIMER1 = BIT(4), IRQ_TIMER2 = BIT(5),
  IRQ_TIMER3 = BIT(6), IRQ_SERIAL = BIT(7), IRQ_DMA0 = BIT(8),
  IRQ_DMA1 = BIT(9), IRQ_DMA2 = BIT(10), IRQ_DMA3 = BIT(11),
  IRQ_KEYPAD = BIT(12), IRQ_GAMEPAK = BIT(13)
};
typedef void (*IntFn)(void);
void irqInit(void);
void irqSet(int mask, IntFn handler);
void irqEnable(int mask);
void irqDisable(int mask);

/* BIOS calls (libgba_asm.s) */
void Halt(void);
void IntrWait(u32 discard_old, u32 flags);
void VBlankIntrWait(void);
s32 Div(s32 number, s32 denom);
u16 Sqrt(u32 value);
s16 ArcTan(s16 tan);
u16 ArcTan2(s16 x, s16 y);
void CpuSet(const void *src, void *dst, u32 mode);
void CpuFastSet(const void *src, void *dst, u32 mode);
void LZ77UnCompVram(const void *src, void *dst);

/* devkitARM's newlib device table, which the suite's mgba.c fills in
   (sys/iosupport.h is this file too). */
#include <sys/types.h>
struct _reent;
typedef struct
{
  const char *name;
  int structSize;
  void *open_r;
  void *close_r;
  ssize_t (*write_r)(struct _reent *r, void *fd, const char *ptr,
   size_t len);
} devoptab_t;
#define STD_OUT 1
#define STD_ERR 2
extern const devoptab_t *devoptab_list[3];

#endif
