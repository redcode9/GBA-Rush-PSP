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

/* Emulator entry point and main loop. update_gba is the hardware scheduler
   the dynarec calls between slices (timers, LCD timing, DMA, IRQs); around it
   live frame pacing, startup, the PSP power and exit callbacks, and the
   link cable, fast-forward and benchmark glue used by gui.c and input.c. */

#include <limits.h>

#include <pspsysmem.h>

#include "common.h"
#include "fast_forward_policy.h"
#include "perf_monitor.h"
#include "perf_report.h"
#include "power_lifecycle.h"
#include "psp_adhoc.h"
#include "runtime_log_psp.h"
#include "sound_math.h"

// Main Thread Params
#define PRIORITY       (32)
#define STACK_SIZE_KB  (512)
#define ATTRIBUTE      (THREAD_ATTR_USER | PSP_THREAD_ATTR_CLEAR_STACK)

PSP_MODULE_INFO("GBA Rush PSP", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_PARAMS(PRIORITY, STACK_SIZE_KB, ATTRIBUTE);
PSP_HEAP_THRESHOLD_SIZE_KB(1024);
/* Negative: the heap takes all free user memory except 2 MB. */
PSP_HEAP_SIZE_KB(-2048);


/* User settings; gui.c loads, edits and saves them. */
u32 option_screen_scale = SCALED_16X9_AUTO;
u32 option_screen_mag = 170;
u32 option_screen_filter = FILTER_BILINEAR;
u32 option_sound_volume = 10;
u32 option_stack_optimize = 1;
u32 option_ram_dynarec_policy = RAM_DYNAREC_PARTIAL_WITH_REUSE;
u32 option_video_renderer = VIDEO_RENDERER_NEW;
u32 option_oam_hijacking_enabled = 0;
u32 option_boot_mode = 0;
u32 option_update_backup = 0;
u32 option_screen_capture_format = 0;
u32 option_enable_analog = 0;
u32 option_analog_sensitivity = 4;
u32 option_language = 0;
u32 option_theme = 0;
u32 option_swap_confirm_buttons = 0;

u32 option_frameskip_type = FRAMESKIP_AUTO;
u32 option_frameskip_value = 9;
u32 option_clock_speed = PSP_CLOCK_333;

u32 option_hblank_irq_window_start = 1;
u32 option_hblank_irq_window_end = 160;
u32 option_psp_vsync = 0;

u32 option_fast_forward_profile = FAST_FORWARD_PROFILE_MODERN;
u32 option_fast_forward_activation = FAST_FORWARD_ACTIVATION_HOLD;
u32 option_fast_forward_speed = FAST_FORWARD_SPEED_MAXIMUM;
u32 option_fast_forward_presentation = FAST_FORWARD_PRESENTATION_BALANCED;
u32 option_fast_forward_audio = FAST_FORWARD_AUDIO_SILENT;

u32 option_fast_forward_game_override = 0;
u32 option_game_fast_forward_profile = FAST_FORWARD_PROFILE_MODERN;
u32 option_game_fast_forward_activation = FAST_FORWARD_ACTIVATION_HOLD;
u32 option_game_fast_forward_speed = FAST_FORWARD_SPEED_MAXIMUM;
u32 option_game_fast_forward_presentation =
 FAST_FORWARD_PRESENTATION_BALANCED;
u32 option_game_fast_forward_audio = FAST_FORWARD_AUDIO_SILENT;

char main_path[MAX_PATH];

int date_format = 0;
u32 enable_home_menu = 1;

/* sleep_flag: set by the power callback while the PSP suspends.
   sleep_auto_saved: the suspend savestate of this suspend is written. */
volatile u32 sleep_flag = 0;
volatile u32 sleep_auto_saved = 0;

/* 0 while fast-forward runs: synchronize() then never waits for VBlank. */
u32 synchronize_flag = 1;
u32 psp_fps_debug = 0;

/* PSP VBlanks seen and GBA frames emulated since the last resync; their
   difference drives frameskip in synchronize(). */
u32 real_frame_count = 0;
u32 virtual_frame_count = 0;


typedef enum
{
  TIMER_INACTIVE,
  TIMER_PRESCALE,
  TIMER_CASCADE
} TIMER_STATUS_TYPE;

typedef enum
{
  TIMER_NO_IRQ,
  TIMER_TRIGGER_IRQ
} TIMER_IRQ_TYPE;

typedef enum
{
  TIMER_DS_CHANNEL_NONE,
  TIMER_DS_CHANNEL_A,
  TIMER_DS_CHANNEL_B,
  TIMER_DS_CHANNEL_BOTH
} TIMER_DS_CHANNEL_TYPE;

/* One GBA timer (TM0-TM3). Part of the savestate. */
typedef struct
{
  TIMER_DS_CHANNEL_TYPE direct_sound_channels; /* FIFOs it clocks (0 and 1) */
  TIMER_IRQ_TYPE irq;
  TIMER_STATUS_TYPE status;
  FIXED08_24 frequency_step;  /* Direct Sound sample step per overflow */
  s32 count;                  /* CPU cycles left until overflow */
  u32 reload;                 /* ticks per overflow: 0x10000 - TMxCNT_L */
  u32 reload_update;          /* reload changed; recompute frequency_step */
  u32 prescale;               /* log2 of CPU cycles per tick */
} TimerType;

/* TMxCNT_H bits 0-1: 1, 64, 256 or 1024 cycles per tick, as shifts. */
const u8 ALIGN_DATA prescale_table[] = { 0, 6, 8, 10 };
TimerType ALIGN_DATA timer[4];

/* CPU cycles a running DMA still stalls the CPU for (set by memory.c). */
u32 dma_cycle_count = 0;
LinkCable gba_link;
static u8 gba_link_open = 0;
static MainLinkEnd gba_link_end = MAIN_LINK_END_NONE;
static u8 gba_link_gameplay_active = 1;
/* Last link state written to the runtime log, so only changes are logged. */
static u8 gba_link_logged_status = LINK_CABLE_OFF;
static u8 gba_link_logged_mode = LINK_CABLE_MODE_NONE;
static u32 gba_link_service_ticks = 0;
/* Statistics of the waits for the other console, logged at session end. */
static u32 gba_link_waits = 0;
static u32 gba_link_wait_max_us = 0;
static u64 gba_link_wait_total_us = 0;
static PowerLifecycle psp_power_lifecycle;
static FastForwardPolicy fast_forward_policy;
/* The ROM file was closed for suspend and must be reopened on resume. */
static u32 sleep_reopen_gamepak = 0;
/* Game code of the ROM the runtime log currently describes. */
static u8 runtime_log_rom_active = 0;
static u32 runtime_log_rom_identity = 0;
/* Shutdown handshake between quit() on the main thread and the exit
   callback thread. */
static volatile int quit_started = 0;
static volatile int exit_requested = 0;
static volatile int quit_cleanup_complete = 0;

#define MAIN_EXIT_CALLBACK_TIMEOUT_US 2000000u
/* About 1 ms of guest time. Idle link service runs at this rate instead of
   on every update_gba pass (~27k/s), which left FireRed no idle time. */
#define MAIN_LINK_SERVICE_CYCLES 16384u
/* While the link holds the guest, poll the network this often. */
#define MAIN_LINK_WAIT_POLL_US 1000u
/* While waiting for VBlank in lockstep mode, check for VBlank this often. */
#define MAIN_LINK_VBLANK_POLL_US 500u
/* A frame that runs late never waits for VBlank, and then no thread below
   the emulator gets the CPU. Part of the PSP's Wi-Fi stack sits there:
   starving it held every datagram for seconds until the link was lost.
   While a link is open, each late frame gives up this much time. */
#define MAIN_LINK_YIELD_US 2000u

/* Guest CPU cycles since reset; wraps. The guest clock for sound and link. */
u32 cpu_ticks = 0;
/* Cycles left in the current LCD phase: 960 of H-draw, then 272 of H-blank
   per scanline. */
s32 video_count = 272;

/* IRQ entry delay: cpu_init_state is set while an IRQ waits irq_ticks
   cycles before it is taken. */
u32 irq_ticks = 0;
u8 cpu_init_state = 0;

/* Frameskip state, and the FPS counter (fps_window_frames, vblank_count and
   interval_skipped_frames cover a 60-frame window). */
u32 skip_next_frame = 0;
u32 num_skipped_frames = 0;
u32 frameskip_counter = 0;
u32 interval_skipped_frames = 0;
u32 fps_window_frames = 0;
u32 vblank_count = 0;

/* The first init_main flushes both translation caches; later ROM loads
   flush the read-only one and clear the RAM block metadata instead. */
static u8 caches_inited = 0;

static void cpu_interrupt(void);

int main(int argc, char *argv[]);
int user_main(int argc, char *argv[]);

static void init_main(void);
static void setup_main(const char *eboot_path);
static void load_setting_cfg(void);
static void load_bios_file(void);

static void setup_callbacks(void);
static int exit_callback(int arg1, int arg2, void *common);
static int power_callback(int unknown, int powerInfo, void *arg);
static int callback_thread(SceSize args, void *argp);

static void vblank_interrupt_handler(u32 sub, u32 *parg);

static void synchronize(void);
static void psp_sleep_loop(void);
static void main_power_save_if_pending(void);
static void main_power_reopen_gamepak(void);
static void main_link_close(MainLinkEnd reason);

/* The clock call under the NID newer firmware exports; setup_main picks
   which one to use. */
#define scePowerSetClockFrequency371  scePower_EBD177D6
static int (*__scePowerSetClockFrequency)(int pllfreq, int cpufreq, int busfreq);


#define SOUND_UPDATE_FREQUENCY_STEP                                           \
  sound_math_timer_step(timer_reload, SOUND_FREQUENCY)                        \

/* Shortens the next CPU slice so the dynarec returns to update_gba by the
   time this event is due. */
#define CHECK_COUNT(count_var)                                                \
  if (count_var < reg[EXECUTE_CYCLES])                                        \
  {                                                                           \
    reg[EXECUTE_CYCLES] = count_var;                                          \
  }                                                                           \

#define CHECK_TIMER(timer_number)                                             \
  if (timer[timer_number].status == TIMER_PRESCALE)                           \
  {                                                                           \
    CHECK_COUNT((u32)timer[timer_number].count);                              \
  }                                                                           \


/* TMxCNT_L write: the new reload value applies at the next start or
   overflow. */
void timer_control_low(u8 timer_number, u32 value)
{
  TimerType *tm = timer + timer_number;

  tm->reload = 0x10000 - value;
  tm->reload_update = 1;
}

/* TMxCNT_H write: bit 7 enables, bit 2 selects count-up (cascade) mode,
   bit 6 enables the overflow IRQ, bits 0-1 pick the prescaler. Only an
   enable edge is acted on; writes to a running timer are ignored. */
CPU_ALERT_TYPE timer_control_high(u8 timer_number, u32 value)
{
  TimerType *tm = timer + timer_number;

  if ((value & 0x80) != 0)
  {
    if (tm->status == TIMER_INACTIVE)
    {
      if ((value & 0x04) != 0)
      {
        tm->status = TIMER_CASCADE;
        tm->prescale = 0;
      }
      else
      {
        tm->status = TIMER_PRESCALE;
        tm->prescale = prescale_table[value & 0x03];
      }

      tm->irq = (value >> 6) & 0x01;

      u32 timer_reload = tm->reload;

      // TMxCNT_L reads back the reload value; timers are 4 bytes apart
      pIO_REG(REG_TM0D + (timer_number << 1)) = 0x10000 - timer_reload;

      timer_reload <<= tm->prescale;
      tm->count = timer_reload;

      // Only timers 0 and 1 can clock the Direct Sound FIFOs
      if (timer_number < 2)
      {
        tm->frequency_step = SOUND_UPDATE_FREQUENCY_STEP;
        tm->reload_update = 0;

        if ((tm->direct_sound_channels & 0x01) != 0)
          adjust_direct_sound_buffer(0, cpu_ticks + timer_reload);

        if ((tm->direct_sound_channels & 0x02) != 0)
          adjust_direct_sound_buffer(1, cpu_ticks + timer_reload);
      }

      return CPU_ALERT_TIMER;
    }
  }
  else
  {
    tm->status = TIMER_INACTIVE;
  }

  return CPU_ALERT_NONE;
}

/* SOUNDCNT_H write: bits 10 and 14 pick timer 0 or 1 for Direct Sound A
   and B. In the resulting masks bit 0 is channel A and bit 1 channel B. */
void direct_sound_timer_select(u32 value)
{
  TIMER_DS_CHANNEL_TYPE timer_select;

  timer_select = ((value >> 13) & 0x02) | ((value >> 10) & 0x01);

  timer[0].direct_sound_channels = timer_select ^ 0x03;
  timer[1].direct_sound_channels = timer_select;
}


/* Takes an IRQ: enters the BIOS IRQ vector in IRQ mode, as the ARM7 does. */
static void cpu_interrupt(void)
{
  // Interrupt handler in BIOS; lr_irq = return address + 4
  reg_mode[MODE_IRQ][6] = reg[REG_PC] + 4;
  spsr[MODE_IRQ] = reg[REG_CPSR];
  reg[REG_CPSR] = (reg[REG_CPSR] & ~0xFF) | 0x92; // set mode IRQ & disable IRQ
  reg[REG_PC] = 0x00000018;
  set_cpu_mode(MODE_IRQ);

  // What a protected BIOS read returns while the BIOS IRQ handler runs
  bios_read_protect = 0xe55ec002;

  reg[CPU_HALT_STATE] = CPU_ACTIVE;
  reg[CHANGED_PC_STATUS] = 1;
}

/* Runs a timer for the cycles just executed. On overflow it raises the IRQ,
   ticks a cascaded next timer, clocks its Direct Sound FIFOs and reloads.
   TMxCNT_L then reads back the current count. */
#define UPDATE_TIMER(timer_number)                                            \
{                                                                             \
  TimerType *tm = timer + timer_number;                                       \
                                                                              \
  if (tm->status != TIMER_INACTIVE)                                           \
  {                                                                           \
    if (tm->status != TIMER_CASCADE)                                          \
      tm->count -= reg[EXECUTE_CYCLES];                                       \
                                                                              \
    if (tm->count <= 0)                                                       \
    {                                                                         \
      if (tm->irq == TIMER_TRIGGER_IRQ)                                       \
      {                                                                       \
        irq_raised |= (IRQ_TIMER0 << timer_number);                           \
      }                                                                       \
                                                                              \
      if (timer_number != 3)                                                  \
      {                                                                       \
        if (tm[1].status == TIMER_CASCADE)                                    \
          tm[1].count--;                                                      \
      }                                                                       \
                                                                              \
      u32 timer_reload = tm->reload << tm->prescale;                          \
                                                                              \
      if (timer_number < 2)                                                   \
      {                                                                       \
        if ((tm->direct_sound_channels & 0x01) != 0)                          \
          sound_timer(tm->frequency_step, 0);                                 \
                                                                              \
        if ((tm->direct_sound_channels & 0x02) != 0)                          \
          sound_timer(tm->frequency_step, 1);                                 \
                                                                              \
        if (tm->reload_update != 0)                                           \
        {                                                                     \
          tm->frequency_step = SOUND_UPDATE_FREQUENCY_STEP;                   \
          tm->reload_update = 0;                                              \
        }                                                                     \
      }                                                                       \
                                                                              \
      tm->count += timer_reload;                                              \
    }                                                                         \
                                                                              \
    pIO_REG(REG_TM0D + (timer_number << 1)) = 0x10000 - (tm->count >> tm->prescale); \
  }                                                                           \
}                                                                             \

#define TRANSFER_HBLANK_DMA(channel)                                          \
  if (dma[channel].start_type == DMA_START_HBLANK)                            \
  {                                                                           \
    dma_transfer(dma + channel);                                              \
  }                                                                           \

#define TRANSFER_VBLANK_DMA(channel)                                          \
  if (dma[channel].start_type == DMA_START_VBLANK)                            \
  {                                                                           \
    dma_transfer(dma + channel);                                              \
  }                                                                           \

/* The ad-hoc worker's state and failure phase as plain u32 fields for
   runtime log records. */
static u32 main_adhoc_state_value(void)
{
  PspAdhocState state = psp_adhoc_state();

  return (u32)state;
}

static u32 main_adhoc_phase_value(void)
{
  PspAdhocPhase phase = psp_adhoc_failure_phase();

  return (u32)phase;
}

/* ---- Link cable --------------------------------------------------------
 * The cable logic lives in link_cable.c and runs on this thread. Here it is
 * fed with datagrams from the ad-hoc worker and the guest clock, and it may
 * hold the guest while a transfer needs the other console. */

int main_link_active(void)
{
  return gba_link_open;
}

int main_link_connected(void)
{
  return gba_link_open && gba_link.status == LINK_CABLE_CONNECTED;
}

/* Why the last session ended or failed to start, for the link menu;
   kept after it closes, NONE once a new one starts. */
MainLinkEnd main_link_end_reason(void)
{
  return gba_link_end;
}

/* Logs a LINK_STATE record only when the link status or mode changed since
   the last one, so the link service can call it on every pass. */
static void main_link_log_state(void)
{
  if(gba_link.status == gba_link_logged_status &&
     gba_link.mode == gba_link_logged_mode)
    return;
  gba_link_logged_status = (u8)gba_link.status;
  gba_link_logged_mode = (u8)gba_link.mode;
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_STATE, cpu_ticks,
   gba_link.host, (u32)gba_link.status, (u32)gba_link.mode,
   main_adhoc_state_value());
}

