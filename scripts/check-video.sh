#!/bin/sh

set -eu

# Source-level guards for psp_video.c and video_dispatch.c that host tests
# cannot exercise: frames go out through libGU, the power callback leaves
# video work to the main loop, and the per-scanline path only uses the
# renderer chosen at the start of the frame.

if grep -Eq 'sceGeListEnQueue' source/src/psp_video.c
then
  echo "Video presentation must remain on the libGU path" >&2
  exit 1
fi

callback_body=$(sed -n \
  '/^static int power_callback/,/^static int callback_thread/p' \
  source/src/main.c)

if printf '%s\n' "$callback_body" | grep -Eq \
  'video_sync_present|video_power_suspend|video_power_resume'
then
  echo "Power callback contains a blocking video operation" >&2
  exit 1
fi

if ! grep -Eq 'video_power_suspend\(\)' source/src/main.c || \
   ! grep -Eq 'video_power_resume\(\)' source/src/main.c
then
  echo "Foreground power service does not reconcile video state" >&2
  exit 1
fi

scanline_body=$(sed -n '/^void update_scanline(void)/,/^}/p' \
  source/src/video_dispatch.c)
if printf '%s\n' "$scanline_body" | grep -Eq \
  'option_video_renderer|sync_renderer_state|video_dispatch_state_begin_frame'
then
  echo "Scanline path performs renderer selection work" >&2
  exit 1
fi

if ! printf '%s\n' "$scanline_body" | grep -Eq 'active_scanline\(\)'
then
  echo "Scanline path does not use the frame-stable renderer" >&2
  exit 1
fi

echo "Video lifecycle checks passed"
