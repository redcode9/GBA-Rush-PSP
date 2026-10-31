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

#include "video_adaptive.h"

#include <stddef.h>
#include <string.h>

/* A GBA frame lasts about 16743 us (59.73 Hz). A window is bad if its 95th
 * percentile frame time or GE wait exceeds the BAD limit, or if any frame
 * was skipped or an audio event reported; good if all stay under GOOD.
 * Two bad windows in a row drop to CRISP, five good ones restore SHARP. */
#define VIDEO_ADAPTIVE_BAD_FRAME_US 17200U
#define VIDEO_ADAPTIVE_GOOD_FRAME_US 16900U
#define VIDEO_ADAPTIVE_BAD_GE_WAIT_US 2500U
#define VIDEO_ADAPTIVE_GOOD_GE_WAIT_US 1000U
#define VIDEO_ADAPTIVE_BAD_WINDOWS 2U
#define VIDEO_ADAPTIVE_GOOD_WINDOWS 5U

/* Insertion sort on a copy; 120 samples is small enough. */
static uint32_t video_adaptive_percentile95(const uint32_t *values)
{
  uint32_t sorted[VIDEO_ADAPTIVE_WINDOW_SIZE];
  uint32_t i;

  memcpy(sorted, values, sizeof(sorted));

  for(i = 1; i < VIDEO_ADAPTIVE_WINDOW_SIZE; i++)
  {
    uint32_t value = sorted[i];
    uint32_t position = i;

    while(position > 0 && sorted[position - 1] > value)
    {
      sorted[position] = sorted[position - 1];
      position--;
    }

    sorted[position] = value;
  }

  return sorted[((VIDEO_ADAPTIVE_WINDOW_SIZE * 95U) / 100U) - 1U];
}

static void video_adaptive_clear_window(VideoAdaptiveState *state)
{
  state->sample_count = 0;
  state->skipped_frames = 0;
  state->audio_events = 0;
}

void video_adaptive_init(VideoAdaptiveState *state, int enabled)
{
  if(state == NULL)
    return;

  memset(state, 0, sizeof(*state));
  state->quality = VIDEO_ADAPTIVE_QUALITY_SHARP;
  state->reason = VIDEO_ADAPTIVE_REASON_INITIAL;
  state->enabled = enabled != 0;
}

void video_adaptive_reset(VideoAdaptiveState *state)
{
  int enabled;
  VideoAdaptiveQuality quality;

  if(state == NULL)
    return;

  enabled = state->enabled;
  quality = state->quality;
  video_adaptive_init(state, enabled);
  if(!enabled)
    state->quality = quality;
  state->reason = VIDEO_ADAPTIVE_REASON_RESET;
}

void video_adaptive_set_enabled(VideoAdaptiveState *state, int enabled,
 VideoAdaptiveQuality manual_quality)
{
  if(state == NULL)
    return;

  video_adaptive_clear_window(state);
  state->bad_windows = 0;
  state->good_windows = 0;
  state->enabled = enabled != 0;

  if(state->enabled)
  {
    state->quality = VIDEO_ADAPTIVE_QUALITY_SHARP;
    state->reason = VIDEO_ADAPTIVE_REASON_INITIAL;
  }
  else
  {
    if(manual_quality > VIDEO_ADAPTIVE_QUALITY_SMOOTH)
      manual_quality = VIDEO_ADAPTIVE_QUALITY_SMOOTH;
    state->quality = manual_quality;
    state->reason = VIDEO_ADAPTIVE_REASON_MANUAL;
  }
}

static VideoAdaptiveReason video_adaptive_bad_reason(
 const VideoAdaptiveState *state)
{
  if(state->audio_events != 0)
    return VIDEO_ADAPTIVE_REASON_AUDIO;
  if(state->skipped_frames != 0)
    return VIDEO_ADAPTIVE_REASON_FRAME_SKIP;
  if(state->last_frame_p95_us > VIDEO_ADAPTIVE_BAD_FRAME_US)
    return VIDEO_ADAPTIVE_REASON_FRAME_TIME;
  return VIDEO_ADAPTIVE_REASON_GE_WAIT;
}

