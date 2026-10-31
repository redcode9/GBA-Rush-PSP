/* PSP display output. Sets up the GU, owns the two 512x272 RGBA5551
 * framebuffers and presents the GBA texture (screen_texture) through the
 * scaler that option_screen_scale selects (update_screen). Also draws the
 * menu boxes and text for gui.c. Presents are queued without waiting;
 * video_sync_present() waits for the GE before the CPU touches VRAM again.
 */

#include "common.h"
#include "perf_monitor.h"
#include "psp_video.h"
/* Icon bitmaps for draw_volume(). */
#include "volume_icon.c"

/* VRAM layout, as offsets from the 2 MiB EDRAM base (CPU 0x04000000):
 *   0x000000  framebuffer 0 (PSP_FRAME_SIZE bytes)
 *   0x044000  framebuffer 1
 *   0x088000  GBA texture, 256x256 16-bit (screen_texture); its rows from
 *             VOLICON_OFFSET down hold the volume icons
 *   0x0A8000  sharp mode render target, 512x512 (480x320 used)
 * The GU takes these offsets as is; CPU pointers add the VRAM base.
 */
#define VOLICON_OFFSET (GBA_SCREEN_HEIGHT + 32)
/* Frames are drawn as sprites 64 texels wide, which suits the GE texture
 * cache better than one large sprite; each strip needs two vertices. */
#define SLICE (64)
#define VERTEX_COUNT (2 * ((u16)(GBA_SCREEN_WIDTH / SLICE) + ((GBA_SCREEN_WIDTH % SLICE) ? 1 : 0)))
#define GBA_TEXTURE_BACKING_BYTES (256 * 256 * 2)
#define SHARP_TEXTURE_WIDTH 512
#define SHARP_TEXTURE_HEIGHT 512
#define SHARP_ACTIVE_WIDTH 480
#define SHARP_ACTIVE_HEIGHT 320
#define SHARP_TEXTURE_OFFSET ((PSP_FRAME_SIZE * 2) + GBA_TEXTURE_BACKING_BYTES)
#define SHARP_TEXTURE_BYTES \
 (SHARP_TEXTURE_WIDTH * SHARP_TEXTURE_HEIGHT * 2)
#define PSP_EDRAM_BYTES (2 * 1024 * 1024)

#if (SHARP_TEXTURE_OFFSET + SHARP_TEXTURE_BYTES) > PSP_EDRAM_BYTES
#error Sharp video texture exceeds PSP EDRAM
#endif

/* GU_DIRECT list reused by every immediate draw. */
static u32 ALIGN_PSPDATA display_list[512];
/* Call list holding the scaled frame blit, built by generate_display_list(). */
static u32 ALIGN_PSPDATA display_list_0[256];

/* Front and back framebuffers, as VRAM offsets. */
static void *disp_frame;
static void *draw_frame;
/* A frame list is queued on the GE and has not been waited for yet. */
static u32 present_pending;
/* Set during sleep and shutdown so bitbilt_gu/bitbilt_sharp queue nothing. */
static u32 present_blocked;
/* One-shot: wait for the next present right away (set after resume). */
static u32 synchronous_present;
/* Time spent in the last video_sync_present() wait. */
static u32 last_ge_wait_us;
static u32 adaptive_last_frame_time;
static int adaptive_has_last_frame_time;
static VideoAdaptiveState adaptive_video;

/* The same buffer seen by the GU (as a render target) and the CPU. */
static void *sharp_draw_frame = (void *)SHARP_TEXTURE_OFFSET;
static u16 *sharp_texture = (u16 *)(0x04000000 + SHARP_TEXTURE_OFFSET);

/* Vertex layouts for the GU_VERTEX_16BIT draws below: textured, and
 * untextured with a 5551 or 8888 color. */
typedef struct
{
  u16 u, v;
  u16 x, y, z;
} Vertex;

typedef struct
{
  u16 color;
  u16 x, y, z;
} VertexLineCol16;

typedef struct
{
  u32 color;
  u16 x, y, z;
} VertexLineCol32;

static void set_gba_resolution(void);
static void generate_display_list(float scale_x, float scale_y,
 int clear_before_draw);
static void bitbilt_gu(void);
static void bitbilt_sharp(void);
static void bitbilt_sw(void);
static int draw_scaled_slices(u32 source_width, u32 source_height,
 u32 destination_width, u32 destination_height, u32 slice_width);
static void apply_adaptive_quality(void);
static VideoAdaptiveQuality selected_manual_quality(void);
static u32 selected_texture_filter(void);
static void restore_gu_state(void);
static void *psp_vram_addr(void *frame, u32 x, u32 y);
static void load_volume_icon(int devkit_version);
static void draw_volume(int volume);

int (*__draw_volume_status)(int draw);


/* ------------------------------------------------------------------
   Popup depth frame — color math and 5-layer renderer
   ------------------------------------------------------------------ */

static void color15_to_rgb(u16 c, u8 *r, u8 *g, u8 *b)
{
    *r = (c >> 0) & 0x1F;
    *g = (c >> 5) & 0x1F;
    *b = (c >> 10) & 0x1F;
}

static u16 rgb_to_color15(u8 r, u8 g, u8 b)
{
    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10);
}

/* Darken 15-bit RGB5551 color by pct% (0-100) */
u16 color15_darken(u16 c, int pct)
{
    u8 r, g, b;
    color15_to_rgb(c, &r, &g, &b);
    r = (r * (100 - pct)) / 100;
    g = (g * (100 - pct)) / 100;
    b = (b * (100 - pct)) / 100;
    return rgb_to_color15(r, g, b);
}

/* Lighten 15-bit RGB5551 color by pct% (0-100) */
u16 color15_lighten(u16 c, int pct)
{
    u8 r, g, b;
    color15_to_rgb(c, &r, &g, &b);
    r = r + ((31 - r) * pct) / 100;
    g = g + ((31 - g) * pct) / 100;
    b = b + ((31 - b) * pct) / 100;
    return rgb_to_color15(r, g, b);
}

