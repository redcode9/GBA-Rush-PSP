#include "video_dispatch_state.h"

/* Any unknown value (e.g. from an old config) falls back to the new one. */
static uint32_t video_dispatch_normalize(uint32_t renderer)
{
  return renderer == VIDEO_DISPATCH_RENDERER_OLD ?
   VIDEO_DISPATCH_RENDERER_OLD : VIDEO_DISPATCH_RENDERER_NEW;
}

void video_dispatch_state_init(VideoDispatchState *state,
 uint32_t renderer)
{
  uint32_t normalized = video_dispatch_normalize(renderer);

  state->requested_renderer = normalized;
  state->active_renderer = normalized;
}

void video_dispatch_state_request(VideoDispatchState *state,
 uint32_t renderer)
{
  state->requested_renderer = video_dispatch_normalize(renderer);
}

int video_dispatch_state_begin_frame(VideoDispatchState *state)
{
  int changed = state->active_renderer != state->requested_renderer;

  state->active_renderer = state->requested_renderer;
  return changed;
}

uint32_t video_dispatch_state_active(const VideoDispatchState *state)
{
  return state->active_renderer;
}
