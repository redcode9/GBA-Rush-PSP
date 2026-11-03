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
 * Both ends of the write log described in gba_ppu_log.h. The writer runs
 * in the emulator; the reader on the Media Engine, and in the host tests
 * on a second thread. Words are counted from the start of the log, never
 * reset, and taken modulo the ring size to find their place; a command is
 * never split by the end of the ring (a WRAP word sends the reader back to
 * the start instead).
 */

#include "gba_ppu_log.h"

#include <stddef.h>
#include <string.h>

#define RING_WORDS   GBA_PPU_LOG_RING_WORDS
#define BLOCK_WORDS  (GBA_PPU_LOG_BLOCK_BYTES / 4)
#define BLOCK_HALFWORDS (GBA_PPU_LOG_BLOCK_BYTES / 2)
#define ROW_BYTES    (GBA_PPU_WIDTH * 2)
/* The register writes a full resend repeats, DISPCNT to BLDY. */
#define IO_HALFWORDS (GBA_PPU_IO_BYTES / 2)
/* A full resend: RESET, the registers, every block in runs of at most a
   group with a header each, the line mark, and a ring end skipped once. */
#define RESEND_WORDS (1 + IO_HALFWORDS + GBA_PPU_LOG_GROUPS + 1 + \
 GBA_PPU_LOG_BLOCKS * BLOCK_WORDS + 1 + \
 (1 + GBA_PPU_LOG_GROUP_BLOCKS * BLOCK_WORDS))
/* Lines counted in a frame once one of them was missed. */
#define LINES_MISSED 0x100u
#define FRAME_MASK   0xFFFFFFu

/* The reader copies a run of blocks straight into its GbaPpuMemory, which
   must hold them in the blocks' order. */
typedef char GbaPpuLogMemoryLayoutCheck[
 offsetof(GbaPpuMemory, oam) == GBA_PPU_LOG_OAM_BLOCK * 64 &&
 offsetof(GbaPpuMemory, vram) == GBA_PPU_LOG_VRAM_BLOCK * 64 &&
 offsetof(GbaPpuMemory, io) == GBA_PPU_LOG_BLOCKS * 64 ? 1 : -1];
typedef char GbaPpuLogRingCheck[
 (RING_WORDS & (RING_WORDS - 1)) == 0 && RESEND_WORDS < RING_WORDS / 2 ?
  1 : -1];

#ifdef __PSP__
/* The two processors' caches know nothing of each other. The mailbox is
   uncached; sync orders the writes before a store to it, and the reads
   after a load from it. Lines are 64 bytes. */
static void store(volatile uint32_t *address, uint32_t value)
{
  __asm__ volatile("sync" ::: "memory");
  *address = value;
}

static uint32_t load(const volatile uint32_t *address)
{
  uint32_t value = *address;

  __asm__ volatile("sync" ::: "memory");
  return value;
}

/* Main processor: write the ring back so the Media Engine finds it. */
static void write_back(const void *start, uint32_t bytes)
{
  uintptr_t line = (uintptr_t)start & ~(uintptr_t)63;
  uintptr_t end = (uintptr_t)start + bytes;

  for(; line < end; line += 64)
    __asm__ volatile("cache 0x1A, 0(%0)" :: "r"(line) : "memory");
}

/* Media Engine: forget what the cache holds of the ring before reading. */
static void forget(const void *start, uint32_t bytes)
{
  uintptr_t line = (uintptr_t)start & ~(uintptr_t)63;
  uintptr_t end = (uintptr_t)start + bytes;

  for(; line < end; line += 64)
    __asm__ volatile("cache 0x19, 0(%0)" :: "r"(line) : "memory");
}

/* Media Engine: put a drawn row in memory for the GE, and out of the
   cache. */
static void hand_over(const void *start, uint32_t bytes)
{
  uintptr_t line = (uintptr_t)start & ~(uintptr_t)63;
  uintptr_t end = (uintptr_t)start + bytes;

  for(; line < end; line += 64)
    __asm__ volatile("cache 0x1B, 0(%0)" :: "r"(line) : "memory");
}
#else
/* Threads on a host share one coherent memory; release and acquire order
   the ring and the picture around the mailbox's counters. */
static void store(volatile uint32_t *address, uint32_t value)
{
  __atomic_store_n(address, value, __ATOMIC_RELEASE);
}

static uint32_t load(const volatile uint32_t *address)
{
  return __atomic_load_n(address, __ATOMIC_ACQUIRE);
}

static void write_back(const void *start, uint32_t bytes)
{
  (void)start;
  (void)bytes;
}

