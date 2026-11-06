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
 * The Exact renderer on the Media Engine, from the emulator's side.
 *
 * The first time Exact is the renderer, media_engine.prx is loaded next to
 * the EBOOT and started with the write log's mailbox; it boots the Media
 * Engine with gba_ppu, which then draws from the log (gba_ppu_log.h). From
 * then on the emulator only fills the log: register writes as the game
 * makes them, written video memory at each line, the line marks and
 * V-Blank. The picture is presented as soon as the Media Engine reports it
 * whole, in the first lines of V-Blank; one not finished by the end of
 * V-Blank is skipped and the last one shown again, counted as late. The
 * emulator never waits for the Media Engine.
 *
 * Where the module cannot load (official firmware), refuses to start (an
 * emulator), the Media Engine never answers, stops answering, faults or
 * keeps falling behind, Exact draws on the main processor again, as before.
 */

#include "common.h"
#include "gba_ppu_log.h"
#include "video_me.h"

/* The handshake may take this long, in 5 ms steps. */
#define START_WAIT_STEPS 50
/* Frames with nothing read from a log that was not empty, or presented
   late in a row, before the Media Engine is given up on. */
#define STALLED_FRAMES   60
#define LATE_FRAMES      300
/* Every this many frames the main processor's time in the hooks is
   measured, and the Media Engine's busy time converted to microseconds. */
#define SAMPLE_FRAMES    16
#define BUSY_FRAMES      60
#define UNCACHED(pointer) ((void *)(0x40000000u | (u32)(pointer)))

/* media_engine.prx, through media_engine_stub.S. */
void media_engine_wake(void);
void media_engine_publish(u32 write);

typedef char VideoMeDirtyMapCheck[
 sizeof(video_dirty) == GBA_PPU_LOG_BLOCKS &&
 sizeof(video_dirty_groups) == GBA_PPU_LOG_GROUP_BYTES ? 1 : -1];

/* The log's memory, allocated when the Media Engine starts. Only the ring
   is ever touched through the cache, and only by the writer. */
typedef struct
{
  GbaPpuLogMailbox mailbox;
  u16 frame[GBA_PPU_LOG_FRAME_STRIDE * GBA_SCREEN_HEIGHT];
  u32 ring[GBA_PPU_LOG_RING_WORDS];
} VideoMeMemory;

static VideoMeMemory *memory;
static GbaPpuLogMailbox *mailbox;
static GbaPpuLogWriter writer;
static VideoMeState state;
static u32 switches;
static SceUID module = -1;
static int drawing;
static u32 present_frame;
/* The write count last handed to media_engine_publish. */
static u32 sram_written;
static VideoMeCounters counters;

/* VIDEO_ME_DIRECT_1X: three whole pictures, the GBA's in the middle of
   each, as the 1x scaler places it. Targets 1-3 of the log are their
   GBA areas; target 0 is memory->frame, which the GE copies. The target
   of the frame being drawn, and of the one waiting to be presented. */
#define DIRECT_PICTURES 3
#define DIRECT_ORIGIN \
 (((PSP_SCREEN_HEIGHT - GBA_SCREEN_HEIGHT) / 2) * PSP_LINE_SIZE + \
 (PSP_SCREEN_WIDTH - GBA_SCREEN_WIDTH) / 2)
static u16 *direct;
static u32 frame_target;
static u32 present_target;

/* Watchdog. */
static u32 last_read;
static u32 stalled_frames;
static u32 late_in_a_row;

/* Hook timing, on sampled frames. */
static int sampling;
static u32 sample_start;

/* The busy time's conversion: the Media Engine's clock and busy ticks and
   the main processor's time when last converted. */
static u32 busy_clock;
static u32 busy_ticks;
static u32 busy_time;
static u32 busy_boots;
static int busy_known;
/* The Media Engine's clock ticks per ms of the main processor's. */
static u32 ticks_per_ms;

