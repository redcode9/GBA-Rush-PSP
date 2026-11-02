/* Runtime switch between the two scanline renderers: video.c (old, built as
 * video_old_wrap.c) and video_cc.cc (new, built as video_cc_wrap.cc).
 * Owns the shared video globals and forwards update_scanline and the
 * savestate hooks to the renderer picked by option_video_renderer. The
 * choice only changes at frame start, so one frame never mixes renderers.
 */

#include "common.h"
#include "video_dispatch_state.h"

void update_scanline_old(void);
void update_scanline_new(void);
void video_read_savestate_old(SceUID savestate_file);
void video_read_savestate_new(SceUID savestate_file);
void video_write_mem_savestate_old(SceUID savestate_file);
void video_write_mem_savestate_new(SceUID savestate_file);

extern void (*update_screen_old)(void);
extern void (*update_screen_new)(void);
extern u16 *screen_texture_old;
extern u16 *screen_texture_new;

/* Set by psp_video.c to the presenter for the current screen scale. */
void (*update_screen)(void);
/* GBA frame texture in PSP VRAM (0x04000000), after the two framebuffers. */
u16 *screen_texture = (u16 *)(0x4000000 + (PSP_FRAME_SIZE * 2));
/* BG2/BG3 affine reference points (signed 20.8 fixed point). The renderer
 * advances them per scanline; main.c reloads them at VBlank and memory.c on
 * BGxX/BGxY writes. */
s32 affine_reference_x[2];
s32 affine_reference_y[2];

static VideoDispatchState renderer_state = {
  VIDEO_DISPATCH_RENDERER_NEW,
  VIDEO_DISPATCH_RENDERER_NEW
};
static void (*active_scanline)(void) = update_scanline_new;

/* Copies the shared globals into each renderer's suffixed copy. Only the
 * screen_texture copies are read; the update_screen copies are unused. */
static void sync_renderer_state(void)
{
  screen_texture_old = screen_texture;
  screen_texture_new = screen_texture;
  update_screen_old = update_screen;
  update_screen_new = update_screen;
}

/* Called by main.c on reset and at the start of each emulated frame. */
void video_dispatch_frame_begin(void)
{
  sync_renderer_state();
  video_dispatch_state_request(&renderer_state, option_video_renderer);

  if(video_dispatch_state_begin_frame(&renderer_state))
  {
    active_scanline =
     video_dispatch_state_active(&renderer_state) == VIDEO_RENDERER_OLD ?
      update_scanline_old : update_scanline_new;
  }
}

void update_scanline(void)
{
  active_scanline();
}

/* Savestate hooks first settle the renderer so the active one handles it. */
void video_read_savestate(SceUID savestate_file)
{
  video_dispatch_frame_begin();
  if (video_dispatch_state_active(&renderer_state) == VIDEO_RENDERER_OLD)
    video_read_savestate_old(savestate_file);
  else
    video_read_savestate_new(savestate_file);
}

void video_write_mem_savestate(SceUID savestate_file)
{
  video_dispatch_frame_begin();
  if (video_dispatch_state_active(&renderer_state) == VIDEO_RENDERER_OLD)
    video_write_mem_savestate_old(savestate_file);
  else
    video_write_mem_savestate_new(savestate_file);
}
