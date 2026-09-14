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

/* Forward declarations for recent ROMs functions (defined in gui.c) */
extern void load_recent_roms(void);
extern void add_recent_rom(const char *filename);



// Main Thread Params
#define PRIORITY       (32)
#define STACK_SIZE_KB  (512)
#define ATTRIBUTE      (THREAD_ATTR_USER | PSP_THREAD_ATTR_CLEAR_STACK)

PSP_MODULE_INFO("GBA Rush PSP", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_PARAMS(PRIORITY, STACK_SIZE_KB, ATTRIBUTE);
PSP_HEAP_THRESHOLD_SIZE_KB(1024);
PSP_HEAP_SIZE_KB(-2048);


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

volatile u32 sleep_flag = 0;
volatile u32 sleep_auto_saved = 0;

u32 synchronize_flag = 1;
u32 psp_fps_debug = 0;

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

typedef struct
{
  TIMER_DS_CHANNEL_TYPE direct_sound_channels;
  TIMER_IRQ_TYPE irq;
  TIMER_STATUS_TYPE status;
  FIXED08_24 frequency_step;
  s32 count;
  u32 reload;
  u32 reload_update;
  u32 prescale;
} TimerType;

const u8 ALIGN_DATA prescale_table[] = { 0, 6, 8, 10 };
TimerType ALIGN_DATA timer[4];

u32 dma_cycle_count = 0;
LinkSession gba_link_session;
static u8 gba_link_gameplay_active = 1;
static PowerLifecycle psp_power_lifecycle;
static FastForwardPolicy fast_forward_policy;
static u32 sleep_reopen_gamepak = 0;
static u8 gba_link_terminal_logged = 0;
static u8 runtime_log_rom_active = 0;
static u32 runtime_log_rom_identity = 0;
static u32 gba_link_attempt_id = 0;
static u32 gba_link_audio_underrun_base = 0;
static PspAdhocFailureSnapshot gba_link_benchmark_base;
static u32 gba_link_benchmark_run_id = 0;
static u32 gba_link_benchmark_logged_run_id = 0;
static u32 gba_link_benchmark_audio_base = 0;
static u64 gba_link_benchmark_barrier_base = 0;
static u64 gba_link_benchmark_last_pump_us = 0;
static u32 gba_link_benchmark_starvation_max_us = 0;
static u32 gba_link_wait_count = 0;
static u32 gba_link_wait_total_us = 0;
static u32 gba_link_wait_max_us = 0;
static u32 gba_link_wait_latest_lead = 0;
static u8 gba_link_wait_flags = 0;
static u64 gba_link_wait_window_started = 0;
static u32 gba_link_transfer_started = 0;
static u32 gba_link_transfer_committed = 0;
static u32 gba_link_transfer_aborted = 0;
static u32 gba_link_serial_irqs = 0;
static u8 gba_link_recovery_checkpointed = 0;
static volatile int quit_started = 0;
static volatile int exit_requested = 0;
static volatile int quit_cleanup_complete = 0;

#define MAIN_EXIT_CALLBACK_TIMEOUT_US 2000000u
#define MAIN_LINK_BENCHMARK_SERVICE_BUDGET_US 1000u
#define MAIN_LINK_BENCHMARK_DRAIN_TIMEOUT_US 250000u
#define MAIN_LINK_BENCHMARK_ROM_ID 0x42454E43u

u32 cpu_ticks = 0;
s32 video_count = 272;

u32 irq_ticks = 0;
u8 cpu_init_state = 0;

u32 skip_next_frame = 0;
u32 num_skipped_frames = 0;
u32 frameskip_counter = 0;
u32 interval_skipped_frames = 0;
u32 frames = 0;
u32 vblank_count = 0;

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
static void main_link_publish_sio(void);
static void main_link_handle_closed(void);
static void main_link_flush_wait_log(void);

#define scePowerSetClockFrequency371  scePower_EBD177D6
static int (*__scePowerSetClockFrequency)(int pllfreq, int cpufreq, int busfreq);


#define SOUND_UPDATE_FREQUENCY_STEP                                           \
  sound_math_timer_step(timer_reload, SOUND_FREQUENCY)                        \

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


void timer_control_low(u8 timer_number, u32 value)
{
  TimerType *tm = timer + timer_number;

  tm->reload = 0x10000 - value;
  tm->reload_update = 1;
}

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

      pIO_REG(REG_TM0D + (timer_number << 1)) = 0x10000 - timer_reload;

      timer_reload <<= tm->prescale;
      tm->count = timer_reload;

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

void direct_sound_timer_select(u32 value)
{
  TIMER_DS_CHANNEL_TYPE timer_select;

  timer_select = ((value >> 13) & 0x02) | ((value >> 10) & 0x01);

  timer[0].direct_sound_channels = timer_select ^ 0x03;
  timer[1].direct_sound_channels = timer_select;
}


static void cpu_interrupt(void)
{
  // Interrupt handler in BIOS
  reg_mode[MODE_IRQ][6] = reg[REG_PC] + 4;
  spsr[MODE_IRQ] = reg[REG_CPSR];
  reg[REG_CPSR] = (reg[REG_CPSR] & ~0xFF) | 0x92; // set mode IRQ & disable IRQ
  reg[REG_PC] = 0x00000018;
  set_cpu_mode(MODE_IRQ);

  bios_read_protect = 0xe55ec002;

  reg[CPU_HALT_STATE] = CPU_ACTIVE;
  reg[CHANGED_PC_STATUS] = 1;
}

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

static u32 main_link_age_ms(u64 now, u64 then)
{
  u64 age;

  if(then == 0 || now < then)
    return 0;
  age = (now - then) / 1000u;
  return age > UINT_MAX ? UINT_MAX : (u32)age;
}

static u32 main_link_bounded_u64(u64 value)
{
  return value > UINT_MAX ? UINT_MAX : (u32)value;
}

static u32 main_link_audio_underruns(void)
{
  u32 current = sound_audio_underrun_count();

  return current >= gba_link_audio_underrun_base ?
   current - gba_link_audio_underrun_base : current;
}

static u32 main_link_counter_delta(u32 current, u32 base)
{
  return current >= base ? current - base : current;
}

static void main_link_benchmark_observe(u64 now)
{
  LinkBenchmarkSummary summary;

  /* Run identity is cheap to observe; do not hash a summary on every pump. */
  if(gba_link_session.benchmark.control.run_id == 0 ||
     gba_link_session.benchmark.control.run_id == gba_link_benchmark_run_id)
    return;
  link_session_benchmark_summary(&gba_link_session, &summary);
  gba_link_benchmark_run_id = summary.run_id;
  gba_link_benchmark_logged_run_id = 0;
  gba_link_benchmark_audio_base = sound_audio_underrun_count();
  gba_link_benchmark_barrier_base = gba_link_session.barrier_max_us;
  gba_link_benchmark_last_pump_us = now;
  gba_link_benchmark_starvation_max_us = 0;
  psp_adhoc_get_failure_snapshot(&gba_link_benchmark_base);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 0x80u,
   summary.run_id, gba_link_session.benchmark.control.duration_ms,
   gba_link_session.benchmark.control.target_words_per_second);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 0x81u,
   summary.run_id, gba_link_session.benchmark.control.seed, summary.role);
}