static VideoMeState load_module(void)
{
  SceKernelLMOption option;
  char path[MAX_PATH];
  u32 argument;
  int status = -1, i;

  memset(&option, 0, sizeof(option));
  option.size = sizeof(option);
  option.mpidtext = PSP_MEMORY_PARTITION_KERNEL;
  option.mpiddata = PSP_MEMORY_PARTITION_KERNEL;
  option.access = 1;
  snprintf(path, sizeof(path), "%smedia_engine.prx", main_path);
  module = kuKernelLoadModule(path, 0, &option);
  if(module < 0)
    return VIDEO_ME_NO_MODULE;

  argument = (u32)mailbox;
  if(sceKernelStartModule(module, sizeof(argument), &argument, &status,
     NULL) < 0 || status < 0)
  {
    /* It touched nothing. */
    sceKernelUnloadModule(module);
    module = -1;
    return VIDEO_ME_NO_DRIVER;
  }

  for(i = 0; i < START_WAIT_STEPS; i++)
  {
    if(mailbox->reader.version == GBA_PPU_LOG_VERSION &&
       mailbox->reader.boots != 0)
      return VIDEO_ME_DRAWING;
    sceKernelDelayThread(5000);
  }
  return VIDEO_ME_NO_ANSWER;
}

/* Holds the Media Engine in reset for good; the module stays loaded, as
   it keeps answering the system's events. */
static void give_up(VideoMeState reason)
{
  int status;

  state = reason;
  drawing = 0;
  present_frame = 0;
  if(module >= 0)
    sceKernelStopModule(module, 0, NULL, &status, NULL);
  /* And should that fail, the program stops by itself once it finds the
     log closed. */
  if(mailbox)
    mailbox->writer.magic = 0;
}

/* The switches media_engine.cfg turns on (see video_me.h); none without
   it. */
static u32 read_switches(void)
{
  char path[MAX_PATH];
  char text[16];
  char *end;
  unsigned long value;
  SceUID file;
  int bytes;

  snprintf(path, sizeof(path), "%smedia_engine.cfg", main_path);
  file = sceIoOpen(path, PSP_O_RDONLY, 0);
  if(file < 0)
    return 0;
  bytes = sceIoRead(file, text, sizeof(text) - 1);
  sceIoClose(file);
  if(bytes <= 0)
    return 0;
  text[bytes] = '\0';
  /* Decimal, or hex after 0x (base 0 would read a leading 0 as octal). */
  value = strtoul(text, &end, 10);
  if(value == 0 && end != text && (*end == 'x' || *end == 'X'))
    value = strtoul(end + 1, NULL, 16);
  return (u32)value & VIDEO_ME_SWITCHES;
}

static void start(void)
{
  switches = read_switches();
  /* The VFPU is only there for threads that ask for it. */
  if((switches & VIDEO_ME_COPY_QUADS) &&
     sceKernelChangeCurrentThreadAttr(0, PSP_THREAD_ATTR_VFPU) < 0)
    switches &= ~VIDEO_ME_COPY_QUADS;
  memory = (VideoMeMemory *)memalign(64, sizeof(*memory));
  if(!memory)
  {
    state = VIDEO_ME_NO_MEMORY;
    return;
  }
  /* The Media Engine writes the mailbox and the picture straight into
     memory: no line of them may be left in this processor's cache. */
  sceKernelDcacheWritebackInvalidateRange(memory, sizeof(*memory));
  mailbox = (GbaPpuLogMailbox *)UNCACHED(&memory->mailbox);
  gba_ppu_log_writer_init(&writer, mailbox, memory->ring, memory->frame,
   palette_ram, oam_ram, (u16 *)vram, io_registers, video_dirty,
   (u8 *)video_dirty_groups);
  writer.copy_quads = (switches & VIDEO_ME_COPY_QUADS) != 0;
  if(switches & VIDEO_ME_SLEEP)
    mailbox->writer.wait = GBA_PPU_LOG_WAIT_SLEEP |
     (switches & VIDEO_ME_SLEEP_CLEAR ? GBA_PPU_LOG_WAIT_CLEAR : 0);
  if(switches & VIDEO_ME_SRAM_FLAG)
    mailbox->writer.wait |= GBA_PPU_LOG_WAIT_SRAM;
  state = load_module();
  if(state != VIDEO_ME_DRAWING)
    give_up(state);
}

/* The Media Engine's busy ticks over the last stretch, as a share of its
   clock ticks, times the main processor's time over the same stretch. A
   stretch longer than a few seconds (the menu was open) is skipped: the
   clock goes round in 25 s at 333 MHz. So is one in which the Media
   Engine started again (after a sleep), with its clock and count new. */
