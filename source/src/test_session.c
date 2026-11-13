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

/* Hardware test session (diagnostic profile only, see test_session.h). At
   start-up it asks whether to run; then, for each known game found in the
   ROM folder, on the Classic and the New core, it loads savestate slot 1,
   lets the game run a few seconds and records one Diagnostics capture for
   each way of drawing: the renderer New on the GE, Exact on the main
   processor and Exact on the Media Engine. Mario Kart also gets a capture
   in fast-forward, whose frame time is the cost of emulating a frame.
   Each capture writes its usual report and one line in
   benchmarks/test-session.txt. All Media Engine captures come last: once
   started the Media Engine keeps polling main memory, which would slow the
   captures that do not use it. Opening the menu or putting the PSP to
   sleep ends the session; from then until the emulator restarts the game
   writes neither its save nor its AUTO savestate, since it holds the
   session's state and not the player's. */

#include <stdarg.h>

#include <pspiofilemgr.h>

#include "common.h"
#include "fast_forward_policy.h"
#include "test_session.h"
#include "video_me.h"

#ifndef TEST_SESSION_WARM_UP_S
#define TEST_SESSION_WARM_UP_S 5
#endif
#ifndef TEST_SESSION_CAPTURE_S
#define TEST_SESSION_CAPTURE_S 60
#endif
#ifndef TEST_SESSION_FAST_S
#define TEST_SESSION_FAST_S 30
#endif
/* A longer gap between two frames is the menu or a sleep; well above the
   longest frame of a capture, the one that starts the Media Engine. */
#define TEST_SESSION_PAUSE_US 5000000u
/* Rough loading time of a step, for the estimate shown at the start. */
#define TEST_SESSION_LOAD_S 3

#define GAME_COUNT 4
#define STEP_CAPACITY (GAME_COUNT * 8)

/* GBA KEYINPUT bit of A: Mario Kart accelerates through its captures. */
#define GBA_KEY_A 0x001u

enum
{
  VIEW_NEW,
  VIEW_EXACT_CPU,
  VIEW_EXACT_ME
};

static const char *const view_names[] = { "new", "exact-cpu", "exact-me" };

/* The names decode_runtime_log.py gives the Media Engine states. */
static const char *const me_state_names[] =
{
  "not-started", "media-engine", "cpu-no-module", "cpu-no-driver",
  "cpu-no-answer", "cpu-stopped", "cpu-fault", "cpu-too-slow",
  "cpu-no-memory"
};

typedef struct
{
  char code[3];
  const char *name;
  u32 keys;
} KnownGame;

/* Matched on the first three letters of the game code (0xAC), so every
   region counts. Mario Kart comes first: plan_steps gives it the
   fast-forward captures. */
static const KnownGame known_games[GAME_COUNT] =
{
  { { 'A', 'M', 'K' }, "mario-kart", GBA_KEY_A },
  { { 'B', 'P', 'R' }, "firered", 0 },
  { { 'B', 'P', 'G' }, "leafgreen", 0 },
  { { 'A', 'G', 'S' }, "golden-sun", 0 }
};

typedef struct
{
  u8 game;
  u8 core;
  u8 view;
  u8 fast;
} Step;

static char rom_paths[GAME_COUNT][MAX_PATH];
static char loaded_path[MAX_PATH];
static Step steps[STEP_CAPACITY];
static unsigned int step_count;
static unsigned int step_index;
static int active;
static int ran;
static int measuring;
static u32 state_loaded;
static u64 phase_start;
static u64 last_frame;
static VideoMeCounters me_before;

/* The settings the session changes, put back when it ends. */
static u32 saved_renderer;
static u32 saved_core;
static u32 saved_show_fps;

static void results_printf(const char *format, ...)
 __attribute__((format(printf, 1, 2)));

static void results_printf(const char *format, ...)
{
  char path[MAX_PATH];
  char line[384];
  va_list args;
  SceUID file;
  int length;

  va_start(args, format);
  length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if(length <= 0)
    return;
  if(length >= (int)sizeof(line))
    length = sizeof(line) - 1;

  /* Opened for each line, so a crash keeps the lines before it. */
  snprintf(path, sizeof(path), "%sbenchmarks/test-session.txt", main_path);
  file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if(file < 0)
    return;
  sceIoWrite(file, line, length);
  sceIoClose(file);
}