static void main_log_link_benchmark(int wait_for_drain)
{
  LinkBenchmarkSummary summary;
  PspAdhocFailureSnapshot snapshot;
  u32 recoveries;
  u32 barrier_max;
  u32 audio_underruns;

  if(!link_benchmark_terminal(&gba_link_session.benchmark) ||
     gba_link_session.benchmark.control.run_id == 0 ||
     gba_link_benchmark_logged_run_id ==
      gba_link_session.benchmark.control.run_id)
    return;
  link_session_benchmark_summary(&gba_link_session, &summary);
  psp_adhoc_get_failure_snapshot(&snapshot);
  /* STOP/ACK may still be draining when the semantic result becomes final.
     A single outstanding final ACK is not persistent queue growth. */
  if(wait_for_drain && link_session_is_connected(&gba_link_session) &&
     (snapshot.outstanding_packets != 0 || snapshot.tx_queue_depth != 0) &&
     sceKernelGetSystemTimeWide() - gba_link_session.benchmark.finished_us <
      MAIN_LINK_BENCHMARK_DRAIN_TIMEOUT_US)
    return;
  recoveries = main_link_counter_delta(snapshot.transport_resyncs,
    gba_link_benchmark_base.transport_resyncs) +
   main_link_counter_delta(snapshot.group_recoveries,
    gba_link_benchmark_base.group_recoveries) +
   main_link_counter_delta(snapshot.stack_recoveries,
    gba_link_benchmark_base.stack_recoveries);
  barrier_max = gba_link_session.barrier_max_us >
    gba_link_benchmark_barrier_base ?
   main_link_bounded_u64(gba_link_session.barrier_max_us) : 0u;
  audio_underruns = main_link_counter_delta(sound_audio_underrun_count(),
   gba_link_benchmark_audio_base);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 0,
   summary.run_id, summary.status, summary.failure);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 1,
   summary.run_id, summary.duration_ms, summary.completed_words);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 2,
   summary.run_id, summary.words_per_second, summary.digest);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 3,
   summary.run_id, summary.rtt_p50_us, summary.rtt_p95_us);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 4,
   summary.run_id, summary.rtt_p99_us, summary.burst_p99_us);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 5,
   summary.run_id, summary.burst_max_us, summary.checksum);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 6,
   summary.run_id, summary.duplicates, summary.replays);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 7,
   summary.run_id, summary.stale_generations, summary.timeout_count);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 8,
   summary.run_id,
   main_link_counter_delta(snapshot.retransmissions,
    gba_link_benchmark_base.retransmissions),
   main_link_counter_delta(snapshot.ack_packets,
    gba_link_benchmark_base.ack_packets));
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 9,
   summary.run_id, snapshot.tx_queue_high_water,
   snapshot.hot_wake_latency_max_us);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 10,
   summary.run_id, barrier_max, audio_underruns);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 11,
   summary.run_id, recoveries, gba_link_benchmark_starvation_max_us);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 12,
   summary.run_id, summary.role, gba_link_attempt_id);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_BENCHMARK, cpu_ticks, 13,
   summary.run_id, snapshot.outstanding_packets,
   snapshot.tx_queue_depth);
  gba_link_benchmark_logged_run_id = summary.run_id;
  runtime_log_psp_flush(RUNTIME_LOG_FLUSH_LINK_CHECKPOINT);
}

static void main_log_link_checkpoint(void)
{
  PspAdhocFailureSnapshot snapshot;
  PspAdhocPhase failure_phase;
  u64 now = sceKernelGetSystemTimeWide();
  u32 main_dropped;
  u32 network_dropped;
  u32 critical_dropped;
  u32 packed;

  psp_adhoc_get_failure_snapshot(&snapshot);
  failure_phase = psp_adhoc_failure_phase();
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 0,
   (u32)gba_link_session.state, (u32)gba_link_session.reason,
   gba_link_session.reason_detail);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 1,
   (u32)gba_link_session.role, gba_link_session.negotiated_capabilities,
   gba_link_session.invariant_violations);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 2,
   gba_link_session.last_sequence, gba_link_session.active_sequence,
   gba_link_session.applied_sequence);
  packed = (u32)gba_link_session.transaction_stage |
   ((u32)(gba_link_session.transaction_applied != 0) << 8) |
   ((u32)(gba_link_session.deferred_prepare_valid != 0) << 9);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 3,
   gba_link_session.transaction_sequence,
   gba_link_session.transaction_hash, packed);
  packed = (u32)gba_link_session.local_sio_latest.mode |
   ((u32)gba_link_session.local_sio_latest.player_id << 8) |
   ((u32)gba_link_session.local_sio_latest.player_count << 16) |
   ((u32)gba_link_session.local_sio_latest.flags << 24);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 4,
   packed, gba_link_session.local_sio_latest.control |
    ((u32)(gba_link_session.local_sio_valid != 0) << 31),
   gba_link_session.local_sio_latest.rcnt);
  packed = (u32)gba_link_session.peer_sio.mode |
   ((u32)gba_link_session.peer_sio.player_id << 8) |
   ((u32)gba_link_session.peer_sio.player_count << 16) |
   ((u32)gba_link_session.peer_sio.flags << 24);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 5,
   packed, gba_link_session.peer_sio.control |
    ((u32)(gba_link_session.peer_sio_valid != 0) << 31),
   gba_link_session.peer_sio.rcnt);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 6,
   (u32)psp_adhoc_last_error(), (u32)failure_phase,
   (u32)snapshot.transition_phase);
  packed = (snapshot.outstanding_packets > 0xFFFFu ? 0xFFFFu :
   snapshot.outstanding_packets) << 16;
  packed |= snapshot.buffered_packets > 0xFFFFu ? 0xFFFFu :
   snapshot.buffered_packets;
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 7,
   snapshot.transport_epoch, snapshot.last_received_sequence, packed);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 8,
   main_link_age_ms(now, gba_link_session.state_started_us),
   main_link_age_ms(now, gba_link_session.barrier_started_us),
   main_link_age_ms(now, gba_link_session.last_rx_us));
  packed = (u32)gba_sio.mode |
   ((u32)gba_sio.player_id << 8) |
   ((u32)gba_sio.player_count << 16) |
   ((u32)(gba_sio.peer_active != 0) << 24) |
   ((u32)(gba_sio.pending_start != 0) << 25) |
   ((u32)(gba_sio.waiting != 0) << 26) |
   ((u32)(gba_sio.active != 0) << 27) |
   ((u32)(gba_sio.decision_locked != 0) << 28);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 9,
   packed, pIO_REG(REG_SIOCNT), pIO_REG(REG_RCNT));
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 10,
   gba_link_session.session_id, gba_link_session.journal.epoch,
   gba_link_session.sio_revision);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 11,
   gba_link_session.clock.local_cycle,
   gba_link_session.clock.peer_cycle, gba_link_session.clock.generation);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 12,
   snapshot.queue_sojourn_high_water_us, snapshot.rtt_estimate_us,
   snapshot.probes);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 13,
   snapshot.shutdown_phase, snapshot.cancellation_token,
   (u32)psp_adhoc_last_error());
  runtime_log_psp_get_drop_counts(&main_dropped, &network_dropped,
   &critical_dropped);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 14,
   main_dropped, network_dropped, critical_dropped);
  packed = (u32)gba_link_session.transport_health |
   (main_link_age_ms(now, gba_link_session.transport_pause_started_us) << 8);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 15,
   (u32)gba_link_session.last_transfer_reason,
   gba_link_session.last_transfer_detail, packed);
  packed = (u32)gba_link_session.journal.role |
   ((u32)gba_link_session.journal.stage << 8) |
   ((u32)(gba_link_session.journal.applied != 0) << 16) |
   ((u32)(gba_link_session.journal.irq_delivered != 0) << 17) |
   ((u32)(gba_link_session.journal.release_shared != 0) << 18) |
   ((u32)(gba_link_session.journal_valid != 0) << 19);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 16,
   gba_link_session.journal.checkpoint,
   gba_link_session.journal.checkpoint_hash, packed);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_CHECKPOINT, cpu_ticks, 17,
   gba_link_session.journal.active_sequence,
   gba_link_session.journal.transfer_hash,
   gba_link_session.journal.epoch);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_FAILURE, cpu_ticks, 0,
   gba_link_attempt_id, snapshot.error_domain,
   (u32)snapshot.internal_error_code);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_FAILURE, cpu_ticks, 1,
   gba_link_attempt_id, snapshot.api_phase, (u32)snapshot.api_error);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_FAILURE, cpu_ticks, 2,
   gba_link_attempt_id, snapshot.internal_error_phase,
   (u32)snapshot.recovery_terminal_error);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_TIMEOUT, cpu_ticks, 0,
   gba_link_attempt_id,
   gba_link_session.last_timeout_source != LINK_TIMEOUT_NONE ?
    (u32)gba_link_session.last_timeout_source : snapshot.timeout_source,
   (u32)gba_link_session.reason);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 0,
   gba_link_attempt_id, gba_link_session.stream_start_tx_count,
   gba_link_session.stream_reply_tx_count);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 1,
   link_stream_apply_count(&gba_link_session.stream),
   gba_link_session.stream.duplicate_count,
   link_stream_replay_count(&gba_link_session.stream));
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 2,
   gba_link_session.stream.stale_generation_count,
   snapshot.rtt_estimate_us, snapshot.rto_us);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 3,
   main_link_bounded_u64(gba_link_session.stream_burst_last_us),
   main_link_bounded_u64(gba_link_session.stream_burst_max_us),
   main_link_bounded_u64(gba_link_session.barrier_last_us));
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 4,
   main_link_bounded_u64(gba_link_session.barrier_max_us),
   snapshot.hot_wake_latency_max_us,
   main_link_audio_underruns());
  packed = (snapshot.hot_drain_count > 0xFFFFu ? 0xFFFFu :
   snapshot.hot_drain_count) << 16;
  packed |= snapshot.hot_drain_high_water > 0xFFFFu ? 0xFFFFu :
   snapshot.hot_drain_high_water;
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 5,
   snapshot.hot_drain_packets, snapshot.hot_yields, packed);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 6,
   snapshot.hot_time_ms, snapshot.cold_time_ms, snapshot.hot_wake_count);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 7,
   gba_link_session.heartbeat_request_tx_count,
   gba_link_session.heartbeat_request_rx_count,
   gba_link_session.heartbeat_reply_tx_count);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 8,
   gba_link_session.heartbeat_reply_rx_count,
   gba_link_session.heartbeat_duplicate_request_count,
   gba_link_session.heartbeat_unmatched_reply_count);
  runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_STREAM_METRICS, cpu_ticks, 9,
   gba_link_session.heartbeat_outstanding_token,
   gba_link_session.heartbeat_probe_pending,
   gba_link_session.heartbeat_last_request_token);
}

