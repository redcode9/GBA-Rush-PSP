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
 * Code in VRAM.
 *
 * vram_<arm|thumb>_<cpu|dma>: a routine copied into VRAM and run, then
 * rewritten by CPU stores or by DMA3 and run again. VRAM is ordinary
 * memory to the CPU, so the new version must run. The detail is what the
 * first run returned (1, the old version) and the second in bits 8-15
 * (2, the new one). Each case has its own 64 bytes of OBJ VRAM from
 * 06014000h, above the mode 3 frame buffer.
 *
 * mGBA 0.10.3 passes every case. To confirm on hardware.
 */

#include "result.h"

#define VRAM_CODE(slot) ((vu16 *)(0x06014000 + (slot) * 0x40))

/* cases_vram_asm.s: the old routine returns 1, the new one 2. */
extern const u16 vram_arm_old[4], vram_arm_new[4];
extern const u16 vram_thumb_old[2], vram_thumb_new[2];

static void cpu_copy(vu16 *to, const u16 *from, u32 halfwords)
{
  while(halfwords--)
    *to++ = *from++;
}

static void dma_copy(vu16 *to, const u16 *from, u32 halfwords)
{
  REG_DMA3SAD = (u32)from;
  REG_DMA3DAD = (u32)to;
  REG_DMA3CNT = DMA_ENABLE | halfwords;
  while(REG_DMA3CNT & DMA_ENABLE)
    ;
}

static int vram_case(u32 *detail, unsigned slot, u32 thumb,
 void (*copy)(vu16 *, const u16 *, u32))
{
  vu16 *code = VRAM_CODE(slot);
  u32 halfwords = thumb ? 2 : 4;
  u32 (*run)(void) = (u32 (*)(void))((u32)code | thumb);

  cpu_copy(code, thumb ? vram_thumb_old : vram_arm_old, halfwords);
  *detail = run();
  copy(code, thumb ? vram_thumb_new : vram_arm_new, halfwords);
  *detail |= run() << 8;
  return *detail == 0x0201 ? RESULT_PASS : RESULT_FAIL;
}

static int vram_arm_cpu(u32 *detail)
{
  return vram_case(detail, 0, 0, cpu_copy);
}

static int vram_arm_dma(u32 *detail)
{
  return vram_case(detail, 1, 0, dma_copy);
}

static int vram_thumb_cpu(u32 *detail)
{
  return vram_case(detail, 2, 1, cpu_copy);
}

static int vram_thumb_dma(u32 *detail)
{
  return vram_case(detail, 3, 1, dma_copy);
}

const TestCase cases_vram[] =
{
  { "vram_arm_cpu", vram_arm_cpu },
  { "vram_arm_dma", vram_arm_dma },
  { "vram_thumb_cpu", vram_thumb_cpu },
  { "vram_thumb_dma", vram_thumb_dma },
  { NULL, NULL }
};
