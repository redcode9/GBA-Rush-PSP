/*
 * Host tests for gba_line.h, the LCD phase changes of main.c: which
 * change the New core's line event waits for, and what the changes it
 * skips do to DISPSTAT and VCOUNT when a pass or a CPU read reaches them.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "gba_line.h"

#define VBLANK 0x01
#define HBLANK 0x02
#define MATCH 0x04
#define HBLANK_IRQ 0x10
#define VCOUNT_IRQ 0x20
#define LYC(line) ((uint16_t)((line) << 8))

/* The cycle line `line` starts, with the H-blank of line 10 at cycle
   1000. */
static uint32_t line_start(uint32_t line)
{
  return 1000 + 272 + (line - 11) * 1232;
}

static void test_step(void)
{
  uint16_t dispstat = LYC(5);
  uint16_t vcount = 4;

  /* H-draw to H-blank: only the H-blank flag */
  assert(gba_line_step(&dispstat, &vcount) == 272);
  assert(dispstat == (LYC(5) | HBLANK) && vcount == 4);

  /* H-blank to the next line, the match line */
  assert(gba_line_step(&dispstat, &vcount) == 960);
  assert(dispstat == (LYC(5) | MATCH) && vcount == 5);

  /* and on past it: the match flag goes */
  gba_line_step(&dispstat, &vcount);
  gba_line_step(&dispstat, &vcount);
  assert(dispstat == LYC(5) && vcount == 6);
}

static void test_next_served(void)
{
  /* Nothing watches: the walk goes on to the start of V-blank */
  assert(gba_line_next_served(1000, 0, 10, 0, 0) == line_start(160));

  /* The VCOUNT match line, only with its IRQ on */
  assert(gba_line_next_served(1000, LYC(50) | VCOUNT_IRQ, 10, 0, 0) ==
   line_start(50));
  assert(gba_line_next_served(1000, LYC(50), 10, 0, 0) == line_start(160));
  /* A match line already behind waits for the next frame */
  assert(gba_line_next_served(1000, LYC(5) | VCOUNT_IRQ, 10, 0, 0) ==
   line_start(160));

  /* An H-blank IRQ stops the walk at the very next H-blank, in V-blank
     too; from H-blank, line 11 starts at 1000 and its H-blank at 1960 */
  assert(gba_line_next_served(1000, HBLANK_IRQ, 10, 0, 0) == 1000);
  assert(gba_line_next_served(1000, VBLANK | HBLANK_IRQ, 170, 0, 0) == 1000);
  assert(gba_line_next_served(1000, HBLANK | HBLANK_IRQ, 10, 0, 0) == 1960);
  assert(gba_line_next_served(1000, HBLANK, 10, 1, 0) == 1960);
  assert(gba_line_next_served(1000, HBLANK | LYC(11) | VCOUNT_IRQ, 10, 1,
   0) == 1000);

  /* A drawn frame or an H-blank DMA serves the visible H-blanks only; a
     picture for the Media Engine, the V-blank ones only */
  assert(gba_line_next_served(1000, 0, 10, 1, 0) == 1000);
  assert(gba_line_next_served(1000, 0, 10, 0, 1) == line_start(160));
  assert(gba_line_next_served(1000, VBLANK, 170, 0, 1) == 1000);

  /* In V-blank nothing else stops the walk before line 228 starts, the
     end of the frame */
  assert(gba_line_next_served(1000, VBLANK, 170, 1, 0) ==
   1000 + (227 - 170) * 1232 + 272);
  assert(gba_line_next_served(1000, VBLANK, 227, 0, 0) == 1272);
  assert(gba_line_next_served(1000, VBLANK | LYC(200) | VCOUNT_IRQ, 170, 0,
   0) == 1000 + (199 - 170) * 1232 + 272);
}

int main(void)
{
  test_step();
  test_next_served();
  printf("LCD line tests passed\n");
  return 0;
}
