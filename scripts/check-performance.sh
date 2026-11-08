#!/bin/sh

set -eu

# Keeps the performance instrumentation cheap enough to leave on: sampling
# never allocates or touches files, and the report is only formatted and
# written outside the emulation hot paths. The toast ring is read on every
# presented frame, so it follows the same rule.

if grep -Eq 'FILE_|sceIo|malloc\(|calloc\(|realloc\(|free\(' \
  source/src/perf_metrics.c source/src/perf_monitor.c source/src/toast.c
then
  echo "Performance sampling or the toast ring performs allocation or file access" >&2
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

# The screenshot encoder fills the caller's buffer with memory from the
# caller's allocator: it opens no file and allocates nothing itself.
if rg -q 'FILE_|sceIo|\bmalloc\(|\bcalloc\(|\brealloc\(|\bfree\(' \
  source/src/png_write.c
then
  echo "The PNG writer opens files or allocates memory itself" >&2
  exit 1
fi

# Benchmarks compare medians of three runs per build with matching settings.
if ! grep -Eq 'nargs=3' scripts/compare-benchmarks.py || \
   ! grep -Eq 'MATCH_FIELDS' scripts/compare-benchmarks.py
then
  echo "Benchmark comparison does not enforce repeated matching runs" >&2
  exit 1
fi

echo "Performance instrumentation checks passed"
