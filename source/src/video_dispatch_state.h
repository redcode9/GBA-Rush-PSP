#ifndef VIDEO_DISPATCH_STATE_H
#define VIDEO_DISPATCH_STATE_H

/* Frame-stable renderer selection for video_dispatch.c. A request can come
 * at any time (options menu, config load), but the active renderer only
 * changes in video_dispatch_state_begin_frame(). Plain C so it can be host
 * tested (tests/test_video_dispatch_state.c).
 */

#include <stdint.h>

/* Same values as VIDEO_RENDERER_OLD/NEW in main.h (option_video_renderer). */
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
/* Applies the pending request; returns nonzero if the renderer changed. */
int video_dispatch_state_begin_frame(VideoDispatchState *state);
uint32_t video_dispatch_state_active(const VideoDispatchState *state);

#endif