/* Convert 15-bit color to 32-bit ARGB for draw_box_alpha */
u32 color15_to_argb(u16 c, u8 alpha)
{
    u8 r5, g5, b5;
    color15_to_rgb(c, &r5, &g5, &b5);
    u8 r8 = (r5 << 3) | (r5 >> 2);
    u8 g8 = (g5 << 3) | (g5 >> 2);
    u8 b8 = (b5 << 3) | (b5 >> 2);
    return ((u32)alpha << 24) | ((u32)r8 << 16) | ((u32)g8 << 8) | b8;
}

#define POPUP_SHADOW_OFS   3
#define POPUP_BORDER       1

/* External theme colors from gui.c */
extern u16 color_bg;
extern u16 color_active_item;

void draw_popup_frame(int x, int y, int w, int h, u16 bg_color)
{
    /* 1. FULL-SCREEN DIMMER — pushes background back with alpha */
    u32 dim_color = 0x8F000000;  /* Pure black, ~55% alpha — no hue shift */
    draw_box_alpha(0, 0, PSP_SCREEN_WIDTH - 1, PSP_SCREEN_HEIGHT - 1, dim_color);

    /* 2. DROP SHADOW — solid dark rectangle offset by 3px */
    u16 shadow_color = color15_darken(color_bg, 60);
    draw_box_fill(x + POPUP_SHADOW_OFS, y + POPUP_SHADOW_OFS,
                  x + POPUP_SHADOW_OFS + w - 1, y + POPUP_SHADOW_OFS + h - 1,
                  shadow_color);

    /* 3. OUTER BORDER — uses active item color for guaranteed contrast */
    draw_box_line(x - POPUP_BORDER, y - POPUP_BORDER,
                  x + w + POPUP_BORDER - 1, y + h + POPUP_BORDER - 1,
                  color_active_item);

    /* 4. POPUP BACKGROUND */
    draw_box_fill(x, y, x + w - 1, y + h - 1, bg_color);

    /* 5. TOP HIGHLIGHT — subtle 1px bevel for 3D lift */
    u16 highlight = color15_lighten(bg_color, 15);
    draw_hline(x + 1, x + w - 2, y + 1, highlight);
}

/* Auto-derive popup bg from theme (slightly lighter than menu bg) */
void draw_popup_frame_auto(int x, int y, int w, int h)
{
    u16 popup_bg = color15_lighten(color_bg, 10);
    draw_popup_frame(x, y, w, h, popup_bg);
}

/* Waits for a GU_DIRECT list to finish, timed when perf capture is on. */
static void video_ge_sync_direct(void)
{
  int measuring = perf_monitor_active();
  u32 sync_start;

  if(measuring)
    sync_start = sceKernelGetSystemTimeLow();

  sceGuSync(0, GU_SYNC_FINISH);

  if(measuring)
    perf_monitor_record_ge_sync_direct(
     sceKernelGetSystemTimeLow() - sync_start);
}

/* Flushes CPU-drawn pixels (screen_texture) so the GE reads fresh data. */
static void video_dcache_writeback(void)
{
  int measuring = perf_monitor_active();
  u32 writeback_start;

  if(measuring)
    writeback_start = sceKernelGetSystemTimeLow();

  sceKernelDcacheWritebackAll();

  if(measuring)
    perf_monitor_record_dcache_writeback(
     sceKernelGetSystemTimeLow() - writeback_start);
}

/* Center of the GE's 4096x4096 virtual screen; sceGuOffset and
 * sceGuViewport place the visible area around it. */
#define GU_COORD_CENTER 2048

void init_video(int devkit_version)
{
  disp_frame = (void *)0;
  draw_frame = (void *)PSP_FRAME_SIZE;
  present_pending = 0;
  present_blocked = 0;
  synchronous_present = 0;
  last_ge_wait_us = 0;
  adaptive_has_last_frame_time = 0;
  adaptive_last_frame_time = 0;
  video_adaptive_init(&adaptive_video, 0);

  sceGuDisplay(GU_FALSE);

  sceGuInit();
  sceGuStart(GU_DIRECT, display_list);

  sceGuDrawBuffer(GU_PSM_5551, draw_frame, PSP_LINE_SIZE);
  sceGuDispBuffer(PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT, disp_frame, PSP_LINE_SIZE);

  sceGuOffset(GU_COORD_CENTER - (PSP_SCREEN_WIDTH / 2),
   GU_COORD_CENTER - (PSP_SCREEN_HEIGHT / 2));
  sceGuViewport(GU_COORD_CENTER, GU_COORD_CENTER, PSP_SCREEN_WIDTH,
   PSP_SCREEN_HEIGHT);

  sceGuScissor(0, 0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
  sceGuEnable(GU_SCISSOR_TEST);

  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);
  /* The 240x160 frame sits in a 256x256 texture (GE sizes are powers of 2). */
  sceGuTexImage(0, 256, 256, GBA_LINE_SIZE, screen_texture);
  sceGuTexFlush();
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuEnable(GU_TEXTURE_2D);

  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuDisable(GU_BLEND);

  sceGuFinish();
  sceGuSync(0, GU_SYNC_FINISH);

  sceDisplayWaitVblankStart();
  sceGuDisplay(GU_TRUE);

  generate_display_list(1.5f, 1.5f, 1);
  update_screen = bitbilt_gu;

  load_volume_icon(devkit_version);
}

void video_term(void)
{
  present_blocked = 1;
  video_sync_present();
  sceGuDisplay(GU_FALSE);
  sceGuTerm();
}

/* Waits for the queued frame list, if any, and records how long it took. */
void video_sync_present(void)
{
  u32 wait_start;

  if(!present_pending)
    return;

  wait_start = sceKernelGetSystemTimeLow();
  sceGuSync(0, GU_SYNC_FINISH);
  last_ge_wait_us = sceKernelGetSystemTimeLow() - wait_start;
  present_pending = 0;
}

