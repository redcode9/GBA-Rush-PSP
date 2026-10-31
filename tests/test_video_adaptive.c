/*
 * Host tests for video_adaptive.c, which lowers the video quality from
 * sharp to crisp under sustained load and restores it later. Frames are
 * judged in windows of VIDEO_ADAPTIVE_WINDOW_SIZE: two bad windows in a
 * row (slow frames, a skipped frame, an audio event or long GE waits)
 * degrade, five good ones recover, and a window in between resets both
 * counts. Times are microseconds; a GBA frame is about 16743 us.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "video_adaptive.h"

/* Feeds one window of identical frames; a skip or audio event is flagged
   on its first frame only. Returns how many quality changes happened. */
static int feed_window(VideoAdaptiveState *state, uint32_t frame_time_us,
 uint32_t ge_wait_us, int skipped_frame, int audio_event)
{
  int changed = 0;
  uint32_t i;

  for(i = 0; i < VIDEO_ADAPTIVE_WINDOW_SIZE; i++)
  {
    changed += video_adaptive_record_frame(state, frame_time_us, ge_wait_us,
     skipped_frame && i == 0, audio_event && i == 0);
  }

  return changed;
}

/* Disabled, the manual quality holds through bad frames and reset;
   re-enabling restarts at SHARP whatever quality is passed. */
static void test_initial_and_manual_state(void)
{
  VideoAdaptiveState state;

  video_adaptive_init(&state, 1);
  assert(state.enabled);
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_SHARP);
  assert(state.reason == VIDEO_ADAPTIVE_REASON_INITIAL);

  video_adaptive_set_enabled(&state, 0, VIDEO_ADAPTIVE_QUALITY_SMOOTH);
  assert(!state.enabled);
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_SMOOTH);
  assert(!video_adaptive_record_frame(&state, 30000, 5000, 1, 1));
  video_adaptive_reset(&state);
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_SMOOTH);

  video_adaptive_set_enabled(&state, 1, VIDEO_ADAPTIVE_QUALITY_CRISP);
  assert(state.enabled);
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_SHARP);
}

static void test_short_spike_does_not_degrade(void)
{
  VideoAdaptiveState state;

  video_adaptive_init(&state, 1);
  assert(!feed_window(&state, 20000, 0, 0, 0));
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_SHARP);
  assert(!feed_window(&state, 16800, 0, 0, 0));
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_SHARP);
}

static void test_sustained_load_degrades_and_recovers(void)
{
  VideoAdaptiveState state;
  uint32_t i;

  video_adaptive_init(&state, 1);
  assert(!feed_window(&state, 20000, 0, 0, 0));
  assert(feed_window(&state, 20000, 0, 0, 0) == 1);
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_CRISP);
  assert(state.reason == VIDEO_ADAPTIVE_REASON_FRAME_TIME);

  for(i = 0; i < 4; i++)
  {
    assert(!feed_window(&state, 16800, 500, 0, 0));
    assert(state.quality == VIDEO_ADAPTIVE_QUALITY_CRISP);
  }

  assert(feed_window(&state, 16800, 500, 0, 0) == 1);
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_SHARP);
  assert(state.reason == VIDEO_ADAPTIVE_REASON_RECOVERED);
}

static void test_skip_audio_and_ge_wait_are_bad_windows(void)
{
  VideoAdaptiveState state;

  video_adaptive_init(&state, 1);
  assert(!feed_window(&state, 16800, 500, 1, 0));
  assert(feed_window(&state, 16800, 500, 1, 0) == 1);
  assert(state.reason == VIDEO_ADAPTIVE_REASON_FRAME_SKIP);

  video_adaptive_reset(&state);
  assert(!feed_window(&state, 16800, 500, 0, 1));
  assert(feed_window(&state, 16800, 500, 0, 1) == 1);
  assert(state.reason == VIDEO_ADAPTIVE_REASON_AUDIO);

  video_adaptive_reset(&state);
  assert(!feed_window(&state, 16800, 3000, 0, 0));
  assert(feed_window(&state, 16800, 3000, 0, 0) == 1);
  assert(state.reason == VIDEO_ADAPTIVE_REASON_GE_WAIT);
}

/* Bad windows separated by in-between windows never add up. */
static void test_neutral_windows_prevent_oscillation(void)
{
  VideoAdaptiveState state;
  uint32_t i;

  video_adaptive_init(&state, 1);

  for(i = 0; i < 10; i++)
  {
    assert(!feed_window(&state, 20000, 0, 0, 0));
    assert(!feed_window(&state, 17000, 1500, 0, 0));
  }

  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_SHARP);
}

static void test_reset_discards_observations(void)
{
  VideoAdaptiveState state;

  video_adaptive_init(&state, 1);
  assert(!feed_window(&state, 20000, 0, 0, 0));
  assert(state.bad_windows == 1);

  video_adaptive_reset(&state);
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_SHARP);
  assert(state.reason == VIDEO_ADAPTIVE_REASON_RESET);
  assert(state.bad_windows == 0);
  assert(state.sample_count == 0);
  assert(!feed_window(&state, 20000, 0, 0, 0));
}

/* A saved quality profile applies only to the same ROM CRC, PSP model and
   clock speed, with the current version and an automatic quality. */
static void test_profile_validation(void)
{
  VideoAdaptiveState state;
  VideoAdaptiveProfile profile;
  VideoAdaptiveProfile invalid;

  video_adaptive_init(&state, 1);
  assert(!feed_window(&state, 20000, 0, 0, 0));
  assert(feed_window(&state, 20000, 0, 0, 0));
  video_adaptive_export_profile(&state, &profile, 0x12345678, 4, 333);

  video_adaptive_reset(&state);
  assert(video_adaptive_apply_profile(&state, &profile, 0x12345678, 4, 333));
  assert(state.quality == VIDEO_ADAPTIVE_QUALITY_CRISP);

  invalid = profile;
  invalid.version++;
  assert(!video_adaptive_apply_profile(&state, &invalid, 0x12345678, 4, 333));

  invalid = profile;
  invalid.quality = VIDEO_ADAPTIVE_QUALITY_SMOOTH;
  assert(!video_adaptive_apply_profile(&state, &invalid, 0x12345678, 4, 333));

  memset(&invalid, 0, sizeof(invalid));
  assert(!video_adaptive_apply_profile(&state, &invalid, 0x12345678, 4, 333));
  assert(!video_adaptive_apply_profile(&state, &profile, 0x87654321, 4, 333));
  assert(!video_adaptive_apply_profile(&state, &profile, 0x12345678, 1, 333));
  assert(!video_adaptive_apply_profile(&state, &profile, 0x12345678, 4, 222));
}

/* Runs every adaptive video case; a failing assert aborts the run. */
int main(void)
{
  test_initial_and_manual_state();
  test_short_spike_does_not_degrade();
  test_sustained_load_degrades_and_recovers();
  test_skip_audio_and_ge_wait_are_bad_windows();
  test_neutral_windows_prevent_oscillation();
  test_reset_discards_observations();
  test_profile_validation();
  puts("Adaptive video tests passed");
  return 0;
}
