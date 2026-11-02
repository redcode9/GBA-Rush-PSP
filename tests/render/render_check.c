/*
 * Draws a game twice and compares: mGBA runs the ROM with its own software
 * renderer, and every video register, palette, OAM and VRAM write it hands
 * that renderer also goes to GBA Rush's gba_ppu, which draws each line too.
 * At the end of each frame the two pictures are compared pixel by pixel.
 * Build it against an mGBA compiled with COLOR_16_BIT, whose colours are
 * the GBA's own (see mgba_pixel).
 *
 * Usage: render_check ROM SCRIPT [BIOS [SAVE]]
 *
 * Script lines, in the format of tests/rom_link/link_e2e.c's scripts so
 * those can be reused (only the first console's keys count, other commands
 * are skipped):
 *   run <frames> [<keys>]     hold keys (A+B+START..., or -) for frames
 *   tap <side> <keys> [frames]  press keys, then release for 10 frames
 *   shot <name>               write <name>.bmp, mGBA left, gba_ppu right
 *
 * Prints the frames and pixels that differ and the time both renderers
 * took, writes the first frame that differs to mismatch.bmp with mGBA's
 * picture on the left, and exits 1 if any pixel differed. With
 * RENDER_VERBOSE set it lists that frame's first differing pixels, with
 * the registers of their line and the two front pixels gba_ppu had there.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/video.h>
#include <mgba-util/vfs.h>

#include "gba_ppu.h"

#define WIDTH GBA_PPU_WIDTH
#define HEIGHT GBA_PPU_HEIGHT

static struct mCore *core;
static struct GBA *gba;
static struct GBAVideoRenderer mgba_renderer;
static GbaPpu ppu;
static color_t mgba_frame[256 * HEIGHT];
static uint16_t ppu_frame[WIDTH * HEIGHT];
static unsigned long frames, bad_frames, bad_pixels, desync_frames;
static unsigned long rounded_pixels;
static double mgba_ns, ppu_ns;
static int written_mismatch;
/* Lines drawn this frame: mGBA starts the first frame part way down. */
static unsigned char drawn[HEIGHT];
/* The video registers each line was drawn with, for RENDER_VERBOSE. */
static uint16_t line_io[HEIGHT][GBA_PPU_IO_BYTES / 2];
static uint32_t line_top[HEIGHT][WIDTH], line_below[HEIGHT][WIDTH];

static void silent_log(struct mLogger *logger, int category,
 enum mLogLevel level, const char *format, va_list args)
{
  (void)logger;
  (void)category;
  (void)level;
  (void)format;
  (void)args;
}

static struct mLogger silent_logger = { .log = silent_log };

static double now_ns(void)
{
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e9 + t.tv_nsec;
}

/* mGBA built with COLOR_16_BIT keeps the GBA's BGR555 and mixes colours
   in 5-bit steps, as the GBA does. Its usual build holds red, green and
   blue in bytes 0, 1 and 2 and mixes them in 8-bit steps, so colour effects
   can come out one step brighter there. */
static uint16_t mgba_pixel(int x, int y)
{
  color_t c = mgba_frame[y * 256 + x];

#ifdef COLOR_16_BIT
  return c & 0x7FFF;
#else
  return ((c >> 3) & 0x1F) | ((c >> 11) & 0x1F) << 5 |
   ((c >> 19) & 0x1F) << 10;
#endif
}

/* RENDER_TRACE=n lists the writes made during frame n with their line;
   RENDER_DUMP=n saves the registers, palette, OAM and VRAM at the end of
   frame n to state.bin, in that order. */
static unsigned long trace_frame, dump_frame;

static void trace(const char *what, uint32_t address, uint16_t value)
{
  if(trace_frame && frames + 1 == trace_frame)
    printf("  line %3d %s %05X = %04X\n", gba->video.vcount, what, address,
     value);
}

static uint16_t on_register(struct GBAVideoRenderer *renderer,
 uint32_t address, uint16_t value)
{
  trace("io", address, value);
  gba_ppu_write_io(&ppu, address, value);
  return mgba_renderer.writeVideoRegister(renderer, address, value);
}

