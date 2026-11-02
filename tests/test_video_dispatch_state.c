/*
 * Host tests for video_dispatch_state.c: a renderer change requested from
 * the menu takes effect only at the next frame boundary, so no frame is
 * drawn by two renderers, and unknown renderer ids fall back to the new one.
 */

#include <assert.h>
#include <stdio.h>

#include "video_dispatch_state.h"

static void test_change_is_deferred_until_frame_boundary(void)
{
  VideoDispatchState state;

  video_dispatch_state_init(&state, VIDEO_DISPATCH_RENDERER_NEW);
  video_dispatch_state_request(&state, VIDEO_DISPATCH_RENDERER_OLD);

  assert(video_dispatch_state_active(&state) == VIDEO_DISPATCH_RENDERER_NEW);
  assert(video_dispatch_state_begin_frame(&state));
  assert(video_dispatch_state_active(&state) == VIDEO_DISPATCH_RENDERER_OLD);
  assert(!video_dispatch_state_begin_frame(&state));
}

static void test_invalid_renderer_uses_new_renderer(void)
{
  VideoDispatchState state;

  video_dispatch_state_init(&state, 99);
  assert(video_dispatch_state_active(&state) == VIDEO_DISPATCH_RENDERER_NEW);

  video_dispatch_state_request(&state, 42);
  assert(!video_dispatch_state_begin_frame(&state));
  assert(video_dispatch_state_active(&state) == VIDEO_DISPATCH_RENDERER_NEW);
}

int main(void)
{
  test_change_is_deferred_until_frame_boundary();
  test_invalid_renderer_uses_new_renderer();
  puts("Video dispatch tests passed");
  return 0;
}
