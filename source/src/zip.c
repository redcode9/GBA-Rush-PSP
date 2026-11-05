/* unofficial gameplaySP kai
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2006 SiberianSTAR
 * Copyright (C) 2007 takka <takka@tfact.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* Loads a ROM from a .zip for load_gamepak() in memory.c. zip_rom.c does the
 * work: it reads the first entry, a stored or deflated .gba, .agb or .bin
 * file, straight into gamepak_rom and checks its size and CRC32. This file
 * only gives it the PSP file and the progress bar. */

#include "common.h"
#include "zip_rom.h"


#define ZIP_BUFFER_SIZE (128 * 1024)   // compressed input chunk size

void draw_progress(u32 output_size, u32 total_size);

static int32_t zip_file_read(void *ctx, uint8_t *buf, uint32_t len)
{
  return FILE_READ(*(SceUID *)ctx, buf, len);
}

static void zip_file_progress(void *ctx, uint32_t done, uint32_t total)
{
  (void)ctx;
  draw_progress(done, total);
  flip_screen(0);
}

s32 load_file_zip(char *filename)
{
  SceUID fd;
  ZipRomIo io;
  uint32_t rom_size = 0;
  int result;

  FILE_OPEN(fd, filename, READ);

  if (!FILE_CHECK_VALID(fd))
    return -1;

  io.ctx = &fd;
  io.read = zip_file_read;
  io.progress = zip_file_progress;
  io.in_buf = malloc(ZIP_BUFFER_SIZE);
  io.in_cap = ZIP_BUFFER_SIZE;
  io.total = file_length(filename);

  if (io.in_buf == NULL)
  {
    FILE_CLOSE(fd);
    return -1;
  }

  result = zip_rom_load(&io, gamepak_rom, gamepak_ram_buffer_size, &rom_size);

  free(io.in_buf);
  FILE_CLOSE(fd);

  if (result == ZIP_ROM_TOO_BIG)
    return -2;

  // An empty ROM must fail here: load_gamepak()'s callers take 0 as success.
  if (result != ZIP_ROM_OK || rom_size == 0)
    return -3;

  return (s32)rom_size;
}

/* 200-pixel progress bar centred on the 480x272 screen. */
void draw_progress(u32 output_size, u32 total_size)
{
  // The size is 0 when unknown, and a division by zero traps on the PSP.
  if (total_size == 0)
    return;

  draw_box_line(140 - 4, 140 - 4, 340 + 4, 165 + 4, COLOR15_WHITE);
  draw_box_line(140 - 3, 140 - 3, 340 + 3, 165 + 3, COLOR15_WHITE);
  draw_box_fill(140, 140, 140 + ((u64)output_size * 200 / total_size), 165, COLOR15_WHITE);
}
