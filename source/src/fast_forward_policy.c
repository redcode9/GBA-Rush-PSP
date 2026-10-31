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

#include "fast_forward_policy.h"

#include <stddef.h>
#include <string.h>

/* 228 lines of 1232 cycles: one GBA frame lasts about 16.74 ms. */
#define GBA_FRAME_CYCLES 280896ULL
#define GBA_CLOCK_HZ 16777216ULL
#define MICROSECONDS_PER_SECOND 1000000ULL

static FastForwardSettings sanitized_settings(
 const FastForwardSettings *settings)
{
  FastForwardSettings result;

  fast_forward_settings_defaults(&result);
  if(settings != NULL)
    result = *settings;
  fast_forward_settings_sanitize(&result);
  return result;
}

static void reset_schedule(FastForwardPolicy *policy)
{
  policy->pacing_ready = 0;
  policy->presentation_ready = 0;
  policy->frame_phase = 0;
  policy->pacing_remainder = 0;
}

static void set_active(FastForwardPolicy *policy, int active)
{
  uint32_t next = active != 0;

  if(policy->active == next)
    return;

  policy->active = next;
  policy->transition_count++;
  policy->audio_fallback = 0;
  reset_schedule(policy);
  policy->force_present = 1;
}

void fast_forward_settings_defaults(FastForwardSettings *settings)
{
  if(settings == NULL)
    return;

  settings->profile = FAST_FORWARD_PROFILE_MODERN;
  settings->activation = FAST_FORWARD_ACTIVATION_HOLD;
  settings->speed = FAST_FORWARD_SPEED_MAXIMUM;
  settings->presentation = FAST_FORWARD_PRESENTATION_BALANCED;
  settings->audio = FAST_FORWARD_AUDIO_SILENT;
}

void fast_forward_settings_sanitize(FastForwardSettings *settings)
{
  if(settings == NULL)
    return;

  if((uint32_t)settings->profile >= FAST_FORWARD_PROFILE_COUNT)
    settings->profile = FAST_FORWARD_PROFILE_MODERN;
  if((uint32_t)settings->activation >= FAST_FORWARD_ACTIVATION_COUNT)
    settings->activation = FAST_FORWARD_ACTIVATION_HOLD;
  if((uint32_t)settings->speed >= FAST_FORWARD_SPEED_COUNT)
    settings->speed = FAST_FORWARD_SPEED_MAXIMUM;
  if((uint32_t)settings->presentation >= FAST_FORWARD_PRESENTATION_COUNT)
    settings->presentation = FAST_FORWARD_PRESENTATION_BALANCED;
  if((uint32_t)settings->audio >= FAST_FORWARD_AUDIO_COUNT)
    settings->audio = FAST_FORWARD_AUDIO_SILENT;
}

void fast_forward_policy_init(FastForwardPolicy *policy,
 const FastForwardSettings *settings)
{
  if(policy == NULL)
    return;

  memset(policy, 0, sizeof(*policy));
  policy->settings = sanitized_settings(settings);
  policy->force_present = 1;
}

void fast_forward_policy_configure(FastForwardPolicy *policy,
 const FastForwardSettings *settings)
{
  if(policy == NULL)
    return;

  fast_forward_policy_stop(policy);
  policy->settings = sanitized_settings(settings);
  policy->held = 0;
  policy->classic_step = 0;
}

void fast_forward_policy_input(FastForwardPolicy *policy, int pressed,
 int held)
{
  if(policy == NULL)
    return;

  policy->held = held != 0;

  switch(policy->settings.profile)
  {
    case FAST_FORWARD_PROFILE_MODERN:
      set_active(policy, policy->held);
      break;

    case FAST_FORWARD_PROFILE_CLASSIC:
      if(pressed)
      {
        policy->classic_step = (policy->classic_step + 1) % 3;
        set_active(policy, policy->classic_step != 0);
        if(policy->active)
        {
          reset_schedule(policy);
          policy->force_present = 1;
        }
      }
      break;

    case FAST_FORWARD_PROFILE_CUSTOM:
      if(policy->settings.activation == FAST_FORWARD_ACTIVATION_HOLD)
        set_active(policy, policy->held);
      else if(pressed)
        set_active(policy, !policy->active);
      break;

    default:
      set_active(policy, 0);
      break;
  }
}

void fast_forward_policy_stop(FastForwardPolicy *policy)
{
  if(policy == NULL)
    return;

  set_active(policy, 0);
  policy->held = 0;
  policy->classic_step = 0;
  policy->audio_fallback = 0;
  reset_schedule(policy);
  policy->force_present = 1;
}

void fast_forward_policy_audio_fallback(FastForwardPolicy *policy)
{
  if(policy == NULL || !policy->active || policy->audio_fallback ||
     fast_forward_policy_audio(policy) != FAST_FORWARD_AUDIO_ACCELERATED)
    return;

  policy->audio_fallback = 1;
  policy->audio_fallback_count++;
}

/* MODERN is always unlimited; CLASSIC takes its speed from the step. */
static FastForwardSpeed effective_speed(const FastForwardPolicy *policy)
{
  if(policy->settings.profile == FAST_FORWARD_PROFILE_MODERN)
    return FAST_FORWARD_SPEED_MAXIMUM;
  if(policy->settings.profile == FAST_FORWARD_PROFILE_CLASSIC)
    return policy->classic_step == 1 ? FAST_FORWARD_SPEED_2X :
                                      FAST_FORWARD_SPEED_3X;
  return policy->settings.speed;
}

