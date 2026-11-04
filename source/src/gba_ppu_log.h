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
 * The write log that lets gba_ppu draw on another processor, the PSP's
 * Media Engine.
 *
 * That processor cannot simply read the emulator's memory when it draws
 * line y: by then the game has moved on. So the emulator (the writer) sends
 * it what the game wrote, in order, with a mark at each line to draw:
 * video register writes as they happen, and the 64-byte blocks of palette
 * RAM, OAM and VRAM written since the last mark, copied when the next mark
 * is due. The renderer (the reader) keeps its own GbaPpuMemory, applies the
 * log to it and draws each line as soon as its mark arrives, so the
 * picture is finished moments after the emulator reaches line 160.
 *
 * The log is a ring of 32-bit words in memory both processors see, and a
 * mailbox of counters. The writer only moves `write` on and the reader only
 * `read`; neither ever waits for the other. When the ring is full the
 * writer drops what follows and, as soon as there is room again, sends the
 * whole video state over. A picture is reported done only if all 160 lines
 * of it were drawn since the frame began, so a picture built from a gap in
 * the log is never shown.
 *
 * On the PSP the mailbox is reached through its uncached address on both
 * processors. The ring is written through the main processor's cache and
 * written back before each mark is published; the Media Engine drops its
 * own cached copy of each part of the ring before reading it, and writes
 * each line it draws back to memory, where the GE fetches the picture.
 * On a host both sides are threads and the mailbox carries the ordering.
 */

#ifndef GBA_PPU_LOG_H
#define GBA_PPU_LOG_H

#include <stdint.h>

#include "gba_ppu.h"

/* The writer sets magic while the log is in use; a reader that finds it
   gone stops. The reader answers with version once it started. */
#define GBA_PPU_LOG_MAGIC   0x4C505047u   /* "GPPL" */
#define GBA_PPU_LOG_VERSION 2u

/* Video memory in 64-byte blocks, in GbaPpuMemory's order: palette RAM
   (blocks 0-15), OAM (16-31), VRAM (32-1567). Whenever the game writes
   into a block the emulator sets its byte in a dirty map, and the byte of
   its group of 32 blocks in a second map, so that finding the written
   blocks takes a glance at 13 words. */
#define GBA_PPU_LOG_BLOCK_BYTES    64u
#define GBA_PPU_LOG_PALETTE_BLOCK  0u
#define GBA_PPU_LOG_OAM_BLOCK      16u
#define GBA_PPU_LOG_VRAM_BLOCK     32u
#define GBA_PPU_LOG_BLOCKS         1568u
#define GBA_PPU_LOG_GROUP_BLOCKS   32u
#define GBA_PPU_LOG_GROUPS         49u
/* The group map's size, whole words. */
#define GBA_PPU_LOG_GROUP_BYTES    52u

/* The ring must hold one whole copy of the video state with room to spare;
   its size is a power of two. */
#define GBA_PPU_LOG_RING_WORDS     (64u * 1024u)

/* The picture the reader draws: 240x160 BGR555, rows this many pixels
   apart, so that every row starts on a 64-byte cache line. */
#define GBA_PPU_LOG_FRAME_STRIDE   256u

/* Commands, in the low byte of a ring word. */
enum
{
  GBA_PPU_LOG_WRAP = 0,   /* go on at the start of the ring */
  GBA_PPU_LOG_IO,         /* bits 8-15 register offset, 16-31 its value */
  GBA_PPU_LOG_LINE,       /* bits 8-15: draw this line */
  GBA_PPU_LOG_VBLANK,     /* bits 8-31: the frame that begins */
  GBA_PPU_LOG_DATA,       /* bits 8-19 first block, 20-31 how many; then
                             0-3 unused words, so that the blocks start on
                             16 bytes, and 16 words per block */
  GBA_PPU_LOG_RESET       /* bits 8-31: start over within this frame */
};

typedef struct
{
  /* Set up by the writer before the reader starts. */
  volatile uint32_t magic;
  uint32_t *volatile ring;
  volatile uint32_t ring_words;
  uint16_t *volatile frame;
  /* Words published, counted from the start of the log. */
  volatile uint32_t write;
} __attribute__((aligned(64))) GbaPpuLogWriterSide;

