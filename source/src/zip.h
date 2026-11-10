/* unofficial gameplaySP kai
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
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

#ifndef ZIP_H
#define ZIP_H


/* ROM files are read this much at a time, so the loading popup moves. */
#define ROM_LOAD_CHUNK_SIZE (64u * 1024u)

/* Called during a ROM load when not NULL: name is the file, stage_msg the
   MSG_ id of the step (MSG_LOADING_STAGE_READ for a .gba,
   MSG_LOADING_STAGE_UNZIP for a .zip), done the bytes of the file used so
   far out of total. A .gba reports after every read, a .zip after each read
   of a stored ROM and each inflate step (zip_rom.h). gui.c points it at the
   loading popup. */
typedef void (*RomLoadProgress)(const char *name, u32 stage_msg, u32 done,
 u32 total);
extern RomLoadProgress rom_load_progress;

/* When the current load's ROM file was open (sceKernelGetSystemTimeWide),
   for the runtime log's stage times; load_gamepak() clears it. */
extern u64 rom_load_opened_us;

/* Loads the ROM in the first entry of a .zip into gamepak_rom. Returns its
   size (above 0), -1 if the file cannot be opened or memory runs out, -2 if
   the ROM does not fit in the ROM buffer, or -3 for any other failure: a
   damaged, truncated or unsupported archive, or a read error. */
s32 load_file_zip(char *filename);


#endif /* ZIP_H */
