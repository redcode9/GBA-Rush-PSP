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

#ifndef MAIN_H
#define MAIN_H

#include "fast_forward_policy.h"
#include "link_cable.h"
#include "perf_metrics.h"

#define GBA_RUSH_VERSION "1.4.1"

// video scale type
#define SCALED_NONE    0
#define SCALED_X15_GU  1
#define SCALED_X15_SW  2
#define SCALED_USER    3
#define SCALED_16X9_GU 4
#define SCALED_16X9_SHARP 5
#define SCALED_16X9_AUTO  6
#define SCREEN_SCALE_COUNT 7

// video filter type
#define FILTER_NEAREST  0
#define FILTER_BILINEAR 1

// frameskip type
#define FRAMESKIP_AUTO   0
#define FRAMESKIP_MANUAL 1
#define FRAMESKIP_NONE   2

// psp cpu clock frequency
#define PSP_CLOCK_222 0
#define PSP_CLOCK_266 1
#define PSP_CLOCK_300 2
#define PSP_CLOCK_333 3

#define CONFIRMATION_NONE  0
#define CONFIRMATION_CONT  1
#define CONFIRMATION_QUIT  2

// RAM dynarec policy
#define RAM_DYNAREC_FULL_FLUSH         0
#define RAM_DYNAREC_PARTIAL_NO_REUSE   1
#define RAM_DYNAREC_PARTIAL_WITH_REUSE 2

// renderer option
#define VIDEO_RENDERER_OLD 0
#define VIDEO_RENDERER_NEW 1


extern u32 option_screen_scale;
extern u32 option_screen_mag;
extern u32 option_screen_filter;
extern u32 option_sound_volume;
extern u32 option_stack_optimize;
extern u32 option_ram_dynarec_policy;
extern u32 option_video_renderer;
extern u32 option_oam_hijacking_enabled;
extern u32 option_boot_mode;
extern u32 option_update_backup;
extern u32 option_screen_capture_format;
extern u32 option_enable_analog;
extern u32 option_analog_sensitivity;
extern u32 option_language;
extern u32 option_theme;
extern u32 option_frameskip_type;
extern u32 option_frameskip_value;
extern u32 option_clock_speed;

/* HBLANK IRQ scanline window (1-based, 0 = off). Either 0 disables the window. */
extern u32 option_hblank_irq_window_start;
extern u32 option_hblank_irq_window_end;
extern u32 option_psp_vsync;

extern u32 option_fast_forward_profile;
extern u32 option_fast_forward_activation;
extern u32 option_fast_forward_speed;
extern u32 option_fast_forward_presentation;
extern u32 option_fast_forward_audio;
extern u32 option_fast_forward_game_override;
extern u32 option_game_fast_forward_profile;
extern u32 option_game_fast_forward_activation;
extern u32 option_game_fast_forward_speed;
extern u32 option_game_fast_forward_presentation;
extern u32 option_game_fast_forward_audio;

extern char main_path[MAX_PATH];

extern int date_format;
extern u32 enable_home_menu;

extern volatile u32 sleep_flag;
extern volatile u32 sleep_auto_saved;

extern u32 synchronize_flag;
extern u32 psp_fps_debug;

extern u32 real_frame_count;
extern u32 virtual_frame_count;

void direct_sound_timer_select(u32 value);

void timer_control_low(u8 timer_number, u32 value);
CPU_ALERT_TYPE timer_control_high(u8 timer_number, u32 value);

extern u32 dma_cycle_count;

/* Why the last link session ended, for the link menu. */
typedef enum
{
  MAIN_LINK_END_NONE,
  MAIN_LINK_END_USER,
  MAIN_LINK_END_SUSPEND,
  MAIN_LINK_END_ROM_CHANGE,
  MAIN_LINK_END_PEER_LEFT,
  MAIN_LINK_END_LOST,
  MAIN_LINK_END_INCOMPATIBLE,
  MAIN_LINK_END_NETWORK
} MainLinkEnd;

extern LinkCable gba_link;

int main_link_host(void);
int main_link_join(void);
void main_link_disconnect(MainLinkEnd reason);
void main_link_pump(void);
int main_link_active(void);
int main_link_connected(void);
MainLinkEnd main_link_end_reason(void);
void main_link_set_gameplay_active(int active);
void main_power_service(void);

void main_fast_forward_input(u32 pressed, u32 held);
void main_fast_forward_stop(void);
int main_fast_forward_active(void);
FastForwardSpeed main_fast_forward_speed(void);
const char *main_fast_forward_status(void);
void main_fast_forward_apply_settings(void);
void main_fast_forward_reset_game_settings(void);

int main_perf_start(void);
int main_perf_stop(void);
void main_perf_snapshot(PerfMetricsSnapshot *snapshot);
int main_perf_write_report(char *output_path, size_t output_path_size);

/* Hardware scheduler called by the dynarec; returns the next slice length
   in CPU cycles. */
u32 update_gba(void);

void reset_gba(void);
void quit(void);

void error_msg(const char *text, u8 confirm);
/* Shows text and waits: returns 0 on the confirm button (O, or X with
   option_swap_confirm_buttons) or START, 1 on the cancel button. */
u32 yesno_dialog(const char *text);
/* Copies src to buffer, replacing everything from its last '.' with
   extension ("" strips it); a name without a '.' is copied unchanged. */
void change_ext(char *src, char *buffer, const char *extension);

u64 ticker(void);
u32 file_length(char *filename);

/* psp_clock is a PSP_CLOCK_* value. */
int set_cpu_clock(u32 psp_clock);

SceUID psp_fopen(const char *filename, const char *mode);
void psp_fclose(SceUID filename_tag);

void *safe_malloc(size_t size);

void main_write_mem_savestate(SceUID savestate_file);
void main_read_savestate(SceUID savestate_file);

/* Defined in memory.c and gui.c. */
u32 load_state_silent(char *savestate_filename);
u32 save_state_silent(char *savestate_filename, u16 *screen_capture);

void auto_savestate_sleep(void);

#endif /* MAIN_H */