u32 video_last_ge_wait_us(void)
{
  return last_ge_wait_us;
}

/* Called from main.c's foreground power handling, never from the power
 * callback itself (scripts/check-video.sh enforces this). */
void video_power_suspend(void)
{
  present_blocked = 1;
  video_sync_present();
}

/* After sleep: reset timing, rebuild the GU state and wait on the next
 * present. */
void video_power_resume(void)
{
  video_sync_present();
  last_ge_wait_us = 0;
  adaptive_has_last_frame_time = 0;
  adaptive_last_frame_time = 0;
  video_adaptive_reset(&adaptive_video);
  set_gba_resolution();
  restore_gu_state();
  synchronous_present = 1;
  present_blocked = 0;
}

VideoAdaptiveQuality video_active_quality(void)
{
  return adaptive_video.quality;
}

/* Called by main.c once per emulated frame; feeds the frame-to-frame time
 * to the perf monitor and, in 16:9 auto mode, to the quality governor. */
void video_adaptive_frame_complete(int frame_skipped, int audio_event)
{
  u32 frame_time;
  u32 now;

  now = sceKernelGetSystemTimeLow();
  if(!adaptive_has_last_frame_time)
  {
    adaptive_has_last_frame_time = 1;
    adaptive_last_frame_time = now;
    return;
  }

  frame_time = now - adaptive_last_frame_time;
  adaptive_last_frame_time = now;

  perf_monitor_record_frame(frame_time, last_ge_wait_us, frame_skipped,
   synchronize_flag == 0);

  if(option_screen_scale != SCALED_16X9_AUTO)
    return;

  if(video_adaptive_record_frame(&adaptive_video, frame_time,
     last_ge_wait_us, frame_skipped, audio_event))
    apply_adaptive_quality();
}

void video_reset_frame_timing(void)
{
  adaptive_has_last_frame_time = 0;
  adaptive_last_frame_time = 0;
}

void flip_screen(u32 vsync)
{
  video_sync_present();

  if (vsync != 0)
  {
    int measuring = perf_monitor_active();
    u32 vblank_start;

    if(measuring)
      vblank_start = sceKernelGetSystemTimeLow();

    sceDisplayWaitVblankStart();

    if(measuring)
      perf_monitor_record_wait(PERF_METRICS_WAIT_PRESENT,
       sceKernelGetSystemTimeLow() - vblank_start);
  }

  disp_frame = draw_frame;
  draw_frame = sceGuSwapBuffers();
}

/* VRAM through its uncached mirror (0x40000000 | 0x04000000): CPU writes
 * reach the GE without a dcache flush. */
#define PSP_VRAM_UNCACHED 0x44000000

/* CPU pointer to pixel (x, y) of a framebuffer given as a VRAM offset;
 * y << 9 is y * PSP_LINE_SIZE. */
static void *psp_vram_addr(void *frame, u32 x, u32 y)
{
  return (void *)(((u32)frame | PSP_VRAM_UNCACHED) + ((x + (y << 9)) << 1));
}

/* Records display_list_0: the GBA frame scaled by (scale_x, scale_y) and
 * centered, drawn as SLICE-wide sprites. bitbilt_gu() replays it. */
static void generate_display_list(float scale_x, float scale_y,
 int clear_before_draw)
{
  u32 i;
  s32 dx, dy;
  s32 dw, dh;
  Vertex *vertices, *vertices_tmp;

  dw = (s32)(GBA_SCREEN_WIDTH  * scale_x);
  dh = (s32)(GBA_SCREEN_HEIGHT * scale_y);

  dx = (PSP_SCREEN_WIDTH  - dw) / 2;
  dy = (PSP_SCREEN_HEIGHT - dh) / 2;

  sceGuStart(GU_CALL, display_list_0);

  if(clear_before_draw)
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

  vertices = (Vertex *)sceGuGetMemory(VERTEX_COUNT * sizeof(Vertex));

  if (vertices != NULL)
  {
    memset(vertices, 0, VERTEX_COUNT * sizeof(Vertex));

    vertices_tmp = vertices;

    for (i = 0; (i + SLICE) < GBA_SCREEN_WIDTH; i += SLICE)
    {
      vertices_tmp[0].u = i;
      vertices_tmp[0].v = 0;
      vertices_tmp[0].x = dx + (u16)((float)i * scale_x);
      vertices_tmp[0].y = dy;

      vertices_tmp[1].u = i + SLICE;
      vertices_tmp[1].v = GBA_SCREEN_HEIGHT;
      vertices_tmp[1].x = dx + (u16)((float)(i + SLICE) * scale_x);
      vertices_tmp[1].y = dy + dh;

      vertices_tmp += 2;
    }

    if (i < GBA_SCREEN_WIDTH)
    {
      vertices_tmp[0].u = i;
      vertices_tmp[0].v = 0;
      vertices_tmp[0].x = dx + (u16)((float)i * scale_x);
      vertices_tmp[0].y = dy;

      vertices_tmp[1].u = GBA_SCREEN_WIDTH;
      vertices_tmp[1].v = GBA_SCREEN_HEIGHT;
      vertices_tmp[1].x = dx + dw;
      vertices_tmp[1].y = dy + dh;
    }

    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, VERTEX_COUNT, 0, vertices);
  }

  sceGuFinish();
}

static VideoAdaptiveQuality selected_manual_quality(void)
{
  if(option_screen_scale == SCALED_16X9_SHARP)
    return VIDEO_ADAPTIVE_QUALITY_SHARP;

  if(option_screen_filter == FILTER_NEAREST)
    return VIDEO_ADAPTIVE_QUALITY_CRISP;

  return VIDEO_ADAPTIVE_QUALITY_SMOOTH;
}

static u32 selected_texture_filter(void)
{
  if(option_screen_scale == SCALED_16X9_AUTO &&
     adaptive_video.quality == VIDEO_ADAPTIVE_QUALITY_CRISP)
    return GU_NEAREST;

  return option_screen_filter;
}

