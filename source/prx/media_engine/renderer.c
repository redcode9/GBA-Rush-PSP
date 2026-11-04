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
 * The program the Media Engine runs: gba_ppu, drawing every GBA line from
 * the write log (source/src/gba_ppu_log.h) the emulator fills.
 *
 * It is linked to run from the Media Engine's own eDRAM (renderer.ld) and
 * copied there by boot.S, so neither its code nor its copy of the video
 * memory ever takes a trip over the bus the main processor and the Wi-Fi
 * share: only the log, the drawn lines and the polls of the mailbox do. It
 * has no operating system, no library and takes no interrupts. Between
 * batches of work it waits in a loop that touches no memory, and polls the
 * mailbox less and less often the longer nothing comes, up to about every
 * 25 us. When the emulator asks for it (GBA_PPU_LOG_WAIT_SLEEP) it then
 * goes further and halts until the emulator interrupts it.
 */

#include <stddef.h>
#include <stdint.h>

#include "gba_ppu_log.h"

/* Spins of the waiting loop between two polls of the mailbox; one spin is
   a handful of cycles. */
#define PAUSE_SHORTEST 32u
#define PAUSE_LONGEST  2048u

/* The Media Engine's interrupt flags in its system registers, which its
   start-up code (boot.S) clears in the same way (GBA_PPU_LOG_WAIT_CLEAR:
   whether that register is the Media Engine's own is still to be seen on
   a console); and the Status bits that let the hardware interrupt lines
   end a halt, still without taking an exception (interrupts stay off). */
#define INTERRUPT_FLAGS     (*(volatile uint32_t *)0xBC100004u)
#define STATUS_ALL_LINES    0xFC00u

void me_start(GbaPpuLogMailbox *mailbox);

/* From renderer.ld. */
extern uint32_t me_bss_start[], me_bss_end[];

/* In eDRAM, cleared by me_start. */
static GbaPpuLogReader reader;
static GbaPpuLogMailbox *fault_mailbox;

/* gba_ppu and the log need nothing else from a C library. */
typedef uint32_t __attribute__((may_alias)) word_t;

void *memcpy(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);

void *memcpy(void *destination, const void *source, size_t size)
{
  uint8_t *to = destination;
  const uint8_t *from = source;

  if((((uintptr_t)to | (uintptr_t)from) & 3) == 0)
  {
    for(; size >= 16; size -= 16, to += 16, from += 16)
    {
      word_t a = ((const word_t *)from)[0], b = ((const word_t *)from)[1];
      word_t c = ((const word_t *)from)[2], d = ((const word_t *)from)[3];

      ((word_t *)to)[0] = a;
      ((word_t *)to)[1] = b;
      ((word_t *)to)[2] = c;
      ((word_t *)to)[3] = d;
    }
    for(; size >= 4; size -= 4, to += 4, from += 4)
      *(word_t *)to = *(const word_t *)from;
  }
  while(size--)
    *to++ = *from++;
  return destination;
}

void *memset(void *destination, int value, size_t size)
{
  uint8_t *to = destination;
  word_t fill = (uint8_t)value * 0x01010101u;

  for(; size && ((uintptr_t)to & 3); size--)
    *to++ = (uint8_t)value;
  for(; size >= 16; size -= 16, to += 16)
  {
    ((word_t *)to)[0] = fill;
    ((word_t *)to)[1] = fill;
    ((word_t *)to)[2] = fill;
    ((word_t *)to)[3] = fill;
  }
  for(; size >= 4; size -= 4, to += 4)
    *(word_t *)to = fill;
  while(size--)
    *to++ = (uint8_t)value;
  return destination;
}

/* The processor's cycle counter (COP0 Count), which runs at half the
   clock. Only differences are used, and only for the busy time. */
static uint32_t clock_now(void)
{
  uint32_t count;

  __asm__ volatile("mfc0 %0, $9" : "=r"(count));
  return count;
}