typedef struct
{
  volatile uint32_t version;
  /* Times the reader started: a new start has an empty GbaPpuMemory. */
  volatile uint32_t boots;
  /* Words consumed. */
  volatile uint32_t read;
  /* The number of the last frame whose 160 lines were all drawn. */
  volatile uint32_t completed;
  volatile uint32_t frames_drawn;
  /* The reader's clock when it last finished some work, and the ticks of
     it spent working; the writer learns the clock's rate from its own. */
  volatile uint32_t clock;
  volatile uint32_t busy;
  /* A processor exception on the reader: cause, address, bad address. */
  volatile uint32_t fault[3];
} __attribute__((aligned(64))) GbaPpuLogReaderSide;

typedef struct
{
  GbaPpuLogWriterSide writer;
  GbaPpuLogReaderSide reader;
} GbaPpuLogMailbox;

typedef struct
{
  GbaPpuLogMailbox *mailbox;
  uint32_t *ring;
  uint32_t mask;
  /* Next word to fill and the last one published, counted from the start
     of the log; write may not pass room, the reader's read plus the ring
     size as last seen. */
  uint32_t write;
  uint32_t published;
  uint32_t room;
  /* The frame being drawn, 1 on, never 0. */
  uint32_t frame;
  /* The reader's boots last seen. */
  uint32_t boots;
  /* Set when the log lost a write: everything goes again at the next
     line, which also happens after a reset or a state load. */
  int lost;
  /* The emulator's memory the blocks are copied from. */
  const uint16_t *palette;
  const uint16_t *oam;
  const uint16_t *vram;
  const uint16_t *io;
  /* The dirty map and its group map; the emulator's store paths set them,
     the writer clears what it sent. groups is 4-byte aligned. */
  uint8_t *dirty;
  uint8_t *groups;
  /* Totals for the runtime log. */
  uint32_t blocks_sent;
  uint32_t io_sent;
  uint32_t resends;
} GbaPpuLogWriter;

typedef struct
{
  GbaPpu ppu;
  GbaPpuMemory memory;
  GbaPpuLogMailbox *mailbox;
  const uint32_t *ring;
  uint32_t mask;
  uint32_t read;
  uint16_t *frame;
  /* The frame being drawn and how many of its lines were drawn in order,
     or more than 160 once one was missed. */
  uint32_t frame_number;
  uint32_t lines;
  uint32_t frames_drawn;
} GbaPpuLogReader;

/* The writer side, on the main processor. The mailbox, ring (of
   GBA_PPU_LOG_RING_WORDS) and frame (160 rows of GBA_PPU_LOG_FRAME_STRIDE
   pixels) are 64-byte aligned; mailbox is the address to reach it by
   (uncached on the PSP). dirty has GBA_PPU_LOG_BLOCKS bytes and groups
   GBA_PPU_LOG_GROUP_BYTES. Sets up the mailbox; the log starts with the
   whole state to send. */
void gba_ppu_log_writer_init(GbaPpuLogWriter *writer,
 GbaPpuLogMailbox *mailbox, uint32_t *ring, uint16_t *frame,
 const uint16_t *palette, const uint16_t *oam, const uint16_t *vram,
 const uint16_t *io, uint8_t *dirty, uint8_t *groups);

/* A video register write, offset from 0x04000000, as the register holds
   it after the write. */
void gba_ppu_log_io(GbaPpuLogWriter *writer, uint32_t offset, uint16_t value);

/* The emulator reached the H-Blank of visible line y: send the blocks
   written since the last line and the mark to draw it. */
void gba_ppu_log_line(GbaPpuLogWriter *writer, uint32_t y);

/* V-Blank begins: the frame is over and the next one begins. */
void gba_ppu_log_vblank(GbaPpuLogWriter *writer);

/* Send the whole state again at the next line: after a reset, a state
   load, or when the log was off for a while. */
void gba_ppu_log_resend(GbaPpuLogWriter *writer);

/* The frame whose lines the writer is sending. */
uint32_t gba_ppu_log_frame(const GbaPpuLogWriter *writer);

/* Whether the reader finished drawing all of that frame. */
int gba_ppu_log_frame_done(const GbaPpuLogWriter *writer, uint32_t frame);

/* The reader side. reader_start reads the ring and frame from the mailbox
   and starts reading at the writer's last published word. */
void gba_ppu_log_reader_start(GbaPpuLogReader *reader,
 GbaPpuLogMailbox *mailbox);

/* Applies and draws everything published so far. Returns 0 if there was
   nothing, 1 otherwise, or -1 on a word that is no command (the reader
   then skips to the end of what was published). */
int gba_ppu_log_read(GbaPpuLogReader *reader);

#endif
