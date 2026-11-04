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
 * The Exact renderer on the Media Engine, from the emulator's side (see
 * video_me.c). video_dispatch.c calls in when Exact is the renderer;
 * memory.c and mips_stub.S mark the video memory the game writes in
 * video_dirty (memory.h).
 */

#ifndef VIDEO_ME_H
#define VIDEO_ME_H

#include "gba_ppu_log.h"

/* Whether the Exact renderer draws on the Media Engine, and if not, why;
   the runtime log and the FPS display report it. Values are kept. */
typedef enum
{
  VIDEO_ME_NOT_STARTED = 0,
  VIDEO_ME_DRAWING,
  /* media_engine.prx did not load: official firmware, no file. */
  VIDEO_ME_NO_MODULE,
  /* It refused to start: Sony's Media Engine driver was not there, as in
     an emulator. */
  VIDEO_ME_NO_DRIVER,
  /* The Media Engine never answered. */
  VIDEO_ME_NO_ANSWER,
  /* It stopped drawing, took an exception, or fell too far behind. */
  VIDEO_ME_STOPPED,
  VIDEO_ME_FAULT,
  VIDEO_ME_TOO_SLOW,
  VIDEO_ME_NO_MEMORY
} VideoMeState;

/* Switches for the ways of using the Media Engine still to be proven on a
   console, each off unless media_engine.cfg next to the EBOOT turns it on:
   that file holds one number (decimal, or hex after 0x), the sum of the
   switches wanted. It is read once, when the Media Engine starts, and the
   runtime log records what was on. */
/* Written video blocks go into the log with VFPU quadword copies. */
#define VIDEO_ME_COPY_QUADS        0x01u
/* A frame the Media Engine drew, following another, is presented without
   writing the whole data cache back first: the main processor wrote
   nothing the GE reads, and the log writes back its own lines. */
#define VIDEO_ME_NO_CACHE_FLUSH    0x02u
/* A frame begins and the Media Engine read nothing of the last one: the
   main processor draws frames until it caught up, instead of showing the
   last picture again for up to a second before giving up on it. */
#define VIDEO_ME_QUICK_FALLBACK    0x04u
/* Once idle for a while, the Media Engine halts until the main processor
   interrupts it after publishing more of the log, instead of polling the
   mailbox in main memory. */
#define VIDEO_ME_SLEEP             0x08u
/* At 1x scale, with Show FPS off, the Media Engine draws into whole
   480x272 pictures in main memory that the display shows as they are,
   without the GE; the HOLD and fast-forward marks are not drawn over
   them. Takes 816 KiB for three pictures. */
#define VIDEO_ME_DIRECT_1X         0x10u
/* With VIDEO_ME_SLEEP: after each wake the Media Engine clears its
   interrupt flags, as its start-up code does; without it a wake that
   stays pending turns the halt back into a poll every 25 us. */
#define VIDEO_ME_SLEEP_CLEAR       0x20u
#define VIDEO_ME_SWITCHES          0x3Fu

typedef struct
{
  /* Frames the Media Engine drew whole, and the frames presented again
     because it had not finished in time. */
  u32 me_frames;
  u32 late_frames;
  /* Time the Media Engine spent drawing, and the main processor's time in
     the line and V-Blank hooks over the frames sampled for it. */
  u64 me_busy_us;
  u64 log_us;
  u32 log_frames;
  /* Words written into the log, and how often it was all sent again. */
  u32 log_words;
  u32 resends;
  u32 frames;
  /* Frames drawn on the main processor because the Media Engine did not
     answer (VIDEO_ME_QUICK_FALLBACK), and the times it was woken from its
     halt (VIDEO_ME_SLEEP). */
  u32 fallback_frames;
  u32 wakes;
  /* Frames shown straight from main memory (VIDEO_ME_DIRECT_1X), and the
     frame presents of the GE with its time on them. */
  u32 direct_frames;
  u32 ge_presents;
  u64 ge_us;
  /* Frames the Media Engine finished, by its working time on each. */
  u32 frame_time[GBA_PPU_LOG_TIME_BUCKETS];
} VideoMeCounters;

/* At the start of each frame: whether the Media Engine draws it, starting
   the Media Engine the first time Exact is chosen. */
int video_me_frame_begin(int exact);
/* The frame being emulated goes to the Media Engine. */
int video_me_drawing(void);
void video_me_line(u32 y);
void video_me_io(u32 offset);
void video_me_vblank(void);
/* Everything goes again at the next line. */
void video_me_resend(void);
/* Leaving the game: holds the Media Engine in reset. */
void video_me_stop(void);

/* The picture of the frame that just ended: presented once the Media
   Engine finished it, polled at every V-Blank line, and at the end of
   V-Blank the last one is presented again if it did not. */
void video_me_present(void);
void video_me_present_poll(void);
void video_me_present_finish(void);

VideoMeState video_me_state(void);
/* The switches in use, VIDEO_ME_*. */
u32 video_me_switches(void);
void video_me_counters(VideoMeCounters *counters);
/* The Media Engine's working time on the frames it finished since since:
   what half of them, 99% of them and all of them stayed within, in us
   (0 while its clock rate is not known yet). */
void video_me_frame_times(const VideoMeCounters *since, u32 *median_us,
 u32 *p99_us, u32 *max_us);
/* A processor exception on the Media Engine: cause and address, or 0. */
void video_me_fault(u32 *cause, u32 *address);

#endif
