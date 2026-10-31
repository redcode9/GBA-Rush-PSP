/* Host test for the psp_video.c frame-time sampling.
 * psp_video.c, perf_monitor.c, perf_metrics.c and
 * video_adaptive.c are linked unmodified; only the PSP SDK entry
 * points are stubbed (tests/host_psp_video_stubs.c), including a
 * controllable sceKernelGetSystemTimeLow().
 *
 * The first sample after init/reset only seeds the last frame time;
 * every later sample is recorded. A last frame time of 0 must not be
 * taken as "no previous frame" (psp_video.c keeps a separate flag),
 * or a clock read of exactly 0, e.g. after a 32-bit wrap, would
 * re-seed and silently drop the sample.
 * Scenarios:
 *   A baseline (no wrap): seeds, then records exactly one frame time;
 *   B wrap 0xFFFFFFFF -> 0 -> 1000: both samples survive;
 *   C first sample exactly at clock 0: the second sample is recorded.
 */
#include <stdint.h>
#include <stdio.h>

#include "psptypes.h"
#include "perf_monitor.h"
#include "psp_video.h"

void host_video_time_reset(void);
void host_video_time_push(u32 value);

static int failures = 0;

static void check(int condition, const char *label)
{
  if(condition)
    printf("PASS %s\n", label);
  else
  {
    printf("FAIL %s\n", label);
    failures++;
  }
}

static void begin_session(void)
{
  perf_monitor_init();
  perf_monitor_start(0);
  host_video_time_reset();
  video_reset_frame_timing();
}

static PerfMetricsSnapshot take_snapshot(void)
{
  PerfMetricsSnapshot snapshot;

  perf_monitor_snapshot(&snapshot);
  return snapshot;
}

int main(void)
{
  PerfMetricsSnapshot snapshot;

  /* A: baseline, no wrap. Seed at 1000, then a 10666 us frame. */
  begin_session();
  host_video_time_push(1000);
  video_adaptive_frame_complete(0, 0); /* seeds, no record */
  host_video_time_push(11666);
  video_adaptive_frame_complete(0, 0); /* records 11666 - 1000 = 10666 */
  snapshot = take_snapshot();
  check(snapshot.emulated_frames == 1, "A emulated_frames==1");
  check(snapshot.frame_time_samples == 1, "A frame_time_samples==1");
  check(snapshot.frame_time_total_us == 10666,
   "A frame_time_total_us==10666");
  perf_monitor_stop(11666);

  /* B: 32-bit wrap 0xFFFFFFFF -> 0. The frame across the wrap (1 us)
   * stores 0 as last time; the following 1000 us sample must survive. */
  begin_session();
  host_video_time_push(0xFFFFFFFFu);
  video_adaptive_frame_complete(0, 0); /* seeds */
  host_video_time_push(0);
  video_adaptive_frame_complete(0, 0); /* records 1, stores 0 */
  host_video_time_push(1000);
  video_adaptive_frame_complete(0, 0); /* records 1000, no re-seed */
  snapshot = take_snapshot();
  check(snapshot.emulated_frames == 2,
   "B emulated_frames==2 (wrap sample kept)");
  check(snapshot.frame_time_samples == 2,
   "B frame_time_samples==2");
  check(snapshot.frame_time_total_us == 1001,
   "B frame_time_total_us==1001");
  perf_monitor_stop(1000);

  /* C: first sample exactly at clock 0. The seed must still record the
   * next frame instead of silently re-seeding. */
  begin_session();
  host_video_time_push(0);
  video_adaptive_frame_complete(0, 0); /* seeds with clock == 0 */
  host_video_time_push(50000);
  video_adaptive_frame_complete(0, 0); /* records 50000, no re-seed */
  snapshot = take_snapshot();
  check(snapshot.emulated_frames == 1,
   "C emulated_frames==1 (seed-at-zero)");
  check(snapshot.frame_time_total_us == 50000,
   "C frame_time_total_us==50000");
  perf_monitor_stop(50000);

  if(failures)
  {
    printf("FAIL %d check(s) failed\n", failures);
    return 1;
  }

  printf("OK all checks passed\n");
  return 0;
}