static int main_link_transfer_sample_due(u32 count)
{
  return count != 0 && (count <= 8u || (count & (count - 1u)) == 0);
}

static int main_link_should_log_state(u32 events)
{
  switch(gba_link_session.state)
  {
    case LINK_SESSION_WAIT_PREPARE:
    case LINK_SESSION_WAIT_ACCEPT:
    case LINK_SESSION_WAIT_COMMIT:
    case LINK_SESSION_WAIT_COMMIT_ACK:
    case LINK_SESSION_WAIT_COMMIT_RELEASE:
      return main_link_transfer_sample_due(gba_link_transfer_started);

    case LINK_SESSION_CONNECTED:
      if((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0)
        return main_link_transfer_sample_due(gba_link_transfer_committed);
      return 1;

    default:
      return 1;
  }
}

static void main_link_log_transfer_sample(void)
{
  const LinkTransferData *transfer = &gba_link_session.applied_transfer;
  u8 flags;

  if(!main_link_transfer_sample_due(gba_link_transfer_committed))
    return;
  flags = transfer->mode & 0x03u;
  flags |= (u8)((gba_link_session.role == LINK_ROLE_HOST) << 2);
  flags |= (u8)(((gba_link_session.negotiated_capabilities &
   LINK_CAPABILITY_FAST_TRANSFER) != 0) << 3);
  flags |= (u8)((transfer->participant_mask & 0x0Fu) << 4);
  if(gba_link_transfer_committed <= 8u)
    runtime_log_psp_critical(RUNTIME_LOG_EVENT_LINK_TRANSFER_SAMPLE,
     cpu_ticks, flags, gba_link_session.applied_sequence,
     transfer->data[0], transfer->data[1]);
  else
    runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_TRANSFER_SAMPLE,
     cpu_ticks, flags, gba_link_session.applied_sequence,
     transfer->data[0], transfer->data[1]);
}

