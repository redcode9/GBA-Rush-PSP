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

# The .sav goes through backup_io, and only save_backup() writes it, after
# checking that the file did not fail to read: else a blank image would
# replace the save. So memory.c opens no file on the save buffer or name,
# never calls backup_io itself, and hands backup_filename only to
# load_backup() and save_backup().
if rg -q 'FILE_(OPEN|WRITE)\([^)]*backup|backup_io->' source/src/memory.c || \
   rg 'backup_filename' source/src/memory.c | rg -v -q \
    'char backup_filename\[|change_ext\(gamepak_filename, backup_filename|load_backup\(backup_filename|save_backup\(backup_filename'
then
  fail "memory.c reaches the .sav outside load_backup and save_backup"
fi
save_backup_body=$(body '/^static u32 save_backup(char \*name)[^;]*$/,/^}/p' \
  source/src/memory.c)
if [ "$(rg -c 'save_store_write\(' source/src/memory.c || true)" != 1 ] || \
   ! printf '%s\n' "$save_backup_body" | awk '
  /backup_write_blocked/ && !io { checked = 1 }
  /scePowerLock|save_store_write\(/ { io = 1; if (!checked) bad = 1 }
  END { exit !(checked && io && !bad) }
'
then
  fail "A .sav write can bypass the unreadable-save block"
fi

# A failed .sav write stays pending, so the save in RAM is never dropped
# while the game runs: save_backup() opens no file itself and reports the
# store's result, and backup_update is set back to "nothing pending" only
# after save_backup() succeeded, or by load_backup() for a new game.
if printf '%s\n' "$save_backup_body" | rg -q 'FILE_[A-Z]+\(' || \
   ! printf '%s\n' "$save_backup_body" | \
    rg -q 'return \(result == SAVE_STORE_OK\);' || \
   [ "$(rg -c '^[[:space:]]+backup_update = WRITE_BACKUP_DELAY \+ 1;' \
    source/src/memory.c || true)" != 2 ] || \
   ! body '/^SaveStoreResult load_backup(/,/^}/p' source/src/memory.c | \
    rg -q 'backup_update = WRITE_BACKUP_DELAY \+ 1;' || \
   ! body '/^u32 update_backup_immediately(void)/,/^}/p' source/src/memory.c | \
    awk '
  /!save_backup\(backup_filename\)\)/ { checked = 1 }
  checked && /return 0;/ { failed = 1 }
  /backup_update = WRITE_BACKUP_DELAY \+ 1;/ { reset = 1; if (!failed) bad = 1 }
  END { exit !(failed && reset && !bad) }
'
then
  fail "A failed .sav write can be dropped as if it had been written"
fi

# Savestates stream through the store's transaction from a static block:
# saving one allocates nothing, so it cannot quit on a full heap, and never
# opens the slot's file for writing, which would truncate the old state.
if ! awk '
  /^(static )?u32 save_state[a-z_]*\([^;]*$/ { inside = 1; functions++ }
  inside && /malloc\(|FILE_OPEN\(.*WRITE/ { bad = 1 }
  inside && /save_store_commit\(/ { commits++ }
  inside && /^}/ { inside = 0 }
  END { exit !(functions >= 3 && commits == 1 && !bad) }
' source/src/memory.c
then
  fail "A savestate save allocates or truncates the old state"
fi

# Deleting a state takes the .bak and .tmp its saves leave, which the next
# load would otherwise recover as the deleted state.
if ! body '/^  void menu_delete_state(void)/,/^  }/p' source/src/gui.c | awk '
  /save_store_remove\(backup_io, dir_state, / { removes++ }
  /sceIoRemove\(/ { bad = 1 }
  END { exit !(removes == 1 && !bad) }
'
then
  fail "Deleting a savestate leaves a .bak or .tmp that brings it back"
fi

# The sceIo binding of the .sav and the settings file allocates nothing,
# so a save is still written when memory has run out.
if ! awk '
  /^static [^(]*config_io_[a-z0-9_]+\(/ { inside = 1; config++ }
  /^static [^(]*save_io_[a-z0-9_]+\(/ { inside = 1; save++ }
  inside && /malloc\(|calloc\(|realloc\(/ { bad = 1 }
  inside && /^}/ { inside = 0 }
  END { exit !(config && save && !bad) }
' source/src/gui.c
then
  fail "The save and settings sceIo binding allocates memory"
fi

# A dialog under load_gamepak()'s power lock would hold off standby.
if ! body '/^s32 load_gamepak(char \*name)/,/^}/p' source/src/memory.c | awk '
  /scePowerLock\(/ { locked = 1 }
  /scePowerUnlock\(/ { locked = 0 }
  /yesno_dialog\(|error_msg\(/ { asked = 1; if (locked) bad = 1 }
  END { exit !(asked && !bad) }
'
then
  fail "load_gamepak() opens a dialog while it holds the power lock"
fi

# Load State File on a state of the running ROM first saves the AUTO state,
# which must not happen when the chosen file is that AUTO state.
if ! body '/^  void menu_load_state_file(void)/,/^  }/p' source/src/gui.c | awk '
  /if \(same_rom\)/ { same = 1 }
  /MSG_LOAD_ROM_AND_STATE/ { same = 0 }
  same && /action_savestate_slot\(AUTO_SAVESTATE_SLOT\)/ {
    calls++
    if (previous !~ /if \(slot_parsed != AUTO_SAVESTATE_SLOT\)/) bad = 1
  }
  /[^[:space:]]/ { previous = $0 }
  END { exit !(calls >= 1 && !bad) }
'
then
  fail "Load State File can overwrite the AUTO state it is loading"
fi

# A suspend at the resume question writes the AUTO state unless no ROM
# counts as loaded, so the name is blank while yesno_dialog is open and back
# before the state loads.
if ! body '/^void maybe_load_auto_savestate(void)/,/^}/p' source/src/gui.c | awk '
  BEGIN { named = 1 }
  /gamepak_filename\[0\] = / { named = 0 }
  /strcpy\(gamepak_filename, / { named = 1 }
  /yesno_dialog\(/ { asked = 1; if (named) bad = 1 }
  /action_loadstate_slot\(/ { loads = 1; if (!named) bad = 1 }
  END { exit !(asked && loads && !bad) }
'
then
  fail "The resume question is open with the ROM name set"
fi

echo "Runtime lifecycle checks passed"
