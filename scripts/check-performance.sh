#!/bin/sh

set -eu

if rg -q 'FILE_|sceIo|malloc\(|calloc\(|realloc\(|free\(' \
  source/src/perf_metrics.c source/src/perf_monitor.c
then
  echo "Performance sampling performs allocation or file access" >&2
  exit 1
fi

if rg -q 'perf_report_format|main_perf_write_report' \
  source/src/cpu.c source/src/memory.c source/src/psp_video.c \
  source/src/sound.c source/src/video_dispatch.c
then
  echo "A performance report is formatted or written from a hot module" >&2
  exit 1
fi

update_body=$(sed -n '/^u32 update_gba/,/^static void init_main/p' \
  source/src/main.c)
if printf '%s\n' "$update_body" | \
  rg -q 'perf_report_format|main_perf_write_report'
then
  echo "A performance report is formatted or written from update_gba" >&2
  exit 1
fi

if ! rg -q 'nargs=3' scripts/compare-benchmarks.py || \
   ! rg -q 'MATCH_FIELDS' scripts/compare-benchmarks.py
then
  echo "Benchmark comparison does not enforce repeated matching runs" >&2
  exit 1
fi

echo "Performance instrumentation checks passed"