static void convert_busy_time(void)
{
  u32 now = sceKernelGetSystemTimeLow();
  u32 clock = mailbox->reader.clock;
  u32 ticks = mailbox->reader.busy;
  u32 boots = mailbox->reader.boots;

  if(busy_known && boots == busy_boots && clock != busy_clock &&
     now - busy_time < 5000000u)
  {
    counters.me_busy_us += (u64)(ticks - busy_ticks) *
     (now - busy_time) / (clock - busy_clock);
    ticks_per_ms = (u32)((u64)(clock - busy_clock) * 1000 /
     (now - busy_time));
  }
  busy_clock = clock;
  busy_ticks = ticks;
  busy_time = now;
  busy_boots = boots;
  busy_known = 1;
}

/* Whether the Media Engine is still drawing; gives up on it if not. */
static int healthy(void)
{
  u32 read = mailbox->reader.read;

  if(mailbox->reader.fault[0])
  {
    give_up(VIDEO_ME_FAULT);
    return 0;
  }
  if(read != last_read || read == writer.published)
    stalled_frames = 0;
  else if(++stalled_frames >= STALLED_FRAMES)
  {
    give_up(VIDEO_ME_STOPPED);
    return 0;
  }
  last_read = read;
  if(late_in_a_row >= LATE_FRAMES)
  {
    give_up(VIDEO_ME_TOO_SLOW);
    return 0;
  }
  return 1;
}

int video_me_frame_begin(int exact)
{
  if(!exact)
  {
    drawing = 0;
    return 0;
  }
  if(state == VIDEO_ME_NOT_STARTED)
    start();
  if(state != VIDEO_ME_DRAWING || !healthy())
    return 0;
  /* It read nothing since the last frame began: the main processor draws
     until it caught up with what it was sent, as a live one does at
     once, while healthy() keeps counting towards giving up on it. */
  if((switches & VIDEO_ME_QUICK_FALLBACK) && stalled_frames)
  {
    drawing = 0;
    counters.fallback_frames++;
    return 0;
  }
  /* The Media Engine missed what was written while it was not used; and
     the picture it drew into last may be on the display now, so this
     frame goes to the GE. */
  if(!drawing)
  {
    gba_ppu_log_resend(&writer);
    gba_ppu_log_target(&writer, 0, GBA_PPU_LOG_FRAME_STRIDE);
    frame_target = 0;
  }
  drawing = 1;
  counters.frames++;
  sampling = counters.frames % SAMPLE_FRAMES == 0;
  if(sampling)
    counters.log_frames++;
  if(counters.frames % BUSY_FRAMES == 0)
    convert_busy_time();
  return 1;
}

int video_me_drawing(void)
{
  return drawing;
}

static void sample_begin(void)
{
  if(sampling)
    sample_start = sceKernelGetSystemTimeLow();
}

static void sample_end(void)
{
  if(sampling)
    counters.log_us += sceKernelGetSystemTimeLow() - sample_start;
}

/* After publishing: the copy of the write count in the boot page moves
   on (VIDEO_ME_SRAM_FLAG), and a Media Engine that went to sleep with
   everything read is woken (VIDEO_ME_SLEEP). */
static void tell_reader(void)
{
  if((switches & VIDEO_ME_SRAM_FLAG) && writer.published != sram_written)
  {
    sram_written = writer.published;
    media_engine_publish(sram_written);
  }
  if(!(switches & VIDEO_ME_SLEEP))
    return;
  /* Pairs with sleep_for_words on the Media Engine, which sets sleeping,
     syncs, then reads write. publish() just stored write: without this
     sync it may still sit in the write buffer when sleeping is read, so
     the Media Engine finds no new words and halts while this side reads
     sleeping as 0 and does not wake it. */
  __asm__ volatile("sync" ::: "memory");
  if(mailbox->reader.sleeping)
  {
    media_engine_wake();
    counters.wakes++;
  }
}

void video_me_line(u32 y)
{
  sample_begin();
  gba_ppu_log_line(&writer, y);
  tell_reader();
  sample_end();
}

void video_me_io(u32 offset)
{
  gba_ppu_log_io(&writer, offset, io_registers[offset >> 1]);
}

static const u16 *direct_picture(u32 target)
{
  return direct + (target - 1) * PSP_LINE_SIZE * PSP_SCREEN_HEIGHT;
}

/* The three pictures, black, written back; gives the switch up if there
   is no memory for them. */