/* Reapplies the render state that init_video() set up. */
static void restore_gu_state(void)
{
  u32 texture_filter = selected_texture_filter();

  sceGuStart(GU_DIRECT, display_list);
  sceGuDrawBufferList(GU_PSM_5551, draw_frame, PSP_LINE_SIZE);
  sceGuOffset(GU_COORD_CENTER - (PSP_SCREEN_WIDTH / 2),
   GU_COORD_CENTER - (PSP_SCREEN_HEIGHT / 2));
  sceGuViewport(GU_COORD_CENTER, GU_COORD_CENTER, PSP_SCREEN_WIDTH,
   PSP_SCREEN_HEIGHT);
  sceGuScissor(0, 0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);
  sceGuTexImage(0, 256, 256, GBA_LINE_SIZE, screen_texture);
  sceGuTexFilter(texture_filter, texture_filter);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuDisable(GU_BLEND);
  sceGuTexFlush();
  sceGuFinish();
  video_ge_sync_direct();
}

/* Presents with the prebuilt call list. Returns without waiting for the
 * GE; the next video_sync_present() does. */
static void bitbilt_gu(void)
{
  if(present_blocked)
    return;

  video_sync_present();
  last_ge_wait_us = 0;
  video_dcache_writeback();

  sceGuStart(GU_DIRECT, display_list);

  sceGuCallList(display_list_0);

  sceGuFinish();
  present_pending = 1;

  if(selected_texture_filter() == GU_NEAREST)
    perf_monitor_record_quality_presented(0);

  if(synchronous_present)
  {
    video_sync_present();
    synchronous_present = 0;
  }
}

/* Draws the bound texture's top-left source rectangle stretched to the
 * destination size at (0, 0). Returns 0 if the GU ran out of list memory. */
static int draw_scaled_slices(u32 source_width, u32 source_height,
 u32 destination_width, u32 destination_height, u32 slice_width)
{
  u32 source_x;
  u32 vertex_count = 2 * ((source_width + slice_width - 1) / slice_width);
  Vertex *vertices = (Vertex *)sceGuGetMemory(vertex_count * sizeof(Vertex));
  Vertex *vertex = vertices;

  if(vertices == NULL)
    return 0;

  memset(vertices, 0, vertex_count * sizeof(Vertex));

  for(source_x = 0; source_x < source_width; source_x += slice_width)
  {
    u32 source_end = source_x + slice_width;

    if(source_end > source_width)
      source_end = source_width;

    vertex[0].u = (u16)source_x;
    vertex[0].v = 0;
    vertex[0].x = (u16)((source_x * destination_width) / source_width);
    vertex[0].y = 0;

    vertex[1].u = (u16)source_end;
    vertex[1].v = (u16)source_height;
    vertex[1].x = (u16)((source_end * destination_width) / source_width);
    vertex[1].y = (u16)destination_height;
    vertex += 2;
  }

  sceGuDrawArray(GU_SPRITES,
   GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
   vertex_count, 0, vertices);
  return 1;
}

/* Two-pass 16:9 scaler: an exact 2x nearest upscale into the 480x320
 * sharp target, then a bilinear squeeze to 480x272. Pixels stay crisp
 * without the uneven widths of a direct nearest scale. Falls back to
 * CRISP if either pass fails. */
static void bitbilt_sharp(void)
{
  int first_pass_ok;
  int second_pass_ok;

  if(present_blocked)
    return;

  video_sync_present();
  last_ge_wait_us = 0;
  video_dcache_writeback();

  sceGuStart(GU_DIRECT, display_list);

  sceGuDrawBufferList(GU_PSM_5551, sharp_draw_frame, SHARP_TEXTURE_WIDTH);
  sceGuOffset(GU_COORD_CENTER - (SHARP_ACTIVE_WIDTH / 2),
   GU_COORD_CENTER - (SHARP_ACTIVE_HEIGHT / 2));
  sceGuViewport(GU_COORD_CENTER, GU_COORD_CENTER, SHARP_ACTIVE_WIDTH,
   SHARP_ACTIVE_HEIGHT);
  sceGuScissor(0, 0, SHARP_ACTIVE_WIDTH, SHARP_ACTIVE_HEIGHT);
  sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);
  sceGuTexImage(0, 256, 256, GBA_LINE_SIZE, screen_texture);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexFlush();

  first_pass_ok = draw_scaled_slices(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT,
   SHARP_ACTIVE_WIDTH, SHARP_ACTIVE_HEIGHT, SLICE);

  sceGuTexFlush();
  sceGuDrawBufferList(GU_PSM_5551, draw_frame, PSP_LINE_SIZE);
  sceGuOffset(GU_COORD_CENTER - (PSP_SCREEN_WIDTH / 2),
   GU_COORD_CENTER - (PSP_SCREEN_HEIGHT / 2));
  sceGuViewport(GU_COORD_CENTER, GU_COORD_CENTER, PSP_SCREEN_WIDTH,
   PSP_SCREEN_HEIGHT);
  sceGuScissor(0, 0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
  sceGuTexImage(0, SHARP_TEXTURE_WIDTH, SHARP_TEXTURE_HEIGHT,
   SHARP_TEXTURE_WIDTH, sharp_texture);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);

  second_pass_ok = draw_scaled_slices(SHARP_ACTIVE_WIDTH,
   SHARP_ACTIVE_HEIGHT, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT, SLICE);

  sceGuTexImage(0, 256, 256, GBA_LINE_SIZE, screen_texture);
  sceGuTexFilter(option_screen_filter, option_screen_filter);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexFlush();
  sceGuFinish();

  if(first_pass_ok && second_pass_ok)
  {
    present_pending = 1;
    perf_monitor_record_quality_presented(1);
  }
  else
  {
    video_ge_sync_direct();
    adaptive_video.quality = VIDEO_ADAPTIVE_QUALITY_CRISP;
    apply_adaptive_quality();
  }

  if(synchronous_present && present_pending)
  {
    video_sync_present();
    synchronous_present = 0;
  }
}

