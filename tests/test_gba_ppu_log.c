/*
 * Host tests for gba_ppu_log.c, the write log that carries the game's
 * video writes to a gba_ppu on the Media Engine. A pretend emulator keeps
 * its video memory, marks what it writes in the dirty maps as the real one
 * does, and draws every frame with a gba_ppu reading that memory in place,
 * as the emulator's own CPU path does. The reader, on the same thread or
 * on a second one, must draw exactly the same pictures from the log, also
 * when the ring wraps, fills up, or the reader starts over.
 */

#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gba_ppu_log.h"

#define WIDTH GBA_PPU_WIDTH
#define HEIGHT GBA_PPU_HEIGHT
#define STRIDE GBA_PPU_LOG_FRAME_STRIDE

/* The pretend emulator's memory, laid out as the dirty map numbers it. */
static GbaPpuMemory game;
static uint8_t dirty[GBA_PPU_LOG_BLOCKS];
static uint32_t group_words[GBA_PPU_LOG_GROUP_BYTES / 4];
static GbaPpu direct;
static uint16_t direct_frame[WIDTH * HEIGHT];

static GbaPpuLogMailbox mailbox __attribute__((aligned(64)));
static uint32_t ring[GBA_PPU_LOG_RING_WORDS] __attribute__((aligned(64)));
static uint16_t frame[STRIDE * HEIGHT] __attribute__((aligned(64)));
static GbaPpuLogWriter writer;
static GbaPpuLogReader reader;

/* Like assert, but also under NDEBUG: most checks below run the log. */
#define check(condition) \
  do \
  { \
    if(!(condition)) \
    { \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
      abort(); \
    } \
  } \
  while(0)

static uint32_t seed = 12345;

static uint32_t next_random(void)
{
  seed = seed * 1103515245u + 12345u;
  return seed >> 8;
}

/* As the emulator's store paths do: palette at 0, OAM at 0x400, VRAM at
   0x800 in the map's numbering. */
static void mark(uint32_t offset)
{
  uint32_t block = offset >> 6;

  dirty[block] = 1;
  ((uint8_t *)group_words)[block >> 5] = 1;
}

static void store16(uint32_t offset, uint16_t value)
{
  ((uint16_t *)&game)[offset >> 1] = value;
  mark(offset);
}

static void store_vram(uint32_t offset, uint16_t value)
{
  store16(0x800 + offset, value);
}

/* A register write: the emulator stores it, tells its own gba_ppu of
   DISPCNT and the reference points, and logs it. */
static void io(uint32_t offset, uint16_t value)
{
  game.io[offset >> 1] = value;
  if(offset == 0 || (offset >= 0x28 && offset < 0x40))
    gba_ppu_write_io(&direct, offset, value);
  gba_ppu_log_io(&writer, offset, value);
}

static void start(void)
{
  memset(&game, 0, sizeof(game));
  memset(dirty, 0, sizeof(dirty));
  memset(group_words, 0, sizeof(group_words));
  memset(&reader, 0, sizeof(reader));
  gba_ppu_init(&direct, game.palette, game.oam, game.vram, game.io);
  gba_ppu_log_writer_init(&writer, &mailbox, ring, frame, game.palette,
   game.oam, game.vram, game.io, dirty, (uint8_t *)group_words);
  gba_ppu_log_reader_start(&reader, &mailbox);
}

/* A busy screen: random tiles, maps, palette and sprites, a rotated BG2
   in mode 1, blending and a window. */
static void scene(void)
{
  uint32_t i;

  for(i = 0; i < 0x18000; i += 2)
    store_vram(i, (uint16_t)next_random());
  for(i = 0; i < 0x400; i += 2)
    store16(i, (uint16_t)(next_random() & 0x7FFF));
  for(i = 0; i < 128; i++)
  {
    uint16_t a0 = (uint16_t)(next_random() & 0xC3FF);

    if(i % 3 == 0)
      a0 |= 0x0100;
    store16(0x400 + i * 8, a0);
    store16(0x400 + i * 8 + 2, (uint16_t)next_random());
    store16(0x400 + i * 8 + 4, (uint16_t)next_random());
    store16(0x400 + i * 8 + 6, (uint16_t)next_random());
  }
  io(0x08, 0x0080 | 0x1C00);
  io(0x0A, 0x1E01);
  io(0x0C, 0x4C82 | 0x2000);
  io(0x20, 0x0100);
  io(0x26, 0x0100);
  io(0x28, 0x1000);
  io(0x2A, 0x0000);
  io(0x50, 0x3F41);
  io(0x52, 0x0808);
  io(0x40, 0x10E0);
  io(0x44, 0x2080);
  io(0x48, 0x3F1F);
  io(0x4A, 0x001B);
  io(0x00, 0x2000 | 0x1000 | 0x0700 | 0x0040 | 1);
}

/* One emulated frame. Before each line the game rewrites the rotation and
   now and then a palette entry or some VRAM, as raster effects do; every
   line is drawn by the direct renderer and marked in the log, unless the
   frame is skipped. V-Blank then begins as in the emulator, and the game
   writes some more. Returns the number of the frame drawn. */
