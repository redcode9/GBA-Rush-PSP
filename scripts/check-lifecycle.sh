#!/bin/sh
set -eu

callback_body=$(sed -n \
  '/^static int power_callback/,/^static int callback_thread/p' \
  source/src/main.c)

if printf '%s\n' "$callback_body" | rg -q \
  'psp_sound_frequency|auto_savestate_sleep|main_link_disconnect|psp_adhoc_stop|sceKernel|sceIo|sceNet|FILE_|error_msg|quit\('
then
  echo "Power callback contains a blocking lifecycle operation" >&2
  exit 1
fi

if ! printf '%s\n' "$callback_body" | rg -q 'sound_power_resume'
then
  echo "Power callback does not use the suspend-safe audio resume path" >&2
  exit 1
fi

if rg -q 'sceKernelWaitThreadEnd\([^,]+, NULL\)' \
  source/src/psp_adhoc.c source/src/sound.c
then
  echo "Lifecycle worker uses an unlimited thread join" >&2
  exit 1
fi

if rg -q 'sceKernelWaitSema\([^,]+,[^,]+,[[:space:]]*(0|NULL)\)' \
  source/src/sound.c
then
  echo "Sound lifecycle uses an unlimited semaphore wait" >&2
  exit 1
fi

if ! sed -n '/^void synchronize_sound/,/^void update_gbc_sound/p' \
  source/src/sound.c | rg -q 'SOUND_OPERATION_TIMEOUT'
then
  echo "Sound synchronization has no recovery deadline" >&2
  exit 1
fi

echo "Runtime lifecycle checks passed"
