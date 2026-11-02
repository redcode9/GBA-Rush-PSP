#!/bin/sh
set -eu

# Source-level guards for PSP-only lifecycle rules that host tests cannot
# exercise: callbacks, thread priorities, bounded waits and link wiring.
# Run from the repository root by check-tree.sh; uses ripgrep (rg).

fail()
{
  echo "$1" >&2
  exit 1
}

body()
{
  sed -n "$1" "$2"
}

# main_link_start() must prepare the SIO registers, returning on failure,
# before any of the three calls that use them.
link_start_body=$(body '/^static int main_link_start(int host)/,/^}/p' \
  source/src/main.c)
if ! printf '%s\n' "$link_start_body" | awk '
  /if\(!gba_sio_prepare\(&gba_sio, io_registers\)\)/ { prepare = 1; next }
  prepare && /return 0;/ { checked = 1 }
  /main_link_disconnect\(|link_cable_start\(|psp_adhoc_start\(/ {
    if (!checked) bad = 1
    consumers++
  }
  END { exit !(checked && consumers >= 3 && !bad) }
'
then
  fail "Link startup uses SIO before guarded register preparation"
fi

# A link session runs at 333 MHz from before ad-hoc startup until it
# closes, and the menu must not change the clock meanwhile.
clock_line=$(printf '%s\n' "$link_start_body" | \
  rg -n 'set_cpu_clock\(PSP_CLOCK_333\)' | head -n 1 | cut -d: -f1 || true)
adhoc_line=$(printf '%s\n' "$link_start_body" | \
  rg -n 'psp_adhoc_start\(' | head -n 1 | cut -d: -f1 || true)
if [ -z "$clock_line" ] || [ -z "$adhoc_line" ] || \
   [ "$clock_line" -ge "$adhoc_line" ]
then
  fail "Link clock is not pinned before ad-hoc startup"
fi
if ! body '/^static void main_link_close/,/^}/p' source/src/main.c | \
  rg -q 'set_cpu_clock\(option_clock_speed\)'
then
  fail "Link clock is not restored after the session ends"
fi

menu_clock_guards=$(body '/^u32 menu(void)/,/^static void update_status_string/p' \
  source/src/gui.c | rg -c 'if\(!main_link_active\(\)\)' || true)
if [ "$menu_clock_guards" -lt 2 ]
then
  fail "Menu clock transitions are not guarded by link state"
fi

# The PSP never time-slices equal priorities: the network stack and the
# link worker must run above the emulator thread (32), or reception stops.
for name in PSP_ADHOC_STACK_PRIORITY PSP_ADHOC_CONTROL_PRIORITY \
  PSP_ADHOC_WORKER_PRIORITY
do
  value=$(sed -n "s/^#define $name[[:space:]]*\(0x[0-9A-Fa-f]*\).*/\1/p" \
    source/src/psp_adhoc.c)
  if [ -z "$value" ] || [ "$((value))" -ge 32 ]
  then
    fail "$name must outrank the emulator thread"
  fi
done
if ! rg -q '#define PRIORITY[[:space:]]+\(32\)' source/src/main.c
then
  fail "Emulator thread priority changed; recheck the network priorities"
fi

# Only the worker touches the network; the emulator thread never blocks on it.
if rg -q 'sceNetAdhoc|sceNetInit|sceUtilityLoadNetModule' \
  source/src/main.c source/src/gui.c source/src/link_cable.c \
  source/src/link_net.c
then
  fail "Network calls are made outside the ad-hoc worker"
fi
for function_name in ring_push ring_pop
do
  if ! body "/^static .*$function_name(/,/^}/p" source/src/psp_adhoc.c | \
    rg -q '__sync_synchronize'
  then
    fail "Ad-hoc queue function lacks publication barriers: $function_name"
  fi
done
if ! rg -q 'sceKernelTerminateDeleteThread' source/src/psp_adhoc.c || \
   ! rg -q 'psp_adhoc_stop_for_exit' source/src/main.c
then
  fail "Exit path cannot bound a stalled ad-hoc worker"
fi
if rg -q 'sceNetAdhocPtp|PSP_ADHOC_PTP' source/src tests Makefile \
  source/Makefile
then
  fail "Obsolete PTP transport is still present"
fi

# The power callback runs on the kernel callback thread and must not block:
# the slow suspend and resume work is left to the main loop.
callback_body=$(body \
  '/^static int power_callback/,/^static int callback_thread/p' \
  source/src/main.c)
if printf '%s\n' "$callback_body" | rg -q \
  'psp_sound_frequency|auto_savestate_sleep|main_link_disconnect|psp_adhoc_stop|sceKernel|sceIo|sceNet|FILE_|error_msg|quit\('
then
  fail "Power callback contains a blocking lifecycle operation"
fi
if ! printf '%s\n' "$callback_body" | rg -q 'sound_power_resume'
then
  fail "Power callback does not use the suspend-safe audio resume path"
fi

exit_callback_body=$(body \
  '/^static int exit_callback/,/^static int power_callback/p' \
  source/src/main.c)
if printf '%s\n' "$exit_callback_body" | rg -q '^[[:space:]]*quit\(\)' || \
   ! printf '%s\n' "$exit_callback_body" | rg -q 'exit_requested = 1' || \
   ! printf '%s\n' "$exit_callback_body" | \
    rg -q 'MAIN_EXIT_CALLBACK_TIMEOUT_US'
then
  fail "Exit callback bypasses the bounded main-thread shutdown path"
fi
if ! body '/^void quit(void)/,/^}/p' source/src/main.c | \
  rg -q '__sync_bool_compare_and_swap'
then
  fail "Emulator shutdown is not guarded against concurrent callbacks"
fi

if rg -q 'sceKernelWaitThreadEnd\([^,]+, NULL\)' \
  source/src/psp_adhoc.c source/src/sound.c
then
  fail "Lifecycle worker uses an unlimited thread join"
fi
if rg -q 'sceKernelWaitSema\([^,]+,[^,]+,[[:space:]]*(0|NULL)\)' \
  source/src/sound.c
then
  fail "Sound lifecycle uses an unlimited semaphore wait"
fi
if rg -q 'sceAudioOutput2' source/src/sound.c
then
  fail "SRC audio lifecycle calls the Output2 API family"
fi
audio_power_body=$(body \
  '/^void sound_power_suspend/,/^void sound_power_resume_finish/p' \
  source/src/sound.c)
if ! printf '%s\n' "$audio_power_body" | rg -q 'SOUND_OPERATION_TIMEOUT' || \
   ! printf '%s\n' "$audio_power_body" | rg -q 'sceKernelWaitSema'
then
  fail "Audio suspend and resume are not serialized with a deadline"
fi
if ! body '/^void synchronize_sound/,/^void update_gbc_sound/p' \
  source/src/sound.c | rg -q 'SOUND_OPERATION_TIMEOUT'
then
  fail "Sound synchronization has no recovery deadline"
fi

# With a paged ROM the header page can be evicted, so title, game code and
# revision must come from the gamepak_header copy taken at load time.
if rg -q 'gamepak_rom[[:space:]]*\+[[:space:]]*0x(A0|AC)|gamepak_rom\[0xBC\]' \
  source/src/main.c || \
   ! rg -q 'gamepak_header\[GAMEPAK_HEADER_SIZE\]' source/src/memory.c || \
   ! rg -q 'memcpy\(gamepak_header, gamepak_rom' source/src/memory.c
then
  fail "Paged ROM metadata is not backed by a stable header copy"
fi

# Logging must never block the emulator thread or the link wait loop.
if body '/^void runtime_log_ring_emit/,/^size_t runtime_log_ring_snapshot/p' \
  source/src/runtime_log.c | rg -q \
  'sceIo|FILE_|printf|malloc|calloc|realloc|sceKernelWait|sceKernelDelay'
then
  fail "Runtime event recording performs a blocking operation"
fi
if body '/^static int main_link_service/,/^}/p' source/src/main.c | \
  rg -q 'runtime_log_psp_flush'
then
  fail "Link wait path writes diagnostics to storage"
fi
if ! rg -q 'test_runtime_log' Makefile || \
   ! rg -q 'test_decode_runtime_log' Makefile || \
   ! rg -q 'test_link_cable' Makefile || \
   ! rg -q 'RUNTIME_LOG_RING_CAPACITY' source/src/runtime_log.c || \
   ! rg -q 'runtime_log_store_rotate' source/src/runtime_log_psp.c
then
  fail "Runtime and link diagnostics are not covered by the validation gate"
fi

echo "Runtime lifecycle checks passed"