static int video_adaptive_evaluate_window(VideoAdaptiveState *state)
{
  int bad;
  int good;

  state->last_frame_p95_us =
   video_adaptive_percentile95(state->frame_time_us);
  state->last_ge_wait_p95_us =
   video_adaptive_percentile95(state->ge_wait_us);

  bad = state->last_frame_p95_us > VIDEO_ADAPTIVE_BAD_FRAME_US ||
   state->last_ge_wait_p95_us > VIDEO_ADAPTIVE_BAD_GE_WAIT_US ||
   state->skipped_frames != 0 || state->audio_events != 0;
  good = state->last_frame_p95_us <= VIDEO_ADAPTIVE_GOOD_FRAME_US &&
   state->last_ge_wait_p95_us <= VIDEO_ADAPTIVE_GOOD_GE_WAIT_US &&
   state->skipped_frames == 0 && state->audio_events == 0;

  if(bad)
  {
    state->bad_windows++;
    state->good_windows = 0;
  }
  else if(good)
  {
    state->good_windows++;
    state->bad_windows = 0;
  }
  else
  {
    state->bad_windows = 0;
    state->good_windows = 0;
  }

  if(state->quality == VIDEO_ADAPTIVE_QUALITY_SHARP &&
     state->bad_windows >= VIDEO_ADAPTIVE_BAD_WINDOWS)
  {
    state->quality = VIDEO_ADAPTIVE_QUALITY_CRISP;
    state->reason = video_adaptive_bad_reason(state);
    state->bad_windows = 0;
    state->good_windows = 0;
    return 1;
  }

  if(state->quality == VIDEO_ADAPTIVE_QUALITY_CRISP &&
     state->good_windows >= VIDEO_ADAPTIVE_GOOD_WINDOWS)
  {
    state->quality = VIDEO_ADAPTIVE_QUALITY_SHARP;
    state->reason = VIDEO_ADAPTIVE_REASON_RECOVERED;
    state->bad_windows = 0;
    state->good_windows = 0;
    return 1;
  }

  return 0;
}

int video_adaptive_record_frame(VideoAdaptiveState *state,
 uint32_t frame_time_us, uint32_t ge_wait_us, int frame_skipped,
 int audio_event)
{
  int changed;

  if(state == NULL || !state->enabled)
    return 0;

  state->frame_time_us[state->sample_count] = frame_time_us;
  state->ge_wait_us[state->sample_count] = ge_wait_us;
  state->sample_count++;

  if(frame_skipped)
    state->skipped_frames++;
  if(audio_event)
    state->audio_events++;

  if(state->sample_count < VIDEO_ADAPTIVE_WINDOW_SIZE)
    return 0;

  changed = video_adaptive_evaluate_window(state);
  video_adaptive_clear_window(state);
  return changed;
}

void video_adaptive_export_profile(const VideoAdaptiveState *state,
 VideoAdaptiveProfile *profile, uint32_t rom_crc32, uint32_t hardware_model,
 uint32_t clock_mhz)
{
  if(state == NULL || profile == NULL)
    return;

  profile->version = VIDEO_ADAPTIVE_PROFILE_VERSION;
  profile->rom_crc32 = rom_crc32;
  profile->hardware_model = hardware_model;
  profile->clock_mhz = clock_mhz;
  profile->quality = (uint32_t)state->quality;
}

int video_adaptive_apply_profile(VideoAdaptiveState *state,
 const VideoAdaptiveProfile *profile, uint32_t rom_crc32,
 uint32_t hardware_model, uint32_t clock_mhz)
{
  if(state == NULL || profile == NULL || !state->enabled)
    return 0;

  if(profile->version != VIDEO_ADAPTIVE_PROFILE_VERSION ||
     profile->rom_crc32 != rom_crc32 ||
     profile->hardware_model != hardware_model ||
     profile->clock_mhz != clock_mhz ||
     profile->quality > VIDEO_ADAPTIVE_QUALITY_CRISP)
    return 0;

  state->quality = (VideoAdaptiveQuality)profile->quality;
  state->reason = VIDEO_ADAPTIVE_REASON_INITIAL;
  state->bad_windows = 0;
  state->good_windows = 0;
  video_adaptive_clear_window(state);
  return 1;
}