static void forget(const void *start, uint32_t bytes)
{
  (void)start;
  (void)bytes;
}

static void hand_over(const void *start, uint32_t bytes)
{
  (void)start;
  (void)bytes;
}
#endif

/* ---- Writer ----------------------------------------------------------- */

void gba_ppu_log_writer_init(GbaPpuLogWriter *writer,
 GbaPpuLogMailbox *mailbox, uint32_t *ring, uint16_t *frame,
 const uint16_t *palette, const uint16_t *oam, const uint16_t *vram,
 const uint16_t *io, uint8_t *dirty, uint8_t *groups)
{
  GbaPpuLogReaderSide *reader = &mailbox->reader;
  int i;

  memset(writer, 0, sizeof(*writer));
  writer->mailbox = mailbox;
  writer->ring = ring;
  writer->mask = RING_WORDS - 1;
  writer->room = RING_WORDS;
  writer->frame = 1;
  writer->lost = 1;
  writer->palette = palette;
  writer->oam = oam;
  writer->vram = vram;
  writer->io = io;
  writer->dirty = dirty;
  writer->groups = groups;

  /* No reader runs yet; the mailbox is set field by field because it may
     only be reached through volatile, uncached accesses. */
  reader->version = 0;
  reader->boots = 0;
  reader->read = 0;
  reader->completed = 0;
  reader->frames_drawn = 0;
  reader->clock = 0;
  reader->busy = 0;
  for(i = 0; i < 3; i++)
    reader->fault[i] = 0;
  mailbox->writer.ring = ring;
  mailbox->writer.ring_words = RING_WORDS;
  mailbox->writer.frame = frame;
  mailbox->writer.write = 0;
  store(&mailbox->writer.magic, GBA_PPU_LOG_MAGIC);
}

/* Whether words more fit in the ring; asks the mailbox how far the reader
   got only when the last answer is used up. */
static int room_for(GbaPpuLogWriter *writer, uint32_t words)
{
  if((int32_t)(writer->room - (writer->write + words)) >= 0)
    return 1;
  writer->room = load(&writer->mailbox->reader.read) + RING_WORDS;
  return (int32_t)(writer->room - (writer->write + words)) >= 0;
}

/* Makes room for a command of words in one piece, skipping the end of the
   ring if it does not fit there. When the ring is full the log is lost:
   nothing more goes in until everything is sent again. */
static int reserve(GbaPpuLogWriter *writer, uint32_t words)
{
  uint32_t at = writer->write & writer->mask;
  uint32_t tail = RING_WORDS - at;

  if(writer->lost)
    return 0;
  if(words > tail)
  {
    if(!room_for(writer, tail + words))
    {
      writer->lost = 1;
      return 0;
    }
    writer->ring[at] = GBA_PPU_LOG_WRAP;
    writer->write += tail;
    return 1;
  }
  if(!room_for(writer, words))
  {
    writer->lost = 1;
    return 0;
  }
  return 1;
}

static void put(GbaPpuLogWriter *writer, uint32_t word)
{
  writer->ring[writer->write++ & writer->mask] = word;
}

/* Where block lies in the emulator's memory; a run never leaves its
   region. */
static const uint16_t *block_source(const GbaPpuLogWriter *writer,
 uint32_t block)
{
  if(block < GBA_PPU_LOG_OAM_BLOCK)
    return writer->palette + block * BLOCK_HALFWORDS;
  if(block < GBA_PPU_LOG_VRAM_BLOCK)
    return writer->oam + (block - GBA_PPU_LOG_OAM_BLOCK) * BLOCK_HALFWORDS;
  return writer->vram + (block - GBA_PPU_LOG_VRAM_BLOCK) * BLOCK_HALFWORDS;
}

/* One DATA command: count blocks from first, as they are now. */
static int send_blocks(GbaPpuLogWriter *writer, uint32_t first,
 uint32_t count)
{
  uint32_t words = count * BLOCK_WORDS;

  if(!reserve(writer, 1 + words))
    return 0;
  put(writer, GBA_PPU_LOG_DATA | first << 8 | count << 20);
  memcpy(writer->ring + (writer->write & writer->mask),
   block_source(writer, first), words * 4);
  writer->write += words;
  writer->blocks_sent += count;
  return 1;
}

