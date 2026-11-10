#!/bin/sh

set -eu

# Keeps the performance instrumentation cheap enough to leave on: sampling
# never allocates or touches files, and the report is only formatted and
# written outside the emulation hot paths.

if grep -Eq 'FILE_|sceIo|malloc\(|calloc\(|realloc\(|free\(' \
  source/src/perf_metrics.c source/src/perf_monitor.c
then
  echo "Performance sampling performs allocation or file access" >&2
  exit 1
fi

if grep -Eq 'perf_report_format|main_perf_write_report' \
  source/src/cpu.c source/src/memory.c source/src/psp_video.c \
  source/src/sound.c source/src/video_dispatch.c
then
  echo "A performance report is formatted or written from a hot module" >&2
  exit 1
fi

update_body=$(sed -n '/^u32 update_gba/,/^static void init_main/p' \
  source/src/main.c)
if printf '%s\n' "$update_body" | \
  grep -Eq 'perf_report_format|main_perf_write_report'
then
  echo "A performance report is formatted or written from update_gba" >&2
  exit 1
fi

# The PSP's C library prints no 64-bit integers: perf_report_append_u64
# writes them out. git grep --untracked leaves out ignored build output.
if git grep -qIE --untracked '%ll|PRIu64|PRId64' -- source/src
then
  echo "A 64-bit printf format is used in source/src" >&2
  exit 1
fi

# Work time per frame reuses the clock reads already taken, so capturing
# costs no more than before. A story that adds a read on purpose updates
# these counts: main.c went from 5 to 14 with the phase timers (7 lines)
# and their calibration (2).
main_reads=$(grep -c sceKernelGetSystemTimeLow source/src/main.c || true)
video_reads=$(grep -c sceKernelGetSystemTimeLow source/src/psp_video.c || true)
if [ "$main_reads" != 14 ] || [ "$video_reads" != 9 ]
then
  echo "sceKernelGetSystemTimeLow lines changed (main.c $main_reads," \
    "psp_video.c $video_reads; pinned at 14 and 9 in" \
    "scripts/check-performance.sh)" >&2
  exit 1
fi

# Inside update_gba the phase timers read the clock only on sampled frames:
# a clock read or a main_phase_ timer there tests perf_phase_sampling on
# its own line. The renderer time, taken under perf_monitor_active(), is
# the one exception.
gba_body=$(awk '/^u32 update_gba/ { inside = 1 }
  inside { print }
  inside && /^}/ { exit }' source/src/main.c)
unguarded=$(printf '%s\n' "$gba_body" | \
  grep -E 'sceKernelGetSystemTime|main_phase_' | \
  grep -v 'perf_phase_sampling' | grep -v 'renderer_start' || true)
if [ -z "$gba_body" ] || [ -n "$unguarded" ]
then
  echo "update_gba reads the clock outside the phase sampling flag" >&2
  exit 1
fi

# Benchmarks compare at least MIN_RUNS runs per build with matching settings.
if ! grep -Eq 'MIN_RUNS = 3' scripts/compare-benchmarks.py || \
   ! grep -Eq 'MATCH_FIELDS' scripts/compare-benchmarks.py
then
  echo "Benchmark comparison does not enforce repeated matching runs" >&2
  exit 1
fi

echo "Performance instrumentation checks passed"