static uint32_t run_frame(int draw)
{
  uint32_t number;
  uint32_t y;

  for(y = 0; y < HEIGHT; y++)
  {
    io(0x20, (uint16_t)(0x100 + y));
    io(0x22, (uint16_t)(y * 3));
    if(y % 7 == 0)
      store16((next_random() & 0x1FF) * 2,
       (uint16_t)(next_random() & 0x7FFF));
    if(y % 13 == 0)
      store_vram(next_random() % 0x18000 & ~1u, (uint16_t)next_random());
    if(y == 80)
      io(0x2C, (uint16_t)(next_random() & 0x3FFF));
    if(!draw)
      continue;
    gba_ppu_draw_line(&direct, (int)y, direct_frame + y * WIDTH);
    gba_ppu_log_line(&writer, y);
  }
  number = gba_ppu_log_frame(&writer);
  gba_ppu_start_frame(&direct);
  gba_ppu_log_vblank(&writer);
  for(y = 0; y < 40; y++)
    store_vram((next_random() % 0x18000) & ~1u, (uint16_t)next_random());
  io(0x10, (uint16_t)(next_random() & 0x1FF));
  io(0x00, (uint16_t)((game.io[0] ^ (next_random() & 0x0200)) | 1));
  return number;
}

static int frames_match(void)
{
  uint32_t y;

  for(y = 0; y < HEIGHT; y++)
    if(memcmp(frame + y * STRIDE, direct_frame + y * WIDTH, WIDTH * 2))
      return 0;
  return 1;
}

/* The reader catches up with the log; whether its picture of frame
   number was complete and equal to the direct one. */
static int caught_up(uint32_t number)
{
  while(gba_ppu_log_read(&reader) > 0)
    ;
  return gba_ppu_log_frame_done(&writer, number) && frames_match();
}

static void test_frames_match_on_one_thread(void)
{
  int i;

  start();
  scene();
  for(i = 0; i < 400; i++)
    check(caught_up(run_frame(1)));
  /* 400 frames went round the ring several times. */
  check(writer.write > 3u * GBA_PPU_LOG_RING_WORDS);
  /* Everything went once at the start and once more when the writer
     learnt that the reader had started. */
  check(writer.resends == 2);
  check(mailbox.reader.frames_drawn == 400);
}

/* The reader falls behind until the ring overflows: frames go missing,
   none is reported done with a wrong picture, and once the reader catches
   up the writer sends everything again and the pictures are right. */
static void test_full_ring_sends_everything_again(void)
{
  int i, done = 0;

  start();
  scene();
  check(caught_up(run_frame(1)));
  for(i = 0; i < 60; i++)
    run_frame(1);
  check(writer.lost);
  /* The frame in which the reader catches up is still lost; the ones after
     it are whole. */
  for(i = 0; i < 5; i++)
    done += caught_up(run_frame(1));
  check(writer.resends == 3);
  check(done == 4);
}

/* A frame drawn without its first lines, as after a log gap, is not
   reported done; the next whole frame is. */
static void test_frame_with_missing_lines_is_not_done(void)
{
  start();
  scene();
  check(caught_up(run_frame(1)));
  check(!caught_up(run_frame(0)));
  check(caught_up(run_frame(1)));
}

/* The reader starts over with empty memory (the Media Engine woke from
   sleep): the writer notices at V-Blank and the frame after is right. */
static void test_reader_start_over(void)
{
  start();
  scene();
  check(caught_up(run_frame(1)));
  /* This one begins with the resend for the reader's first start. */
  check(caught_up(run_frame(1)));
  memset(&reader.memory, 0, sizeof(reader.memory));
  gba_ppu_log_reader_start(&reader, &mailbox);
  check(!caught_up(run_frame(1)));
  check(caught_up(run_frame(1)));
  check(mailbox.reader.boots == 2);
}

static void test_unknown_word_is_refused(void)
{
  start();
  scene();
  check(caught_up(run_frame(1)));
  ring[writer.write & writer.mask] = 0x77;
  writer.write++;
  gba_ppu_log_line(&writer, 0);
  check(gba_ppu_log_read(&reader) == -1);
  check(mailbox.reader.read == mailbox.writer.write);
}

/* ---- The reader on its own thread, as on the Media Engine ----------- */

static volatile int stop_reader;

static void *reader_thread(void *unused)
{
  (void)unused;
  while(!__atomic_load_n(&stop_reader, __ATOMIC_ACQUIRE))
    if(gba_ppu_log_read(&reader) == 0)
      sched_yield();
  return NULL;
}

static int wait_done(uint32_t number)
{
  struct timespec pause = { 0, 100000 };
  int i;

  for(i = 0; i < 20000; i++)
  {
    if(gba_ppu_log_frame_done(&writer, number))
      return 1;
    nanosleep(&pause, NULL);
  }
  return 0;
}

static void test_frames_match_on_two_threads(void)
{
  pthread_t thread;
  int i;

  start();
  scene();
  stop_reader = 0;
  check(pthread_create(&thread, NULL, reader_thread, NULL) == 0);
  for(i = 0; i < 300; i++)
  {
    check(wait_done(run_frame(1)));
    check(frames_match());
  }
  __atomic_store_n(&stop_reader, 1, __ATOMIC_RELEASE);
  pthread_join(thread, NULL);
}

int main(void)
{
  test_frames_match_on_one_thread();
  test_full_ring_sends_everything_again();
  test_frame_with_missing_lines_is_not_done();
  test_reader_start_over();
  test_unknown_word_is_refused();
  test_frames_match_on_two_threads();
  printf("gba_ppu_log tests passed\n");
  return 0;
}