static int allocate_direct(void)
{
  u32 bytes = DIRECT_PICTURES * PSP_LINE_SIZE * PSP_SCREEN_HEIGHT * 2;
  u32 i;

  direct = (u16 *)memalign(64, bytes);
  if(!direct)
  {
    switches &= ~VIDEO_ME_DIRECT_1X;
    return 0;
  }
  memset(direct, 0, bytes);
  sceKernelDcacheWritebackInvalidateRange(direct, bytes);
  for(i = 1; i <= DIRECT_PICTURES; i++)
    mailbox->writer.targets[i] =
     (u16 *)direct_picture(i) + DIRECT_ORIGIN;
  return 1;
}

/* Where the frame that begins is drawn: a picture the display does not
   read and that does not hold the frame still waiting to be presented,
   or memory->frame for the GE. */
static u32 choose_target(void)
{
  u32 target;

  if(!(switches & VIDEO_ME_DIRECT_1X) || option_screen_scale != SCALED_NONE ||
     psp_fps_debug || (!direct && !allocate_direct()))
    return 0;
  for(target = 1; target <= DIRECT_PICTURES; target++)
    if(video_direct_free(direct_picture(target)) &&
       !(present_frame && target == present_target))
      return target;
  return 0;
}

void video_me_vblank(void)
{
  u32 target;

  sample_begin();
  gba_ppu_log_vblank(&writer);
  target = choose_target();
  if(target != frame_target)
    gba_ppu_log_target(&writer, target,
     target ? PSP_LINE_SIZE : GBA_PPU_LOG_FRAME_STRIDE);
  frame_target = target;
  tell_reader();
  sample_end();
}

void video_me_stop(void)
{
  if(module >= 0)
    give_up(state);
}

void video_me_resend(void)
{
  if(state == VIDEO_ME_DRAWING)
    gba_ppu_log_resend(&writer);
}

void video_me_present(void)
{
  present_frame = gba_ppu_log_frame(&writer);
  present_target = frame_target;
  video_me_present_poll();
}

void video_me_present_poll(void)
{
  if(!present_frame || !gba_ppu_log_frame_done(&writer, present_frame))
    return;
  present_frame = 0;
  late_in_a_row = 0;
  if(present_target)
  {
    video_show_direct(direct_picture(present_target));
    counters.direct_frames++;
    return;
  }
  video_texture_from(memory->frame,
   (switches & VIDEO_ME_NO_CACHE_FLUSH) != 0);
  (*update_screen)();
}

void video_me_present_finish(void)
{
  if(!present_frame)
    return;
  present_frame = 0;
  counters.late_frames++;
  late_in_a_row++;
  /* The last picture again, from where it is shown. */
  if(!video_show_direct_again())
    (*update_screen)();
}

VideoMeState video_me_state(void)
{
  return state;
}

u32 video_me_switches(void)
{
  return switches;
}

void video_me_counters(VideoMeCounters *out)
{
  u32 i;

  *out = counters;
  if(mailbox)
  {
    out->me_frames = mailbox->reader.frames_drawn;
    for(i = 0; i < GBA_PPU_LOG_TIME_BUCKETS; i++)
      out->frame_time[i] = mailbox->reader.frame_time[i];
  }
  out->log_words = writer.write;
  out->resends = writer.resends;
  video_present_time(&out->ge_presents, &out->ge_us);
}

void video_me_frame_times(const VideoMeCounters *since, u32 *median_us,
 u32 *p99_us, u32 *max_us)
{
  VideoMeCounters now;
  u32 frame_time[GBA_PPU_LOG_TIME_BUCKETS];
  u32 i;

  video_me_counters(&now);
  for(i = 0; i < GBA_PPU_LOG_TIME_BUCKETS; i++)
    frame_time[i] = now.frame_time[i] - since->frame_time[i];
  *median_us = *p99_us = *max_us = 0;
  if(!ticks_per_ms)
    return;
  *median_us = (u32)((u64)gba_ppu_log_time_within(frame_time, 500) * 1000 /
   ticks_per_ms);
  *p99_us = (u32)((u64)gba_ppu_log_time_within(frame_time, 990) * 1000 /
   ticks_per_ms);
  *max_us = (u32)((u64)gba_ppu_log_time_within(frame_time, 1000) * 1000 /
   ticks_per_ms);
}

void video_me_fault(u32 *cause, u32 *address)
{
  *cause = mailbox ? mailbox->reader.fault[0] : 0;
  *address = mailbox ? mailbox->reader.fault[1] : 0;
}