/* The written blocks of one group, in runs. */
static int send_group(GbaPpuLogWriter *writer, uint32_t group)
{
  uint8_t *dirty = writer->dirty;
  uint32_t block = group * GBA_PPU_LOG_GROUP_BLOCKS;
  uint32_t end = block + GBA_PPU_LOG_GROUP_BLOCKS;

  while(block < end)
  {
    uint32_t start = block, limit;

    if(!dirty[block])
    {
      block++;
      continue;
    }
    limit = start < GBA_PPU_LOG_OAM_BLOCK ? GBA_PPU_LOG_OAM_BLOCK : end;
    while(block < limit && dirty[block])
      dirty[block++] = 0;
    if(!send_blocks(writer, start, block - start))
      return 0;
  }
  return 1;
}

static void send_dirty(GbaPpuLogWriter *writer)
{
  const uint32_t *words = (const uint32_t *)writer->groups;
  uint32_t word, group;

  for(word = 0; word < GBA_PPU_LOG_GROUP_BYTES / 4; word++)
  {
    if(!words[word])
      continue;
    for(group = word * 4; group < word * 4 + 4; group++)
    {
      if(group >= GBA_PPU_LOG_GROUPS || !writer->groups[group])
        continue;
      writer->groups[group] = 0;
      if(!send_group(writer, group))
        return;
    }
  }
}

/* Everything again: RESET, every register and every block. Waits, sending
   nothing, until the ring has room for all of it. */
static int send_all(GbaPpuLogWriter *writer)
{
  uint32_t offset, block;

  if(!room_for(writer, RESEND_WORDS))
    return 0;
  writer->lost = 0;
  writer->resends++;
  put(writer, GBA_PPU_LOG_RESET | writer->frame << 8);
  for(offset = 0; offset < GBA_PPU_IO_BYTES; offset += 2)
    put(writer, GBA_PPU_LOG_IO | offset << 8 |
     (uint32_t)writer->io[offset >> 1] << 16);
  send_blocks(writer, GBA_PPU_LOG_PALETTE_BLOCK,
   GBA_PPU_LOG_OAM_BLOCK - GBA_PPU_LOG_PALETTE_BLOCK);
  send_blocks(writer, GBA_PPU_LOG_OAM_BLOCK,
   GBA_PPU_LOG_VRAM_BLOCK - GBA_PPU_LOG_OAM_BLOCK);
  for(block = GBA_PPU_LOG_VRAM_BLOCK; block < GBA_PPU_LOG_BLOCKS;
   block += GBA_PPU_LOG_GROUP_BLOCKS)
    send_blocks(writer, block, GBA_PPU_LOG_GROUP_BLOCKS);
  memset(writer->dirty, 0, GBA_PPU_LOG_BLOCKS);
  memset(writer->groups, 0, GBA_PPU_LOG_GROUP_BYTES);
  return 1;
}

/* Writes back what was added since the last time, then tells the
   reader. */
static void publish(GbaPpuLogWriter *writer)
{
  uint32_t from = writer->published, to = writer->write;

  if(from == to)
    return;
  if(to - from >= RING_WORDS)
    write_back(writer->ring, RING_WORDS * 4);
  else
  {
    uint32_t a = from & writer->mask, b = to & writer->mask;

    if(a < b)
      write_back(writer->ring + a, (b - a) * 4);
    else
    {
      write_back(writer->ring + a, (RING_WORDS - a) * 4);
      write_back(writer->ring, b * 4);
    }
  }
  store(&writer->mailbox->writer.write, to);
  writer->published = to;
}

void gba_ppu_log_io(GbaPpuLogWriter *writer, uint32_t offset, uint16_t value)
{
  if(!reserve(writer, 1))
    return;
  put(writer, GBA_PPU_LOG_IO | (offset & 0xFE) << 8 | (uint32_t)value << 16);
  writer->io_sent++;
}

void gba_ppu_log_line(GbaPpuLogWriter *writer, uint32_t y)
{
  if(writer->lost)
  {
    if(!send_all(writer))
      return;
  }
  else
    send_dirty(writer);
  if(reserve(writer, 1))
    put(writer, GBA_PPU_LOG_LINE | y << 8);
  publish(writer);
}

void gba_ppu_log_vblank(GbaPpuLogWriter *writer)
{
  uint32_t boots = load(&writer->mailbox->reader.boots);

  /* A reader that started again knows nothing. */
  if(boots != writer->boots)
  {
    writer->boots = boots;
    writer->lost = 1;
  }
  writer->frame = (writer->frame + 1) & FRAME_MASK;
  if(!writer->frame)
    writer->frame = 1;
  if(reserve(writer, 1))
    put(writer, GBA_PPU_LOG_VBLANK | writer->frame << 8);
  publish(writer);
}

void gba_ppu_log_resend(GbaPpuLogWriter *writer)
{
  writer->lost = 1;
}

