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
  /* The Media Engine's cycle counter (half its clock) and the main
     processor's microseconds over the same stretches: its clock rate. */
  u64 me_clock_ticks;
  u64 me_clock_us;
  /* Words written into the log, and how often it was all sent again. */
  u32 log_words;
  u32 resends;
  u32 frames;
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
void video_me_counters(VideoMeCounters *counters);
/* A processor exception on the Media Engine: cause and address, or 0. */
void video_me_fault(u32 *cause, u32 *address);

#endif
