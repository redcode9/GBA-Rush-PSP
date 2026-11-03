/* Runtime switch between the three scanline renderers: video.c (old, built
 * as video_old_wrap.c), video_cc.cc (new, built as video_cc_wrap.cc) and
 * gba_ppu.c, which this file drives, on the main processor or, through
 * video_me.c, on the Media Engine. Owns the shared video globals and
 * forwards update_scanline, the presentation of each frame and the
 * savestate hooks to the renderer picked by option_video_renderer. The
 * choice only changes at frame start, so one frame never mixes renderers.
 */

#include "common.h"
#include "gba_ppu.h"
#include "video_dispatch_state.h"
#include "video_me.h"

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

/* gba_ppu reads the emulator's own palette RAM, OAM, VRAM and I/O
   registers. It hears of register writes, V-Blank and resets whichever
   renderer is active, so it is ready the moment it is picked. */
static GbaPpu ppu;

static void update_scanline_ppu(void)
{
  u32 line = pIO_REG(REG_VCOUNT);

  gba_ppu_draw_line(&ppu, line, screen_texture + line * GBA_LINE_SIZE);
}

/* gba_ppu on the Media Engine: only the mark that the line is due. */
static void update_scanline_me(void)
{
  video_me_line(pIO_REG(REG_VCOUNT));
}

/* reset_gba: the machine starts over. */
void video_reset(void)
{
  gba_ppu_init(&ppu, palette_ram, oam_ram, (u16 *)vram, io_registers);
  video_me_resend();
}

/* main.c, when V-Blank begins. */
void video_start_vblank(void)
{
  gba_ppu_start_frame(&ppu);
  if(video_me_drawing())
    video_me_vblank();
}

/* memory.c, after storing a write to a video register, DISPCNT to BLDY
   (address is the offset from 0x4000000; a write to either half of a
   reference point reports its low half). */
void video_io_written(u32 address)
{
  gba_ppu_write_io(&ppu, address, io_registers[address >> 1]);
  if(!video_me_drawing())
    return;
  video_me_io(address);
  if(address >= 0x28 && address < 0x40 && (address & 8))
    video_me_io(address + 2);
}

/* main.c at the start of V-Blank, for a frame that is drawn. */
void video_present(void)
{
  if(video_me_drawing())
    video_me_present();
  else
    (*update_screen)();
}

/* main.c at each line of V-Blank, and at its end before the flip. */
void video_present_poll(void)
{
  video_me_present_poll();
}

void video_present_finish(void)
{
  video_me_present_finish();
}

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
    u32 active = video_dispatch_state_active(&renderer_state);

    if(active == VIDEO_RENDERER_OLD)
      active_scanline = update_scanline_old;
    else if(active == VIDEO_RENDERER_NEW)
      active_scanline = update_scanline_new;
  }
  /* Exact draws on the Media Engine whenever it is up, which can change
     from one frame to the next. */
  if(video_dispatch_state_active(&renderer_state) == VIDEO_RENDERER_PPU)
    active_scanline = video_me_frame_begin(1) ? update_scanline_me :
     update_scanline_ppu;
  else
    video_me_frame_begin(0);
}

void update_scanline(void)
{
  active_scanline();
}

/* Savestate hooks first settle the renderer so the active one handles it;
   with gba_ppu active the new renderer's hooks keep the file format. */
void video_read_savestate(SceUID savestate_file)
{
  video_dispatch_frame_begin();
  if (video_dispatch_state_active(&renderer_state) == VIDEO_RENDERER_OLD)
    video_read_savestate_old(savestate_file);
  else
    video_read_savestate_new(savestate_file);
  /* gba_ppu keeps its own reference points: reload them from the loaded
     registers, as V-Blank would. The Media Engine's copy is all new. */
  gba_ppu_start_frame(&ppu);
  video_me_resend();
}

void video_write_mem_savestate(SceUID savestate_file)
{
  video_dispatch_frame_begin();
  if (video_dispatch_state_active(&renderer_state) == VIDEO_RENDERER_OLD)
    video_write_mem_savestate_old(savestate_file);
  else
    video_write_mem_savestate_new(savestate_file);
}