static void main_link_apply_events(u32 events)
{
  if((events & LINK_SESSION_EVENT_SERIAL_IRQ) != 0)
  {
    pIO_REG(REG_IF) |= IRQ_SERIAL;
    gba_link_serial_irqs++;
  }

  if((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0)
    gba_link_transfer_started++;
  if((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0)
    gba_link_transfer_committed++;
  if((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0)
    gba_link_transfer_aborted++;

  if((events & LINK_SESSION_EVENT_STATE_CHANGE) != 0 &&
     main_link_should_log_state(events))
  {
    runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_STATE, cpu_ticks,
     (u8)events, (u32)gba_link_session.state,
     (u32)gba_link_session.reason, gba_link_session.reason_detail);
  }

  if((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0)
    main_link_log_transfer_sample();

  if((events & LINK_SESSION_EVENT_STATE_CHANGE) != 0 &&
     gba_link_session.state == LINK_SESSION_RECOVERING &&
     !gba_link_recovery_checkpointed)
  {
    gba_link_recovery_checkpointed = 1;
    main_log_link_checkpoint();
    runtime_log_psp_flush(RUNTIME_LOG_FLUSH_LINK_CHECKPOINT);
  }

  if((events & (LINK_SESSION_EVENT_BARRIER_BEGIN |
                LINK_SESSION_EVENT_BARRIER_END)) != 0)
    gba_sio_state_dirty = 1;

  if((events & LINK_SESSION_EVENT_SIO_REPUBLISH) != 0)
    gba_sio_state_dirty = 1;

  if((events & LINK_SESSION_EVENT_STATE_CHANGE) != 0 &&
     link_session_state(&gba_link_session) == LINK_SESSION_CONNECTED)
    gba_sio_state_dirty = 1;
}

static int main_link_session_open(void)
{
  LinkSessionState state = link_session_state(&gba_link_session);

  return state != LINK_SESSION_IDLE &&
   state != LINK_SESSION_DISCONNECTED && state != LINK_SESSION_ERROR;
}

static u32 main_adhoc_state_value(void)
{
  PspAdhocState state = psp_adhoc_state();

  return (u32)state;
}

static u32 main_adhoc_failure_phase_value(void)
{
  PspAdhocPhase phase = psp_adhoc_failure_phase();

  return (u32)phase;
}

static void main_link_flush_wait_log(void)
{
  u32 packed;

  if(gba_link_wait_count == 0)
    return;
  packed = (gba_link_wait_count > 0xFFFFu ? 0xFFFFu :
   gba_link_wait_count) << 16;
  packed |= (u32)gba_link_session.state & 0xFFFFu;
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_PAUSE_END, cpu_ticks,
   gba_link_wait_flags | 0x80u, gba_link_wait_count,
   gba_link_wait_total_us, gba_link_wait_max_us);
  if(gba_link_wait_max_us >= 20000u)
    runtime_log_psp_main(RUNTIME_LOG_EVENT_PERFORMANCE_SLOW, cpu_ticks,
     gba_link_wait_flags | 0x80u, gba_link_wait_max_us,
     gba_link_wait_latest_lead, packed);
  gba_link_wait_count = 0;
  gba_link_wait_total_us = 0;
  gba_link_wait_max_us = 0;
  gba_link_wait_latest_lead = 0;
  gba_link_wait_flags = 0;
  gba_link_wait_window_started = 0;
}

static void main_link_record_wait(u64 duration, u8 flags, u32 clock_lead,
 u64 now)
{
  u32 bounded = duration > UINT_MAX ? UINT_MAX : (u32)duration;
  u64 total = (u64)gba_link_wait_total_us + bounded;

  if(gba_link_wait_count != UINT_MAX)
    gba_link_wait_count++;
  gba_link_wait_total_us = total > UINT_MAX ? UINT_MAX : (u32)total;
  if(bounded > gba_link_wait_max_us)
    gba_link_wait_max_us = bounded;
  gba_link_wait_latest_lead = clock_lead;
  gba_link_wait_flags |= flags;
  if(gba_link_wait_window_started == 0)
    gba_link_wait_window_started = now;
  if(duration >= 100000u || now < gba_link_wait_window_started ||
     now - gba_link_wait_window_started >= 1000000u)
    main_link_flush_wait_log();
}

int main_link_active(void)
{
  return main_link_session_open() ||
   (psp_adhoc_shutdown_pending() &&
    psp_adhoc_state() != PSP_ADHOC_FAILED);
}

int main_link_connected(void)
{
  return link_session_is_connected(&gba_link_session);
}

int main_link_benchmark_available(void)
{
  return link_session_benchmark_available(&gba_link_session);
}

int main_link_benchmark_active(void)
{
  return link_benchmark_active(&gba_link_session.benchmark);
}

int main_link_benchmark_start(void)
{
  LinkBenchmarkControl control;
  u64 now;
  u32 run_id;
  int started;

  if(!main_link_benchmark_available())
    return 0;
  main_log_link_benchmark(0);
  now = sceKernelGetSystemTimeWide();
  run_id = (u32)(now ^ (now >> 32) ^ gba_link_session.session_id ^
   0xB34C41D7u);
  if(run_id == 0)
    run_id = 1u;
  link_benchmark_default_control(&control, run_id,
   run_id ^ gba_link_session.session_id ^ 0x9E3779B9u);
  started = link_session_benchmark_start(&gba_link_session, &control);
  main_link_benchmark_observe(now);
  return started;
}

void main_link_benchmark_stop(void)
{
  main_link_apply_events(link_session_benchmark_stop(&gba_link_session));
}

void main_link_benchmark_summary(LinkBenchmarkSummary *summary)
{
  link_session_benchmark_summary(&gba_link_session, summary);
}

void main_link_set_gameplay_active(int active)
{
  u8 value = active != 0;

  if(value)
  {
    main_log_link_benchmark(0);
    link_session_benchmark_clear(&gba_link_session);
  }

  if(gba_link_gameplay_active == value)
    return;
  gba_link_gameplay_active = value;
  gba_sio_state_dirty = 1;
  runtime_log_psp_main(RUNTIME_LOG_EVENT_MENU_PRESENCE, cpu_ticks,
   0, value, (u32)gba_link_session.state,
   main_adhoc_state_value());
  if(!value)
  {
    main_link_flush_wait_log();
    runtime_log_psp_flush(RUNTIME_LOG_FLUSH_MENU);
  }
}

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

static void main_fast_forward_record_change(int was_active,
 FastForwardSpeed was_speed)
{
  int active = fast_forward_policy_active(&fast_forward_policy);

  if(was_active != active ||
     (active && was_speed != fast_forward_policy_speed(&fast_forward_policy)))
    perf_monitor_record_fast_forward_transition();
}

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

static u32 main_link_rom_crc32(void)
{
  u8 buffer[8192];
  uLong crc = crc32(0L, Z_NULL, 0);
  u32 remaining = gamepak_file_size;
  SceUID file;

  if(gamepak_file_size <= gamepak_ram_buffer_size)
    return (u32)crc32(crc, gamepak_rom, gamepak_file_size);

  FILE_OPEN(file, gamepak_filename_raw, READ);
  if(!FILE_CHECK_VALID(file))
    return (u32)crc32(crc, gamepak_rom,
     gamepak_file_size < 0x100u ? gamepak_file_size : 0x100u);

  while(remaining != 0)
  {
    u32 wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    s32 received = FILE_READ(file, buffer, wanted);

    if(received <= 0)
      break;
    crc = crc32(crc, buffer, (u32)received);
    remaining -= (u32)received;
  }
  FILE_CLOSE(file);
  return (u32)crc;
}

static void main_link_hello(LinkHello *hello)
{
  static const u8 benchmark_title[12] =
   { 'L', 'I', 'N', 'K', ' ', 'B', 'E', 'N', 'C', 'H', ' ', ' ' };
  static const u8 benchmark_code[4] = { 'D', 'I', 'A', 'G' };

  memset(hello, 0, sizeof(*hello));
  hello->capabilities = LINK_SESSION_SUPPORTED_CAPABILITIES;
  hello->max_players = 2;
  if(gamepak_filename[0] == '\0' || gamepak_file_size < 0xC0u)
  {
    hello->rom_crc32 = MAIN_LINK_BENCHMARK_ROM_ID;
    hello->rom_size = 1u;
    memcpy(hello->title, benchmark_title, sizeof(hello->title));
    memcpy(hello->game_code, benchmark_code, sizeof(hello->game_code));
    return;
  }
  hello->rom_crc32 = main_link_rom_crc32();
  hello->rom_size = gamepak_file_size;
  memcpy(hello->title, gamepak_header + 0xA0, sizeof(hello->title));
  memcpy(hello->game_code, gamepak_header + 0xAC,
   sizeof(hello->game_code));
  hello->revision = gamepak_header[0xBC];
}

static void main_log_link_snapshot(void)
{
  PspAdhocFailureSnapshot snapshot;

  main_link_flush_wait_log();
  psp_adhoc_get_failure_snapshot(&snapshot);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 0,
   snapshot.transport_resyncs, snapshot.group_recoveries,
   snapshot.stack_recoveries);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 1,
   snapshot.tx_datagrams, snapshot.rx_datagrams,
   snapshot.retransmissions);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 2,
   gba_link_session.heartbeat_tx_count,
   gba_link_session.heartbeat_rx_count,
   ((u32)gba_link_session.last_message_type << 24) |
    ((u32)gba_link_session.state << 16) |
    ((u32)gba_link_session.reason << 8) |
    gba_link_session.reason_detail);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 3,
   snapshot.transport_epoch, snapshot.last_received_sequence,
   snapshot.outstanding_packets);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 4,
   snapshot.tx_queue_depth, snapshot.tx_queue_coalesced,
   snapshot.tx_queue_rejected);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 5,
   snapshot.recovery_state, snapshot.resync_state,
   (u32)snapshot.transition_phase);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 6,
   snapshot.tx_queue_high_water, snapshot.backpressure_events,
   snapshot.api_retries);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 7,
   snapshot.duplicates, snapshot.out_of_window,
   snapshot.invalid_packets);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 8,
   snapshot.ack_packets, snapshot.probes, snapshot.rto_us);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 9,
   snapshot.cpu_clock, snapshot.bus_clock, snapshot.tx_base_sequence);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 10,
   snapshot.tx_base_attempts, snapshot.tx_base_message,
   snapshot.tx_base_age_ms);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 11,
   snapshot.next_tx_sequence, snapshot.next_rx_sequence,
   snapshot.buffered_packets);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 12,
   snapshot.received_data_packets, snapshot.received_ack_packets,
   snapshot.ack_advances);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 13,
   snapshot.ack_progress_age_ms, snapshot.sent_bytes,
   snapshot.received_bytes);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 14,
   gba_link_transfer_started, gba_link_transfer_committed,
   gba_link_transfer_aborted);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 15,
   gba_link_serial_irqs,
   ((u32)gba_sio.mode << 28) |
    ((u32)(gba_sio.pending_start != 0) << 27) |
    ((u32)(gba_sio.waiting != 0) << 26) |
    ((u32)(gba_sio.active != 0) << 25) |
    ((u32)(gba_sio.transfer_error != 0) << 24) |
   pIO_REG(REG_SIOCNT),
   gba_sio.cycles_remaining);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 16,
   snapshot.tx_sio_mailbox_depth |
    (snapshot.tx_sio_high_water << 16),
   snapshot.tx_sio_published, snapshot.tx_sio_coalesced);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 17,
   snapshot.tx_heartbeat_mailbox_depth |
    (snapshot.tx_heartbeat_high_water << 16),
   snapshot.tx_heartbeat_published, snapshot.tx_heartbeat_coalesced);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 18,
   snapshot.tx_clock_mailbox_depth |
    (snapshot.tx_clock_high_water << 16),
   snapshot.tx_clock_published, snapshot.tx_clock_coalesced);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 19,
   snapshot.tx_critical_handoff_depth |
    (snapshot.tx_critical_handoff_high_water << 16),
   snapshot.tx_critical_pending_depth |
    (snapshot.tx_critical_pending_high_water << 16),
   snapshot.tx_critical_retry_suppressed);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 20,
   (u32)gba_link_session.last_transfer_reason,
   gba_link_session.last_transfer_detail,
   (u32)gba_link_session.transport_health);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 21,
   snapshot.backpressure_age_ms, snapshot.backpressure_streak,
   snapshot.backpressure_episode_count);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 22,
   snapshot.backpressure_max_age_ms,
   snapshot.backpressure_streak_high_water,
   (snapshot.last_blocked_kind & 0xFFFFu) |
    ((snapshot.last_blocked_state_class & 0xFFFFu) << 16));
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 23,
   snapshot.clock_backoff_us, snapshot.clock_next_delay_us,
   snapshot.clock_success_streak);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 24,
   snapshot.clock_backoff_raises, snapshot.clock_deferred,
   snapshot.health_causes);
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COUNTERS, cpu_ticks, 25,
   snapshot.session_recovery_requests, snapshot.session_recovery_reason,
   0);
}