uint32_t gba_ppu_log_frame(const GbaPpuLogWriter *writer)
{
  return writer->frame;
}

int gba_ppu_log_frame_done(const GbaPpuLogWriter *writer, uint32_t frame)
{
  return load(&writer->mailbox->reader.completed) == frame;
}

/* ---- Reader ----------------------------------------------------------- */

void gba_ppu_log_reader_start(GbaPpuLogReader *reader,
 GbaPpuLogMailbox *mailbox)
{
  GbaPpuLogReaderSide *side = &mailbox->reader;

  reader->mailbox = mailbox;
  reader->ring = mailbox->writer.ring;
  reader->mask = mailbox->writer.ring_words - 1;
  reader->frame = mailbox->writer.frame;
  reader->read = load(&mailbox->writer.write);
  reader->frame_number = 0;
  reader->lines = LINES_MISSED;
  gba_ppu_init(&reader->ppu, reader->memory.palette, reader->memory.oam,
   reader->memory.vram, reader->memory.io);
  store(&side->read, reader->read);
  store(&side->version, GBA_PPU_LOG_VERSION);
  store(&side->boots, load(&side->boots) + 1);
}

/* Forgets the cached copy of the ring from word from up to word to. */
static void forget_ring(const GbaPpuLogReader *reader, uint32_t from,
 uint32_t to)
{
  uint32_t size = reader->mask + 1;
  uint32_t a = from & reader->mask, b = to & reader->mask;

  if(to - from >= size)
    forget(reader->ring, size * 4);
  else if(a < b)
    forget(reader->ring + a, (b - a) * 4);
  else
  {
    forget(reader->ring + a, (size - a) * 4);
    forget(reader->ring, b * 4);
  }
}

static void draw_line(GbaPpuLogReader *reader, uint32_t y)
{
  uint16_t *row = reader->frame + y * GBA_PPU_LOG_FRAME_STRIDE;

  gba_ppu_draw_line(&reader->ppu, (int)y, row);
  hand_over(row, ROW_BYTES);
  reader->lines = y == reader->lines ? reader->lines + 1 : LINES_MISSED;
  if(reader->lines == GBA_PPU_HEIGHT)
  {
    reader->frames_drawn++;
    store(&reader->mailbox->reader.frames_drawn, reader->frames_drawn);
    store(&reader->mailbox->reader.completed, reader->frame_number);
  }
}

int gba_ppu_log_read(GbaPpuLogReader *reader)
{
  uint32_t write = load(&reader->mailbox->writer.write);
  uint32_t size = reader->mask + 1;
  int result = 1;

  if(write == reader->read)
    return 0;
  forget_ring(reader, reader->read, write);
  while(reader->read != write)
  {
    uint32_t at = reader->read & reader->mask;
    uint32_t word = reader->ring[at];
    uint32_t first = (word >> 8) & 0xFFF, count = word >> 20;
    int known = 1;

    switch(word & 0xFF)
    {
      case GBA_PPU_LOG_WRAP:
        reader->read += size - at;
        continue;

      case GBA_PPU_LOG_IO:
        gba_ppu_write_io(&reader->ppu, (word >> 8) & 0xFF,
         (uint16_t)(word >> 16));
        break;

      case GBA_PPU_LOG_LINE:
        if(((word >> 8) & 0xFF) < GBA_PPU_HEIGHT)
          draw_line(reader, (word >> 8) & 0xFF);
        break;

      case GBA_PPU_LOG_VBLANK:
        gba_ppu_start_frame(&reader->ppu);
        reader->frame_number = word >> 8;
        reader->lines = 0;
        break;

      case GBA_PPU_LOG_DATA:
        /* A run past the end of video memory or of the ring is no
           command. */
        known = first + count <= GBA_PPU_LOG_BLOCKS &&
         at + 1 + count * BLOCK_WORDS <= size;
        if(known)
        {
          memcpy((uint8_t *)&reader->memory + first * 64,
           reader->ring + at + 1, count * 64);
          reader->read += count * BLOCK_WORDS;
        }
        break;

      case GBA_PPU_LOG_RESET:
        gba_ppu_init(&reader->ppu, reader->memory.palette,
         reader->memory.oam, reader->memory.vram, reader->memory.io);
        reader->frame_number = word >> 8;
        reader->lines = 0;
        break;

      default:
        known = 0;
        break;
    }
    if(!known)
    {
      reader->read = write;
      reader->lines = LINES_MISSED;
      result = -1;
      break;
    }
    reader->read++;
  }
  store(&reader->mailbox->reader.read, reader->read);
  return result;
}
