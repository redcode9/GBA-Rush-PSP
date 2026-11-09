/*
 * GBA Rush PSP
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * Runs a ROM in mGBA for a number of frames, with no keys pressed, and
 * writes its save chip to a file: the reference run of the GBA test ROMs
 * (scripts/check-test-roms.sh), whose results tools/decode_test_rom.py
 * reads from that file. Build it against an mGBA 0.10 libmgba.a built
 * with 32-bit colour (the default).
 *
 * Usage: mgba_run [--bios FILE] [--frames N] [--save FILE]
 *                 [--savetype TYPE] [--frame FILE] ROM
 *   --bios      the BIOS to run on; without it mGBA uses its own HLE BIOS.
 *               The ROM starts directly either way (opts.skipBios): no
 *               BIOS code runs before it.
 *   --frames    frames to run, 600 by default. Then the run goes on until
 *               the save chip has not changed for QUIET frames (at most
 *               3600 more), so the file never holds a write cut halfway.
 *   --save      loaded first if it exists, and written at the end with
 *               the size of the chip mGBA emulated.
 *   --savetype  SRAM, SRAM512, FLASH512, FLASH1M, EEPROM or EEPROM512, as
 *               in mGBA's overrides. mGBA 0.10 does not read the ROM's
 *               save library ID string: it knows commercial games from
 *               its own list and guesses the others from the first
 *               access, which takes a Flash chip read before it is
 *               written for SRAM, and 64 KiB of SRAM for 32 KiB. Like a
 *               user, it has to be told.
 *   --frame     the last frame as a binary PPM.
 * Prints the frames run, the save size and the CRC-32 of the last frame
 * (its 240x160 pixels as 32-bit words), so two runs can be compared.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* First: the build's options change the layout of struct mCore, and
   savedataClone comes after the debugger fields. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/crc32.h>
#include <mgba-util/vfs.h>

#define WIDTH 240
#define HEIGHT 160
#define STRIDE 256
/* Frames without a change that end a run: longer than the save ROMs'
   pauses inside a write (about a frame to fill the next Flash sector's
   data), shorter than their second between two writes. */
#define QUIET 10

static color_t frame[STRIDE * HEIGHT];

static const struct
{
  const char *name;
  enum SavedataType type;
} save_types[] =
{
  { "SRAM", SAVEDATA_SRAM }, { "SRAM512", SAVEDATA_SRAM512 },
  { "FLASH512", SAVEDATA_FLASH512 }, { "FLASH1M", SAVEDATA_FLASH1M },
  { "EEPROM", SAVEDATA_EEPROM }, { "EEPROM512", SAVEDATA_EEPROM512 }
};

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

static int write_file(const char *path, const void *data, size_t size)
{
  FILE *f = fopen(path, "wb");
  int ok;

  if(!f)
    return 0;
  ok = fwrite(data, 1, size, f) == size;
  return fclose(f) == 0 && ok;
}

static int write_ppm(const char *path)
{
  FILE *f = fopen(path, "wb");
  unsigned x, y;

  if(!f)
    return 0;
  fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
  for(y = 0; y < HEIGHT; y++)
    for(x = 0; x < WIDTH; x++)
    {
      uint32_t c = frame[y * STRIDE + x];

      fputc(c & 0xFF, f);
      fputc((c >> 8) & 0xFF, f);
      fputc((c >> 16) & 0xFF, f);
    }
  return fclose(f) == 0;
}

int main(int argc, char **argv)
{
  const char *bios = NULL, *save = NULL, *frame_path = NULL, *rom = NULL;
  const char *save_type = NULL;
  unsigned frames = 600, run = 0, quiet = 0, y;
  int i;
  struct mCore *core;
  struct GBA *gba;
  void *data, *last = NULL;
  size_t size, last_size = 0;
  uint32_t crc = 0;

  for(i = 1; i < argc; i++)
  {
    if(strcmp(argv[i], "--bios") == 0 && i + 1 < argc)
      bios = argv[++i];
    else if(strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
      frames = (unsigned)strtoul(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--save") == 0 && i + 1 < argc)
      save = argv[++i];
    else if(strcmp(argv[i], "--frame") == 0 && i + 1 < argc)
      frame_path = argv[++i];
    else if(strcmp(argv[i], "--savetype") == 0 && i + 1 < argc)
      save_type = argv[++i];
    else if(!rom && argv[i][0] != '-')
      rom = argv[i];
    else
      rom = NULL, i = argc;
  }
  if(!rom)
  {
    fprintf(stderr, "usage: %s [--bios FILE] [--frames N] [--save FILE]"
     " [--savetype TYPE] [--frame FILE] ROM\n", argv[0]);
    return 2;
  }

  mLogSetDefaultLogger(&silent_logger);
  core = GBACoreCreate();
  core->init(core);
  mCoreInitConfig(core, NULL);
  core->loadConfig(core, &core->config);
  /* Read by reset; mGBA fills opts only from mCoreLoadConfig, never
     from the config values loadConfig takes. */
  core->opts.skipBios = true;
  core->setVideoBuffer(core, frame, STRIDE);
  if(!core->loadROM(core, VFileOpen(rom, O_RDONLY)))
  {
    fprintf(stderr, "cannot load %s\n", rom);
    return 2;
  }
  if(bios && !core->loadBIOS(core, VFileOpen(bios, O_RDONLY), 0))
  {
    fprintf(stderr, "cannot load %s\n", bios);
    return 2;
  }
  if(save && (data = read_file(save, &size)) != NULL)
    core->loadSave(core, VFileMemChunk(data, size));
  core->reset(core);
  gba = core->board;
  if(save_type)
  {
    for(i = 0; i < (int)(sizeof(save_types) / sizeof(save_types[0])); i++)
      if(strcmp(save_type, save_types[i].name) == 0)
        break;
    if(i == (int)(sizeof(save_types) / sizeof(save_types[0])))
    {
      fprintf(stderr, "unknown save type %s\n", save_type);
      return 2;
    }
    GBASavedataForceType(&gba->memory.savedata, save_types[i].type);
  }

  for(; run < frames; run++)
    core->runFrame(core);
  /* On until the chip has been left alone for QUIET frames. */
  size = core->savedataClone(core, &data);
  while(quiet < QUIET && run < frames + 3600)
  {
    free(last);
    last = data;
    last_size = size;
    core->runFrame(core);
    run++;
    size = core->savedataClone(core, &data);
    if(size == last_size && (!size || memcmp(data, last, size) == 0))
      quiet++;
    else
      quiet = 0;
  }

  for(y = 0; y < HEIGHT; y++)
    crc = crc32(crc, &frame[y * STRIDE], WIDTH * sizeof(color_t));
  printf("mgba_run: %s: %u frames, save of %zu bytes, frame CRC %08X\n",
   rom, run, size, (unsigned)crc);
  if(save && size && !write_file(save, data, size))
  {
    fprintf(stderr, "cannot write %s\n", save);
    return 1;
  }
  if(frame_path && !write_ppm(frame_path))
  {
    fprintf(stderr, "cannot write %s\n", frame_path);
    return 1;
  }
  return 0;
}