/* Average of two 5:5:5 colors without unpacking; 0x7bde drops each
 * channel's low bit so the shift cannot borrow from the next channel. */
#define NORMAL_BLEND(c0, c1) ((c0 & c1) + (((c0 ^ c1) & 0x7bde) >> 1))

/* Software x1.5 scaler: each 2x2 GBA block becomes 3x3 PSP pixels, the
 * middle row and column being averages. The 360x240 result is centered at
 * (60, 16). */
static void bitbilt_sw(void)
{
  u32 x, y;
  u16 *vptr, *vptr0;
  u16 *d, *d0;

  video_sync_present();
  video_dcache_writeback();

  sceGuStart(GU_DIRECT, display_list);

  sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

  sceGuFinish();
  video_ge_sync_direct();

  vptr0 = (u16 *)psp_vram_addr(draw_frame, 60, 16);
  d0 = screen_texture;

  for (y = 0; y < (GBA_SCREEN_HEIGHT / 2); y++)
  {
    vptr = vptr0;
    d = d0;

    for (x = 0; x < (GBA_SCREEN_WIDTH / 2); x++, d += 2)
    {
      vptr[0] = d[0];
      vptr[2] = d[1];

      vptr[0 + PSP_LINE_SIZE * 2] = d[0 + GBA_LINE_SIZE];
      vptr[2 + PSP_LINE_SIZE * 2] = d[1 + GBA_LINE_SIZE];

      *++vptr = NORMAL_BLEND(d[0], d[1]);
      vptr += (PSP_LINE_SIZE - 1);

      *vptr++ = NORMAL_BLEND(d[0], d[0 + GBA_LINE_SIZE]);
      *vptr++ = NORMAL_BLEND(NORMAL_BLEND(d[0], d[0 + GBA_LINE_SIZE]), NORMAL_BLEND(d[1], d[1 + GBA_LINE_SIZE]));
      *vptr   = NORMAL_BLEND(d[1], d[1 + GBA_LINE_SIZE]);
      vptr += (PSP_LINE_SIZE - 1);

      *vptr   = NORMAL_BLEND(d[0 + GBA_LINE_SIZE], d[1 + GBA_LINE_SIZE]);
      vptr += (2 - PSP_LINE_SIZE * 2);
    }

    vptr0 += (PSP_LINE_SIZE * 3);
    d0 += (GBA_LINE_SIZE * 2);
  }
}

/* Called at startup and when entering the menu; nothing to do on PSP. */
void video_resolution_large(void)
{
}

/* Called before gameplay starts or resumes: applies the screen scale
 * option and restarts the adaptive governor. */
void video_resolution_small(void)
{
  u32 gameplay_filter = option_screen_filter;

  video_sync_present();
  adaptive_has_last_frame_time = 0;
  adaptive_last_frame_time = 0;
  video_adaptive_set_enabled(&adaptive_video,
   option_screen_scale == SCALED_16X9_AUTO,
   selected_manual_quality());
  set_gba_resolution();

  if(option_screen_scale == SCALED_16X9_AUTO &&
     adaptive_video.quality == VIDEO_ADAPTIVE_QUALITY_CRISP)
    gameplay_filter = GU_NEAREST;

  sceGuStart(GU_DIRECT, display_list);

  sceGuClearColor(0);
  sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

  sceGuTexFilter(gameplay_filter, gameplay_filter);

  sceGuFinish();
  video_ge_sync_direct();
}

/* Picks the presenter (update_screen) for option_screen_scale. */
static void set_gba_resolution(void)
{
  switch (option_screen_scale)
  {
    case SCALED_NONE:
      generate_display_list(1.0f, 1.0f, 1);
      update_screen = bitbilt_gu;
      break;

    case SCALED_X15_GU:
      generate_display_list(1.5f, 1.5f, 1);
      update_screen = bitbilt_gu;
      break;

    case SCALED_X15_SW:
      update_screen = bitbilt_sw;
      break;

    case SCALED_USER:
      generate_display_list(option_screen_mag / 100.0f,
       option_screen_mag / 100.0f, 1);
      update_screen = bitbilt_gu;
      break;

    case SCALED_16X9_GU:
      generate_display_list((float)PSP_SCREEN_WIDTH / (float)GBA_SCREEN_WIDTH,
                            (float)PSP_SCREEN_HEIGHT / (float)GBA_SCREEN_HEIGHT,
                            0);
      update_screen = bitbilt_gu;
      break;

    case SCALED_16X9_SHARP:
      update_screen = bitbilt_sharp;
      break;

    case SCALED_16X9_AUTO:
      apply_adaptive_quality();
      break;
  }
}

/* SHARP uses the two-pass scaler; CRISP a single pass with nearest. */
static void apply_adaptive_quality(void)
{
  if(adaptive_video.quality == VIDEO_ADAPTIVE_QUALITY_SHARP)
  {
    update_screen = bitbilt_sharp;
  }
  else
  {
    generate_display_list((float)PSP_SCREEN_WIDTH /
                           (float)GBA_SCREEN_WIDTH,
                          (float)PSP_SCREEN_HEIGHT /
                           (float)GBA_SCREEN_HEIGHT, 0);

    video_sync_present();
    sceGuStart(GU_DIRECT, display_list);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuFinish();
    video_ge_sync_direct();
    update_screen = bitbilt_gu;
  }
}

