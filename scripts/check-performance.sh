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

# Replay state hashes go to a caller's array; the caller writes the file.
if grep -Eq 'FILE_|sceIo|fopen|fwrite|printf|malloc\(|calloc\(|realloc\(|free\(' \
  source/src/state_hash.c
then
  echo "State hashing performs allocation or file access" >&2
  exit 1
fi

# They are formatted only in gui.c (gui_trace_write_hashes) and exported
# once: at menu entry after a replay finishes, or by main_autorun_frame
# after an autorun's last frame. update_gba calls that every frame, so that
# one export runs inside update_gba; the bodies of update_gba and
# update_input, which run every frame, must not export.
exporters=$(grep -rlE 'state_hash_export|state_hash_text_size' source/src |
  grep -vxE 'source/src/(gui\.c|state_hash\.c|state_hash\.h)' || true)
if [ -n "$exporters" ]
then
  echo "Replay state hashes are formatted outside gui.c: $exporters" >&2
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

input_body=$(sed -n '/^u32 update_input/,/^}/p' source/src/input.c)
if [ -z "$update_body" ] || [ -z "$input_body" ]
then
  echo "update_gba or update_input not found" >&2
  exit 1
fi
if printf '%s\n%s\n' "$update_body" "$input_body" | \
  grep -Eq 'gui_trace_write_hashes|state_hash_export|state_hash_text_size'
then
  echo "Replay state hashes are exported from update_gba or update_input" >&2
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