/* The first .gba file in the ROM folder for each known game. */
static void find_games(void)
{
  SceIoDirent entry;
  SceUID dir = sceIoDopen(dir_roms);

  if(dir < 0)
    return;

  while(1)
  {
    char path[MAX_PATH];
    char code[3];
    size_t length;
    SceUID file;
    u32 i;

    memset(&entry, 0, sizeof(entry));
    if(sceIoDread(dir, &entry) <= 0)
      break;
    length = strlen(entry.d_name);
    if(FIO_S_ISDIR(entry.d_stat.st_mode) || length < 4 ||
       strcasecmp(entry.d_name + length - 4, ".gba") != 0)
      continue;

    snprintf(path, sizeof(path), "%s%s", dir_roms, entry.d_name);
    file = sceIoOpen(path, PSP_O_RDONLY, 0);
    if(file < 0)
      continue;
    if(sceIoLseek32(file, 0xAC, PSP_SEEK_SET) == 0xAC &&
       sceIoRead(file, code, sizeof(code)) == sizeof(code))
    {
      for(i = 0; i < GAME_COUNT; i++)
      {
        if(rom_paths[i][0] == 0 &&
           memcmp(code, known_games[i].code, sizeof(code)) == 0)
          snprintf(rom_paths[i], sizeof(rom_paths[i]), "%s", path);
      }
    }
    sceIoClose(file);
  }
  sceIoDclose(dir);
}

static void add_step(u32 game, u32 core, u32 view, u32 fast)
{
  Step *step = &steps[step_count++];

  step->game = game;
  step->core = core;
  step->view = view;
  step->fast = fast;
}

static void plan_steps(void)
{
  u32 game;
  u32 core;

  for(game = 0; game < GAME_COUNT; game++)
  {
    if(rom_paths[game][0] == 0)
      continue;
    for(core = CPU_CORE_CLASSIC; core <= CPU_CORE_NEW; core++)
    {
      add_step(game, core, VIEW_NEW, 0);
      add_step(game, core, VIEW_EXACT_CPU, 0);
      if(game == 0)
        add_step(game, core, VIEW_NEW, 1);
    }
  }
  for(game = 0; game < GAME_COUNT; game++)
  {
    if(rom_paths[game][0] == 0)
      continue;
    for(core = CPU_CORE_CLASSIC; core <= CPU_CORE_NEW; core++)
      add_step(game, core, VIEW_EXACT_ME, 0);
  }
}

static unsigned int planned_minutes(void)
{
  unsigned int seconds = 0;
  unsigned int i;

  for(i = 0; i < step_count; i++)
  {
    seconds += TEST_SESSION_LOAD_S + TEST_SESSION_WARM_UP_S +
     (steps[i].fast ? TEST_SESSION_FAST_S : TEST_SESSION_CAPTURE_S);
  }
  return (seconds + 59) / 60;
}

/* Back to the player's settings, and this game's own ones in place of the
   ones the session set over them. */
static void restore_settings(void)
{
  option_video_renderer = saved_renderer;
  option_cpu_core = saved_core;
  psp_fps_debug = saved_show_fps;
  load_game_config_file();
  main_fast_forward_apply_settings();
  set_cpu_clock(option_clock_speed);
}

static void end_session(const char *how)
{
  if(measuring)
    main_perf_stop();
  measuring = 0;
  active = 0;
  results_printf("end=%s captures=%u of %u\n", how, step_index, step_count);
  restore_settings();
}

/* Loads the game of the current step on its core, from slot 1 when the
   game has one there; 0 if the game did not load. */
static int begin_step(void)
{
  const Step *step = &steps[step_index];
  char name[MAX_FILE];

  if(strcmp(loaded_path, rom_paths[step->game]) != 0)
  {
    if(load_gamepak(rom_paths[step->game]) < 0)
    {
      results_printf("capture=%u game=%s load=failed\n", step_index + 1,
       known_games[step->game].name);
      return 0;
    }
    snprintf(loaded_path, sizeof(loaded_path), "%s", rom_paths[step->game]);
    /* The new game's own volume and screen, as menu() applies them. */
    set_sound_volume();
    video_resolution_small();
  }

  /* After load_gamepak, which reads the game's own settings. */
  option_cpu_core = step->core;
  option_video_renderer =
   step->view == VIEW_NEW ? VIDEO_RENDERER_NEW : VIDEO_RENDERER_PPU;
  option_clock_speed = PSP_CLOCK_333;
  option_frameskip_type = FRAMESKIP_AUTO;
  option_frameskip_value = 9;
  psp_fps_debug = 1;
  /* Fast-forward at maximum speed while held, whatever the player set. */
  option_fast_forward_game_override = 1;
  option_game_fast_forward_profile = FAST_FORWARD_PROFILE_MODERN;
  main_fast_forward_apply_settings();

  reset_gba();
  reg[CHANGED_PC_STATUS] = 1;
  change_ext(gamepak_filename, name, "_1.svs");
  state_loaded = load_state_silent(name);
  set_cpu_clock(PSP_CLOCK_333);

  measuring = 0;
  phase_start = sceKernelGetSystemTimeWide();
  /* The first frame may come late after a load: it only starts the
     watch for pauses. */
  last_frame = 0;
  return 1;
}

