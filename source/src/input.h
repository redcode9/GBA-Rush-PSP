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

#ifndef INPUT_H
#define INPUT_H

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
#include "input_trace.h"
#endif

/* GBA key bits as in KEYINPUT (which stores them inverted). */
typedef enum
{
  BUTTON_L      = 0x200,
  BUTTON_R      = 0x100,
  BUTTON_DOWN   = 0x80,
  BUTTON_UP     = 0x40,
  BUTTON_LEFT   = 0x20,
  BUTTON_RIGHT  = 0x10,
  BUTTON_START  = 0x08,
  BUTTON_SELECT = 0x04,
  BUTTON_B      = 0x02,
  BUTTON_A      = 0x01,
  BUTTON_NONE   = 0x00
} INPUT_BUTTONS_TYPE;

/* Actions a PSP button can be mapped to: GBA keys up to BUTTON_ID_SELECT,
   emulator functions after. Stored in the settings. */
typedef enum
{
  BUTTON_ID_UP,
  BUTTON_ID_DOWN,
  BUTTON_ID_LEFT,
  BUTTON_ID_RIGHT,
  BUTTON_ID_A,
  BUTTON_ID_B,
  BUTTON_ID_L,
  BUTTON_ID_R,
  BUTTON_ID_START,
  BUTTON_ID_SELECT,
  BUTTON_ID_MENU,
  BUTTON_ID_FASTFORWARD,
  BUTTON_ID_LOADSTATE,
  BUTTON_ID_SAVESTATE,
  BUTTON_ID_RAPIDFIRE_A,
  BUTTON_ID_RAPIDFIRE_B,
  BUTTON_ID_RAPIDFIRE_L,
  BUTTON_ID_RAPIDFIRE_R,
  BUTTON_ID_FPS,
  BUTTON_ID_NONE
} INPUT_BUTTONS_ID_TYPE;

/* Menu actions returned by get_gui_input. */
typedef enum
{
  CURSOR_RTRIGGER,
  CURSOR_LTRIGGER,
  CURSOR_UP,
  CURSOR_DOWN,
  CURSOR_LEFT,
  CURSOR_RIGHT,
  CURSOR_SELECT,
  CURSOR_BACK,
  CURSOR_EXIT,
  CURSOR_DEFAULT,
  CURSOR_NONE
} GUI_ACTION_TYPE;


extern u32 gamepad_config_map[16];

extern u32 enable_tilt_sensor;
extern u32 tilt_sensorX;
extern u32 tilt_sensorY;

void init_input(void);
void init_input_kernel(void);

u32 update_input(void);
void rebuild_fast_forward_button_mask(void);

u32 get_pad_input(u32 mask);

GUI_ACTION_TYPE get_gui_input(void);
GUI_ACTION_TYPE get_gui_input_fs_hold(u32 button_id);

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
int input_diagnostic_trace_begin_record(void);
int input_diagnostic_trace_finish_record(void);
int input_diagnostic_trace_encode(u8 *output, size_t output_size,
 size_t *written);
int input_diagnostic_trace_begin_replay(const u8 *encoded,
 size_t encoded_size);
void input_diagnostic_trace_cancel(void);
InputTraceState input_diagnostic_trace_state(void);
InputTraceError input_diagnostic_trace_error(void);
u32 input_diagnostic_trace_event_count(void);
size_t input_diagnostic_trace_max_encoded_size(void);
#endif

void input_write_mem_savestate(SceUID savestate_file);
void input_read_savestate(SceUID savestate_file);


#endif /* INPUT_H */
