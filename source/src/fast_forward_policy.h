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

#ifndef FAST_FORWARD_POLICY_H
#define FAST_FORWARD_POLICY_H

#include <stdint.h>

typedef enum
{
  FAST_FORWARD_PROFILE_MODERN,
  FAST_FORWARD_PROFILE_CLASSIC,
  FAST_FORWARD_PROFILE_CUSTOM,
  FAST_FORWARD_PROFILE_COUNT
} FastForwardProfile;

typedef enum
{
  FAST_FORWARD_ACTIVATION_HOLD,
  FAST_FORWARD_ACTIVATION_TOGGLE,
  FAST_FORWARD_ACTIVATION_COUNT
} FastForwardActivation;

typedef enum
{
  FAST_FORWARD_SPEED_2X,
  FAST_FORWARD_SPEED_3X,
  FAST_FORWARD_SPEED_MAXIMUM,
  FAST_FORWARD_SPEED_COUNT
} FastForwardSpeed;

typedef enum
{
  FAST_FORWARD_PRESENTATION_SMOOTH,
  FAST_FORWARD_PRESENTATION_BALANCED,
  FAST_FORWARD_PRESENTATION_PERFORMANCE,
  FAST_FORWARD_PRESENTATION_COUNT
} FastForwardPresentation;

typedef enum
{
  FAST_FORWARD_AUDIO_SILENT,
  FAST_FORWARD_AUDIO_ACCELERATED,
  FAST_FORWARD_AUDIO_COUNT
} FastForwardAudio;

typedef struct
{
  FastForwardProfile profile;
  FastForwardActivation activation;
  FastForwardSpeed speed;
  FastForwardPresentation presentation;
  FastForwardAudio audio;
} FastForwardSettings;

typedef struct
{
  uint32_t present;
  uint32_t delay_us;
} FastForwardFrameDecision;

typedef struct
{
  FastForwardSettings settings;
  uint32_t active;
  uint32_t held;
  uint32_t classic_step;
  uint32_t force_present;
  uint32_t pacing_ready;
  uint32_t presentation_ready;
  uint32_t frame_phase;
  uint32_t next_deadline_us;
  uint32_t next_present_us;
  uint64_t pacing_remainder;
  uint64_t transition_count;
  uint64_t audio_fallback_count;
  uint32_t audio_fallback;
} FastForwardPolicy;

void fast_forward_settings_defaults(FastForwardSettings *settings);
void fast_forward_settings_sanitize(FastForwardSettings *settings);
void fast_forward_policy_init(FastForwardPolicy *policy,
 const FastForwardSettings *settings);
void fast_forward_policy_configure(FastForwardPolicy *policy,
 const FastForwardSettings *settings);
void fast_forward_policy_input(FastForwardPolicy *policy, int pressed,
 int held);
void fast_forward_policy_stop(FastForwardPolicy *policy);
void fast_forward_policy_audio_fallback(FastForwardPolicy *policy);
FastForwardFrameDecision fast_forward_policy_frame(FastForwardPolicy *policy,
 uint32_t now_us);
int fast_forward_policy_take_force_present(FastForwardPolicy *policy);
int fast_forward_policy_active(const FastForwardPolicy *policy);
FastForwardSpeed fast_forward_policy_speed(const FastForwardPolicy *policy);
FastForwardAudio fast_forward_policy_audio(const FastForwardPolicy *policy);
uint32_t fast_forward_policy_speed_multiplier(
 const FastForwardPolicy *policy);

#endif /* FAST_FORWARD_POLICY_H */
