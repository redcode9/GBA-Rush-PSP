/*
 * Host tests for fast_forward_policy.c: how the fast-forward button turns
 * on the feature per profile, how fast emulation runs and which frames are
 * shown. Times are microseconds on the PSP's 32-bit clock. One GBA frame is
 * 280896 cycles at 16.78 MHz = 16742.7 us, so 2x paces frames 8371 us apart
 * and 3x 5581 us apart. "Present" means the frame is drawn to the screen.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "fast_forward_policy.h"

static FastForwardSettings custom_settings(FastForwardActivation activation,
 FastForwardSpeed speed, FastForwardPresentation presentation,
 FastForwardAudio audio)
{
  FastForwardSettings settings;

  fast_forward_settings_defaults(&settings);
  settings.profile = FAST_FORWARD_PROFILE_CUSTOM;
  settings.activation = activation;
  settings.speed = speed;
  settings.presentation = presentation;
  settings.audio = audio;
  return settings;
}

static void test_defaults_and_invalid_fields(void)
{
  FastForwardSettings settings;

  fast_forward_settings_defaults(&settings);
  assert(settings.profile == FAST_FORWARD_PROFILE_MODERN);
  assert(settings.activation == FAST_FORWARD_ACTIVATION_HOLD);
  assert(settings.speed == FAST_FORWARD_SPEED_MAXIMUM);
  assert(settings.presentation == FAST_FORWARD_PRESENTATION_BALANCED);
  assert(settings.audio == FAST_FORWARD_AUDIO_SILENT);

  settings.profile = (FastForwardProfile)99;
  settings.activation = (FastForwardActivation)99;
  settings.speed = (FastForwardSpeed)99;
  settings.presentation = (FastForwardPresentation)99;
  settings.audio = (FastForwardAudio)99;
  fast_forward_settings_sanitize(&settings);
  assert(settings.profile == FAST_FORWARD_PROFILE_MODERN);
  assert(settings.activation == FAST_FORWARD_ACTIVATION_HOLD);
  assert(settings.speed == FAST_FORWARD_SPEED_MAXIMUM);
  assert(settings.presentation == FAST_FORWARD_PRESENTATION_BALANCED);
  assert(settings.audio == FAST_FORWARD_AUDIO_SILENT);
}

/* Modern profile: active while held, unlimited speed (multiplier 0), and
   frames drawn at 30 Hz (every 33333 us). Releasing forces one redraw. */
static void test_modern_hold_and_presentation(void)
{
  FastForwardPolicy policy;
  FastForwardFrameDecision frame;

  fast_forward_policy_init(&policy, NULL);
  fast_forward_policy_input(&policy, 1, 1);
  assert(fast_forward_policy_active(&policy));
  assert(fast_forward_policy_speed_multiplier(&policy) == 0);
  assert(fast_forward_policy_audio(&policy) == FAST_FORWARD_AUDIO_SILENT);
  assert(policy.transition_count == 1);

  frame = fast_forward_policy_frame(&policy, 1000);
  assert(frame.present && frame.delay_us == 0);
  frame = fast_forward_policy_frame(&policy, 34000);
  assert(!frame.present);
  frame = fast_forward_policy_frame(&policy, 34333);
  assert(frame.present);

  fast_forward_policy_input(&policy, 0, 0);
  assert(!fast_forward_policy_active(&policy));
  assert(policy.transition_count == 2);
  assert(fast_forward_policy_take_force_present(&policy));
  assert(!fast_forward_policy_take_force_present(&policy));
}

/* Classic profile: each press cycles 2x, 3x, off, and draws every 2nd or
   3rd emulated frame instead of pacing by time. */
static void test_classic_cycle_and_frame_divisors(void)
{
  FastForwardSettings settings;
  FastForwardPolicy policy;
  FastForwardFrameDecision frame;

  fast_forward_settings_defaults(&settings);
  settings.profile = FAST_FORWARD_PROFILE_CLASSIC;
  fast_forward_policy_init(&policy, &settings);

  fast_forward_policy_input(&policy, 1, 1);
  assert(fast_forward_policy_speed(&policy) == FAST_FORWARD_SPEED_2X);
  assert(fast_forward_policy_speed_multiplier(&policy) == 2);
  frame = fast_forward_policy_frame(&policy, 0);
  assert(frame.present && frame.delay_us == 8371);
  frame = fast_forward_policy_frame(&policy, 8371);
  assert(!frame.present && frame.delay_us >= 8371 && frame.delay_us <= 8372);
  frame = fast_forward_policy_frame(&policy, 16742);
  assert(frame.present);

  fast_forward_policy_input(&policy, 1, 1);
  assert(fast_forward_policy_speed(&policy) == FAST_FORWARD_SPEED_3X);
  frame = fast_forward_policy_frame(&policy, 20000);
  assert(frame.present && frame.delay_us >= 5580 && frame.delay_us <= 5581);
  frame = fast_forward_policy_frame(&policy, 25580);
  assert(!frame.present);
  frame = fast_forward_policy_frame(&policy, 31161);
  assert(!frame.present);
  frame = fast_forward_policy_frame(&policy, 36742);
  assert(frame.present);

  fast_forward_policy_input(&policy, 1, 1);
  assert(!fast_forward_policy_active(&policy));
  assert(fast_forward_policy_take_force_present(&policy));
}

