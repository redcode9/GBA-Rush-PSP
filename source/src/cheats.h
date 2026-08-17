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

#ifndef CHEATS_H
#define CHEATS_H

#define CHEAT_NAME_LENGTH (25)//(24*5)
#define MAX_CHEATS_PAGE 64
#define MAX_CHEATS (MAX_CHEATS_PAGE * 10)
#define MAX_CHEAT_HOOKS MAX_CHEATS

typedef enum
{
  CHEAT_TYPE_GAMESHARK_V1,
  CHEAT_TYPE_GAMESHARK_V3,
  CHEAT_TYPE_DIRECT_V1,
  CHEAT_TYPE_DIRECT_V3,
  CHEAT_TYPE_CODEBREAKER,
  CHEAT_TYPE_INVALID
} CHEAT_VARIANT_ENUM;

typedef struct
{
  char cheat_name[CHEAT_NAME_LENGTH];
  u32 cheat_active;
  u32 cheat_codes[256];
  u32 num_cheat_lines;
  CHEAT_VARIANT_ENUM cheat_variant;
} CHEAT_TYPE;

typedef enum
{
  CHEAT_LOAD_OK,
  CHEAT_LOAD_ERROR_OPEN,
  CHEAT_LOAD_ERROR_FORMAT,
  CHEAT_LOAD_ERROR_CAPACITY,
  CHEAT_LOAD_ERROR_EMPTY
} CHEAT_LOAD_RESULT;

// グローバル変数宣言

extern CHEAT_TYPE cheats[MAX_CHEATS];
extern u32 num_cheats;
extern char loaded_cheat_filename[MAX_FILE];

// 関数宣言

void process_cheats(void);
u32 cheat_hook_configured(u32 pc);
void process_cheats_at_hook(u32 pc);
void clear_cheats(void);
void rebuild_active_cheat_indexes(void);
CHEAT_LOAD_RESULT load_cheats(const char *cheats_path, u32 *error_line);

#endif /* CHEATS_H */