/* color is a COLOR32 value, stored as 5551 with the alpha bit set. */
void clear_screen(u32 color)
{
  u32 r8 = (color >>  0) & 0xFF;
  u32 g8 = (color >>  8) & 0xFF;
  u32 b8 = (color >> 16) & 0xFF;
  u16 color16 = ((r8 >> 3) << 0) | ((g8 >> 3) << 5) | ((b8 >> 3) << 10) | 0x8000;

  u16 *vram_ptr;
  u32 pixels = PSP_LINE_SIZE * PSP_SCREEN_HEIGHT;

  video_sync_present();
  vram_ptr = (u16 *)((u32)draw_frame | PSP_VRAM_UNCACHED);

  for (u32 i = 0; i < pixels; i++)
  {
    vram_ptr[i] = color16;
  }
}

void clear_texture(u16 color)
{
  u32 x, y;
  u16 *p_dest, *p_dest0;

  video_sync_present();
  p_dest0 = screen_texture;

  for (y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    p_dest = p_dest0;

    for (x = 0; x < GBA_SCREEN_WIDTH; x++, p_dest++)
      *p_dest = color;

    p_dest0 += GBA_LINE_SIZE;
  }
}

/* Returns a malloc'd 240x160 copy of the last GBA frame for the menu
 * background; the caller frees it. */
u16 *copy_screen(void)
{
  u32 x, y;
  u16 *copy;
  u16 *p_src, *p_src0;
  u16 *p_dest;

  // Wait for vblank and sync GPU/cache to ensure screen_texture is stable before copying
  // This prevents crashes when menu is called during active gameplay
  sceDisplayWaitVblankStart();
  video_sync_present();
  video_ge_sync_direct();
  video_dcache_writeback();

  copy = (u16 *)safe_malloc(GBA_SCREEN_SIZE);

  p_src0 = screen_texture;
  p_dest = copy;

  for (y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    p_src = p_src0;

    for (x = 0; x < GBA_SCREEN_WIDTH; x++, p_src++, p_dest++)
      *p_dest = *p_src;

    p_src0 += GBA_LINE_SIZE;
  }

  return copy;
}

void blit_to_screen(u16 *src, u16 w, u16 h, u16 dest_x, u16 dest_y)
{
  u32 x, y;
  u16 *p_src, *p_dest, *p_dest0;

  video_sync_present();
  p_src   = src;
  p_dest0 = (u16 *)psp_vram_addr(draw_frame, dest_x, dest_y);

  for (y = 0; y < h; y++)
  {
    p_dest = p_dest0;

    for (x = 0; x < w; x++, p_src++, p_dest++)
      *p_dest = *p_src;

    p_dest0 += PSP_LINE_SIZE;
  }
}

void draw_box_line(u16 x1, u16 y1, u16 x2, u16 y2, u16 color)
{
  VertexLineCol16 *vertices;

  video_sync_present();
  sceGuStart(GU_DIRECT, display_list);

  sceGuDisable(GU_TEXTURE_2D);

  vertices = (VertexLineCol16 *)sceGuGetMemory(5 * sizeof(VertexLineCol16));

  if (vertices != NULL)
  {
    memset(vertices, 0, 5 * sizeof(VertexLineCol16));

    vertices[0].x = x1;
    vertices[0].y = y1;
    vertices[0].color = color;

    vertices[1].x = x2;
    vertices[1].y = y1;
    vertices[1].color = color;

    vertices[2].x = x2;
    vertices[2].y = y2 + 1;
    vertices[2].color = color;

    vertices[3].x = x1;
    vertices[3].y = y2;
    vertices[3].color = color;

    vertices[4].x = x1;
    vertices[4].y = y1;
    vertices[4].color = color;

    sceGuDrawArray(GU_LINE_STRIP, GU_COLOR_5551 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 5, NULL, vertices);
  }

  sceGuEnable(GU_TEXTURE_2D);

  sceGuFinish();
  video_ge_sync_direct();
}

void draw_box_fill(u16 x1, u16 y1, u16 x2, u16 y2, u16 color)
{
  VertexLineCol16 *vertices;

  video_sync_present();
  sceGuStart(GU_DIRECT, display_list);

  sceGuDisable(GU_TEXTURE_2D);

  vertices = (VertexLineCol16 *)sceGuGetMemory(4 * sizeof(VertexLineCol16));

  if (vertices != NULL)
  {
    memset(vertices, 0, 4 * sizeof(VertexLineCol16));

    vertices[0].x = x1;
    vertices[0].y = y1;
    vertices[0].color = color;

    vertices[1].x = x2 + 1;
    vertices[1].y = y1;
    vertices[1].color = color;

    vertices[2].x = x1;
    vertices[2].y = y2 + 1;
    vertices[2].color = color;

    vertices[3].x = x2 + 1;
    vertices[3].y = y2 + 1;
    vertices[3].color = color;

    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_5551 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, NULL, vertices);
  }

  sceGuEnable(GU_TEXTURE_2D);

  sceGuFinish();
  video_ge_sync_direct();
}

void draw_box_alpha(u16 x1, u16 y1, u16 x2, u16 y2, u32 color)
{
  VertexLineCol32 *vertices;

  video_sync_present();
  sceGuStart(GU_DIRECT, display_list);

  sceGuDisable(GU_TEXTURE_2D);
  sceGuEnable(GU_BLEND);

  vertices = (VertexLineCol32 *)sceGuGetMemory(4 * sizeof(VertexLineCol32));

  if (vertices != NULL)
  {
    memset(vertices, 0, 4 * sizeof(VertexLineCol32));

    vertices[0].x = x1;
    vertices[0].y = y1;
    vertices[0].color = color;

    vertices[1].x = x2 + 1;
    vertices[1].y = y1;
    vertices[1].color = color;

    vertices[2].x = x1;
    vertices[2].y = y2 + 1;
    vertices[2].color = color;

    vertices[3].x = x2 + 1;
    vertices[3].y = y2 + 1;
    vertices[3].color = color;

    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, NULL, vertices);
  }

  sceGuDisable(GU_BLEND);
  sceGuEnable(GU_TEXTURE_2D);

  sceGuFinish();
  video_ge_sync_direct();
}

