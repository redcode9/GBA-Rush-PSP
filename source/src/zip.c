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
 * only gives it the PSP file and reports the archive bytes read to the
 * loading popup. */

#include "common.h"
#include "zip_rom.h"


typedef struct
{
  SceUID fd;
  const char *name;
} ZipFile;

static int32_t zip_file_read(void *ctx, uint8_t *buf, uint32_t len)
{
  return FILE_READ(((ZipFile *)ctx)->fd, buf, len);
}

static void zip_file_progress(void *ctx, uint32_t done, uint32_t total)
{
  if (rom_load_progress != NULL)
    rom_load_progress(((ZipFile *)ctx)->name, MSG_LOADING_STAGE_UNZIP, done,
     total);
}

s32 load_file_zip(char *filename)
{
  ZipFile file;
  ZipRomIo io;
  uint32_t rom_size = 0;
  int result;

  FILE_OPEN(file.fd, filename, READ);
  file.name = filename;

  if (!FILE_CHECK_VALID(file.fd))
    return -1;

  io.ctx = &file;
  io.read = zip_file_read;
  io.progress = zip_file_progress;
  io.in_buf = malloc(ROM_LOAD_CHUNK_SIZE);
  io.in_cap = ROM_LOAD_CHUNK_SIZE;
  io.total = file_length(filename);

  if (io.in_buf == NULL)
  {
    FILE_CLOSE(file.fd);
    return -1;
  }

  result = zip_rom_load(&io, gamepak_rom, gamepak_ram_buffer_size, &rom_size);

  free(io.in_buf);
  FILE_CLOSE(file.fd);

  if (result == ZIP_ROM_TOO_BIG)
    return -2;

  // An empty ROM must fail here: load_gamepak()'s callers take 0 as success.
  if (result != ZIP_ROM_OK || rom_size == 0)
    return -3;

  return (s32)rom_size;
}