static void on_vram(struct GBAVideoRenderer *renderer, uint32_t address)
{
  address &= 0x1FFFE;
  if(address >= 0x18000)
    address -= 0x8000;
  trace("vram", address, renderer->vram[address >> 1]);
  gba_ppu_write_vram(&ppu, address, &renderer->vram[address >> 1], 2);
  mgba_renderer.writeVRAM(renderer, address);
}

static void on_palette(struct GBAVideoRenderer *renderer, uint32_t address,
 uint16_t value)
{
  trace("palette", address, value);
  gba_ppu_write_palette(&ppu, address & 0x3FE, &value, 2);
#ifdef COLOR_16_BIT
  /* The GBA ignores bit 15 of a colour; mGBA's 16-bit build keeps it and
     lets it spill into colour effects. */
  value &= 0x7FFF;
#endif
  mgba_renderer.writePalette(renderer, address, value);
}

static void on_oam(struct GBAVideoRenderer *renderer, uint32_t index)
{
  index &= 0x1FF;
  trace("oam", index * 2, renderer->oam->raw[index]);
  gba_ppu_write_oam(&ppu, index * 2, &renderer->oam->raw[index], 2);
  mgba_renderer.writeOAM(renderer, index);
}

static void on_line(struct GBAVideoRenderer *renderer, int y)
{
  double t0 = now_ns(), t1;

  mgba_renderer.drawScanline(renderer, y);
  t1 = now_ns();
  gba_ppu_draw_line(&ppu, y, ppu_frame + y * WIDTH);
  ppu_ns += now_ns() - t1;
  drawn[y] = 1;
  memcpy(line_io[y], ppu.io, sizeof(ppu.io));
  memcpy(line_top[y], ppu.top, sizeof(ppu.top));
  memcpy(line_below[y], ppu.below, sizeof(ppu.below));
  mgba_ns += t1 - t0;
}

/* 24-bit BMP of both pictures side by side, mGBA's on the left. */
static void write_pair(const char *path)
{
  unsigned char header[54] = { 'B', 'M' };
  unsigned size = 54 + WIDTH * 2 * HEIGHT * 3;
  FILE *f = fopen(path, "wb");
  int x, y;

  if(!f)
    return;
  header[2] = size;
  header[3] = size >> 8;
  header[4] = size >> 16;
  header[10] = 54;
  header[14] = 40;
  header[18] = (WIDTH * 2) & 0xFF;
  header[19] = (WIDTH * 2) >> 8;
  header[22] = HEIGHT;
  header[26] = 1;
  header[28] = 24;
  fwrite(header, 1, sizeof(header), f);
  for(y = HEIGHT - 1; y >= 0; y--)
    for(x = 0; x < WIDTH * 2; x++)
    {
      uint16_t c = x < WIDTH ? mgba_pixel(x, y) :
       ppu_frame[y * WIDTH + x - WIDTH];
      unsigned char bgr[3] = { (c >> 10 & 31) << 3, (c >> 5 & 31) << 3,
       (c & 31) << 3 };

      fwrite(bgr, 1, 3, f);
    }
  fclose(f);
}

/* Both renderers must have seen the same memory; a difference means a
   write that never reached a hook, not a drawing error. */
static void check_copies(void)
{
  if(memcmp(ppu.vram, gba->video.vram, sizeof(ppu.vram)) ||
     memcmp(ppu.palette, gba->video.palette, sizeof(ppu.palette)) ||
     memcmp(ppu.oam, gba->video.oam.raw, sizeof(ppu.oam)))
    desync_frames++;
}

static void print_registers(int y)
{
  int i;

  printf("line %d registers:", y);
  for(i = 0; i < GBA_PPU_IO_BYTES / 2; i++)
    printf("%s%04X", i % 8 ? " " : "\n  ", line_io[y][i]);
  printf("\n");
}

/* mGBA's 16-bit build darkens green and blue by c - ceil(c * evy / 16)
   instead of the GBA's c - floor(c * evy / 16), while red comes out
   right: such pixels are counted apart and do not fail the check. */
