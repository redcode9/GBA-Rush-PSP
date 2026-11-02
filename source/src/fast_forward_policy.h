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
 * Fast-forward decisions, free of PSP calls so the host tests can drive
 * them. From the button state and a microsecond clock the policy decides
 * whether fast-forward is on, which frames are shown and how long to sleep
 * to hold a 2x or 3x speed; main.c applies the decisions.
 */

#ifndef FAST_FORWARD_POLICY_H
#define FAST_FORWARD_POLICY_H

#include <stdint.h>

/* MODERN: hold for maximum speed. CLASSIC: each press steps off, 2x, 3x.
   CUSTOM: the activation, speed, presentation and audio settings below. */
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

/* How many frames are shown: about 60, 30 or 15 per second. */
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

/* For the next frame: whether to show it, and how long to sleep first. */
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
  uint32_t classic_step;        /* CLASSIC: 0 off, 1 2x, 2 3x */
  uint32_t force_present;       /* show the next frame, restart pacing */
  uint32_t pacing_ready;        /* next_deadline_us is set */
  uint32_t presentation_ready;  /* next_present_us is set */
  uint32_t frame_phase;         /* CLASSIC: frames since the last shown */
  uint32_t next_deadline_us;    /* speed limiter: when the frame is due */
  uint32_t next_present_us;
  uint64_t pacing_remainder;    /* fraction of a microsecond carried */
  uint64_t transition_count;
  uint64_t audio_fallback_count;
  /* Accelerated audio failed; stay silent until fast-forward restarts. */
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
/* Returns 1 once after a change that needs the next frame shown; main.c
   then restarts normal frame pacing. */
int fast_forward_policy_take_force_present(FastForwardPolicy *policy);
int fast_forward_policy_active(const FastForwardPolicy *policy);
/* Speed in effect: CLASSIC from its step, MODERN always MAXIMUM, CUSTOM as
   set. MAXIMUM too while inactive, so check fast_forward_policy_active(). */
FastForwardSpeed fast_forward_policy_speed(const FastForwardPolicy *policy);
FastForwardAudio fast_forward_policy_audio(const FastForwardPolicy *policy);
/* 2 or 3 for a limited speed; 0 when unlimited or inactive. */
uint32_t fast_forward_policy_speed_multiplier(
 const FastForwardPolicy *policy);

#endif /* FAST_FORWARD_POLICY_H */