static void main_link_handle_closed(void)
{
  LinkSessionState state = link_session_state(&gba_link_session);
  RuntimeLogFlushReason flush_reason;
  int shutdown_result;

  if(state != LINK_SESSION_DISCONNECTED && state != LINK_SESSION_ERROR)
    return;
  if(!gba_link_terminal_logged)
  {
    /* Preserve the terminal benchmark and live transport counters before
       worker teardown clears the queues and failure context. */
    main_log_link_benchmark(0);
    if(quit_started)
      psp_adhoc_stop_for_exit();
    else
      psp_adhoc_stop();
    shutdown_result = psp_adhoc_service_shutdown();
    if(shutdown_result == 0)
      return;

    runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_ERROR, cpu_ticks,
     (u8)state, (u32)psp_adhoc_last_error(),
     (u32)gba_link_session.reason,
     ((u32)gba_link_session.reason_detail << 16) |
      main_adhoc_failure_phase_value());
    main_log_link_checkpoint();
    main_log_link_snapshot();
    gba_link_terminal_logged = 1;

    if(gba_link_session.reason == LINK_REASON_USER)
      flush_reason = RUNTIME_LOG_FLUSH_DISCONNECT;
    else if(gba_link_session.reason == LINK_REASON_SUSPEND)
      flush_reason = RUNTIME_LOG_FLUSH_SUSPEND;
    else if(gba_link_session.reason == LINK_REASON_ROM_CHANGE)
      flush_reason = RUNTIME_LOG_FLUSH_ROM_CHANGE;
    else
      flush_reason = RUNTIME_LOG_FLUSH_LINK_FAILURE;
    runtime_log_psp_flush(flush_reason);
  }
  else if(psp_adhoc_shutdown_pending())
    psp_adhoc_service_shutdown();
  set_cpu_clock(option_clock_speed);
}

static int main_link_start(int host)
{
  LinkHello hello;
  u64 now;
  u32 session_id;

  if(!psp_adhoc_wlan_enabled())
    return 0;

  /* The no-ROM menu has never called reset_gba. Prepare only a cold SIO;
     an already loaded game's registers and pending transfer stay intact. */
  if(!gba_sio_prepare(&gba_sio, io_registers))
    return 0;

  gba_link_attempt_id++;
  if(gba_link_attempt_id == 0)
    gba_link_attempt_id = 1u;
  gba_link_audio_underrun_base = sound_audio_underrun_count();
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_COMMAND, cpu_ticks,
   (u8)(host != 0), main_adhoc_state_value(), 0, gba_link_attempt_id);

  main_fast_forward_stop();

  if(main_link_session_open())
    main_link_disconnect(LINK_REASON_USER);
  else
    psp_adhoc_stop();

  main_link_hello(&hello);
  link_session_init(&gba_link_session, &gba_sio, psp_adhoc_transport(),
   &hello);
  gba_link_transfer_started = 0;
  gba_link_transfer_committed = 0;
  gba_link_transfer_aborted = 0;
  gba_link_serial_irqs = 0;
  gba_link_recovery_checkpointed = 0;
  gba_link_terminal_logged = 0;
  set_cpu_clock(PSP_CLOCK_333);
  psp_adhoc_set_transition_phase(PSP_ADHOC_TRANSITION_LINK_START);
  if(!psp_adhoc_start())
  {
    runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_ERROR, cpu_ticks,
     0, (u32)psp_adhoc_last_error(), main_adhoc_state_value(),
     main_adhoc_failure_phase_value());
    main_log_link_checkpoint();
    main_log_link_snapshot();
    runtime_log_psp_flush(RUNTIME_LOG_FLUSH_LINK_FAILURE);
    set_cpu_clock(option_clock_speed);
    return 0;
  }

  if(!host)
  {
    if(link_session_join(&gba_link_session))
    {
      runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_STATE, cpu_ticks, 0,
       (u32)gba_link_session.state, (u32)gba_link_session.reason, 0);
      return 1;
    }
    main_link_handle_closed();
    return 0;
  }

  now = sceKernelGetSystemTimeWide();
  session_id = (u32)(now ^ (now >> 32) ^ hello.rom_crc32 ^
   hello.rom_size);
  if(session_id == 0)
    session_id = 1;
  if(link_session_host(&gba_link_session, session_id))
  {
    runtime_log_psp_main(RUNTIME_LOG_EVENT_LINK_STATE, cpu_ticks, 0,
     (u32)gba_link_session.state, (u32)gba_link_session.reason, 0);
    return 1;
  }

  main_link_handle_closed();
  return 0;
}