static uint32_t presentation_interval_us(const FastForwardPolicy *policy)
{
  FastForwardPresentation presentation = policy->settings.presentation;

  if(policy->settings.profile == FAST_FORWARD_PROFILE_MODERN)
    presentation = FAST_FORWARD_PRESENTATION_BALANCED;

  switch(presentation)
  {
    case FAST_FORWARD_PRESENTATION_SMOOTH:
      return 16667;
    case FAST_FORWARD_PRESENTATION_PERFORMANCE:
      return 66667;
    case FAST_FORWARD_PRESENTATION_BALANCED:
    default:
      return 33333;
  }
}

/* Shows at most one frame per interval. The signed differences keep the
   32-bit clock comparisons valid across wraparound; after falling four
   intervals behind it restarts the schedule instead of catching up. */
static uint32_t time_based_presentation(FastForwardPolicy *policy,
 uint32_t now_us)
{
  uint32_t interval = presentation_interval_us(policy);

  if(!policy->presentation_ready)
  {
    policy->presentation_ready = 1;
    policy->next_present_us = now_us + interval;
    return 1;
  }

  if((int32_t)(now_us - policy->next_present_us) < 0)
    return 0;

  policy->next_present_us += interval;
  if((int32_t)(now_us - policy->next_present_us) >=
     (int32_t)(interval * 4U))
    policy->next_present_us = now_us + interval;
  return 1;
}

/* CLASSIC shows every second or third frame; the other profiles show
   frames on a timer. */
static uint32_t should_present(FastForwardPolicy *policy, uint32_t now_us)
{
  uint32_t divisor;

  if(policy->force_present)
  {
    policy->force_present = 0;
    policy->presentation_ready = 0;
    policy->frame_phase = 0;
    return time_based_presentation(policy, now_us);
  }

  if(policy->settings.profile != FAST_FORWARD_PROFILE_CLASSIC)
    return time_based_presentation(policy, now_us);

  divisor = fast_forward_policy_speed_multiplier(policy);
  policy->frame_phase++;
  if(policy->frame_phase < divisor)
    return 0;

  policy->frame_phase = 0;
  return 1;
}

/* Each emulated frame moves the deadline on by one GBA frame time divided
   by the multiplier, carrying the fractional microseconds. Returns how long
   to sleep until the deadline; 0 when running late. */
static uint32_t pace_limited_speed(FastForwardPolicy *policy,
 uint32_t now_us, uint32_t multiplier)
{
  uint64_t numerator = GBA_FRAME_CYCLES * MICROSECONDS_PER_SECOND;
  uint64_t denominator = GBA_CLOCK_HZ * multiplier;
  uint32_t whole = (uint32_t)(numerator / denominator);
  uint64_t fraction = numerator % denominator;

  if(!policy->pacing_ready)
  {
    policy->pacing_ready = 1;
    policy->next_deadline_us = now_us;
    policy->pacing_remainder = 0;
  }

  policy->next_deadline_us += whole;
  policy->pacing_remainder += fraction;
  if(policy->pacing_remainder >= denominator)
  {
    policy->next_deadline_us++;
    policy->pacing_remainder -= denominator;
  }

  if((int32_t)(policy->next_deadline_us - now_us) <= 0)
    return 0;
  return policy->next_deadline_us - now_us;
}

FastForwardFrameDecision fast_forward_policy_frame(FastForwardPolicy *policy,
 uint32_t now_us)
{
  FastForwardFrameDecision decision = { 1, 0 };
  uint32_t multiplier;

  if(policy == NULL || !policy->active)
    return decision;

  decision.present = should_present(policy, now_us);
  multiplier = fast_forward_policy_speed_multiplier(policy);
  if(multiplier != 0)
    decision.delay_us = pace_limited_speed(policy, now_us, multiplier);
  return decision;
}

int fast_forward_policy_take_force_present(FastForwardPolicy *policy)
{
  if(policy == NULL || !policy->force_present)
    return 0;

  policy->force_present = 0;
  return 1;
}

int fast_forward_policy_active(const FastForwardPolicy *policy)
{
  return policy != NULL && policy->active;
}

FastForwardSpeed fast_forward_policy_speed(const FastForwardPolicy *policy)
{
  if(policy == NULL || !policy->active)
    return FAST_FORWARD_SPEED_MAXIMUM;
  return effective_speed(policy);
}

/* Accelerated audio is only offered by CUSTOM at 2x or 3x. */
FastForwardAudio fast_forward_policy_audio(const FastForwardPolicy *policy)
{
  if(policy == NULL || !policy->active || policy->audio_fallback ||
     policy->settings.profile != FAST_FORWARD_PROFILE_CUSTOM ||
     effective_speed(policy) == FAST_FORWARD_SPEED_MAXIMUM)
    return FAST_FORWARD_AUDIO_SILENT;
  return policy->settings.audio;
}

uint32_t fast_forward_policy_speed_multiplier(
 const FastForwardPolicy *policy)
{
  FastForwardSpeed speed;

  if(policy == NULL || !policy->active)
    return 0;

  speed = effective_speed(policy);
  if(speed == FAST_FORWARD_SPEED_2X)
    return 2;
  if(speed == FAST_FORWARD_SPEED_3X)
    return 3;
  return 0;
}