static int darkening_rounds(int x, int y)
{
#ifdef COLOR_16_BIT
  uint16_t theirs = mgba_pixel(x, y), ours = ppu_frame[y * WIDTH + x];
  int shift;

  if(((line_io[y][0x50 >> 1] >> 6) & 3) != 3 ||
     (theirs & 0x1F) != (ours & 0x1F))
    return 0;
  for(shift = 5; shift < 15; shift += 5)
  {
    int difference = ((ours >> shift) & 31) - ((theirs >> shift) & 31);

    if(difference != 0 && difference != 1)
      return 0;
  }
  return 1;
#else
  (void)x;
  (void)y;
  return 0;
#endif
}

static void on_frame_end(struct GBAVideoRenderer *renderer)
{
  unsigned long differ = 0;
  int x, y;

  mgba_renderer.finishFrame(renderer);
  if(dump_frame && frames + 1 == dump_frame)
  {
    FILE *f = fopen("state.bin", "wb");

    if(f)
    {
      fwrite(ppu.io, 1, sizeof(ppu.io), f);
      fwrite(ppu.palette, 1, sizeof(ppu.palette), f);
      fwrite(ppu.oam, 1, sizeof(ppu.oam), f);
      fwrite(ppu.vram, 1, sizeof(ppu.vram), f);
      fclose(f);
    }
  }
  gba_ppu_start_frame(&ppu);
  check_copies();
  frames++;
  for(y = 0; y < HEIGHT; y++)
    for(x = 0; x < WIDTH && drawn[y]; x++)
      if(mgba_pixel(x, y) == ppu_frame[y * WIDTH + x])
        continue;
      else if(darkening_rounds(x, y))
        rounded_pixels++;
      else
      {
        if(!written_mismatch && differ < 20 && getenv("RENDER_VERBOSE"))
        {
          if(!differ)
            print_registers(y);
          printf("frame %lu x %d y %d: mGBA %04X gba_ppu %04X"
           " (front %06X, behind %06X, palette now %04X %04X)\n", frames,
           x, y, mgba_pixel(x, y), ppu_frame[y * WIDTH + x], line_top[y][x],
           line_below[y][x], ppu.palette[line_top[y][x] & 0x1FF],
           ppu.palette[line_below[y][x] & 0x1FF]);
        }
        differ++;
      }
  memset(drawn, 0, sizeof(drawn));
  if(!differ)
    return;
  bad_frames++;
  bad_pixels += differ;
  if(!written_mismatch)
  {
    printf("first difference in frame %lu: %lu pixels\n", frames, differ);
    write_pair("mismatch.bmp");
    written_mismatch = 1;
  }
}

static uint32_t parse_keys(const char *text)
{
  static const char *names[] = { "A", "B", "SEL", "START", "RIGHT", "LEFT",
   "UP", "DOWN", "R", "L" };
  uint32_t keys = 0;
  char buffer[128];
  char *token;
  unsigned i;

  if(!text || strcmp(text, "-") == 0)
    return 0;
  snprintf(buffer, sizeof(buffer), "%s", text);
  for(token = strtok(buffer, "+"); token; token = strtok(NULL, "+"))
    for(i = 0; i < 10; i++)
      if(strcmp(token, names[i]) == 0 ||
         (i == 2 && strcmp(token, "SELECT") == 0))
        keys |= 1u << i;
  return keys;
}

static void run_frames(unsigned count, uint32_t keys)
{
  core->setKeys(core, keys);
  while(count--)
    core->runFrame(core);
  core->setKeys(core, 0);
}