static void finish_capture(void)
{
  const Step *step = &steps[step_index];
  VideoMeCounters me_after;
  VideoMeState me_state = video_me_state();
  char report[MAX_PATH];
  const char *report_name = "none";

  main_perf_stop();
  measuring = 0;
  if(main_perf_write_report(report, sizeof(report)) == 0)
  {
    report_name = strrchr(report, '/');
    report_name = report_name ? report_name + 1 : report;
  }
  video_me_counters(&me_after);

  results_printf("capture=%u game=%s core=%s view=%s fast-forward=%s "
   "slot1=%s report=%s me-state=%s me-frames=%u me-late=%u\n",
   step_index + 1, known_games[step->game].name,
   step->core == CPU_CORE_NEW ? "new" : "classic", view_names[step->view],
   step->fast ? "yes" : "no", state_loaded ? "loaded" : "missing",
   report_name,
   (u32)me_state < sizeof(me_state_names) / sizeof(me_state_names[0]) ?
   me_state_names[me_state] : "unknown",
   (unsigned int)(me_after.me_frames - me_before.me_frames),
   (unsigned int)(me_after.late_frames - me_before.late_frames));
}

int test_session_start(void)
{
  char question[64];
  char folder[MAX_PATH];
  ScePspDateTime date;

  find_games();
  plan_steps();
  if(step_count == 0)
    return 0;

  snprintf(question, sizeof(question), "Run the hardware tests? (%u min)",
   planned_minutes());
#ifndef TEST_SESSION_AUTOSTART
  if(yesno_dialog(question) != 0)
    return 0;
#endif

  saved_renderer = option_video_renderer;
  saved_core = option_cpu_core;
  saved_show_fps = psp_fps_debug;

  memset(&date, 0, sizeof(date));
  sceRtcGetCurrentClockLocalTime(&date);
  /* main_perf_write_report makes the folder too, but only at the first
     report. */
  snprintf(folder, sizeof(folder), "%sbenchmarks", main_path);
  sceIoMkdir(folder, 0777);
  results_printf("test-session version=%s build=%s start=%04u-%02u-%02u "
   "%02u:%02u:%02u captures=%u\n", GBA_RUSH_VERSION, GBA_RUSH_BUILD_ID,
   date.year, date.month, date.day, date.hour, date.minute, date.second,
   step_count);

  active = 1;
  ran = 1;
  step_index = 0;
  if(!begin_step())
  {
    end_session("load-failed");
    return 0;
  }
  return 1;
}

int test_session_frame(void)
{
  u64 now;
  u64 length;

  if(!active)
    return 0;

  now = sceKernelGetSystemTimeWide();
  if(last_frame != 0 && now - last_frame > TEST_SESSION_PAUSE_US)
  {
    end_session("interrupted");
    error_msg("Hardware tests stopped. Saving is off until the emulator "
     "restarts.", CONFIRMATION_CONT);
    return 0;
  }
  /* No button is pressed for half an hour: keep Auto Sleep away. */
  if(now / 1000000u != last_frame / 1000000u)
    scePowerTick(PSP_POWER_TICK_ALL);
  last_frame = now;

  if(!measuring)
  {
    if(now - phase_start < TEST_SESSION_WARM_UP_S * 1000000ull)
      return 0;
    video_me_counters(&me_before);
    /* Should a capture already run, its report is the one written. */
    main_perf_start();
    measuring = 1;
    phase_start = now;
    return 0;
  }

  length = steps[step_index].fast ? TEST_SESSION_FAST_S :
   TEST_SESSION_CAPTURE_S;
  if(now - phase_start < length * 1000000ull)
    return 0;

  finish_capture();
  step_index++;
  if(step_index < step_count && begin_step())
    return 1;

  end_session(step_index < step_count ? "load-failed" : "done");
  error_msg("Hardware tests finished: connect the PSP to the Mac.",
   CONFIRMATION_QUIT);
  quit();
  return 0;
}

u32 test_session_gba_keys(void)
{
  return active ? known_games[steps[step_index].game].keys : 0;
}

int test_session_fast_forward(void)
{
  return active && steps[step_index].fast;
}

int test_session_exact_cpu(void)
{
  return active && steps[step_index].view == VIEW_EXACT_CPU;
}

int test_session_keeps_slow_me(void)
{
  return active;
}

void test_session_stop(void)
{
  if(active)
    end_session("interrupted");
}

int test_session_ran(void)
{
  return ran;
}
