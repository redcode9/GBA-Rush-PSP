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
static SceUID module = -1;
static int drawing;
static u32 present_frame;
static VideoMeCounters counters;

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

static void start(void)
{
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
  state = load_module();
  if(state != VIDEO_ME_DRAWING)
    give_up(state);
}

/* The Media Engine's busy ticks over the last stretch, as a share of its
   clock ticks, times the main processor's time over the same stretch. A
   stretch longer than a few seconds (the menu was open) is skipped: the
   clock goes round in 25 s at 333 MHz. So is one across a reboot (its
   clock restarts on resume) or a clock change. */
static void convert_busy_time(void)
{
  u32 now = sceKernelGetSystemTimeLow();
  u32 clock = mailbox->reader.clock;
  u32 ticks = mailbox->reader.busy;
  u32 boots = mailbox->reader.boots;

  /* After a clock change, wait until it has finished some work: its clock
     from before then (the menu was open) would add the gap to a stretch. */
  if(!busy_known && clock == busy_clock)
    return;
  if(busy_known && boots == busy_boots && clock != busy_clock &&
     now - busy_time < 5000000u)
  {
    counters.me_busy_us += (u64)(ticks - busy_ticks) *
     (now - busy_time) / (clock - busy_clock);
    counters.me_clock_ticks += clock - busy_clock;
    counters.me_clock_us += now - busy_time;
  }
  busy_clock = clock;
  busy_ticks = ticks;
  busy_time = now;
  busy_boots = boots;
  busy_known = 1;
}

void video_me_clock_changed(void)
{
  busy_known = 0;
  if(mailbox)
    busy_clock = mailbox->reader.clock;
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
  /* The Media Engine missed what was written while it was not used. */
  if(!drawing)
    gba_ppu_log_resend(&writer);
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

void video_me_line(u32 y)
{
  sample_begin();
  gba_ppu_log_line(&writer, y);
  sample_end();
}

void video_me_io(u32 offset)
{
  gba_ppu_log_io(&writer, offset, io_registers[offset >> 1]);
}

void video_me_vblank(void)
{
  sample_begin();
  gba_ppu_log_vblank(&writer);
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
  video_me_present_poll();
}

void video_me_present_poll(void)
{
  if(!present_frame || !gba_ppu_log_frame_done(&writer, present_frame))
    return;
  present_frame = 0;
  late_in_a_row = 0;
  video_texture_from(memory->frame);
  (*update_screen)();
}

void video_me_present_finish(void)
{
  if(!present_frame)
    return;
  present_frame = 0;
  counters.late_frames++;
  late_in_a_row++;
  (*update_screen)();
}

VideoMeState video_me_state(void)
{
  return state;
}

void video_me_counters(VideoMeCounters *out)
{
  *out = counters;
  if(mailbox)
    out->me_frames = mailbox->reader.frames_drawn;
  out->log_words = writer.write;
  out->resends = writer.resends;
}

void video_me_fault(u32 *cause, u32 *address)
{
  *cause = mailbox ? mailbox->reader.fault[0] : 0;
  *address = mailbox ? mailbox->reader.fault[1] : 0;
}