static void pause_for(uint32_t spins)
{
  while(spins--)
    __asm__ volatile("");
}

static void halt(void)
{
  __asm__ volatile(".word 0x70000000" ::: "memory");   /* halt */
}

/* Stops for good: halt waits for an interrupt, and none is enabled (if
   one is, it halts again). */
static void __attribute__((noreturn)) stop(void)
{
  for(;;)
    halt();
}

/* Sleeps until the emulator interrupts it, unless words came meanwhile:
   the emulator looks at sleeping after each word it publishes. A wake
   that comes too early, or never ends, only costs a poll. */
static void sleep_for_words(GbaPpuLogMailbox *mailbox)
{
  GbaPpuLogReaderSide *side = &mailbox->reader;

  side->sleeping = 1;
  __asm__ volatile("sync" ::: "memory");
  if(mailbox->writer.write == side->read)
    halt();
  if(mailbox->writer.wait & GBA_PPU_LOG_WAIT_CLEAR)
  {
    INTERRUPT_FLAGS = 0xFFFFFFFFu;
    __asm__ volatile("sync" ::: "memory");
  }
  side->sleeping = 0;
}

/* Every processor exception comes here (COP0 register 25 holds the
   address). Leaves its cause, address and bad address in the mailbox for
   the emulator's log, then stops. */
static void __attribute__((noreturn)) fault(void)
{
  GbaPpuLogMailbox *mailbox = fault_mailbox;
  uint32_t cause, address, bad;

  __asm__ volatile("mfc0 %0, $13" : "=r"(cause));
  __asm__ volatile("mfc0 %0, $14" : "=r"(address));
  __asm__ volatile("mfc0 %0, $8" : "=r"(bad));
  if(mailbox)
  {
    mailbox->reader.fault[1] = address;
    mailbox->reader.fault[2] = bad;
    __asm__ volatile("sync" ::: "memory");
    mailbox->reader.fault[0] = cause | 1;
  }
  stop();
}

static void __attribute__((noreturn)) run(GbaPpuLogMailbox *mailbox)
{
  GbaPpuLogReaderSide *side = &mailbox->reader;
  uint32_t busy = 0, spins = PAUSE_SHORTEST;

  gba_ppu_log_reader_start(&reader, mailbox);
  for(;;)
  {
    uint32_t started = clock_now();

    if(gba_ppu_log_read(&reader))
    {
      uint32_t now = clock_now();

      busy += now - started;
      gba_ppu_log_reader_busy(&reader, now - started);
      side->busy = busy;
      side->clock = now;
      spins = PAUSE_SHORTEST;
      continue;
    }
    pause_for(spins);
    if(spins < PAUSE_LONGEST)
      spins <<= 1;
    /* Long idle: the emulator may have closed; then its memory is no
       longer ours to write. */
    else if(mailbox->writer.magic != GBA_PPU_LOG_MAGIC)
      stop();
    else if(mailbox->writer.wait & GBA_PPU_LOG_WAIT_SLEEP)
      sleep_for_words(mailbox);
  }
}

/* boot.S jumps here, the first word of the program, with the mailbox's
   uncached address and the stack at the top of eDRAM. */
void __attribute__((section(".text.start"), noreturn))
 me_start(GbaPpuLogMailbox *mailbox)
{
  uint32_t *word;

  for(word = me_bss_start; word < me_bss_end; word++)
    *word = 0;
  fault_mailbox = mailbox;
  __asm__ volatile("mtc0 %0, $25" :: "r"(fault));
  if(mailbox->writer.wait & GBA_PPU_LOG_WAIT_SLEEP)
  {
    uint32_t status;

    __asm__ volatile("mfc0 %0, $12" : "=r"(status));
    __asm__ volatile("mtc0 %0, $12" :: "r"(status | STATUS_ALL_LINES));
  }
  run(mailbox);
}