int main_link_host(void)
{
  return main_link_start(1);
}

int main_link_join(void)
{
  return main_link_start(0);
}

void main_link_disconnect(LinkSessionReason reason)
{
  LinkSessionState state = link_session_state(&gba_link_session);

  if(state != LINK_SESSION_IDLE && state != LINK_SESSION_DISCONNECTED &&
     state != LINK_SESSION_ERROR)
    main_link_apply_events(link_session_disconnect(&gba_link_session,
     reason));
  main_link_handle_closed();
  if(link_session_state(&gba_link_session) == LINK_SESSION_IDLE)
  {
    if(quit_started)
      psp_adhoc_stop_for_exit();
    else
      psp_adhoc_stop();
  }
}

void main_link_pump(void)
{
  LinkSessionState state;
  u64 budget_started;
  u64 now;
  u64 gap;

  state = link_session_state(&gba_link_session);
  if(state == LINK_SESSION_DISCONNECTED || state == LINK_SESSION_ERROR)
  {
    main_link_handle_closed();
    return;
  }
  if(!main_link_session_open())
  {
    if(psp_adhoc_shutdown_pending())
      psp_adhoc_service_shutdown();
    return;
  }

  budget_started = sceKernelGetSystemTimeWide();
  do
  {
    now = sceKernelGetSystemTimeWide();
    if(main_link_benchmark_active() && gba_link_benchmark_last_pump_us != 0)
    {
      gap = now >= gba_link_benchmark_last_pump_us ?
       now - gba_link_benchmark_last_pump_us : 0;
      if(gap > gba_link_benchmark_starvation_max_us)
        gba_link_benchmark_starvation_max_us = main_link_bounded_u64(gap);
    }
    link_session_set_emulated_cycle(&gba_link_session, cpu_ticks);
    main_link_apply_events(link_session_pump(&gba_link_session));
    main_link_publish_sio();
    main_link_apply_events(link_session_publish_clock(&gba_link_session,
     cpu_ticks));
    main_link_benchmark_observe(now);
    gba_link_benchmark_last_pump_us = now;
    main_log_link_benchmark(1);
    if(!main_link_benchmark_active() || now < budget_started ||
       now - budget_started >= MAIN_LINK_BENCHMARK_SERVICE_BUDGET_US)
      break;
    sceKernelDelayThread(50u);
  }
  while(1);
  state = link_session_state(&gba_link_session);
  if(state == LINK_SESSION_DISCONNECTED || state == LINK_SESSION_ERROR)
    main_link_handle_closed();
}

static void main_link_publish_sio(void)
{
  LinkSioState state;
  u32 events;

  if(!gba_sio_state_dirty || !link_session_is_connected(&gba_link_session))
    return;

  memset(&state, 0, sizeof(state));
  state.mode = (u8)gba_sio.mode;
  state.player_id = gba_sio.player_id;
  state.player_count = gba_sio.player_count;
  state.flags = 0;
  if(gba_sio.pending_start)
    state.flags |= LINK_SIO_FLAG_PENDING_START;
  if(gba_sio.waiting)
    state.flags |= LINK_SIO_FLAG_WAITING;
  if(gba_sio.active)
    state.flags |= LINK_SIO_FLAG_ACTIVE;
  if(gba_link_gameplay_active)
    state.flags |= LINK_SIO_FLAG_GAMEPLAY_ACTIVE;
  state.control = pIO_REG(REG_SIOCNT);
  state.rcnt = pIO_REG(REG_RCNT);

  events = link_session_publish_sio_state(&gba_link_session, &state);
  main_link_apply_events(events);
  if(link_session_state(&gba_link_session) == LINK_SESSION_CONNECTED)
    gba_sio_state_dirty = 0;
}

static void main_link_service(void)
{
  LinkSessionPauseReason pause_reason;
  u64 wait_started;
  u64 wait_finished;
  u64 wait_duration;
  u32 wait_flags;
  u32 clock_lead = 0;
  u32 events;
  u32 audio_wait_active;

  link_session_set_emulated_cycle(&gba_link_session, cpu_ticks);
  events = link_session_pump(&gba_link_session);
  main_link_apply_events(events);
  main_link_publish_sio();
  main_link_apply_events(link_session_publish_clock(&gba_link_session,
   cpu_ticks));

  if(!main_link_session_open())
  {
    main_link_handle_closed();
    return;
  }

  events = link_session_begin_transfer(&gba_link_session);
  main_link_apply_events(events);

  pause_reason = link_session_pause_reason(&gba_link_session);
  if(pause_reason == LINK_SESSION_PAUSE_NONE)
    return;

  wait_flags = 0;
  if(pause_reason == LINK_SESSION_PAUSE_SIO_DEPENDENCY)
    wait_flags |= 1u;
  if(pause_reason == LINK_SESSION_PAUSE_AHEAD_PACING)
    wait_flags |= 2u;
  if(pause_reason == LINK_SESSION_PAUSE_RECOVERY)
    wait_flags |= 16u;
  if(link_session_in_barrier(&gba_link_session))
    wait_flags |= 4u;
  if(gba_link_gameplay_active)
    wait_flags |= 8u;
  if(gba_link_session.clock.peer_valid)
  {
    u32 local_elapsed = gba_link_session.clock.local_cycle -
     gba_link_session.clock.local_base;
    u32 peer_elapsed = gba_link_session.clock.peer_cycle -
     gba_link_session.clock.peer_base;

    clock_lead = local_elapsed - peer_elapsed;
  }
  wait_started = sceKernelGetSystemTimeWide();
  audio_wait_active = 0;
  while(main_link_session_open() &&
        pause_reason != LINK_SESSION_PAUSE_NONE)
  {
    if(!audio_wait_active &&
       pause_reason != LINK_SESSION_PAUSE_AHEAD_PACING)
    {
      sound_link_wait_begin();
      audio_wait_active = 1;
    }
    main_power_service();
    if(sleep_flag != 0)
      events = link_session_disconnect(&gba_link_session,
       LINK_REASON_SUSPEND);
    else
      events = link_session_pump(&gba_link_session);

    main_link_apply_events(events);
    main_link_apply_events(link_session_publish_clock(&gba_link_session,
     cpu_ticks));
    pause_reason = link_session_pause_reason(&gba_link_session);
    if(main_link_session_open() && pause_reason != LINK_SESSION_PAUSE_NONE)
      sceKernelDelayThread(1000);
  }
  if(audio_wait_active)
    sound_link_wait_end(cpu_ticks);
  wait_finished = sceKernelGetSystemTimeWide();
  wait_duration = wait_finished >= wait_started ?
   wait_finished - wait_started : 0;
  if((wait_flags & 1u) != 0 || wait_duration >= 5000u)
    main_link_record_wait(wait_duration, (u8)wait_flags, clock_lead,
     wait_finished);
  main_link_publish_sio();
  main_link_handle_closed();
}