/* Ends the session without telling the peer (main_link_disconnect sends
   the goodbye): fails any SIO transfer, logs counters, stops the network. */
static void main_link_close(MainLinkEnd reason)
{
  uint32_t sends;
  uint32_t send_errors;
  uint32_t receive_drops;

  if(!gba_link_open)
    return;

  gba_link_open = 0;
  gba_link_end = reason;
  if(gba_sio_disconnect(&gba_sio) & GBA_SIO_EVENT_SERIAL_IRQ)
    pIO_REG(REG_IF) |= IRQ_SERIAL;

  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 0,
   gba_link.net.rtt_us, gba_link.net.rtt_max_us, gba_link.net.resends);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 1,
   gba_link.net.datagrams_sent, gba_link.net.datagrams_received,
   gba_link.net.datagrams_rejected);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 2,
   gba_link.pokemon.frames_sent, gba_link.pokemon.frames_received,
   gba_link.lockstep.transfers);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 3,
   gba_link_waits, gba_link_wait_max_us,
   gba_link_wait_total_us > UINT_MAX ? UINT_MAX :
    (u32)gba_link_wait_total_us);
  psp_adhoc_counters(&sends, &send_errors, &receive_drops);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 4,
   sends, send_errors, receive_drops);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_ERROR, cpu_ticks,
   (u8)reason, (u32)gba_link.status, (u32)psp_adhoc_last_error(),
   main_adhoc_phase_value());
  if(reason != MAIN_LINK_END_USER)
    runtime_log_psp_flush(RUNTIME_LOG_FLUSH_LINK_FAILURE);

  if(quit_started)
    psp_adhoc_stop_for_exit();
  else
  {
    psp_adhoc_stop();
    set_cpu_clock(option_clock_speed);
  }
}

