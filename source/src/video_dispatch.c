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

void (*update_screen)(void);
u16 *screen_texture = (u16 *)(0x4000000 + (PSP_FRAME_SIZE * 2));
s32 affine_reference_x[2];
s32 affine_reference_y[2];

static VideoDispatchState renderer_state = {
  VIDEO_DISPATCH_RENDERER_NEW,
  VIDEO_DISPATCH_RENDERER_NEW
};
static void (*active_scanline)(void) = update_scanline_new;

static void sync_renderer_state(void)
{
  screen_texture_old = screen_texture;
  screen_texture_new = screen_texture;
  update_screen_old = update_screen;
  update_screen_new = update_screen;
}

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