static void *read_file(const char *path, size_t *size)
{
  FILE *f = fopen(path, "rb");
  void *data;

  if(!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  data = malloc(*size ? *size : 1);
  if(fread(data, 1, *size, f) != *size)
    *size = 0;
  fclose(f);
  return data;
}

/* Hands every hook to both renderers, and starts gba_ppu from the state
   mGBA's reset left behind. */
static void attach(void)
{
  struct GBAVideoRenderer *renderer = gba->video.renderer;
  uint32_t offset;

  mgba_renderer = *renderer;
  renderer->writeVideoRegister = on_register;
  renderer->writeVRAM = on_vram;
  renderer->writePalette = on_palette;
  renderer->writeOAM = on_oam;
  renderer->drawScanline = on_line;
  renderer->finishFrame = on_frame_end;

  memset(&ppu, 0, sizeof(ppu));
  for(offset = 0; offset < GBA_PPU_IO_BYTES; offset += 2)
    gba_ppu_write_io(&ppu, offset, gba->memory.io[offset >> 1]);
  gba_ppu_write_vram(&ppu, 0, gba->video.vram, sizeof(ppu.vram));
  gba_ppu_write_palette(&ppu, 0, gba->video.palette, sizeof(ppu.palette));
  gba_ppu_write_oam(&ppu, 0, gba->video.oam.raw, sizeof(ppu.oam));
  gba_ppu_start_frame(&ppu);
}

int main(int argc, char **argv)
{
  char line[512];
  FILE *script;

  if(argc < 3)
  {
    fprintf(stderr, "usage: %s rom script [bios [save]]\n", argv[0]);
    return 2;
  }
  mLogSetDefaultLogger(&silent_logger);
  if(getenv("RENDER_TRACE"))
    trace_frame = strtoul(getenv("RENDER_TRACE"), NULL, 10);
  if(getenv("RENDER_DUMP"))
    dump_frame = strtoul(getenv("RENDER_DUMP"), NULL, 10);
  core = GBACoreCreate();
  core->init(core);
  mCoreInitConfig(core, NULL);
  mCoreConfigSetValue(&core->config, "skipBios", "1");
  core->loadConfig(core, &core->config);
  core->setVideoBuffer(core, mgba_frame, 256);
  if(!core->loadROM(core, VFileOpen(argv[1], O_RDONLY)))
  {
    fprintf(stderr, "cannot load %s\n", argv[1]);
    return 2;
  }
  if(argc > 3)
    core->loadBIOS(core, VFileOpen(argv[3], O_RDONLY), 0);
  if(argc > 4)
  {
    size_t size;
    void *data = read_file(argv[4], &size);

    if(!data)
    {
      fprintf(stderr, "cannot read %s\n", argv[4]);
      return 2;
    }
    core->loadSave(core, VFileMemChunk(data, size));
  }
  core->reset(core);
  gba = core->board;
  attach();

  script = strcmp(argv[2], "-") == 0 ? stdin : fopen(argv[2], "r");
  if(!script)
  {
    fprintf(stderr, "cannot read %s\n", argv[2]);
    return 2;
  }
  while(fgets(line, sizeof(line), script))
  {
    char *cmd = strtok(line, " \t\n");
    char *a = strtok(NULL, " \t\n");
    char *b = strtok(NULL, " \t\n");
    char *c = strtok(NULL, " \t\n");

    if(!cmd || cmd[0] == '#' || !a)
      continue;
    if(strcmp(cmd, "run") == 0)
      run_frames((unsigned)atoi(a), parse_keys(b));
    else if(strcmp(cmd, "tap") == 0)
    {
      run_frames(c ? (unsigned)atoi(c) : 6u, atoi(a) == 0 ? parse_keys(b) : 0);
      run_frames(10, 0);
    }
    else if(strcmp(cmd, "shot") == 0)
    {
      char path[512];

      snprintf(path, sizeof(path), "%s.bmp", a);
      write_pair(path);
    }
  }

  printf("%s: %lu frames, %lu differ, %lu pixels differ", argv[1], frames,
   bad_frames, bad_pixels);
  if(rounded_pixels)
    printf(", %lu more darkened one step less by mGBA", rounded_pixels);
  if(desync_frames)
    printf(", memory copies out of step in %lu frames", desync_frames);
  printf("\n  per frame: mGBA renderer %.0f us, gba_ppu %.0f us\n",
   frames ? mgba_ns / frames / 1e3 : 0.0, frames ? ppu_ns / frames / 1e3 : 0.0);
  return bad_frames || desync_frames ? 1 : 0;
}