u32 update_gba(void)
{
  s32 i;
  IRQ_TYPE irq_raised = IRQ_NONE;
  GbaSioEvent sio_event;

  do
  {
    reg[CPU_DMA_HACK] = 0;

    update_gba_loop:

    main_power_service();

    cpu_ticks += reg[EXECUTE_CYCLES];

    if(gba_sio.active)
    {
      sio_event = gba_sio_advance(&gba_sio, reg[EXECUTE_CYCLES]);
      if((sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
      {
        irq_raised |= IRQ_SERIAL;
        gba_link_serial_irqs++;
      }
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
      u16 dispstat = pIO_REG(REG_DISPSTAT);
      u16 vcount   = pIO_REG(REG_VCOUNT);

      if ((dispstat & 0x02) == 0)
      {
        // Transition from hrefresh to hblank
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
        // Transition from hblank to next line
        video_count += 960;
        dispstat &= ~0x02;

        vcount++;

        if (vcount == 160)
        {
          // Transition from vrefresh to vblank
          dispstat |= 0x01;

          if (update_input() != 0)
            continue;

          if (!skip_next_frame)
            (*update_screen)();

          update_gbc_sound(cpu_ticks);
          gbc_sound_update = 0;

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
          // Transition from vblank to next screen
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
          // vcount trigger
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

    if(gba_link_session.state != LINK_SESSION_IDLE &&
       gba_link_session.state != LINK_SESSION_DISCONNECTED &&
       gba_link_session.state != LINK_SESSION_ERROR)
      main_link_service();

    reg[EXECUTE_CYCLES] = video_count;

    for (i = 0; i < 4; i++)
    {
      CHECK_TIMER(i);
    }

    if(gba_sio.active)
    {
      CHECK_COUNT(gba_sio.cycles_remaining);
    }
    else
    {
      u32 link_pause_cycles =
       link_session_cycles_until_pause(&gba_link_session);

      if(link_pause_cycles != GBA_SIO_NO_EVENT)
        CHECK_COUNT(link_pause_cycles);
    }

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


static void vblank_interrupt_handler(u32 sub, u32 *parg)
{
  real_frame_count++;
  vblank_count++;
}

static void synchronize(void)
{
  static s32 fps = 60;
  static s32 frames_drawn = 60;

  u32 used_frameskip_type  = option_frameskip_type;
  u32 used_frameskip_value = option_frameskip_value;

  frames++;

  if (frames == 60)
  {
    fps = vblank_count ? (3600 / vblank_count) : 0;
    frames_drawn = 60 - interval_skipped_frames;

    vblank_count = 0;
    frames = 0;
    interval_skipped_frames = 0;
  }

  if (!skip_next_frame)
  {
    // GBA - sleep mode
    if (reg[CPU_HALT_STATE] == CPU_STOP)
    {
      clear_screen(0);
      if (option_language == 0)
        print_string(MSG[MSG_GBA_SLEEP_MODE], X_POS_CENTER, 130, color_active_item, BG_NO_FILL);
      else
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
//    sprintf(print_buffer, "%02d(%02d)", fps % 100, frames_drawn);
      print_string(print_buffer, 0, 0, COLOR15_WHITE, COLOR15_BLACK);
    }
  }

  if (main_fast_forward_active())
  {
    if (psp_fps_debug != 0)
    {
      if (option_language == 0)
        print_string(main_fast_forward_status(), 0, 12, COLOR15_WHITE,
         COLOR15_BLACK);
      else
        print_string_gbk(main_fast_forward_status(), 0, 12, COLOR15_WHITE,
         COLOR15_BLACK);
    }
    else
    {
      if (option_language == 0)
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

  if (real_frame_count >= virtual_frame_count)
  {
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
      sceDisplayWaitVblankStart();
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


static void init_main(void)
{
  s32 i;

  init_cpu();

  skip_next_frame = 0;

  vblank_count = 0;
  frames = 0;
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

static void setup_main(const char *eboot_path)
{
  FastForwardSettings fast_forward_settings;

  scePowerLock(0);

  fast_forward_settings_defaults(&fast_forward_settings);
  fast_forward_policy_init(&fast_forward_policy, &fast_forward_settings);

  initExceptionHandler();

  int devkit_version = sceKernelDevkitVersion();

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

int user_main(int argc, char *argv[])
{
  char load_filename[MAX_FILE];
  const char *file_ext[] = { ".zip", ".gba", ".bin", ".agb", ".gbz", NULL };
  u32 rom_loaded = 0;

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
        menu();
      }
      else
      {
        /* Fixed single-ROM package: do not update Recent ROMs */
        rom_loaded = 1;
      }
    }
    else if (load_file(file_ext, load_filename, dir_roms, 1) < 0)
    {
      menu();
    }
    else if (load_gamepak(load_filename) < 0)
    {
      clear_screen(COLOR32_BLACK);
      error_msg(MSG[MSG_ERR_LOAD_GAMEPACK], CONFIRMATION_CONT);
      menu();
    }
    else
    {
      add_recent_rom(load_filename);
      rom_loaded = 1;
    }
  }

  if (rom_loaded)
  {
    reset_gba();

    set_cpu_clock(option_clock_speed);

    sceDisplayWaitVblankStart();
    video_resolution_small();

    sound_pause = 0;

    // We'll never actually return from here.
    execute_arm_translate(reg[EXECUTE_CYCLES]);
  }

  /* If we get here, menu() returned without loading a ROM */
  return 0;
}

int main(int argc, char *argv[])
{
    user_main(argc, argv);
    return 0;
}


void quit(void)
{
  if(!__sync_bool_compare_and_swap(&quit_started, 0, 1))
    return;

  main_fast_forward_stop();
  main_link_disconnect(LINK_REASON_USER);

  update_backup_immediately();
  save_config_file();
  runtime_log_psp_main(RUNTIME_LOG_EVENT_SESSION_END, cpu_ticks, 0,
   (u32)gba_link_session.state, (u32)gba_link_session.reason, 0);
  runtime_log_psp_flush(RUNTIME_LOG_FLUSH_EXIT);

  sound_term();
  memory_term();
  video_term();

  set_cpu_clock(PSP_CLOCK_222);

  sceKernelDisableSubIntr(PSP_VBLANK_INT, 0);
  sceKernelReleaseSubIntrHandler(PSP_VBLANK_INT, 0);

  scePowerUnregisterCallback(0);

  quit_cleanup_complete = 1;
  __sync_synchronize();
  sceKernelExitGame();
}

void reset_gba(void)
{
  u32 new_identity = 0;

  main_fast_forward_stop();
  perf_monitor_invalidate(sceKernelGetSystemTimeWide());
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
  input_diagnostic_trace_cancel();
#endif
  main_link_disconnect(LINK_REASON_ROM_CHANGE);
  if(main_link_active())
    return;

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
  link_session_init(&gba_link_session, &gba_sio, psp_adhoc_transport(),
   NULL);
  gba_link_terminal_logged = 0;
  if(gamepak_file_size >= 0xC0u)
  {
    runtime_log_psp_set_rom(gamepak_header + 0xAC, gamepak_header[0xBC],
     gamepak_file_size, cpu_ticks);
    runtime_log_rom_identity = new_identity;
    runtime_log_rom_active = 1;
  }
  reset_sound();
}


static void main_power_save_if_pending(void)
{
  if(power_lifecycle_take_save(&psp_power_lifecycle) &&
     !sleep_auto_saved && gamepak_filename[0] != '\0')
  {
    auto_savestate_sleep();
    sleep_auto_saved = 1;
  }
}

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

static void psp_sleep_loop(void)
{
  runtime_log_psp_main(RUNTIME_LOG_EVENT_POWER_SUSPEND, cpu_ticks, 0,
   (u32)gba_link_session.state, main_adhoc_state_value(), sleep_flag);
  main_fast_forward_stop();
  perf_monitor_invalidate(sceKernelGetSystemTimeWide());
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
  input_diagnostic_trace_cancel();
#endif
  video_power_suspend();

  if(main_link_session_open())
    main_link_disconnect(LINK_REASON_SUSPEND);

  main_power_save_if_pending();

  if (FILE_CHECK_VALID(gamepak_file_large))
  {
    FILE_CLOSE(gamepak_file_large);
    gamepak_file_large = -1;
    sleep_reopen_gamepak = 1;
  }

  runtime_log_psp_flush(RUNTIME_LOG_FLUSH_SUSPEND);

  do
  {
    main_link_handle_closed();
    if(psp_adhoc_shutdown_pending())
      psp_adhoc_service_shutdown();
    sceKernelDelayThread(100 * 1000);
  }
  while (sleep_flag != 0);
}

void main_power_service(void)
{
  PowerLifecycleState state;
  int audio_result;

  if(exit_requested && !quit_started)
    quit();

  if(link_session_state(&gba_link_session) == LINK_SESSION_DISCONNECTED ||
     link_session_state(&gba_link_session) == LINK_SESSION_ERROR)
    main_link_handle_closed();
  else if(psp_adhoc_shutdown_pending())
    psp_adhoc_service_shutdown();

  state = power_lifecycle_state(&psp_power_lifecycle);
  if(sleep_flag != 0 || state == POWER_LIFECYCLE_SUSPENDED)
    psp_sleep_loop();

  state = power_lifecycle_state(&psp_power_lifecycle);
  if(state != POWER_LIFECYCLE_RUNNING && main_link_session_open())
    main_link_disconnect(LINK_REASON_SUSPEND);

  main_power_save_if_pending();

  if(!power_lifecycle_take_resume(&psp_power_lifecycle, &audio_result))
    return;

  if(FILE_CHECK_VALID(gamepak_file_large) && !sleep_reopen_gamepak)
  {
    FILE_CLOSE(gamepak_file_large);
    gamepak_file_large = -1;
    sleep_reopen_gamepak = 1;
  }

  main_power_reopen_gamepak();
  video_power_resume();
  sound_power_resume_finish(cpu_ticks, audio_result);
  sleep_auto_saved = 0;
  runtime_log_psp_main(RUNTIME_LOG_EVENT_POWER_RESUME, cpu_ticks,
   (u8)(audio_result >= 0), (u32)audio_result,
   (u32)gba_link_session.state, main_adhoc_state_value());
}


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

static void setup_callbacks(void)
{
  SceUID thid = -1;

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


u64 ticker(void)
{
  u64 current_ticks;

  sceRtcGetCurrentTick(&current_ticks);

  return current_ticks;
}

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

int main_perf_start(void)
{
  if(!perf_monitor_start(sceKernelGetSystemTimeWide()))
    return 0;

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

int main_perf_write_report(char *output_path, size_t output_path_size)
{
  FastForwardSettings fast_forward_settings;
  PerfMetricsSnapshot metrics;
  PerfReportMetadata metadata;
  ScePspDateTime date = { 0 };
  SceIoStat directory_stat;
  char directory[MAX_PATH];
  char filename[MAX_PATH];
  char report[2048];
  SceUID file;
  int formatted;
  int report_length;
  int model;

  perf_monitor_snapshot(&metrics);
  if(metrics.state == PERF_METRICS_IDLE ||
     metrics.state == PERF_METRICS_ACTIVE)
    return -1;

  memset(&metadata, 0, sizeof(metadata));
  metadata.emulator_version = GBA_RUSH_VERSION;
  model = kuKernelGetModel();
  metadata.psp_model = model < 0 ? UINT32_MAX : (u32)model;
  metadata.clock_mhz = main_perf_clock_mhz();
  metadata.renderer = option_video_renderer;
  metadata.scale = option_screen_scale;
  metadata.filter = option_screen_filter;
  metadata.frameskip_type = option_frameskip_type;
  metadata.frameskip_value = option_frameskip_value;
  metadata.sound_volume = option_sound_volume;
  fast_forward_settings = main_fast_forward_settings();
  metadata.fast_forward_game_override = option_fast_forward_game_override;
  metadata.fast_forward_profile = fast_forward_settings.profile;
  metadata.fast_forward_activation = fast_forward_settings.activation;
  metadata.fast_forward_speed = fast_forward_settings.speed;
  metadata.fast_forward_presentation = fast_forward_settings.presentation;
  metadata.fast_forward_audio = fast_forward_settings.audio;
  if(gamepak_file_size >= 0xC0U)
  {
    metadata.rom_title = gamepak_header + 0xA0;
    metadata.rom_title_length = 12;
    metadata.game_code = gamepak_header + 0xAC;
    metadata.game_code_length = 4;
    metadata.rom_revision = gamepak_header[0xBC];
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

  draw_popup_frame_auto(popup_x, popup_y, popup_w, popup_h);

  if (option_language == 0)
  {
    print_string(text, X_POS_CENTER, popup_y + 16, color_active_item, BG_NO_FILL);
  }
  else
  {
    print_string_gbk(text, X_POS_CENTER, popup_y + 16, color_active_item, BG_NO_FILL);
  }
  print_swap_aware(MSG[MSG_YES_NO], X_POS_CENTER, popup_y + 46, color_active_item, BG_NO_FILL);

  flip_screen(1);

  while ((gui_action != CURSOR_SELECT) && (gui_action != CURSOR_EXIT))
  {
    gui_action = get_gui_input();
    sceKernelDelayThread(15000);
  }

  return (gui_action == CURSOR_SELECT) ? 0 : 1;
}


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

void psp_fclose(SceUID filename_tag)
{
  if (filename_tag < 0)
    return;

  sceIoClose(filename_tag);
  filename_tag = -1;
}


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
