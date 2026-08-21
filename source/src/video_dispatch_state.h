#ifndef VIDEO_DISPATCH_STATE_H
#define VIDEO_DISPATCH_STATE_H

#include <stdint.h>

#define VIDEO_DISPATCH_RENDERER_OLD 0u
#define VIDEO_DISPATCH_RENDERER_NEW 1u

typedef struct
{
  uint32_t requested_renderer;
  uint32_t active_renderer;
} VideoDispatchState;

void video_dispatch_state_init(VideoDispatchState *state,
 uint32_t renderer);
void video_dispatch_state_request(VideoDispatchState *state,
 uint32_t renderer);
int video_dispatch_state_begin_frame(VideoDispatchState *state);
uint32_t video_dispatch_state_active(const VideoDispatchState *state);

#endif