void draw_hline(u16 sx, u16 ex, u16 y, u16 color)
{
  VertexLineCol16 *vertices;

  video_sync_present();
  sceGuStart(GU_DIRECT, display_list);

  sceGuDisable(GU_TEXTURE_2D);

  vertices = (VertexLineCol16 *)sceGuGetMemory(2 * sizeof(VertexLineCol16));

  if (vertices != NULL)
  {
    memset(vertices, 0, 2 * sizeof(VertexLineCol16));

    vertices[0].x = sx;
    vertices[0].y = y;
    vertices[0].color = color;

    vertices[1].x = ex + 1;
    vertices[1].y = y;
    vertices[1].color = color;

    sceGuDrawArray(GU_LINES, GU_COLOR_5551 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, vertices);
  }

  sceGuEnable(GU_TEXTURE_2D);

  sceGuFinish();
  video_ge_sync_direct();
}

void draw_vline(u16 x, u16 sy, u16 ey, u16 color)
{
  VertexLineCol16 *vertices;

  video_sync_present();
  sceGuStart(GU_DIRECT, display_list);

  sceGuDisable(GU_TEXTURE_2D);

  vertices = (VertexLineCol16 *)sceGuGetMemory(2 * sizeof(VertexLineCol16));

  if (vertices != NULL)
  {
    memset(vertices, 0, 2 * sizeof(VertexLineCol16));

    vertices[0].x = x;
    vertices[0].y = sy;
    vertices[0].color = color;

    vertices[1].x = x;
    vertices[1].y = ey + 1;
    vertices[1].color = color;

    sceGuDrawArray(GU_LINES, GU_COLOR_5551 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, vertices);
  }

  sceGuEnable(GU_TEXTURE_2D);

  sceGuFinish();
  video_ge_sync_direct();
}

void print_string(const char *str, s16 x, u16 y, u16 fg_color, s16 bg_color)
{
  video_sync_present();
  if (x < 0) x = (PSP_SCREEN_WIDTH - (strlen(str) * FONTWIDTH)) >> 1;

  mh_print(str, x, y, fg_color, bg_color, (u16 *)((u32)draw_frame | PSP_VRAM_UNCACHED), PSP_LINE_SIZE);
}

void print_string_ext(const char *str, s16 x, u16 y, u16 fg_color, s16 bg_color, void *_dest_ptr, u16 pitch)
{
  if (x < 0) x = (pitch - (strlen(str) * FONTWIDTH)) >> 1;

  mh_print(str, x, y, fg_color, bg_color, _dest_ptr, pitch);
}

void print_string_gbk(const char *str, s16 x, u16 y, u16 fg_color, s16 bg_color)
{
  video_sync_present();
  if (x < 0) x = (PSP_SCREEN_WIDTH - (strlen(str) * FONTWIDTH)) >> 1;

  ch_print(str, x, y, fg_color, bg_color, (u16 *)((u32)draw_frame | PSP_VRAM_UNCACHED), PSP_LINE_SIZE);
}

void print_string_ext_gbk(const char *str, s16 x, u16 y, u16 fg_color, s16 bg_color, void *_dest_ptr, u16 pitch)
{
  if (x < 0) x = (pitch - (strlen(str) * FONTWIDTH)) >> 1;

  ch_print(str, x, y, fg_color, bg_color, _dest_ptr, pitch);
}

/* Expands the 4-bit icon masks from volume_icon.c into RGBA4444 texels
 * below the GBA frame in screen_texture: white for icons, black for their
 * shadows. Only on firmware 3.52 and later (devkit 0x03050210). */
static void load_volume_icon(int devkit_version)
{
  if (devkit_version >= 0x03050210)
  {
    u32 x, y, alpha;
    u16 *dst, *tex_volicon;

    tex_volicon = screen_texture + (GBA_LINE_SIZE * VOLICON_OFFSET);

    dst = tex_volicon + SPEEKER_X;
    for (y = 0; y < 32; y++)
    {
      for (x = 0; x < 32; x++)
      {
        if ((x & 1) != 0)
          alpha = icon_speeker[y][(x >> 1)] >> 4;
        else
          alpha = icon_speeker[y][(x >> 1)] & 0x0f;

        dst[x] = (alpha << 12) | 0x0fff;
      }
      dst += GBA_LINE_SIZE;
    }

    dst = tex_volicon + SPEEKER_SHADOW_X;
    for (y = 0; y < 32; y++)
    {
      for (x = 0; x < 32; x++)
      {
        if ((x & 1) != 0)
          alpha = icon_speeker_shadow[y][(x >> 1)] >> 4;
        else
          alpha = icon_speeker_shadow[y][(x >> 1)] & 0x0f;

        dst[x] = alpha << 12;
      }
      dst += GBA_LINE_SIZE;
    }

    dst = tex_volicon + VOLUME_BAR_X;
    for (y = 0; y < 32; y++)
    {
      for (x = 0; x < 12; x++)
      {
        if ((x & 1) != 0)
          alpha = icon_bar[y][(x >> 1)] >> 4;
        else
          alpha = icon_bar[y][(x >> 1)] & 0x0f;

        dst[x] = (alpha << 12) | 0x0fff;
      }
      dst += GBA_LINE_SIZE;
    }

    dst = tex_volicon + VOLUME_BAR_SHADOW_X;
    for (y = 0; y < 32; y++)
    {
      for (x = 0; x < 12; x++)
      {
        if ((x & 1) != 0)
          alpha = icon_bar_shadow[y][(x >> 1)] >> 4;
        else
          alpha = icon_bar_shadow[y][(x >> 1)] & 0x0f;

        dst[x] = alpha << 12;
      }
      dst += GBA_LINE_SIZE;
    }

    dst = tex_volicon + VOLUME_DOT_X;
    for (y = 0; y < 32; y++)
    {
      for (x = 0; x < 12; x++)
      {
        if ((x & 1) != 0)
          alpha = icon_dot[y][(x >> 1)] >> 4;
        else
          alpha = icon_dot[y][(x >> 1)] & 0x0f;

        dst[x] = (alpha << 12) | 0x0fff;
      }
      dst += GBA_LINE_SIZE;
    }

    dst = tex_volicon + VOLUME_DOT_SHADOW_X;
    for (y = 0; y < 32; y++)
    {
      for (x = 0; x < 12; x++)
      {
        if ((x & 1) != 0)
          alpha = icon_dot_shadow[y][(x >> 1)] >> 4;
        else
          alpha = icon_dot_shadow[y][(x >> 1)] & 0x0f;

        dst[x] = alpha << 12;
      }
      dst += GBA_LINE_SIZE;
    }
  }
}

