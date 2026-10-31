/* Host link stubs for psp_video.c in tests/test_perf_video_sentinel.c.
 * psp_video.c is compiled unmodified against the real PSP SDK headers
 * (host build, -w); this TU satisfies its external references on the
 * 64-bit host. sceKernelGetSystemTimeLow() is backed by a controllable
 * clock queue (host_video_time_*) so the test can exercise the
 * clock-zero and 32-bit wrap paths of the frame-time sentinel.
 * sceGu* signatures match pspdev pspgu.h exactly. */
#include <psptypes.h>
#include <pspgu.h>
#include <pspdisplay.h>
#include <pspimpose_driver.h>
#include <stddef.h>
#include <stdlib.h>

/* ---- Controllable low-resolution clock ---- */

#define HOST_VIDEO_TIME_CAPACITY 64

static u32 host_video_time_values[HOST_VIDEO_TIME_CAPACITY];
static size_t host_video_time_head = 0;
static size_t host_video_time_count = 0;
static u32 host_video_time_last = 0;

/* Each clock read pops the next pushed value; once the queue is empty it
   keeps returning the last one. */
void host_video_time_reset(void)
{
  host_video_time_head = 0;
  host_video_time_count = 0;
  host_video_time_last = 0;
}

void host_video_time_push(u32 value)
{
  if(host_video_time_count < HOST_VIDEO_TIME_CAPACITY)
    host_video_time_values[host_video_time_count] = value;
  host_video_time_count++;
  host_video_time_last = value;
}

u32 sceKernelGetSystemTimeLow(void)
{
  if(host_video_time_head < host_video_time_count)
    return host_video_time_values[host_video_time_head++];
  return host_video_time_last;
}

/* ---- PSP kernel / display ---- */

int sceKernelDcacheWritebackAll(void)
{
  return 0;
}

int sceDisplayWaitVblankStart(void)
{
  return 0;
}

/* ---- GE (no-op; not exercised by the frame-time path) ---- */

int sceGuInit(void)
{
  return 0;
}

void sceGuTerm(void)
{
}

int sceGuSync(int mode, int what)
{
  (void)mode;
  (void)what;
  return 0;
}

int sceGuFinish(void)
{
  return 0;
}

int sceGuStart(int ctype, void *list)
{
  (void)ctype;
  (void)list;
  return 0;
}

int sceGuCallList(const void *list)
{
  (void)list;
  return 0;
}

void sceGuEnable(int state)
{
  (void)state;
}

void sceGuDisable(int state)
{
  (void)state;
}

void sceGuDispBuffer(int width, int height, void *dispbp, int dispbw)
{
  (void)width;
  (void)height;
  (void)dispbp;
  (void)dispbw;
}

void sceGuDrawBuffer(int psm, void *fbp, int fbw)
{
  (void)psm;
  (void)fbp;
  (void)fbw;
}

void sceGuDrawBufferList(int psm, void *fbp, int fbw)
{
  (void)psm;
  (void)fbp;
  (void)fbw;
}

static u8 host_ge_memory[1 << 20];

void *sceGuGetMemory(int memsz)
{
  (void)memsz;
  return host_ge_memory;
}

void *sceGuSwapBuffers(void)
{
  return host_ge_memory + (1 << 19);
}

void sceGuClearColor(unsigned int color)
{
  (void)color;
}

void sceGuClear(int flags)
{
  (void)flags;
}

void sceGuOffset(unsigned int x, unsigned int y)
{
  (void)x;
  (void)y;
}

void sceGuViewport(int cx, int cy, int width, int height)
{
  (void)cx;
  (void)cy;
  (void)width;
  (void)height;
}

void sceGuScissor(int x, int y, int w, int h)
{
  (void)x;
  (void)y;
  (void)w;
  (void)h;
}

void sceGuBlendFunc(int op, int src, int dest, unsigned int srcfix,
 unsigned int destfix)
{
  (void)op;
  (void)src;
  (void)dest;
  (void)srcfix;
  (void)destfix;
}

void sceGuTexFilter(int min, int mag)
{
  (void)min;
  (void)mag;
}

void sceGuTexFlush(void)
{
}

void sceGuTexFunc(int tfx, int tcc)
{
  (void)tfx;
  (void)tcc;
}

void sceGuTexImage(int mipmap, int width, int height, int tbw,
 const void *tbp)
{
  (void)mipmap;
  (void)width;
  (void)height;
  (void)tbw;
  (void)tbp;
}

void sceGuTexMode(int tpsm, int maxmips, int mc, int swizzle)
{
  (void)tpsm;
  (void)maxmips;
  (void)mc;
  (void)swizzle;
}

void sceGuTexWrap(int u, int v)
{
  (void)u;
  (void)v;
}

int sceGuDisplay(int state)
{
  (void)state;
  return 0;
}

void sceGuDrawArray(int prim, int vtype, int count, const void *indices,
 const void *vertices)
{
  (void)prim;
  (void)vtype;
  (void)count;
  (void)indices;
  (void)vertices;
}

/* ---- Project data symbols (main.h / gui.h / psp_video.h / video.h) ---- */

u32 option_screen_scale = 0;
u32 option_screen_mag = 0;
u32 option_screen_filter = 0;
u32 synchronize_flag = 1;
u16 color_bg = 0;
u16 color_active_item = 0;

static u16 host_screen_texture[512 * 160];
u16 *screen_texture = host_screen_texture;
void (*update_screen)(void) = 0;

/* ---- Project function symbols (not exercised by the frame-time path) ---- */

void *safe_malloc(size_t size)
{
  return malloc(size);
}

u64 ticker(void)
{
  return 0;
}

u32 get_pad_input(u32 mask)
{
  (void)mask;
  return 0;
}

int kuImposeGetParam(SceImposeParam param)
{
  (void)param;
  return 0;
}

void ch_print(const char *str, u16 x, u16 y, u16 col, s16 bg_col,
 u16 *base_vram, u16 bufferwidth)
{
  (void)str;
  (void)x;
  (void)y;
  (void)col;
  (void)bg_col;
  (void)base_vram;
  (void)bufferwidth;
}

void mh_print(const char *str, u16 x, u16 y, u16 col, s16 bg_col,
 u16 *base_vram, u16 bufferwidth)
{
  (void)str;
  (void)x;
  (void)y;
  (void)col;
  (void)bg_col;
  (void)base_vram;
  (void)bufferwidth;
}
