#!/bin/sh

set -eu

# Keeps the performance instrumentation cheap enough to leave on: sampling
# never allocates or touches files, and the report is only formatted and
# written outside the emulation hot paths, where the memory probe never runs.

if grep -Eq 'FILE_|sceIo|malloc\(|calloc\(|realloc\(|free\(' \
  source/src/perf_metrics.c source/src/perf_monitor.c
then
  echo "Performance sampling performs allocation or file access" >&2
  exit 1
fi

if grep -Eq 'perf_report_format|main_perf_write_report|mem_stats_largest_block' \
  source/src/cpu.c source/src/memory.c source/src/psp_video.c \
  source/src/sound.c source/src/video_dispatch.c
then
  echo "A performance report or the memory probe runs in a hot module" >&2
  exit 1
fi

update_body=$(sed -n '/^u32 update_gba/,/^static void init_main/p' \
  source/src/main.c)
# The one exception: synchronize() saves the unbound benchmark's report
# once, through gui.c's menu_diagnostics_write_report, two lines under the
# perf_monitor_bench_take_done() that is true only after the bench's last
# frame.
bench_writes=$(printf '%s\n' "$update_body" | \
  grep -c 'menu_diagnostics_write_report' || true)
guarded_writes=$(printf '%s\n' "$update_body" | \
  grep -B2 'menu_diagnostics_write_report' | \
  grep -c 'perf_monitor_bench_take_done()' || true)
if printf '%s\n' "$update_body" | \
  grep -Eq 'perf_report_format|main_perf_write_report|mem_stats_largest_block|main_memory_sample' || \
  [ "$bench_writes" != 1 ] || [ "$guarded_writes" != 1 ]
then
  echo "A performance report or the memory probe runs in update_gba" \
    "(beyond the benchmark's one save after perf_monitor_bench_take_done)" >&2
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
# and their calibration (2), then to 16 with the preemption calibration,
# which runs once per capture.
main_reads=$(grep -c sceKernelGetSystemTimeLow source/src/main.c || true)
video_reads=$(grep -c sceKernelGetSystemTimeLow source/src/psp_video.c || true)
if [ "$main_reads" != 16 ] || [ "$video_reads" != 9 ]
then
  echo "sceKernelGetSystemTimeLow lines changed (main.c $main_reads," \
    "psp_video.c $video_reads; pinned at 16 and 9 in" \
    "scripts/check-performance.sh)" >&2
  exit 1
fi

# In update_gba and main_perf_phase_frame, which it calls every frame, the
# phase timers read the clock only on sampled frames: a clock read or a
# main_phase_ timer there tests perf_phase_sampling on its own line. The
# renderer time is the one exception: its start read follows
# if(perf_monitor_active()) and its end read if(renderer_start != 0).
if ! awk '/^(static void main_perf_phase_frame|u32 update_gba)\(/ {
    inside = 1; found++ }
  inside && /sceKernelGetSystemTime|main_phase_/ &&
    !/perf_phase_sampling/ &&
    !(/renderer_start = / && prev1 ~ /if\(perf_monitor_active\(\)\)/) &&
    !(/- renderer_start\)/ && prev2 ~ /if\(renderer_start != 0\)/) { bad++ }
  inside { prev2 = prev1; prev1 = $0 }
  inside && /^}/ { inside = 0 }
  END { exit (found != 2 || bad) }' source/src/main.c
then
  echo "update_gba reads the clock outside the phase sampling flag" \
    "(or it or main_perf_phase_frame is missing)" >&2
  exit 1
fi

# The clock option can change in the menu, so a capture started there is
# calibrated when the game resumes: main_perf_begin only sets the pending
# flag, the two calibrations run only from main_perf_calibrate, which only
# main_link_set_gameplay_active calls, and menu() restores
# option_clock_speed before main_link_set_gameplay_active(1). A function
# starts at a column-0 line with a ( and no ;.
if ! awk '/^[a-z].*\([^;]*$/ {
    fn = $0; sub(/\(.*/, "", fn); sub(/.*[ *]/, "", fn) }
  /main_perf_calibrate_pending = 1;/ {
    sets++; if (fn != "main_perf_begin") bad++ }
  /main_perf_calibrate_(probe|preempt)\(\);/ {
    reads++; if (fn != "main_perf_calibrate") bad++ }
  /main_perf_calibrate\(\);/ {
    calls++; if (fn != "main_link_set_gameplay_active") bad++ }
  END { exit (sets != 1 || reads != 2 || calls != 1 || bad) }' \
    source/src/main.c || \
  ! awk '/^u32 menu\(void\)/ { inside = 1 }
  inside && /set_cpu_clock\(option_clock_speed\)/ { clock = 1 }
  inside && /main_link_set_gameplay_active\(1\)/ {
    resumes++; if (!clock) bad++ }
  inside && /^}/ { inside = 0 }
  END { exit (resumes != 1 || bad) }' source/src/gui.c
then
  echo "The perf calibration may run at the menu's clock or not at all" \
    "(main_perf_begin must set the pending flag, main_perf_calibrate run" \
    "only from main_link_set_gameplay_active, and menu() restore" \
    "option_clock_speed before main_link_set_gameplay_active(1))" >&2
  exit 1
fi

# The audio-off bench arm of diagnostic builds leaves out only the writes
# of sound samples into the ring, so the guest runs the same: what sound.c
# runs under sound_bench_audio_off, from each if that tests it to the end
# of the block that if opens (one in sound_timer, one for the PSG
# channels), never touches the Direct Sound FIFO or a DMA request, and no
# line ties the FIFO or DMA to the flag outside an if either. The PSG arm
# still runs the channel's render loop with no output, so the sample
# position and the length, envelope and sweep counters keep stepping.
if ! awk '/sound_bench_audio_off/ && /fifo|dma/ { bad++ }
  /if[ ]*\(.*sound_bench_audio_off/ {
    inside = 1; depth = 0; opened = 0; found++ }
  inside {
    if ($0 ~ /fifo|dma/) bad++
    if ($0 ~ /GBC_SOUND_RENDER_##type[(]NULL,/) steps++
    opens = gsub(/[{]/, "{"); closes = gsub(/[}]/, "}")
    depth += opens - closes
    if (opens) opened = 1
    if (opened && depth <= 0) inside = 0
  }
  END { exit (found != 2 || bad || steps != 1) }' source/src/sound.c
then
  echo "sound.c runs FIFO or DMA code under the audio-off bench arm" \
    "(or the arm is not in sound_timer and the PSG render, or the PSG" \
    "arm no longer steps the channel)" >&2
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