/* Draws the speaker icon and a 30-step bar at the bottom left (y 230),
 * each element as a shadow sprite offset by 3 pixels plus the icon:
 * (1 speaker + 30 steps) x 2 sprites x 2 vertices = 2 * 31 * 2. */
static void draw_volume(int volume)
{
  Vertex *vertices, *vertices_tmp;

  video_sync_present();
  video_dcache_writeback();

  sceGuStart(GU_DIRECT, display_list);

  sceGuEnable(GU_BLEND);
  sceGuTexMode(GU_PSM_4444, 0, 0, GU_FALSE);

  vertices = (Vertex *)sceGuGetMemory(2 * 31 * 2 * sizeof(Vertex));

  if (vertices != NULL)
  {
    int i, x;

    memset(vertices, 0, 2 * 31 * 2 * sizeof(Vertex));
    vertices_tmp = vertices;

    x = 24;

    vertices_tmp[0].u = SPEEKER_SHADOW_X;
    vertices_tmp[0].v = 0 + VOLICON_OFFSET;
    vertices_tmp[0].x = 3 + x;
    vertices_tmp[0].y = 3 + 230;

    vertices_tmp[1].u = SPEEKER_SHADOW_X + 32;
    vertices_tmp[1].v = 32 + VOLICON_OFFSET;
    vertices_tmp[1].x = 3 + x + 32;
    vertices_tmp[1].y = 3 + 230 + 32;

    vertices_tmp += 2;

    vertices_tmp[0].u = SPEEKER_X;
    vertices_tmp[0].v = 0 + VOLICON_OFFSET;
    vertices_tmp[0].x = x;
    vertices_tmp[0].y = 230;

    vertices_tmp[1].u = SPEEKER_X + 32;
    vertices_tmp[1].v = 32 + VOLICON_OFFSET;
    vertices_tmp[1].x = x + 32;
    vertices_tmp[1].y = 230 + 32;

    vertices_tmp += 2;

    x = 64;

    for (i = 0; i < volume; i++)
    {
      vertices_tmp[0].u = VOLUME_BAR_SHADOW_X;
      vertices_tmp[0].v = 0 + VOLICON_OFFSET;
      vertices_tmp[0].x = 3 + x;
      vertices_tmp[0].y = 3 + 230;

      vertices_tmp[1].u = VOLUME_BAR_SHADOW_X + 12;
      vertices_tmp[1].v = 32 + VOLICON_OFFSET;
      vertices_tmp[1].x = 3 + x + 12;
      vertices_tmp[1].y = 3 + 230 + 32;

      vertices_tmp += 2;

      vertices_tmp[0].u = VOLUME_BAR_X;
      vertices_tmp[0].v = 0 + VOLICON_OFFSET;
      vertices_tmp[0].x = x;
      vertices_tmp[0].y = 230;

      vertices_tmp[1].u = VOLUME_BAR_X + 12;
      vertices_tmp[1].v = 32 + VOLICON_OFFSET;
      vertices_tmp[1].x = x + 12;
      vertices_tmp[1].y = 230 + 32;

      vertices_tmp += 2;

      x += 12;
    }

    for (; i < 30; i++)
    {
      vertices_tmp[0].u = VOLUME_DOT_SHADOW_X;
      vertices_tmp[0].v = 0 + VOLICON_OFFSET;
      vertices_tmp[0].x = 3 + x;
      vertices_tmp[0].y = 3 + 230;

      vertices_tmp[1].u = VOLUME_DOT_SHADOW_X + 12;
      vertices_tmp[1].v = 32 + VOLICON_OFFSET;
      vertices_tmp[1].x = 3 + x + 12;
      vertices_tmp[1].y = 3 + 230 + 32;

      vertices_tmp += 2;

      vertices_tmp[0].u = VOLUME_DOT_X;
      vertices_tmp[0].v = 0 + VOLICON_OFFSET;
      vertices_tmp[0].x = x;
      vertices_tmp[0].y = 230;

      vertices_tmp[1].u = VOLUME_DOT_X + 12;
      vertices_tmp[1].v = 32 + VOLICON_OFFSET;
      vertices_tmp[1].x = x + 12;
      vertices_tmp[1].y = 230 + 32;

      vertices_tmp += 2;

      x += 12;
    }

    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2 * 31 * 2, 0, vertices);
  }

  sceGuDisable(GU_BLEND);
  sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);

  sceGuFinish();
  video_ge_sync_direct();
}

/* Volume overlay, shown for 2 s after a volume key press. main.c installs
 * draw_volume_status_null in __draw_volume_status, so it is currently off. */
int draw_volume_status(int draw)
{
  static u64 disp_end = 0;
  int volume = kuImposeGetParam(PSP_IMPOSE_MAIN_VOLUME);

  if ((volume < 0) || (volume > 30))
    return 0;

  if (get_pad_input(PSP_CTRL_VOLUP | PSP_CTRL_VOLDOWN | PSP_CTRL_NOTE) != 0)
  {
    disp_end = ticker() + (2 * 1000 * 1000);
    draw = 1;
  }

  if (disp_end != 0)
  {
    if (ticker() < disp_end)
    {
      if (draw != 0)
        draw_volume(volume);
    }
    else
    {
      disp_end = 0;
    }
  }

  return 0;
}

int draw_volume_status_null(int draw)
{
  return 0;
}