/* Moves datagrams in both directions and runs the cable. Returns 1 if a
   serial interrupt is due. */
static int main_link_exchange(void)
{
  u8 datagram[LINK_NET_MAX_DATAGRAM];
  u64 now = sceKernelGetSystemTimeWide();
  GbaSioEvent events;
  size_t size;

  while((size = psp_adhoc_receive(datagram, sizeof(datagram))) != 0)
    link_cable_receive(&gba_link, datagram, size, now);
  events = link_cable_service(&gba_link, cpu_ticks, now);
  size = link_cable_build(&gba_link, datagram, sizeof(datagram), cpu_ticks,
   now);
  if(size != 0)
    psp_adhoc_send(datagram, size);
  main_link_log_state();

  if(gba_link.status == LINK_CABLE_PEER_LEFT)
    main_link_close(MAIN_LINK_END_PEER_LEFT);
  else if(gba_link.status == LINK_CABLE_LOST)
    main_link_close(MAIN_LINK_END_LOST);
  else if(gba_link.status == LINK_CABLE_INCOMPATIBLE)
    main_link_close(MAIN_LINK_END_INCOMPATIBLE);
  else if(psp_adhoc_state() == PSP_ADHOC_FAILED)
    main_link_close(MAIN_LINK_END_NETWORK);

  return (events & GBA_SIO_EVENT_SERIAL_IRQ) != 0;
}

/* Runs the cable from update_gba and holds the guest while the cable says
   it must wait. Input, the menu and power events stay live during the
   wait. Returns 1 if the menu changed the machine and update_gba should
   start over, as it does after the menu at VBlank. */
static int main_link_service(IRQ_TYPE *irq_raised)
{
  u64 started;
  u32 waited;
  int restart = 0;

  if(main_link_exchange())
    *irq_raised |= IRQ_SERIAL;
  if(!gba_link_open || !link_cable_blocked(&gba_link, cpu_ticks))
    return 0;

  started = sceKernelGetSystemTimeWide();
  sound_link_wait_begin();
  while(gba_link_open && link_cable_blocked(&gba_link, cpu_ticks))
  {
    main_power_service();
    if(update_input() != 0)
    {
      restart = 1;
      break;
    }
    psp_adhoc_wait(MAIN_LINK_WAIT_POLL_US);
    if(main_link_exchange())
      *irq_raised |= IRQ_SERIAL;
  }
  sound_link_wait_end(cpu_ticks);

  waited = (u32)(sceKernelGetSystemTimeWide() - started);
  gba_link_waits++;
  gba_link_wait_total_us += waited;
  if(waited > gba_link_wait_max_us)
    gba_link_wait_max_us = waited;
  /* Log only pauses of 100 ms or more. */
  if(waited >= 100000u)
    runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_PAUSE_END, cpu_ticks,
     (u8)gba_link.mode, waited, (u32)gba_link.status,
     link_net_backlog(&gba_link.net));
  return restart;
}

/* Hosts or joins after stopping fast-forward and any earlier session.
   0 if WLAN is off or the SIO or the network could not be started. */
static int main_link_start(int host)
{
  char game[4] = { 0, 0, 0, 0 };
  u64 now;
  u32 id;

  if(!psp_adhoc_wlan_enabled())
    return 0;

  /* The no-ROM menu has never called reset_gba. Prepare only a cold SIO;
     an already loaded game's registers stay intact. */
  if(!gba_sio_prepare(&gba_sio, io_registers))
    return 0;

  main_fast_forward_stop();
  main_link_disconnect(MAIN_LINK_END_USER);

  /* The game code sits at 0xAC in the 0xC0-byte cartridge header. */
  if(gamepak_filename[0] != '\0' && gamepak_file_size >= 0xC0u)
    memcpy(game, gamepak_header + 0xAC, sizeof(game));
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COMMAND, cpu_ticks,
   (u8)(host != 0), ((u32)(u8)game[0] << 24) | ((u32)(u8)game[1] << 16) |
   ((u32)(u8)game[2] << 8) | (u8)game[3], 0, 0);

  /* Pin the clock before the network starts; changing it later can
     disturb the WLAN. */
  set_cpu_clock(PSP_CLOCK_333);
  if(!psp_adhoc_start(host))
  {
    set_cpu_clock(option_clock_speed);
    gba_link_end = MAIN_LINK_END_NETWORK;
    runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_ERROR, cpu_ticks,
     (u8)MAIN_LINK_END_NETWORK, 0, (u32)psp_adhoc_last_error(),
     main_adhoc_phase_value());
    return 0;
  }

  /* Session id from the clock, salted with "HOST" or "JOIN" so the two
     consoles differ; never 0. */
  now = sceKernelGetSystemTimeWide();
  id = (u32)now ^ (u32)(now >> 32) ^ (host ? 0x484F5354u : 0x4A4F494Eu);
  link_cable_start(&gba_link, &gba_sio, host, game, id != 0 ? id : 1u,
   now);
  gba_link_open = 1;
  gba_link_end = MAIN_LINK_END_NONE;
  gba_link_waits = 0;
  gba_link_wait_max_us = 0;
  gba_link_wait_total_us = 0;
  main_link_log_state();
  return 1;
}

int main_link_host(void)
{
  return main_link_start(1);
}

int main_link_join(void)
{
  return main_link_start(0);
}

/* Ends the session and tells the other console. During quit it also makes
   sure the network worker is gone when no session is open. */
void main_link_disconnect(MainLinkEnd reason)
{
  u8 datagram[LINK_NET_MAX_DATAGRAM];
  size_t size;

  if(!gba_link_open)
  {
    if(quit_started)
      psp_adhoc_stop_for_exit();
    return;
  }

  /* Tell the other console, so it does not wait for a timeout. */
  link_cable_leave(&gba_link);
  size = link_cable_build(&gba_link, datagram, sizeof(datagram), cpu_ticks,
   sceKernelGetSystemTimeWide());
  if(size != 0)
    psp_adhoc_send(datagram, size);
  main_link_close(reason);
}

/* Waits for the next PSP VBlank like sceDisplayWaitVblankStart, but
   answers link traffic meanwhile. In lockstep mode the other console may
   be waiting for our reply, and a whole frame of sleep would add up to
   16 ms to every transfer. */
static void main_link_wait_vblank(void)
{
  u32 vcount = sceDisplayGetVcount();

  if(!gba_link_open || gba_link.mode != LINK_CABLE_MODE_LOCKSTEP)
  {
    sceDisplayWaitVblankStart();
    return;
  }
  while(gba_link_open && sceDisplayGetVcount() == vcount)
  {
    psp_adhoc_wait(MAIN_LINK_VBLANK_POLL_US);
    if(main_link_exchange())
      pIO_REG(REG_IF) |= IRQ_SERIAL;
  }
}

/* Keeps the link alive while a menu has the emulation stopped. */
void main_link_pump(void)
{
  if(gba_link_open && main_link_exchange())
    pIO_REG(REG_IF) |= IRQ_SERIAL;
}

/* menu() reports leaving (0) and resuming (1) the game. Each change is
   logged, and the runtime log is flushed when the menu opens. */
void main_link_set_gameplay_active(int active)
{
  u8 value = active != 0;

  if(gba_link_gameplay_active == value)
    return;
  gba_link_gameplay_active = value;
  runtime_log_psp_main(RUNTIME_LOG_EVENT_MENU_PRESENCE, cpu_ticks,
   0, value, (u32)gba_link.status, main_adhoc_state_value());
  if(!value)
    runtime_log_psp_flush(RUNTIME_LOG_FLUSH_MENU);
}

/* ---- Fast-forward ------------------------------------------------------
 * fast_forward_policy.c decides speed and which frames to show; this glue
 * feeds it the button state and applies its decisions to sync and sound. */

/* The game's own settings replace the global ones when it has an override. */
static FastForwardSettings main_fast_forward_settings(void)
{
  FastForwardSettings settings;

  if(option_fast_forward_game_override)
  {
    settings.profile = (FastForwardProfile)option_game_fast_forward_profile;
    settings.activation =
     (FastForwardActivation)option_game_fast_forward_activation;
    settings.speed = (FastForwardSpeed)option_game_fast_forward_speed;
    settings.presentation =
     (FastForwardPresentation)option_game_fast_forward_presentation;
    settings.audio = (FastForwardAudio)option_game_fast_forward_audio;
  }
  else
  {
    settings.profile = (FastForwardProfile)option_fast_forward_profile;
    settings.activation =
     (FastForwardActivation)option_fast_forward_activation;
    settings.speed = (FastForwardSpeed)option_fast_forward_speed;
    settings.presentation =
     (FastForwardPresentation)option_fast_forward_presentation;
    settings.audio = (FastForwardAudio)option_fast_forward_audio;
  }

  fast_forward_settings_sanitize(&settings);
  return settings;
}

/* Counts on/off and speed changes for the benchmark report. */
static void main_fast_forward_record_change(int was_active,
 FastForwardSpeed was_speed)
{
  int active = fast_forward_policy_active(&fast_forward_policy);

  if(was_active != active ||
     (active && was_speed != fast_forward_policy_speed(&fast_forward_policy)))
    perf_monitor_record_fast_forward_transition();
}

/* Matches sound to the policy: normal, muted fast-forward, or accelerated
   audio at the 2x or 3x factor. */
static void main_fast_forward_update_sound(void)
{
  if(!fast_forward_policy_active(&fast_forward_policy))
  {
    sound_set_fast_forward(0);
    return;
  }

  if(fast_forward_policy_audio(&fast_forward_policy) ==
     FAST_FORWARD_AUDIO_ACCELERATED)
    sound_set_fast_forward_audio(
     fast_forward_policy_speed_multiplier(&fast_forward_policy));
  else
    sound_set_fast_forward(1);
}

