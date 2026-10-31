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

/* Quality governor for the "16:9 auto" screen scale (SCALED_16X9_AUTO).
 * psp_video.c feeds it one sample per frame; it steps down from SHARP to
 * CRISP when frames run late and back up once they recover. Plain C with no
 * PSP calls so it can be host tested (tests/test_video_adaptive.c).
 */

#ifndef VIDEO_ADAPTIVE_H
#define VIDEO_ADAPTIVE_H

#include <stdint.h>

/* Frames per evaluation window (about two seconds at 60 Hz). */
#define VIDEO_ADAPTIVE_WINDOW_SIZE 120
/* Bump when VideoAdaptiveProfile changes so stale profiles are ignored. */
#define VIDEO_ADAPTIVE_PROFILE_VERSION 1

/* SHARP: two-pass integer upscale then bilinear (blit_sharp).
 * CRISP: single pass, nearest filter. SMOOTH: single pass, user filter;
 * only reachable as a manual choice. */
typedef enum
{
  VIDEO_ADAPTIVE_QUALITY_SHARP,
  VIDEO_ADAPTIVE_QUALITY_CRISP,
  VIDEO_ADAPTIVE_QUALITY_SMOOTH
} VideoAdaptiveQuality;

typedef enum
{
  VIDEO_ADAPTIVE_REASON_INITIAL,
  VIDEO_ADAPTIVE_REASON_FRAME_TIME,
  VIDEO_ADAPTIVE_REASON_GE_WAIT,
  VIDEO_ADAPTIVE_REASON_FRAME_SKIP,
  VIDEO_ADAPTIVE_REASON_AUDIO,
  VIDEO_ADAPTIVE_REASON_RECOVERED,
  VIDEO_ADAPTIVE_REASON_MANUAL,
  VIDEO_ADAPTIVE_REASON_RESET
} VideoAdaptiveReason;

/* Last chosen quality, valid only for the same ROM, PSP model and clock. */
typedef struct
{
  uint32_t version;
  uint32_t rom_crc32;
  uint32_t hardware_model;
  uint32_t clock_mhz;
  uint32_t quality;
} VideoAdaptiveProfile;

typedef struct
{
  uint32_t frame_time_us[VIDEO_ADAPTIVE_WINDOW_SIZE];
  uint32_t ge_wait_us[VIDEO_ADAPTIVE_WINDOW_SIZE];
  uint32_t sample_count;
  uint32_t skipped_frames;
  uint32_t audio_events;
  uint32_t bad_windows;
  uint32_t good_windows;
  uint32_t last_frame_p95_us;
  uint32_t last_ge_wait_p95_us;
  VideoAdaptiveQuality quality;
  VideoAdaptiveReason reason;
  int enabled;
} VideoAdaptiveState;

void video_adaptive_init(VideoAdaptiveState *state, int enabled);
void video_adaptive_reset(VideoAdaptiveState *state);
void video_adaptive_set_enabled(VideoAdaptiveState *state, int enabled,
 VideoAdaptiveQuality manual_quality);
/* Returns 1 when the quality changed and the presenter must be switched. */
int video_adaptive_record_frame(VideoAdaptiveState *state,
 uint32_t frame_time_us, uint32_t ge_wait_us, int frame_skipped,
 int audio_event);
void video_adaptive_export_profile(const VideoAdaptiveState *state,
 VideoAdaptiveProfile *profile, uint32_t rom_crc32, uint32_t hardware_model,
 uint32_t clock_mhz);
/* Restores a saved SHARP/CRISP choice if the identity fields match. */
int video_adaptive_apply_profile(VideoAdaptiveState *state,
 const VideoAdaptiveProfile *profile, uint32_t rom_crc32,
 uint32_t hardware_model, uint32_t clock_mhz);

#endif /* VIDEO_ADAPTIVE_H */