static void test_custom_hold_toggle_and_reconfigure(void)
{
  FastForwardSettings settings = custom_settings(
   FAST_FORWARD_ACTIVATION_TOGGLE, FAST_FORWARD_SPEED_2X,
   FAST_FORWARD_PRESENTATION_SMOOTH, FAST_FORWARD_AUDIO_ACCELERATED);
  FastForwardPolicy policy;

  fast_forward_policy_init(&policy, &settings);
  fast_forward_policy_input(&policy, 1, 1);
  fast_forward_policy_input(&policy, 0, 0);
  assert(fast_forward_policy_active(&policy));
  assert(fast_forward_policy_audio(&policy) ==
   FAST_FORWARD_AUDIO_ACCELERATED);
  fast_forward_policy_input(&policy, 1, 1);
  assert(!fast_forward_policy_active(&policy));

  settings.activation = FAST_FORWARD_ACTIVATION_HOLD;
  settings.speed = FAST_FORWARD_SPEED_3X;
  fast_forward_policy_configure(&policy, &settings);
  fast_forward_policy_input(&policy, 1, 1);
  assert(fast_forward_policy_active(&policy));
  assert(fast_forward_policy_speed_multiplier(&policy) == 3);
  fast_forward_policy_input(&policy, 0, 0);
  assert(!fast_forward_policy_active(&policy));
}

/* Performance presentation draws at 15 Hz (every 66667 us); at maximum
   speed accelerated audio is not offered, so the policy reports silence. */
static void test_custom_presentation_rates(void)
{
  FastForwardSettings settings = custom_settings(
   FAST_FORWARD_ACTIVATION_HOLD, FAST_FORWARD_SPEED_MAXIMUM,
   FAST_FORWARD_PRESENTATION_PERFORMANCE, FAST_FORWARD_AUDIO_ACCELERATED);
  FastForwardPolicy policy;
  FastForwardFrameDecision frame;

  fast_forward_policy_init(&policy, &settings);
  fast_forward_policy_input(&policy, 1, 1);
  assert(fast_forward_policy_audio(&policy) == FAST_FORWARD_AUDIO_SILENT);
  frame = fast_forward_policy_frame(&policy, 500);
  assert(frame.present);
  frame = fast_forward_policy_frame(&policy, 67166);
  assert(!frame.present);
  frame = fast_forward_policy_frame(&policy, 67167);
  assert(frame.present);
}

/* When emulation falls behind the 2x schedule, no catch-up delay is added. */
static void test_limited_speed_never_delays_when_behind(void)
{
  FastForwardSettings settings = custom_settings(
   FAST_FORWARD_ACTIVATION_HOLD, FAST_FORWARD_SPEED_2X,
   FAST_FORWARD_PRESENTATION_BALANCED, FAST_FORWARD_AUDIO_SILENT);
  FastForwardPolicy policy;
  FastForwardFrameDecision frame;

  fast_forward_policy_init(&policy, &settings);
  fast_forward_policy_input(&policy, 1, 1);
  frame = fast_forward_policy_frame(&policy, 1000);
  assert(frame.delay_us == 8371);
  frame = fast_forward_policy_frame(&policy, 20000);
  assert(frame.delay_us == 0);
  frame = fast_forward_policy_frame(&policy, 40000);
  assert(frame.delay_us == 0);
}

/* Pacing and presentation keep working across the 32-bit clock wrap. */
static void test_clock_wrap(void)
{
  FastForwardSettings settings = custom_settings(
   FAST_FORWARD_ACTIVATION_HOLD, FAST_FORWARD_SPEED_2X,
   FAST_FORWARD_PRESENTATION_BALANCED, FAST_FORWARD_AUDIO_SILENT);
  FastForwardPolicy policy;
  FastForwardFrameDecision frame;
  uint32_t start = UINT32_MAX - 4000U;

  fast_forward_policy_init(&policy, &settings);
  fast_forward_policy_input(&policy, 1, 1);
  frame = fast_forward_policy_frame(&policy, start);
  assert(frame.present);
  assert(frame.delay_us == 8371);
  frame = fast_forward_policy_frame(&policy, start + 8371U);
  assert(frame.delay_us >= 8371 && frame.delay_us <= 8372);
  frame = fast_forward_policy_frame(&policy, start + 33333U);
  assert(frame.present);
}

/* A fallback request from the sound code switches audio to silent and is
   counted once; a forced stop (main_fast_forward_stop) also clears the
   classic step and forces a redraw. */
static void test_forced_stop_and_audio_fallback(void)
{
  FastForwardSettings settings = custom_settings(
   FAST_FORWARD_ACTIVATION_TOGGLE, FAST_FORWARD_SPEED_3X,
   FAST_FORWARD_PRESENTATION_BALANCED, FAST_FORWARD_AUDIO_ACCELERATED);
  FastForwardPolicy policy;

  fast_forward_policy_init(&policy, &settings);
  fast_forward_policy_input(&policy, 1, 1);
  assert(fast_forward_policy_audio(&policy) ==
   FAST_FORWARD_AUDIO_ACCELERATED);
  fast_forward_policy_audio_fallback(&policy);
  fast_forward_policy_audio_fallback(&policy);
  assert(fast_forward_policy_audio(&policy) == FAST_FORWARD_AUDIO_SILENT);
  assert(policy.audio_fallback_count == 1);

  fast_forward_policy_stop(&policy);
  assert(!fast_forward_policy_active(&policy));
  assert(policy.classic_step == 0);
  assert(fast_forward_policy_take_force_present(&policy));
  fast_forward_policy_input(&policy, 0, 0);
  assert(!fast_forward_policy_active(&policy));
}

int main(void)
{
  test_defaults_and_invalid_fields();
  test_modern_hold_and_presentation();
  test_classic_cycle_and_frame_divisors();
  test_custom_hold_toggle_and_reconfigure();
  test_custom_presentation_rates();
  test_limited_speed_never_delays_when_behind();
  test_clock_wrap();
  test_forced_stop_and_audio_fallback();
  puts("Fast-forward policy tests passed");
  return 0;
}