/* Called from update_input every frame with the fast-forward button's new
   press and held state. Fast-forward is unavailable during a link session. */
void main_fast_forward_input(u32 pressed, u32 held)
{
  int was_active;
  FastForwardSpeed was_speed;

  if(main_link_active())
  {
    if(fast_forward_policy_active(&fast_forward_policy))
      main_fast_forward_stop();
    return;
  }

  was_active = fast_forward_policy_active(&fast_forward_policy);
  was_speed = fast_forward_policy_speed(&fast_forward_policy);
  fast_forward_policy_input(&fast_forward_policy, pressed != 0, held != 0);
  main_fast_forward_record_change(was_active, was_speed);
  synchronize_flag = !fast_forward_policy_active(&fast_forward_policy);
  main_fast_forward_update_sound();
}

/* Ends fast-forward at once and restores normal pacing and sound; used by
   the menu, reset, suspend, quit and link start. */
void main_fast_forward_stop(void)
{
  int was_active = fast_forward_policy_active(&fast_forward_policy);
  FastForwardSpeed was_speed =
   fast_forward_policy_speed(&fast_forward_policy);

  fast_forward_policy_stop(&fast_forward_policy);
  main_fast_forward_record_change(was_active, was_speed);
  synchronize_flag = 1;
  sound_set_fast_forward(0);
}

int main_fast_forward_active(void)
{
  return fast_forward_policy_active(&fast_forward_policy);
}

FastForwardSpeed main_fast_forward_speed(void)
{
  return fast_forward_policy_speed(&fast_forward_policy);
}

/* The on-screen label drawn while fast-forward runs. */
const char *main_fast_forward_status(void)
{
  switch(fast_forward_policy_speed(&fast_forward_policy))
  {
    case FAST_FORWARD_SPEED_2X:
      return "FF 2.00x";
    case FAST_FORWARD_SPEED_3X:
      return "FF 3.00x";
    case FAST_FORWARD_SPEED_MAXIMUM:
    default:
      return "FF MAX";
  }
}

/* Called after the fast-forward settings change, in the menu or with a
   loaded game config; any running fast-forward stops. */
void main_fast_forward_apply_settings(void)
{
  FastForwardSettings settings = main_fast_forward_settings();
  int was_active = fast_forward_policy_active(&fast_forward_policy);
  FastForwardSpeed was_speed =
   fast_forward_policy_speed(&fast_forward_policy);

  fast_forward_policy_configure(&fast_forward_policy, &settings);
  main_fast_forward_record_change(was_active, was_speed);
  synchronize_flag = 1;
  sound_set_fast_forward(0);
}

/* Drops the per-game fast-forward override: the game values take the
   global ones, which are then applied. */
void main_fast_forward_reset_game_settings(void)
{
  option_fast_forward_game_override = 0;
  option_game_fast_forward_profile = option_fast_forward_profile;
  option_game_fast_forward_activation = option_fast_forward_activation;
  option_game_fast_forward_speed = option_fast_forward_speed;
  option_game_fast_forward_presentation = option_fast_forward_presentation;
  option_game_fast_forward_audio = option_fast_forward_audio;
  main_fast_forward_apply_settings();
}

/* The hardware scheduler. The dynarec calls it when a slice of
   reg[EXECUTE_CYCLES] cycles has run: it advances serial, timers, LCD timing
   and DMA by that amount, raises IRQs, and returns the length of the next
   slice (cycles to the next event). While the CPU is halted it keeps
   advancing time itself. Once per frame it also runs input, rendering,
   sound sync and frame pacing. */
