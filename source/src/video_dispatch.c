#include "common.h"

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

static u32 active_renderer = ~(u32)0;

static void sync_renderer_state(void)
{
  screen_texture_old = screen_texture;
  screen_texture_new = screen_texture;
  update_screen_old = update_screen;
  update_screen_new = update_screen;
}

static void select_renderer(void)
{
  if (active_renderer == option_video_renderer)
    return;

  active_renderer = option_video_renderer;
}

void update_scanline(void)
{
  sync_renderer_state();
  select_renderer();
  if (active_renderer == VIDEO_RENDERER_OLD)
    update_scanline_old();
  else
    update_scanline_new();
}

void video_read_savestate(SceUID savestate_file)
{
  sync_renderer_state();
  select_renderer();
  if (active_renderer == VIDEO_RENDERER_OLD)
    video_read_savestate_old(savestate_file);
  else
    video_read_savestate_new(savestate_file);
}

void video_write_mem_savestate(SceUID savestate_file)
{
  sync_renderer_state();
  select_renderer();
  if (active_renderer == VIDEO_RENDERER_OLD)
    video_write_mem_savestate_old(savestate_file);
  else
    video_write_mem_savestate_new(savestate_file);
}