u32 update_gba(void)
{
  s32 i;
  IRQ_TYPE irq_raised = IRQ_NONE;
  GbaSioEvent sio_event;

  perf_monitor_record_gba_update_entry();

  do
  {
    reg[CPU_DMA_HACK] = 0;

    update_gba_loop:

    perf_monitor_record_gba_update_iteration();

    main_power_service();

    cpu_ticks += reg[EXECUTE_CYCLES];

    if(gba_sio.active)
    {
      sio_event = gba_sio_advance(&gba_sio, reg[EXECUTE_CYCLES]);
      if((sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
        irq_raised |= IRQ_SERIAL;
      if(!gba_sio.active)
        gba_sio_state_dirty = 1;
    }

    if (gbc_sound_update != 0)
    {
      update_gbc_sound(cpu_ticks);
      gbc_sound_update = 0;
    }

    for (i = 0; i < 4; i++)
    {
      UPDATE_TIMER(i);
    }

    video_count -= reg[EXECUTE_CYCLES];

    if (video_count <= 0)
    {
      /* DISPSTAT bits: 0 V-blank, 1 H-blank, 2 VCOUNT match flags;
         3-5 their IRQ enables; 8-15 the VCOUNT match line. */
      u16 dispstat = pIO_REG(REG_DISPSTAT);
      u16 vcount   = pIO_REG(REG_VCOUNT);

      if ((dispstat & 0x02) == 0)
      {
        // Transition from hdraw to hblank (272 cycles)
        video_count += 272;
        dispstat |= 0x02;

        if ((dispstat & 0x01) == 0)
        {
          if (!skip_next_frame)
            update_scanline();

          // If in visible area also fire HDMA
          for (i = 0; i < 4; i++)
          {
            TRANSFER_HBLANK_DMA(i);
          }
        }

        // H-blank interrupts do occur during v-blank (unlike hdma, which does not)
        if ((dispstat & 0x10) != 0)
        {
          u32 allow_hblank_irq = 1;

          // Optional scanline window; H-blank IRQs outside it are dropped
          if (option_hblank_irq_window_start != 0 &&
              option_hblank_irq_window_end != 0)
          {
            u32 scanline = vcount + 1;

            allow_hblank_irq =
              (scanline >= option_hblank_irq_window_start &&
               scanline <= option_hblank_irq_window_end);
          }

          if (allow_hblank_irq != 0)
            irq_raised |= IRQ_HBLANK;
        }
      }
      else
      {
        // Transition from hblank to next line (960 cycles of hdraw)
        video_count += 960;
        dispstat &= ~0x02;

        vcount++;

        if (vcount == 160)
        {
          // Transition from vrefresh to vblank (lines 160-227)
          dispstat |= 0x01;

          // The menu or a state load replaced the machine; drop this pass
          if (update_input() != 0)
            continue;

          if (!skip_next_frame)
          {
            u32 renderer_start = 0;

            if(perf_monitor_active())
              renderer_start = sceKernelGetSystemTimeLow();

            (*update_screen)();

            if(renderer_start != 0)
              perf_monitor_record_renderer_frame(
               sceKernelGetSystemTimeLow() - renderer_start);
          }

          update_gbc_sound(cpu_ticks);
          gbc_sound_update = 0;

          // Reload the BG2/BG3 affine reference points from BG2X/Y and
          // BG3X/Y (0x28, 0x2C, 0x38, 0x3C), sign-extending 28 bits
          affine_reference_x[0] = (s32)(ADDRESS32(io_registers, 0x28) << 4) >> 4;
          affine_reference_y[0] = (s32)(ADDRESS32(io_registers, 0x2C) << 4) >> 4;
          affine_reference_x[1] = (s32)(ADDRESS32(io_registers, 0x38) << 4) >> 4;
          affine_reference_y[1] = (s32)(ADDRESS32(io_registers, 0x3C) << 4) >> 4;

          for (i = 0; i < 4; i++)
          {
            TRANSFER_VBLANK_DMA(i);
          }

          if ((dispstat & 0x08) != 0)
          {
            irq_raised |= IRQ_VBLANK;
          }
        }
        else

        if (vcount > 227)
        {
          // Transition from vblank to next screen; the frame ends here
          dispstat &= ~0x01;

          if (option_update_backup != 0)
            update_backup();

          process_cheats();

          u32 draw_this_frame = skip_next_frame ^ 0x01;

          synchronize();
          synchronize_sound();

          __draw_volume_status(draw_this_frame);

          if (draw_this_frame != 0)
            flip_screen(option_psp_vsync);

          video_adaptive_frame_complete(draw_this_frame == 0, 0);

          vcount = 0;
          video_dispatch_frame_begin();
        }

        if (vcount == (dispstat >> 8))
        {
          // vcount trigger (line set in DISPSTAT bits 8-15)
          dispstat |= 0x04;

          if ((dispstat & 0x20) != 0)
          {
            irq_raised |= IRQ_VCOUNT;
          }
        }
        else
        {
          dispstat &= ~0x04;
        }

        pIO_REG(REG_VCOUNT) = vcount;
      }

      pIO_REG(REG_DISPSTAT) = dispstat;
    }

    // Serve the link when the game starts a transfer, the cable asks for
    // it, or about every millisecond of guest time
    if(gba_link_open &&
       (gba_sio.pending_start || link_cable_due(&gba_link, cpu_ticks) ||
        cpu_ticks - gba_link_service_ticks >= MAIN_LINK_SERVICE_CYCLES))
    {
      gba_link_service_ticks = cpu_ticks;
      if(main_link_service(&irq_raised))
        continue;
    }

    reg[EXECUTE_CYCLES] = video_count;

    for (i = 0; i < 4; i++)
    {
      CHECK_TIMER(i);
    }

    if(gba_sio.active)
    {
      CHECK_COUNT(gba_sio.cycles_remaining);
    }
    if(gba_link_open)
    {
      u32 link_cycles = link_cable_cycles_until_service(&gba_link,
       cpu_ticks);

      CHECK_COUNT(link_cycles);
    }

    // A DMA stalls the CPU: spend its cycles here without running code
    if (dma_cycle_count != 0)
    {
      CHECK_COUNT(dma_cycle_count);
      dma_cycle_count -= reg[EXECUTE_CYCLES];

      goto update_gba_loop;
    }

    if (irq_raised != IRQ_NONE)
    {
      pIO_REG(REG_IF) |= irq_raised;
      irq_raised = IRQ_NONE;
    }

    if ((pIO_REG(REG_IF) != 0) && GBA_IME_STATE && ARM_IRQ_STATE)
    {
      // Only serial, keypad and game pak IRQs wake the GBA from STOP
      u16 irq_mask = (reg[CPU_HALT_STATE] == CPU_STOP) ? 0x3080 : 0x3FFF;

      if ((pIO_REG(REG_IE) & pIO_REG(REG_IF) & irq_mask) != 0)
      {
        if (cpu_init_state != 0)
        {
          if (irq_ticks == 0)
          {
            cpu_interrupt();
            cpu_init_state = 0;
          }
        }
        else
        {
          if (reg[CPU_HALT_STATE] != CPU_ACTIVE)
          {
            cpu_interrupt();
          }
          else
          {
            // IRQ delay - Tsyncmax=3, Texc=3, Tirq=2, Tldm=20
            //             Tsyncmin=2
            irq_ticks = 9;
            cpu_init_state = 1;
          }
        }
      }
    }

    if (irq_ticks != 0)
    {
      CHECK_COUNT(irq_ticks);
      irq_ticks -= reg[EXECUTE_CYCLES];
    }
  }
  while (reg[CPU_HALT_STATE] != CPU_ACTIVE);

  return reg[EXECUTE_CYCLES];
}


/* PSP VBlank interrupt (about 60 Hz): the real-time clock for pacing. */
static void vblank_interrupt_handler(u32 sub, u32 *parg)
{
  real_frame_count++;
  vblank_count++;
}

/* Runs once per GBA frame, at the end of V-blank. Draws the on-screen
   status, decides whether the next frame is rendered (frameskip or the
   fast-forward policy), and waits for the PSP VBlank when emulation is
   ahead of real time. */
static void synchronize(void)
{
  static s32 fps = 60;
  static s32 frames_drawn = 60;

  u32 used_frameskip_type  = option_frameskip_type;
  u32 used_frameskip_value = option_frameskip_value;

  fps_window_frames++;

  if (fps_window_frames == 60)
  {
    // 60 GBA frames took vblank_count PSP frames at 60 Hz
    fps = vblank_count ? (3600 / vblank_count) : 0;
    frames_drawn = 60 - interval_skipped_frames;

    vblank_count = 0;
    fps_window_frames = 0;
    interval_skipped_frames = 0;
  }

  if (!skip_next_frame)
  {
    // GBA - sleep mode
    if (reg[CPU_HALT_STATE] == CPU_STOP)
    {
      clear_screen(0);
      print_string_gbk(MSG[MSG_GBA_SLEEP_MODE], X_POS_CENTER, 130, color_active_item, BG_NO_FILL);
    }

    // PSP controller - hold
    if (get_pad_input(PSP_CTRL_HOLD) != 0)
      print_string(FONT_KEY_ICON, 6, 258, color_batt_low, BG_NO_FILL);

    // fps
    if (psp_fps_debug != 0)
    {
      char print_buffer[24];

      if(option_screen_scale == SCALED_16X9_AUTO)
      {
        const char *quality =
         video_active_quality() == VIDEO_ADAPTIVE_QUALITY_SHARP ?
          "SHARP" : "CRISP";

        sprintf(print_buffer, "%02ld(%02ld) %s", (long)fps,
         (long)frames_drawn, quality);
      }
      else
      {
        sprintf(print_buffer, "%02ld(%02ld)", (long)fps,
         (long)frames_drawn);
      }
      print_string(print_buffer, 0, 0, COLOR15_WHITE, COLOR15_BLACK);
    }
  }

  if (main_fast_forward_active())
  {
    if (psp_fps_debug != 0)
    {
      if (option_language == LANGUAGE_JAPANESE)
        print_string(main_fast_forward_status(), 0, 12, COLOR15_WHITE,
         COLOR15_BLACK);
      else
        print_string_gbk(main_fast_forward_status(), 0, 12, COLOR15_WHITE,
         COLOR15_BLACK);
    }
    else
    {
      if (option_language == LANGUAGE_JAPANESE)
        print_string(main_fast_forward_status(), 0, 0, COLOR15_WHITE,
         COLOR15_BLACK);
      else
        print_string_gbk(main_fast_forward_status(), 0, 0, COLOR15_WHITE,
         COLOR15_BLACK);
    }
  }

  if(main_fast_forward_active())
  {
    FastForwardFrameDecision decision;
    u32 now = sceKernelGetSystemTimeLow();

    if(sound_take_fast_forward_fallback())
    {
      u64 fallback_count = fast_forward_policy.audio_fallback_count;

      fast_forward_policy_audio_fallback(&fast_forward_policy);
      if(fast_forward_policy.audio_fallback_count != fallback_count)
        perf_monitor_record_fast_forward_audio_fallback();
      main_fast_forward_update_sound();
    }
    decision = fast_forward_policy_frame(&fast_forward_policy, now);
    skip_next_frame = !decision.present;
    if(decision.delay_us != 0)
      sceKernelDelayThread(decision.delay_us);

    interval_skipped_frames += skip_next_frame;
    return;
  }

  // Fast-forward stopped or was reconfigured: restart pacing from now
  if(fast_forward_policy_take_force_present(&fast_forward_policy))
  {
    skip_next_frame = 0;
    real_frame_count = 0;
    virtual_frame_count = 0;
    num_skipped_frames = 0;
    frameskip_counter = 0;
  }

  skip_next_frame = 0;
  virtual_frame_count++;

  // Behind real time: auto frameskip skips up to option_frameskip_value
  // frames in a row, then resyncs. Ahead: wait for the PSP VBlank.
  if (real_frame_count >= virtual_frame_count)
  {
    if(gba_link_open)
      sceKernelDelayThread(MAIN_LINK_YIELD_US);

    if ((real_frame_count > virtual_frame_count) &&
        (used_frameskip_type == FRAMESKIP_AUTO) &&
        (num_skipped_frames < option_frameskip_value))
    {
      skip_next_frame = 1;
      num_skipped_frames++;
    }
    else
    {
      real_frame_count = 0;
      virtual_frame_count = 0;

      num_skipped_frames = 0;
    }
  }
  else
  {
    if (synchronize_flag != 0)
    {
      int measuring = perf_monitor_active();
      u32 vblank_start;

      if(measuring)
        vblank_start = sceKernelGetSystemTimeLow();

      main_link_wait_vblank();

      if(measuring)
        perf_monitor_record_wait(PERF_METRICS_WAIT_BACKPRESSURE,
         sceKernelGetSystemTimeLow() - vblank_start);

      real_frame_count = 0;
      virtual_frame_count = 0;
    }

    num_skipped_frames = 0;
  }

  if (used_frameskip_type == FRAMESKIP_MANUAL)
  {
    frameskip_counter = (frameskip_counter + 1) % (used_frameskip_value + 1);

    if (frameskip_counter != 0)
      skip_next_frame = 1;
  }

  interval_skipped_frames += skip_next_frame;

}


/* Resets the CPU and the scheduler state in this file (timers, DMA, LCD
   timing, IRQ delay) for reset_gba, and flushes translated code. */
static void init_main(void)
{
  s32 i;

  init_cpu();

  skip_next_frame = 0;

  vblank_count = 0;
  fps_window_frames = 0;
  interval_skipped_frames = 0;

  for (i = 0; i < 4; i++)
  {
    memset(&dma[i], 0, sizeof(DmaTransferType));

    dma[i].start_type = DMA_INACTIVE;
    dma[i].direct_sound_channel = DMA_NO_DIRECT_SOUND;

    memset(&timer[i], 0, sizeof(TimerType));

    timer[i].status = TIMER_INACTIVE;
    timer[i].reload = 0x10000;
    timer[i].direct_sound_channels = TIMER_DS_CHANNEL_NONE;
  }

  cpu_ticks = 0;

  // BIOS boot starts at the top of line 0; a direct boot starts mid-frame
  // (init_memory sets VCOUNT to 126)
  video_count = option_boot_mode ? 960 : 272;
  reg[EXECUTE_CYCLES] = video_count;
  video_dispatch_frame_begin();

  irq_ticks = 0;
  cpu_init_state = 0;

  if (!caches_inited)
  {
    flush_translation_cache(TRANSLATION_REGION_READONLY, FLUSH_REASON_INITIALIZING);
    flush_translation_cache(TRANSLATION_REGION_WRITABLE, FLUSH_REASON_INITIALIZING);
  }
  else
  {
    flush_translation_cache(TRANSLATION_REGION_READONLY, FLUSH_REASON_LOADING_ROM);
    clear_metadata_area(METADATA_AREA_EWRAM, CLEAR_REASON_LOADING_ROM);
    clear_metadata_area(METADATA_AREA_IWRAM, CLEAR_REASON_LOADING_ROM);
    clear_metadata_area(METADATA_AREA_VRAM,  CLEAR_REASON_LOADING_ROM);
  }

  caches_inited = 1;
}


/* dir.ini names the ROM, save, state and other folders; a missing or bad
   file is reported but not fatal. */
static void load_setting_cfg(void)
{
  char filename[MAX_FILE];

  sprintf(filename, "%sdir.ini", main_path);

  if (load_dir_cfg(filename) < 0)
  {
    sprintf(filename, MSG[MSG_ERR_LOAD_DIR_INI], main_path);
    error_msg(filename, CONFIRMATION_CONT);
  }
}

/* Loads gba_bios.bin from the emulator folder; the emulator cannot run
   without it, so a missing BIOS is reported and quits. */
static void load_bios_file(void)
{
  char filename[MAX_FILE];

  sprintf(filename, "%sgba_bios.bin", main_path);

  if (load_bios(filename) < 0)
  {
    error_msg(MSG[MSG_ERR_BIOS_NONE], CONFIRMATION_QUIT);
    quit();
  }
}

/* Find the emulator's home directory.
   Uses argv[0] (works for XMB, chain-load, PSPLink, any folder depth).
   Falls back to getcwd(). */
static void find_main_path(const char *eboot_path)
{
    main_path[0] = '\0';

    /* Strategy 1: argv[0] directory */
    if (eboot_path != NULL && eboot_path[0] != '\0' && strchr(eboot_path, '/') != NULL)
    {
        char temp[MAX_PATH];
        strncpy(temp, eboot_path, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        char *slash = strrchr(temp, '/');
        if (slash != NULL)
        {
            *(slash + 1) = '\0';
            strcpy(main_path, temp);
            return;
        }
    }

    /* Strategy 2: getcwd() — last resort */
    {
        char cwd[MAX_PATH];
        if (getcwd(cwd, MAX_PATH) != NULL && cwd[0] != '\0')
        {
            size_t len = strlen(cwd);
            if (len > 0 && cwd[len - 1] != '/')
                snprintf(main_path, sizeof(main_path), "%s/", cwd);
            else
                strcpy(main_path, cwd);
        }
    }
}

/* One-time startup: platform services, settings, BIOS, sound and the
   power callbacks. The power lock keeps the PSP from suspending meanwhile. */
static void setup_main(const char *eboot_path)
{
  FastForwardSettings fast_forward_settings;

  scePowerLock(0);

  fast_forward_settings_defaults(&fast_forward_settings);
  fast_forward_policy_init(&fast_forward_policy, &fast_forward_settings);

  initExceptionHandler();

  int devkit_version = sceKernelDevkitVersion();

  // Firmware 5.00 and later get the clock call by its newer NID
  if (devkit_version < 0x05000010)
    __scePowerSetClockFrequency = scePowerSetClockFrequency;
  else
    __scePowerSetClockFrequency = scePowerSetClockFrequency371;

  __draw_volume_status = draw_volume_status_null;

  init_input();
  perf_monitor_init();
  init_video(devkit_version);
  video_resolution_large();

  find_main_path(eboot_path);
  runtime_log_psp_init(main_path[0] != '\0' ? main_path : NULL);

  sceKernelRegisterSubIntrHandler(PSP_VBLANK_INT, 0, vblank_interrupt_handler, NULL);
  sceKernelEnableSubIntr(PSP_VBLANK_INT, 0);

  sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT, &date_format);

  /* Kernel-mode controls are disabled for PPSSPP compatibility. */
  enable_home_menu = 1;

  load_config_file();
  rebuild_fast_forward_button_mask();
  load_theme_config();
  load_recent_roms();

  // HOME opens the emulator menu, so hide the system HOME popup
  sceImposeSetHomePopup(enable_home_menu ^ 1);

  load_setting_cfg();
  load_bios_file();

  init_gamepak_buffer();

  sound_pause = 1;
  set_sound_volume();
  init_sound();

  setup_callbacks();

  scePowerUnlock(0);
}

/* True when path exists and is a regular file (not a directory). */
static int path_is_regular_file(const char *path)
{
  SceIoStat stats;

  if (sceIoGetstat(path, &stats) < 0)
    return 0;

  return FIO_S_ISREG(stats.st_mode) != 0;
}

/* Picks the ROM (a path argument, then roms/game.gba, then the browser or
   menu) and runs it in the dynarec, which never returns; else quits. */
int user_main(int argc, char *argv[])
{
  char load_filename[MAX_FILE];
  const char *file_ext[] = { ".zip", ".gba", ".bin", ".agb", ".gbz", NULL };
  u32 rom_loaded = 0;
  u32 prepared_by_menu = 0;

  setup_main((argc > 0 && argv[0] != NULL) ? argv[0] : NULL);

  gamepak_filename[0] = 0;
  gamepak_file_size = 0;

  /* Priority 1: launcher or file manager passed a ROM path */
  if (argc > 1)
  {
    if (load_gamepak((char *)argv[1]) < 0)
    {
      clear_screen(COLOR32_BLACK);
      error_msg(MSG[MSG_ERR_LOAD_GAMEPACK], CONFIRMATION_QUIT);
      quit();
    }
    /* Single-game bubble launch: do not update Recent ROMs */
    rom_loaded = 1;
  }
  /* Priority 2: pre-installed game.gba for single-game packages */
  else
  {
    /* Drop-in compatible with GrabowskiDev single-game installs: when
     * roms/game.gba exists, load it directly (no ROM browser). */
    snprintf(load_filename, sizeof(load_filename), "%sgame.gba", dir_roms);

    if (path_is_regular_file(load_filename))
    {
      if (load_gamepak(load_filename) < 0)
      {
        clear_screen(COLOR32_BLACK);
        error_msg(MSG[MSG_ERR_LOAD_GAMEPACK], CONFIRMATION_CONT);
        prepared_by_menu = rom_loaded = menu() != 0;
      }
      else
      {
        /* Fixed single-ROM package: do not update Recent ROMs */
        rom_loaded = 1;
      }
    }
    else if (load_file(file_ext, load_filename, dir_roms, 1) < 0)
    {
      prepared_by_menu = rom_loaded = menu() != 0;
    }
    else if (load_gamepak(load_filename) < 0)
    {
      clear_screen(COLOR32_BLACK);
      error_msg(MSG[MSG_ERR_LOAD_GAMEPACK], CONFIRMATION_CONT);
      prepared_by_menu = rom_loaded = menu() != 0;
    }
    else
    {
      add_recent_rom(load_filename);
      rom_loaded = 1;
    }
  }

  if (rom_loaded)
  {
    if (!prepared_by_menu)
    {
      reset_gba();
      maybe_load_auto_savestate();
    }

    sceImposeSetHomePopup(enable_home_menu ^ 1);

    set_cpu_clock(option_clock_speed);

    sceDisplayWaitVblankStart();
    video_resolution_small();

    sound_pause = 0;

    // We'll never actually return from here.
    execute_arm_translate(reg[EXECUTE_CYCLES]);
  }

  quit();
  return 0;
}

int main(int argc, char *argv[])
{
    user_main(argc, argv);
    return 0;
}


/* The one shutdown path: saves, closes the link, writes the logs and exits.
   The exit callback only requests it (see main_power_service); a second
   caller returns at once. */
void quit(void)
{
  if(!__sync_bool_compare_and_swap(&quit_started, 0, 1))
    return;

  main_fast_forward_stop();
  main_link_disconnect(MAIN_LINK_END_USER);

  update_backup_immediately();
  save_config_file();
  runtime_log_psp_main(RUNTIME_LOG_EVENT_SESSION_END, cpu_ticks, 0,
   (u32)gba_link.status, (u32)gba_link_end, 0);
  runtime_log_psp_flush(RUNTIME_LOG_FLUSH_EXIT);

  sound_term();
  memory_term();
  video_term();

  set_cpu_clock(PSP_CLOCK_222);

  sceKernelDisableSubIntr(PSP_VBLANK_INT, 0);
  sceKernelReleaseSubIntrHandler(PSP_VBLANK_INT, 0);

  scePowerUnregisterCallback(0);

  /* Lets a waiting exit callback return. */
  quit_cleanup_complete = 1;
  __sync_synchronize();
  sceKernelExitGame();
}

/* Resets the GBA with the loaded ROM. Ends fast-forward, benchmark capture,
   input traces and the link first; flushes the runtime log if the ROM
   changed. */
void reset_gba(void)
{
  u32 new_identity = 0;

  main_fast_forward_stop();
  perf_monitor_invalidate(sceKernelGetSystemTimeWide());
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
  input_diagnostic_trace_cancel();
#endif
  main_link_disconnect(MAIN_LINK_END_ROM_CHANGE);

  /* Cartridge header: 4-byte game code at 0xAC, software version at 0xBC. */
  if(gamepak_file_size >= 0xC0u)
  {
    new_identity = ((u32)gamepak_header[0xAC] << 24) |
     ((u32)gamepak_header[0xAD] << 16) |
     ((u32)gamepak_header[0xAE] << 8) | gamepak_header[0xAF];
  }
  if(runtime_log_rom_active && new_identity != runtime_log_rom_identity)
    runtime_log_psp_flush(RUNTIME_LOG_FLUSH_ROM_CHANGE);

  clear_texture(0x7FFF);
  init_memory();
  init_main();
  if(gamepak_file_size >= 0xC0u)
  {
    runtime_log_psp_set_rom(gamepak_header + 0xAC, gamepak_header[0xBC],
     gamepak_file_size, cpu_ticks);
    runtime_log_rom_identity = new_identity;
    runtime_log_rom_active = 1;
  }
  reset_sound();
}


/* ---- Power ------------------------------------------------------------
 * The power callback only records suspend and resume in psp_power_lifecycle.
 * The emulator thread acts on them here, from main_power_service. */

/* Writes the suspend auto-savestate once per suspend, if one was asked for. */
static void main_power_save_if_pending(void)
{
  if(power_lifecycle_take_save(&psp_power_lifecycle) &&
     !sleep_auto_saved && gamepak_filename[0] != '\0')
  {
    auto_savestate_sleep();
    sleep_auto_saved = 1;
  }
}

/* File handles do not survive suspend. The Memory Stick can take a moment
   to return after resume, so retry for about 2.5 s before giving up. */
static void main_power_reopen_gamepak(void)
{
  s32 i;

  if(!sleep_reopen_gamepak)
    return;

  for(i = 0; i < 5; i++)
  {
    FILE_OPEN(gamepak_file_large, gamepak_filename_raw, READ);

    if(FILE_CHECK_VALID(gamepak_file_large))
    {
      sleep_reopen_gamepak = 0;
      return;
    }

    sceKernelDelayThread(500 * 1000);
  }

  clear_screen(COLOR32_BLACK);
  error_msg(MSG[MSG_ERR_OPEN_GAMEPACK], CONFIRMATION_QUIT);
  quit();
}

/* Suspend is under way: stop fast-forward, video and the link, auto-save,
   close the ROM file and flush the logs, then park until the PSP resumes. */
static void psp_sleep_loop(void)
{
  runtime_log_psp_main(RUNTIME_LOG_EVENT_POWER_SUSPEND, cpu_ticks, 0,
   (u32)gba_link.status, main_adhoc_state_value(), sleep_flag);
  main_fast_forward_stop();
  perf_monitor_invalidate(sceKernelGetSystemTimeWide());
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
  input_diagnostic_trace_cancel();
#endif
  video_power_suspend();

  main_link_disconnect(MAIN_LINK_END_SUSPEND);

  main_power_save_if_pending();

  if (FILE_CHECK_VALID(gamepak_file_large))
  {
    FILE_CLOSE(gamepak_file_large);
    gamepak_file_large = GAMEPAK_FILE_NONE;
    sleep_reopen_gamepak = 1;
  }

  runtime_log_psp_flush(RUNTIME_LOG_FLUSH_SUSPEND);

  do
  {
    sceKernelDelayThread(100 * 1000);
  }
  while (sleep_flag != 0);
}

/* Polled by the emulator thread from update_gba, menus and link waits.
   Quits on an exit request, sleeps through a suspend, and after resume
   reopens the ROM and restores video and audio. */
void main_power_service(void)
{
  PowerLifecycleState state;
  int audio_result;

  if(exit_requested && !quit_started)
    quit();

  state = power_lifecycle_state(&psp_power_lifecycle);
  if(sleep_flag != 0 || state == POWER_LIFECYCLE_SUSPENDED)
    psp_sleep_loop();

  state = power_lifecycle_state(&psp_power_lifecycle);
  if(state != POWER_LIFECYCLE_RUNNING)
    main_link_disconnect(MAIN_LINK_END_SUSPEND);

  main_power_save_if_pending();

  if(!power_lifecycle_take_resume(&psp_power_lifecycle, &audio_result))
    return;

  if(FILE_CHECK_VALID(gamepak_file_large) && !sleep_reopen_gamepak)
  {
    FILE_CLOSE(gamepak_file_large);
    gamepak_file_large = GAMEPAK_FILE_NONE;
    sleep_reopen_gamepak = 1;
  }

  main_power_reopen_gamepak();
  video_power_resume();
  sound_power_resume_finish(cpu_ticks, audio_result);
  sleep_auto_saved = 0;
  runtime_log_psp_main(RUNTIME_LOG_EVENT_POWER_RESUME, cpu_ticks,
   (u8)(audio_result >= 0), (u32)audio_result,
   (u32)gba_link.status, main_adhoc_state_value());
}


/* Runs on the callback thread when the user picks Exit in the HOME menu.
   It asks the main thread to shut down and waits up to 2 s for that,
   then exits regardless. */
static int exit_callback(int arg1, int arg2, void *common)
{
  u64 started;

  (void)arg1;
  (void)arg2;
  (void)common;

  exit_requested = 1;
  __sync_synchronize();
  started = sceKernelGetSystemTimeWide();
  while(!quit_cleanup_complete &&
        sceKernelGetSystemTimeWide() - started <
         MAIN_EXIT_CALLBACK_TIMEOUT_US)
    sceKernelDelayThread(10000u);

  if(!quit_cleanup_complete)
    sceKernelExitGame();

  return 0;
}

/* Runs on the callback thread. It must not do slow or file work, so it only
   releases or re-reserves the audio channel and records the event for
   main_power_service. */
static int power_callback(int unknown, int powerInfo, void *arg)
{
  if ((powerInfo & PSP_POWER_CB_SUSPENDING) != 0)
  {
    sound_power_suspend();
    power_lifecycle_suspend(&psp_power_lifecycle,
     gamepak_filename[0] != '\0');
    sleep_flag = 1;
  }
  if ((powerInfo & PSP_POWER_CB_RESUME_COMPLETE) != 0)
  {
    if(power_lifecycle_state(&psp_power_lifecycle) ==
       POWER_LIFECYCLE_SUSPENDED)
    {
      int audio_result = sound_power_resume();

      power_lifecycle_resume(&psp_power_lifecycle, audio_result);
    }
    sleep_flag = 0;
  }

  return 0;
}

/* Hosts the exit and power callbacks: it sleeps in a callback-enabled wait
   so the system can run them. */
static int callback_thread(SceSize args, void *argp)
{
  SceUID cbid;

  cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
  sceKernelRegisterExitCallback(cbid);

  cbid = sceKernelCreateCallback("Power Callback", power_callback, NULL);
  scePowerRegisterCallback(0, cbid);

  sceKernelSleepThreadCB();

  return 0;
}

/* Starts the thread that receives the exit and power callbacks; quits if
   it cannot be created. */
static void setup_callbacks(void)
{
  SceUID thid = -1;

  // Priority 0x11 runs well above the emulator thread (32)
  thid = sceKernelCreateThread("Update Thread",
                               callback_thread,
                               0x11,
                               0xFA0,
                               0,
                               NULL);
  if (thid < 0)
  {
    error_msg(MSG[MSG_ERR_START_CALLBACK_THREAD], CONFIRMATION_QUIT);
    quit();
  }

  power_lifecycle_init(&psp_power_lifecycle);
  sleep_flag = 0;
  sleep_auto_saved = 0;
  sceKernelStartThread(thid, 0, 0);
}


/* Current RTC tick count (microseconds on the PSP). */
u64 ticker(void)
{
  u64 current_ticks;

  sceRtcGetCurrentTick(&current_ticks);

  return current_ticks;
}

/* The configured CPU clock in MHz, for the benchmark report. */
static u32 main_perf_clock_mhz(void)
{
  switch(option_clock_speed)
  {
    case PSP_CLOCK_333:
      return 333;
    case PSP_CLOCK_300:
      return 300;
    case PSP_CLOCK_266:
      return 266;
    default:
      return 222;
  }
}

/* ---- Benchmark capture ------------------------------------------------
 * The diagnostics menu starts and stops a capture; perf_monitor.c collects
 * it and perf_report.c formats the report written here. */

/* Starts a capture and pins the settings it runs under for the report. */
int main_perf_start(void)
{
  FastForwardSettings fast_forward_settings;
  PerfMonitorIdentity identity;

  if(!perf_monitor_start(sceKernelGetSystemTimeWide()))
    return 0;

  fast_forward_settings = main_fast_forward_settings();
  memset(&identity, 0, sizeof(identity));
  identity.clock_mhz = main_perf_clock_mhz();
  identity.psp_vsync = option_psp_vsync;
  identity.renderer = option_video_renderer;
  identity.scale = option_screen_scale;
  identity.filter = option_screen_filter;
  identity.frameskip_type = option_frameskip_type;
  identity.frameskip_value = option_frameskip_value;
  identity.sound_volume = option_sound_volume;
  identity.fast_forward_game_override = option_fast_forward_game_override;
  identity.fast_forward_profile = fast_forward_settings.profile;
  identity.fast_forward_activation = fast_forward_settings.activation;
  identity.fast_forward_speed = fast_forward_settings.speed;
  identity.fast_forward_presentation = fast_forward_settings.presentation;
  identity.fast_forward_audio = fast_forward_settings.audio;
  identity.gamepak_file_size = gamepak_file_size;
  identity.gamepak_ram_buffer_size = gamepak_ram_buffer_size;
  identity.gamepak_ram_pages = gamepak_ram_pages;
  /* Cartridge header: title at 0xA0, game code at 0xAC, version at 0xBC. */
  if(gamepak_file_size >= 0xC0U)
  {
    memcpy(identity.rom_title, gamepak_header + 0xA0,
     sizeof(identity.rom_title));
    memcpy(identity.game_code, gamepak_header + 0xAC,
     sizeof(identity.game_code));
    identity.rom_revision = gamepak_header[0xBC];
  }
  perf_monitor_set_identity(&identity);

  perf_monitor_observe_memory_available(sceKernelTotalFreeMemSize());
  video_reset_frame_timing();
  return 1;
}

int main_perf_stop(void)
{
  if(!perf_monitor_active())
    return 0;

  perf_monitor_observe_memory_available(sceKernelTotalFreeMemSize());
  return perf_monitor_stop(sceKernelGetSystemTimeWide());
}

void main_perf_snapshot(PerfMetricsSnapshot *snapshot)
{
  perf_monitor_snapshot(snapshot);
}

/* Writes all length bytes, continuing after short writes; 0 if a write
   fails. */
static int main_perf_write_all(SceUID file, const char *data, u32 length)
{
  u32 written = 0;

  while(written < length)
  {
    s32 result = FILE_WRITE(file, data + written, length - written);

    if(result <= 0)
      return 0;
    written += (u32)result;
  }

  return 1;
}

/* Writes a finished capture to benchmarks/perf-<local time>.txt beside the
   EBOOT and returns its path. 0 on success, -1 on failure or while idle or
   still recording. */
int main_perf_write_report(char *output_path, size_t output_path_size)
{
  PerfMetricsSnapshot metrics;
  PerfReportMetadata metadata;
  PerfMonitorIdentity identity;
  ScePspDateTime date = { 0 };
  SceIoStat directory_stat;
  char directory[MAX_PATH];
  char filename[MAX_PATH];
  char report[PERF_REPORT_CAPACITY];
  SceUID file;
  int formatted;
  int report_length;
  int model;

  perf_monitor_snapshot(&metrics);
  if(metrics.state == PERF_METRICS_IDLE ||
     metrics.state == PERF_METRICS_ACTIVE)
    return -1;

  perf_monitor_identity(&identity);
  memset(&metadata, 0, sizeof(metadata));
  metadata.emulator_version = GBA_RUSH_VERSION;
  model = kuKernelGetModel();
  metadata.psp_model = model < 0 ? UINT32_MAX : (u32)model;
  metadata.clock_mhz = identity.clock_mhz;
/* _PSP_FW_VERSION is the compile-time SDK macro, not the device firmware. */
#ifdef _PSP_FW_VERSION
  metadata.psp_sdk_fw_macro = _PSP_FW_VERSION;
#else
  metadata.psp_sdk_fw_macro = 0;
#endif
  metadata.psp_vsync = identity.psp_vsync;
  metadata.renderer = identity.renderer;
  metadata.scale = identity.scale;
  metadata.filter = identity.filter;
  metadata.frameskip_type = identity.frameskip_type;
  metadata.frameskip_value = identity.frameskip_value;
  metadata.sound_volume = identity.sound_volume;
  metadata.audio_sample_rate = SOUND_FREQUENCY;
  metadata.audio_samples = SOUND_SAMPLES;
  metadata.fast_forward_game_override = identity.fast_forward_game_override;
  metadata.fast_forward_profile = identity.fast_forward_profile;
  metadata.fast_forward_activation = identity.fast_forward_activation;
  metadata.fast_forward_speed = identity.fast_forward_speed;
  metadata.fast_forward_presentation = identity.fast_forward_presentation;
  metadata.fast_forward_audio = identity.fast_forward_audio;
  metadata.gamepak_file_size = identity.gamepak_file_size;
  metadata.gamepak_ram_buffer_size = identity.gamepak_ram_buffer_size;
  metadata.gamepak_ram_pages = identity.gamepak_ram_pages;
  if(identity.gamepak_file_size >= 0xC0U)
  {
    metadata.rom_title = identity.rom_title;
    metadata.rom_title_length = 12;
    metadata.game_code = identity.game_code;
    metadata.game_code_length = 4;
    metadata.rom_revision = identity.rom_revision;
  }
  metadata.duration_us = perf_monitor_duration_us();

  report_length = perf_report_format(report, sizeof(report), &metadata,
   &metrics);
  if(report_length < 0)
    return -1;

  formatted = snprintf(directory, sizeof(directory), "%sbenchmarks",
   main_path);
  if(formatted < 0 || (size_t)formatted >= sizeof(directory))
    return -1;

  if(sceIoGetstat(directory, &directory_stat) < 0)
  {
    if(sceIoMkdir(directory, 0777) < 0)
      return -1;
  }
  else if(!FIO_S_ISDIR(directory_stat.st_mode))
  {
    return -1;
  }

  sceRtcGetCurrentClockLocalTime(&date);
  formatted = snprintf(filename, sizeof(filename),
   "%s/perf-%04u%02u%02u-%02u%02u%02u-%06u.txt", directory,
   date.year, date.month, date.day, date.hour, date.minute, date.second,
   date.microsecond);
  if(formatted < 0 || (size_t)formatted >= sizeof(filename))
    return -1;

  FILE_OPEN(file, filename, WRITE);
  if(!FILE_CHECK_VALID(file))
    return -1;

  if(!main_perf_write_all(file, report, (u32)report_length))
  {
    FILE_CLOSE(file);
    sceIoRemove(filename);
    return -1;
  }
  FILE_CLOSE(file);

  if(output_path != NULL && output_path_size != 0)
  {
    strncpy(output_path, filename, output_path_size - 1U);
    output_path[output_path_size - 1U] = '\0';
  }
  return 0;
}

/* Size in bytes; 0 when the file is missing. */
u32 file_length(char *filename)
{
  SceIoStat stats;

  if (sceIoGetstat(filename, &stats) < 0)
    return 0;

  return (u32)stats.st_size;
}

void change_ext(char *src, char *buffer, const char *extension)
{
  char *dot_position;

  strcpy(buffer, src);
  dot_position = strrchr(buffer, '.');

  if (dot_position)
  {
    strcpy(dot_position, extension);
  }
}

/* Shows text in a popup until any button is pressed. confirm only adds
   the continue or quit hint; callers do the quitting themselves. */
void error_msg(const char *text, u8 confirm)
{
  char text_buff[512];
  GUI_ACTION_TYPE gui_action = CURSOR_NONE;
  const int popup_w = 440;
  const int popup_h = 120;
  const int popup_x = (PSP_SCREEN_WIDTH - popup_w) / 2;
  const int popup_y = (PSP_SCREEN_HEIGHT - popup_h) / 2;

  switch (confirm)
  {
    case CONFIRMATION_NONE:
      strcpy(text_buff, text);
      break;

    case CONFIRMATION_CONT:
      sprintf(text_buff, "%s\n\n%s", text, MSG[MSG_ERR_CONT]);
      break;

    case CONFIRMATION_QUIT:
      sprintf(text_buff, "%s\n\n%s", text, MSG[MSG_ERR_QUIT]);
      break;
  }

  draw_popup_frame_auto(popup_x, popup_y, popup_w, popup_h);
  print_swap_aware(text_buff, popup_x + 12, popup_y + 12, color_active_item, BG_NO_FILL);
  flip_screen(1);

  while (gui_action == CURSOR_NONE)
  {
    gui_action = get_gui_input();
  }
}

u32 yesno_dialog(const char *text)
{
  GUI_ACTION_TYPE gui_action = CURSOR_NONE;
  const int popup_w = 336;
  const int popup_h = 80;
  const int popup_x = (PSP_SCREEN_WIDTH - popup_w) / 2;
  const int popup_y = (PSP_SCREEN_HEIGHT - popup_h) / 2;

  // Wait for release, so the press that opened the dialog cannot answer it
  while (get_pad_input(PSP_CTRL_CROSS | PSP_CTRL_CIRCLE) != 0)
  {
    main_power_service();
    main_link_pump();
    sceKernelDelayThread(5000);
  }

  draw_popup_frame_auto(popup_x, popup_y, popup_w, popup_h);

  print_swap_aware(text, X_POS_CENTER, popup_y + 16,
   color_active_item, BG_NO_FILL);
  print_swap_aware(MSG[MSG_YES_NO], X_POS_CENTER, popup_y + 46, color_active_item, BG_NO_FILL);

  flip_screen(1);

  while ((gui_action != CURSOR_SELECT) && (gui_action != CURSOR_EXIT))
  {
    gui_action = get_gui_input();
    sceKernelDelayThread(15000);
  }

  return (gui_action == CURSOR_SELECT) ? 0 : 1;
}


/* Sets PLL, CPU and bus clocks in MHz; the bus runs at half the CPU. */
int set_cpu_clock(u32 psp_clock)
{
  int ret = -1;

  if (__scePowerSetClockFrequency != NULL)
  {
    switch (psp_clock)
    {
      case PSP_CLOCK_333:
        ret = (*__scePowerSetClockFrequency)(333, 333, 166);
        break;

      case PSP_CLOCK_300:
        ret = (*__scePowerSetClockFrequency)(300, 300, 150);
        break;

      case PSP_CLOCK_266:
        ret = (*__scePowerSetClockFrequency)(266, 266, 133);
        break;

      default:
      case PSP_CLOCK_222:
        ret = (*__scePowerSetClockFrequency)(222, 222, 111);
        break;
    }
  }

  return ret;
}


/* Minimal fopen for the FILE_OPEN macros: "r" opens read-only, "w" creates
   or truncates. Returns a negative id on failure. */
SceUID psp_fopen(const char *filename, const char *mode)
{
  SceUID tag;

  if ((*mode != 'r') && (*mode != 'w'))
    return -1;

  tag = sceIoOpen(filename, PSP_O_RDONLY, 0777);

  if (*mode == 'w')
  {
    int flags;

    if (tag < 0)
    {
      flags = PSP_O_WRONLY | PSP_O_CREAT;
    }
    else
    {
      sceIoClose(tag);
      flags = PSP_O_WRONLY | PSP_O_TRUNC;
    }

    tag = sceIoOpen(filename, flags, 0777);
  }

  return tag;
}

/* FILE_CLOSE's close; negative (invalid) ids are ignored. The -1 store
   only clears this local copy, not the caller's handle. */
void psp_fclose(SceUID filename_tag)
{
  if (filename_tag < 0)
    return;

  sceIoClose(filename_tag);
  filename_tag = -1;
}


/* MEM_ALIGN-aligned allocation that reports the error and quits instead of
   returning NULL. */
void *safe_malloc(size_t size)
{
  void *p;

  if ((p = memalign(MEM_ALIGN, size)) == NULL)
  {
    clear_screen(COLOR32_BLACK);
    error_msg(MSG[MSG_ERR_MALLOC], CONFIRMATION_QUIT);
    quit();
  }

  return p;
}


// type = READ / WRITE_MEM
#define MAIN_SAVESTATE_BODY(type)                                             \
{                                                                             \
  FILE_##type##_VARIABLE(savestate_file, cpu_ticks);                          \
  FILE_##type##_VARIABLE(savestate_file, video_count);                        \
  FILE_##type##_VARIABLE(savestate_file, irq_ticks);                          \
  FILE_##type##_VARIABLE(savestate_file, cpu_init_state);                     \
  FILE_##type##_ARRAY(savestate_file, timer);                                 \
}                                                                             \

void main_read_savestate(SceUID savestate_file)
{
  MAIN_SAVESTATE_BODY(READ);
}

void main_write_mem_savestate(SceUID savestate_file)
{
  MAIN_SAVESTATE_BODY(WRITE_MEM);
}
