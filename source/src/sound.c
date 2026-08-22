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

#include "common.h"
#include "fast_forward_audio.h"
#include "perf_monitor.h"
#include "sound_math.h"


#define SOUND_BUFFER_SIZE (SOUND_SAMPLES * 2)
#define RING_BUFFER_SIZE  (65536)

u32 sound_pause = 0;

u32 gbc_sound_update = 0;

typedef enum
{
  DIRECT_SOUND_INACTIVE,
  DIRECT_SOUND_RIGHT,
  DIRECT_SOUND_LEFT,
  DIRECT_SOUND_LEFTRIGHT
} DIRECT_SOUND_STATUS_TYPE;

typedef enum
{
  DIRECT_SOUND_VOLUME_50,
  DIRECT_SOUND_VOLUME_100
} DIRECT_SOUND_VOLUME_TYPE;

typedef enum
{
  GBC_SOUND_INACTIVE,
  GBC_SOUND_RIGHT,
  GBC_SOUND_LEFT,
  GBC_SOUND_LEFTRIGHT
} GBC_SOUND_STATUS_TYPE;

typedef struct
{
  DIRECT_SOUND_STATUS_TYPE status;
  DIRECT_SOUND_VOLUME_TYPE volume;

  FIXED08_24 fifo_fractional;
  // The + 1 is to give some extra room for linear interpolation
  // when wrapping around.

  u32 buffer_index;

  s8 fifo[32];
  u32 fifo_base;
  u32 fifo_top;
} DirectSoundStruct;

typedef struct
{
  GBC_SOUND_STATUS_TYPE status;

  FIXED08_24 frequency_step;
  FIXED08_24 tick_counter;

  FIXED08_24 sample_index;
  s8 *sample_data;

  u32 active_flag;

  u32 envelope_status;
  u32 envelope_direction;
  u32 envelope_initial_volume;
  u32 envelope_volume;
  u32 envelope_initial_ticks;
  u32 envelope_ticks;

  u32 sweep_status;
  u32 sweep_direction;
  u32 sweep_shift;
  u32 sweep_initial_ticks;
  u32 sweep_ticks;

  u32 length_status;
  u32 length_ticks;

  u32 rate;
} GBCSoundStruct;

DirectSoundStruct ALIGN_DATA direct_sound_channel[2];
GBCSoundStruct    ALIGN_DATA gbc_sound_channel[4];

s16 ALIGN_DATA psp_sound_buffer[2][SOUND_BUFFER_SIZE];
// Full-scale mix bus with headroom for four PSG and two FIFO channels.
s32 ALIGN_DATA sound_buffer[RING_BUFFER_SIZE];

int sound_volume = PSP_AUDIO_VOLUME_MAX;

SceUID sound_thread = -1;
SceUID sound_sema = -1;
static int start_sound_thread(void);
static int psp_sound_release(void);
static int psp_sound_reserve(u16 sample_count, u16 frequency);

volatile int sound_active = 0;
static volatile u32 sound_fast_forward = 0;
static volatile u32 sound_fast_forward_factor = 1;
static volatile u32 sound_fast_forward_fallback_pending = 0;
static volatile u32 sound_power_suspended = 0;
static volatile u32 sound_output_ready = 0;
static volatile u32 sound_perf_underrun_pending = 0;
static volatile u32 sound_perf_recovery_pending = 0;
static volatile u32 sound_channel_reserved = 0;
static void fill_sound_buffer(s16 *stream, u16 frames, u32 factor);
static int sound_update_thread(SceSize args, void *argp);

static volatile u8 sound_sleep = 0;
static void sound_thread_wakeup(void);
static u32 sound_link_wait_active = 0;
static u32 sound_link_saved_pause = 0;
static u32 sound_fast_forward_audio_started = 0;
static u64 sound_fast_forward_starvation_since = 0;

#define SOUND_OPERATION_TIMEOUT 500000u
#define SOUND_RECOVERY_RETRY_US 250000u

u32 sound_buffer_base = 0;
static u32 buffer_length(u32 top, u32 base, u32 length);

static u64 delta_ticks(u32 ticks, u32 last_ticks);

static void sound_reset_fifo(u8 channel);

u8 sound_on = 0;

u8 master_enable = 0;
u8 gbc_sound_wave_update = 0;
u8 wave_type = 0;
u8 wave_bank = 0;
u8 wave_bank_user = 0x00;
u32 wave_volume = 0;
u8 ALIGN_DATA wave_ram_data[32];
s8 ALIGN_DATA wave_samples[64];
const u32 ALIGN_DATA gbc_sound_wave_volume[4] = { 0, 16384, 8192, 4096 };

u8 noise_type = 0;
u32 noise_index = 0;
u32 ALIGN_DATA noise_table15[1024];
u32 ALIGN_DATA noise_table7[4];
static void init_noise_table(u32 *table, u32 period, u32 bit_length);

// Initial pattern data = 4bits (signed)
// Channel volume = 12bits
// Envelope volume = 14bits
// Master volume = 2bits

// Recalculate left and right volume as volume changes.
// To calculate the current sample, use (sample * volume) >> 16

// Square waves range from -8 (low) to 7 (high)

s8 ALIGN_DATA square_pattern_duty[4][8] =
{
  { 0x07, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8 },
  { 0x07, 0x07, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8 },
  { 0x07, 0x07, 0x07, 0x07, 0xF8, 0xF8, 0xF8, 0xF8 },
  { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0xF8, 0xF8 }
};

const u32 ALIGN_DATA gbc_sound_master_volume_table[4] = { 1, 2, 4, 0 };

const u32 ALIGN_DATA gbc_sound_channel_volume_table[8] =
{
  FIXED_DIV(0, 7, 12),
  FIXED_DIV(1, 7, 12),
  FIXED_DIV(2, 7, 12),
  FIXED_DIV(3, 7, 12),
  FIXED_DIV(4, 7, 12),
  FIXED_DIV(5, 7, 12),
  FIXED_DIV(6, 7, 12),
  FIXED_DIV(7, 7, 12)
};

const u32 ALIGN_DATA gbc_sound_envelope_volume_table[16] =
{
  FIXED_DIV( 0, 15, 14),
  FIXED_DIV( 1, 15, 14),
  FIXED_DIV( 2, 15, 14),
  FIXED_DIV( 3, 15, 14),
  FIXED_DIV( 4, 15, 14),
  FIXED_DIV( 5, 15, 14),
  FIXED_DIV( 6, 15, 14),
  FIXED_DIV( 7, 15, 14),
  FIXED_DIV( 8, 15, 14),
  FIXED_DIV( 9, 15, 14),
  FIXED_DIV(10, 15, 14),
  FIXED_DIV(11, 15, 14),
  FIXED_DIV(12, 15, 14),
  FIXED_DIV(13, 15, 14),
  FIXED_DIV(14, 15, 14),
  FIXED_DIV(15, 15, 14)
};

u8 gbc_sound_master_volume_left;
u8 gbc_sound_master_volume_right;
u8 gbc_sound_master_volume;

u32 gbc_sound_buffer_index = 0;
u32 gbc_sound_last_cpu_ticks = 0;
u32 gbc_sound_partial_ticks = 0;

FIXED08_24 gbc_sound_tick_step;


void gbc_sound_tone_control_low(u8 channel, u32 value)
{
  GBCSoundStruct *gs = gbc_sound_channel + channel;

  u32 envelope_volume = (value >> 12) & 0x0F;
  u32 envelope_ticks = ((value >>  8) & 0x07) << 2;

  gs->length_ticks = 64 - (value & 0x3F);
  gs->sample_data = square_pattern_duty[(value >> 6) & 0x03];

  gs->envelope_status = (envelope_ticks != 0);
  gs->envelope_direction = (value >> 11) & 0x01;

  gs->envelope_initial_volume = envelope_volume;
  gs->envelope_initial_ticks  = envelope_ticks;

  // No Sound
  if (envelope_volume == 0)
    gs->envelope_volume = 0;

  // No Envelope
  if (envelope_ticks == 0)
    gs->envelope_ticks = 0;

  gbc_sound_update = 1;
}

void gbc_sound_tone_control_high(u8 channel, u32 value)
{
  GBCSoundStruct *gs = gbc_sound_channel + channel;

  u32 rate = value & 0x7FF;

  gs->rate = rate;
  gs->frequency_step = sound_math_tone_step(rate, SOUND_FREQUENCY);
  gs->length_status = (value >> 14) & 0x01;

  if ((value & 0x8000) != 0)
  {
    gs->active_flag = 1;
    gs->sample_index -= sound_math_retrigger_offset();
    gs->envelope_ticks  = gs->envelope_initial_ticks;
    gs->envelope_volume = gs->envelope_initial_volume;
    gs->sweep_ticks = gs->sweep_initial_ticks;
  }

  gbc_sound_update = 1;
}

void gbc_sound_tone_control_sweep(u32 value)
{
  GBCSoundStruct *gs = gbc_sound_channel + 0;

  u32 sweep_shift = value & 0x07;
  u32 sweep_ticks = ((value >> 4) & 0x07) << 1;

  gs->sweep_status = (sweep_shift != 0) && (sweep_ticks != 0);

  gs->sweep_shift = sweep_shift;
  gs->sweep_direction = (value >> 3) & 0x01;

  gs->sweep_initial_ticks = sweep_ticks;

  gbc_sound_update = 1;
}

void gbc_sound_wave_control(u32 value)
{
  wave_bank = (value >> 6) & 0x01;
  wave_bank_user = (wave_bank ^ 0x01) << 4;

  wave_type = (value >> 5) & 0x01;

  master_enable = (value >> 7) & 0x01;

  gbc_sound_update = 1;
}

void gbc_sound_tone_control_low_wave(u32 value)
{
  GBCSoundStruct *gs = gbc_sound_channel + 2;

  gs->length_ticks = 256 - (value & 0xFF);

  if ((value & 0x8000) != 0)
    wave_volume = 12288;
  else
    wave_volume = gbc_sound_wave_volume[(value >> 13) & 0x03];

  gbc_sound_update = 1;
}

void gbc_sound_tone_control_high_wave(u32 value)
{
  GBCSoundStruct *gs = gbc_sound_channel + 2;

  u32 rate = value & 0x7FF;

  gs->rate = rate;
  gs->frequency_step = sound_math_wave_step(rate, SOUND_FREQUENCY);
  gs->length_status = (value >> 14) & 0x01;

  if ((value & 0x8000) != 0)
  {
    gs->sample_index = 0;
    gs->active_flag = 1;
  }

  gbc_sound_update = 1;
}

void gbc_sound_wave_pattern_ram16(u32 address, u32 value)
{
  if (wave_bank_user != 0)
    gbc_sound_wave_update |= 2;
  else
    gbc_sound_wave_update |= 1;

  ADDRESS16(wave_ram_data, (address & 0x0e) | wave_bank_user) = value;
}

void gbc_sound_noise_control(u32 value)
{
  GBCSoundStruct *gs = gbc_sound_channel + 3;

  u32 dividing_ratio = value & 0x07;
  u32 frequency_shift = (value >> 4) & 0x0F;

  gs->frequency_step = sound_math_noise_step(dividing_ratio,
   frequency_shift, SOUND_FREQUENCY);

  noise_type = (value >> 3) & 0x01;
  gs->length_status = (value >> 14) & 0x01;

  if ((value & 0x8000) != 0)
  {
    noise_index = 0;

    gs->sample_index = 0;
    gs->active_flag = 1;
    gs->envelope_ticks = gs->envelope_initial_ticks;
    gs->envelope_volume = gs->envelope_initial_volume;
  }

  gbc_sound_update = 1;
}

#define GBC_SOUND_CHANNEL_STATUS(channel)                                     \
  gbc_sound_channel[channel].status = ((value >> (channel + 11)) & 0x02) |    \
                                      ((value >> (channel +  8)) & 0x01)      \

void sound_control_low(u32 value)
{
  gbc_sound_master_volume_right = value & 0x07;
  gbc_sound_master_volume_left = (value >> 4) & 0x07;

  GBC_SOUND_CHANNEL_STATUS(0);
  GBC_SOUND_CHANNEL_STATUS(1);
  GBC_SOUND_CHANNEL_STATUS(2);
  GBC_SOUND_CHANNEL_STATUS(3);
}

void sound_control_high(u32 value)
{
  gbc_sound_master_volume = value & 0x03;

  direct_sound_timer_select(value);

  direct_sound_channel[0].volume = (value >>  2) & 0x01;
  direct_sound_channel[0].status = (value >>  8) & 0x03;
  direct_sound_channel[1].volume = (value >>  3) & 0x01;
  direct_sound_channel[1].status = (value >> 12) & 0x03;

  if ((value & 0x0800) != 0)
    sound_reset_fifo(0);

  if ((value & 0x8000) != 0)
    sound_reset_fifo(1);
}

void sound_control_x(u32 value)
{
  if ((value & 0x80) != 0)
  {
    sound_on = 1;
  }
  else
  {
    gbc_sound_channel[0].active_flag = 0;
    gbc_sound_channel[1].active_flag = 0;
    gbc_sound_channel[2].active_flag = 0;
    gbc_sound_channel[3].active_flag = 0;
    sound_on = 0;
  }
}


static u64 delta_ticks(u32 now_ticks, u32 last_ticks)
{
  if (now_ticks == last_ticks)
    return 0ULL;

  if (now_ticks > last_ticks)
    return (u64)now_ticks - last_ticks;

  return 4294967296ULL - last_ticks + now_ticks;
}

void adjust_direct_sound_buffer(u8 channel, u32 cpu_ticks)
{
  u64 count_ticks;
  u32 buffer_ticks, partial_ticks;

  count_ticks = delta_ticks(cpu_ticks, gbc_sound_last_cpu_ticks) * SOUND_FREQUENCY;

  buffer_ticks = FP08_24_TO_U32(count_ticks);
  partial_ticks = gbc_sound_partial_ticks + FP08_24_FRACTIONAL_PART(count_ticks);

  if (partial_ticks > 0x00FFFFFF)
    buffer_ticks++;

  direct_sound_channel[channel].buffer_index = (gbc_sound_buffer_index + (buffer_ticks << 1)) % RING_BUFFER_SIZE;
}

void sound_timer_queue(u8 channel)
{
  DirectSoundStruct *ds = direct_sound_channel + channel;

  u32 i;
  u32 fifo_top = ds->fifo_top;
  s8 *fifo = ds->fifo;
  s8 *fifo_data = (s8 *)io_registers + (0xA0 + (channel << 2));

  for (i = 0; i < 4; i++)
  {
    fifo[fifo_top] = fifo_data[i];
    fifo_top = (fifo_top + 1) % 32;
  }

  ds->fifo_top = fifo_top;
}

static void sound_reset_fifo(u8 channel)
{
  DirectSoundStruct *ds = direct_sound_channel + channel;

  ds->fifo_top  = 0;
  ds->fifo_base = 0;

  memset(ds->fifo, 0, 32);
}

static u32 buffer_length(u32 top, u32 base, u32 length)
{
  if (top == base)
    return 0;

  if (top > base)
    return top - base;

  return length - base + top;
}


// Unqueue 1 sample from the base of the DS FIFO and place it on the audio
// buffer for as many samples as necessary. If the DS FIFO is 16 bytes or
// smaller and if DMA is enabled for the sound channel initiate a DMA transfer
// to the DS FIFO.

#define RENDER_SAMPLE_NULL()                                                  \

#define RENDER_SAMPLE_RIGHT()                                                 \
  sound_buffer[buffer_index + 1] += sound_math_interpolate(current_sample,    \
   next_sample, fifo_fractional);                                             \

#define RENDER_SAMPLE_LEFT()                                                  \
  sound_buffer[buffer_index + 0] += sound_math_interpolate(current_sample,    \
   next_sample, fifo_fractional);                                             \

#define RENDER_SAMPLE_BOTH()                                                  \
  dest_sample = sound_math_interpolate(current_sample, next_sample,           \
   fifo_fractional);                                                         \
  sound_buffer[buffer_index + 0] += dest_sample;                              \
  sound_buffer[buffer_index + 1] += dest_sample;                              \

#define RENDER_SAMPLES(type)                                                  \
  while (fifo_fractional <= 0x00FFFFFF)                                       \
  {                                                                           \
    RENDER_SAMPLE_##type();                                                   \
    fifo_fractional += frequency_step;                                        \
    buffer_index = (buffer_index + 2) % RING_BUFFER_SIZE;                     \
  }                                                                           \

void sound_timer(FIXED08_24 frequency_step, u8 channel)
{
  DirectSoundStruct *ds = direct_sound_channel + channel;

  FIXED08_24 fifo_fractional = ds->fifo_fractional;
  u32 fifo_base = ds->fifo_base;
  u32 buffer_index = ds->buffer_index;

  s32 current_sample, next_sample, dest_sample;

  current_sample = ds->fifo[fifo_base] * 256;

  ds->fifo[fifo_base] = 0;
  fifo_base = (fifo_base + 1) % 32;

  next_sample = ds->fifo[fifo_base] * 256;

  if (sound_on == 1)
  {
    if (ds->volume == DIRECT_SOUND_VOLUME_50)
    {
      current_sample >>= 1;
      next_sample >>= 1;
    }

    switch (ds->status)
    {
      case DIRECT_SOUND_INACTIVE:
        RENDER_SAMPLES(NULL);
        break;

      case DIRECT_SOUND_RIGHT:
        RENDER_SAMPLES(RIGHT);
        break;

      case DIRECT_SOUND_LEFT:
        RENDER_SAMPLES(LEFT);
        break;

      case DIRECT_SOUND_LEFTRIGHT:
        RENDER_SAMPLES(BOTH);
        break;
    }
  }
  else
  {
    RENDER_SAMPLES(NULL);
  }

  ds->buffer_index = buffer_index;
  ds->fifo_base = fifo_base;
  ds->fifo_fractional = fifo_fractional - U32_TO_FP08_24(1);

  if (buffer_length(ds->fifo_top, ds->fifo_base, 32) <= 16)
  {
    if (dma[1].direct_sound_channel == channel)
      dma_transfer(dma + 1);

    if (dma[2].direct_sound_channel == channel)
      dma_transfer(dma + 2);
  }
}


#define UPDATE_VOLUME_CHANNEL_ENVELOPE(channel)                               \
  volume_##channel = gbc_sound_envelope_volume_table[envelope_volume] *       \
                     gbc_sound_channel_volume_table[gbc_sound_master_volume_##channel] * \
                     gbc_sound_master_volume_table[gbc_sound_master_volume]   \

#define UPDATE_VOLUME_CHANNEL_NOENVELOPE(channel)                             \
  volume_##channel = wave_volume *                                            \
                     gbc_sound_channel_volume_table[gbc_sound_master_volume_##channel] * \
                     gbc_sound_master_volume_table[gbc_sound_master_volume]   \

#define UPDATE_VOLUME(type)                                                   \
  UPDATE_VOLUME_CHANNEL_##type(left);                                         \
  UPDATE_VOLUME_CHANNEL_##type(right);                                        \

#define UPDATE_TONE_SWEEP()                                                   \
  if (gs->sweep_status != 0)                                                  \
  {                                                                           \
    u32 sweep_ticks = gs->sweep_ticks - 1;                                    \
                                                                              \
    if (sweep_ticks == 0)                                                     \
    {                                                                         \
      u32 rate = gs->rate;                                                    \
                                                                              \
      if (gs->sweep_direction != 0)                                           \
        rate = rate - (rate >> gs->sweep_shift);                              \
      else                                                                    \
        rate = rate + (rate >> gs->sweep_shift);                              \
                                                                              \
      if (rate > 2047)                                                        \
      {                                                                       \
        gs->active_flag = 0;                                                  \
        break;                                                                \
      }                                                                       \
                                                                              \
      frequency_step = sound_math_tone_step(rate, SOUND_FREQUENCY);           \
                                                                              \
      gs->frequency_step = frequency_step;                                    \
      gs->rate = rate;                                                        \
                                                                              \
      sweep_ticks = gs->sweep_initial_ticks;                                  \
    }                                                                         \
                                                                              \
    gs->sweep_ticks = sweep_ticks;                                            \
  }                                                                           \

#define UPDATE_TONE_NOSWEEP()                                                 \

#define UPDATE_TONE_ENVELOPE()                                                \
  if (gs->envelope_status != 0)                                               \
  {                                                                           \
    u32 envelope_ticks = gs->envelope_ticks - 1;                              \
    envelope_volume = gs->envelope_volume;                                    \
                                                                              \
    if (envelope_ticks == 0)                                                  \
    {                                                                         \
      if (gs->envelope_direction != 0)                                        \
      {                                                                       \
        if (envelope_volume != 15)                                            \
          envelope_volume++;                                                  \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        if (envelope_volume != 0)                                             \
          envelope_volume--;                                                  \
      }                                                                       \
                                                                              \
      UPDATE_VOLUME(ENVELOPE);                                                \
                                                                              \
      gs->envelope_volume = envelope_volume;                                  \
      envelope_ticks = gs->envelope_initial_ticks;                            \
    }                                                                         \
                                                                              \
    gs->envelope_ticks = envelope_ticks;                                      \
  }                                                                           \

#define UPDATE_TONE_NOENVELOPE()                                              \

#define UPDATE_TONE_COUNTERS(envelope_op, sweep_op)                           \
  tick_counter += gbc_sound_tick_step;                                        \
  if (tick_counter > 0x00FFFFFF)                                              \
  {                                                                           \
    tick_counter &= 0x00FFFFFF;                                               \
                                                                              \
    if (gs->length_status != 0)                                               \
    {                                                                         \
      u32 length_ticks = gs->length_ticks - 1;                                \
      gs->length_ticks = length_ticks;                                        \
                                                                              \
      if (length_ticks == 0)                                                  \
      {                                                                       \
        gs->active_flag = 0;                                                  \
        break;                                                                \
      }                                                                       \
    }                                                                         \
                                                                              \
    UPDATE_TONE_##envelope_op();                                              \
    UPDATE_TONE_##sweep_op();                                                 \
  }                                                                           \

#define GBC_SOUND_RENDER_SAMPLE_RIGHT()                                       \
  sound_buffer[buffer_index + 1] += sound_math_scale_psg(current_sample,      \
   volume_right);                                                            \

#define GBC_SOUND_RENDER_SAMPLE_LEFT()                                        \
  sound_buffer[buffer_index + 0] += sound_math_scale_psg(current_sample,      \
   volume_left);                                                             \

#define GBC_SOUND_RENDER_SAMPLE_BOTH()                                        \
  GBC_SOUND_RENDER_SAMPLE_RIGHT();                                            \
  GBC_SOUND_RENDER_SAMPLE_LEFT();                                             \

#define GBC_SOUND_RENDER_SAMPLES(type, sample_length, envelope_op, sweep_op)  \
  for (i = 0; i < buffer_ticks; i++)                                          \
  {                                                                           \
    current_sample = sample_data[FP08_24_TO_U32(sample_index) % sample_length]; \
                                                                              \
    GBC_SOUND_RENDER_SAMPLE_##type();                                         \
                                                                              \
    sample_index += frequency_step;                                           \
    buffer_index = (buffer_index + 2) % RING_BUFFER_SIZE;                     \
                                                                              \
    UPDATE_TONE_COUNTERS(envelope_op, sweep_op);                              \
  }                                                                           \

#define GBC_NOISE_WRAP_FULL 32767

#define GBC_NOISE_WRAP_HALF 127

#define GET_NOISE_SAMPLE_FULL()                                               \
  current_sample = ((s32)(noise_table15[noise_index >> 5] << (noise_index & 0x1F)) >> 31) ^ 0x07; \

#define GET_NOISE_SAMPLE_HALF()                                               \
  current_sample = ((s32)(noise_table7[noise_index >> 5]  << (noise_index & 0x1F)) >> 31) ^ 0x07; \

#define GBC_SOUND_RENDER_NOISE(type, noise_type, envelope_op, sweep_op)       \
  for (i = 0; i < buffer_ticks; i++)                                          \
  {                                                                           \
    GET_NOISE_SAMPLE_##noise_type();                                          \
    GBC_SOUND_RENDER_SAMPLE_##type();                                         \
                                                                              \
    sample_index += frequency_step;                                           \
                                                                              \
    if (sample_index > 0x00FFFFFF)                                            \
    {                                                                         \
      noise_index = (noise_index + 1) % GBC_NOISE_WRAP_##noise_type;          \
      sample_index = FP08_24_FRACTIONAL_PART(sample_index);                   \
    }                                                                         \
                                                                              \
    buffer_index = (buffer_index + 2) % RING_BUFFER_SIZE;                     \
                                                                              \
    UPDATE_TONE_COUNTERS(envelope_op, sweep_op);                              \
  }                                                                           \

#define GBC_SOUND_RENDER_CHANNEL(type, sample_length, envelope_op, sweep_op)  \
  buffer_index = gbc_sound_buffer_index;                                      \
  sample_index = gs->sample_index;                                            \
  frequency_step = gs->frequency_step;                                        \
  tick_counter = gs->tick_counter;                                            \
                                                                              \
  UPDATE_VOLUME(envelope_op);                                                 \
                                                                              \
  switch (gs->status)                                                         \
  {                                                                           \
    case GBC_SOUND_INACTIVE:                                                  \
      break;                                                                  \
                                                                              \
    case GBC_SOUND_RIGHT:                                                     \
      GBC_SOUND_RENDER_##type(RIGHT, sample_length, envelope_op, sweep_op);   \
      break;                                                                  \
                                                                              \
    case GBC_SOUND_LEFT:                                                      \
      GBC_SOUND_RENDER_##type(LEFT, sample_length, envelope_op, sweep_op);    \
      break;                                                                  \
                                                                              \
    case GBC_SOUND_LEFTRIGHT:                                                 \
      GBC_SOUND_RENDER_##type(BOTH, sample_length, envelope_op, sweep_op);    \
      break;                                                                  \
  }                                                                           \
                                                                              \
  gs->sample_index = sample_index;                                            \
  gs->tick_counter = tick_counter;                                            \


#define GBC_SOUND_LOAD_WAVE_RAM()                                             \
  for (i = 0, i2 = 0; i < 16; i++, i2 += 2)                                   \
  {                                                                           \
    current_sample = wave_ram[i];                                             \
    wave_ram_bank[i2 + 0] = ((current_sample >> 4) & 0x0F) - 8;               \
    wave_ram_bank[i2 + 1] = ((current_sample >> 0) & 0x0F) - 8;               \
  }                                                                           \

#define GBC_SOUND_UPDATE_WAVE_RAM()                                           \
{                                                                             \
  u8 *wave_ram = wave_ram_data;                                               \
  s8 *wave_ram_bank = wave_samples;                                           \
                                                                              \
  /* Wave RAM Bank 0 */                                                       \
  if ((gbc_sound_wave_update & 1) != 0)                                       \
  {                                                                           \
    GBC_SOUND_LOAD_WAVE_RAM();                                                \
  }                                                                           \
                                                                              \
  /* Wave RAM Bank 1 */                                                       \
  if ((gbc_sound_wave_update & 2) != 0)                                       \
  {                                                                           \
    wave_ram += 16;                                                           \
    wave_ram_bank += 32;                                                      \
    GBC_SOUND_LOAD_WAVE_RAM();                                                \
  }                                                                           \
                                                                              \
  gbc_sound_wave_update = 0;                                                  \
}                                                                             \


#define SOUND_BUFFER_LENGTH                                                   \
  buffer_length(gbc_sound_buffer_index, sound_buffer_base, RING_BUFFER_SIZE)  \

static void sound_set_fast_forward_mode(u32 enabled, u32 factor)
{
  enabled = enabled != 0;
  if(!enabled)
    factor = 1;
  else if(factor != 2 && factor != 3)
    factor = 0;

  if(sound_fast_forward == enabled &&
     sound_fast_forward_factor == factor)
    return;

  sound_fast_forward = enabled;
  sound_fast_forward_factor = factor;
  sound_fast_forward_fallback_pending = 0;
  sound_fast_forward_audio_started = 0;
  sound_fast_forward_starvation_since = 0;
  sound_buffer_base = gbc_sound_buffer_index;
  __builtin_allegrex_sync();

  if((!enabled || factor != 0) && sound_thread >= 0 && sound_sleep != 0)
    sceKernelWakeupThread(sound_thread);
}

void sound_set_fast_forward(u32 enabled)
{
  sound_set_fast_forward_mode(enabled, 0);
}

void sound_set_fast_forward_audio(u32 factor)
{
  sound_set_fast_forward_mode(1, factor);
}

int sound_take_fast_forward_fallback(void)
{
  if(!sound_fast_forward_fallback_pending)
    return 0;

  sound_fast_forward_fallback_pending = 0;
  __builtin_allegrex_sync();
  return 1;
}

void sound_power_suspend(void)
{
  SceUInt timeout = SOUND_OPERATION_TIMEOUT;

  sound_fast_forward = 0;
  sound_fast_forward_factor = 1;
  sound_fast_forward_fallback_pending = 0;
  sound_fast_forward_audio_started = 0;
  sound_fast_forward_starvation_since = 0;
  sound_power_suspended = 1;
  __builtin_allegrex_sync();

  if(sound_sema >= 0 && sceKernelWaitSema(sound_sema, 1, &timeout) >= 0)
  {
    psp_sound_release();
    sceKernelSignalSema(sound_sema, 1);
  }
}

int sound_power_resume(void)
{
  SceUInt timeout = SOUND_OPERATION_TIMEOUT;
  int result;

  if(sound_sema < 0 || sceKernelWaitSema(sound_sema, 1, &timeout) < 0)
    return -1;

  psp_sound_release();
  result = psp_sound_reserve(SOUND_SAMPLES, SOUND_FREQUENCY);
  sceKernelSignalSema(sound_sema, 1);
  return result;
}

void sound_power_resume_finish(u32 cpu_ticks, int audio_result)
{
  sound_fast_forward = 0;
  sound_fast_forward_factor = 1;
  sound_fast_forward_fallback_pending = 0;
  sound_fast_forward_audio_started = 0;
  sound_fast_forward_starvation_since = 0;
  synchronize_flag = 1;
  sound_buffer_base = gbc_sound_buffer_index;
  gbc_sound_last_cpu_ticks = cpu_ticks;
  memset(psp_sound_buffer, 0, sizeof(psp_sound_buffer));

  if(audio_result < 0)
    sound_output_ready = 0;

  __builtin_allegrex_sync();
  sound_power_suspended = 0;
  sound_thread_wakeup();
}

void sound_link_wait_begin(void)
{
  if(sound_link_wait_active)
    return;

  sound_link_wait_active = 1;
  sound_link_saved_pause = sound_pause;
  sound_pause = 1;
  sound_buffer_base = gbc_sound_buffer_index;
}

void sound_link_wait_end(u32 cpu_ticks)
{
  if(!sound_link_wait_active)
    return;

  gbc_sound_last_cpu_ticks = cpu_ticks;
  sound_buffer_base = gbc_sound_buffer_index;
  sound_pause = sound_link_saved_pause;
  sound_link_wait_active = 0;
  sound_thread_wakeup();
}

void synchronize_sound(void)
{
  u64 started;
  u32 factor;
  u32 limit;

  __builtin_allegrex_sync();
  if(sound_perf_underrun_pending)
  {
    sound_perf_underrun_pending = 0;
    perf_monitor_record_audio_underrun();
  }
  if(sound_perf_recovery_pending)
  {
    sound_perf_recovery_pending = 0;
    perf_monitor_record_audio_recovery();
  }
  __builtin_allegrex_sync();

  if(sound_fast_forward && sound_fast_forward_factor == 0)
  {
    sound_buffer_base = gbc_sound_buffer_index;
    return;
  }

  factor = sound_fast_forward ? sound_fast_forward_factor : 1;
  limit = SOUND_BUFFER_SIZE * factor * 4;
  if (synchronize_flag != 0 || factor > 1)
  {
    if (SOUND_BUFFER_LENGTH > limit)
    {
      if (option_frameskip_type == FRAMESKIP_AUTO)
      {
        sceDisplayWaitVblankStart();
        real_frame_count = 0;
        virtual_frame_count = 0;
      }

      started = sceKernelGetSystemTimeWide();
      while (SOUND_BUFFER_LENGTH > limit)
      {
        if(sleep_flag != 0 || sound_power_suspended ||
           sceKernelGetSystemTimeWide() - started >=
            SOUND_OPERATION_TIMEOUT)
        {
          sound_buffer_base = gbc_sound_buffer_index;
          perf_monitor_record_audio_recovery();
          break;
        }
        sceKernelDelayThread(100);
      }
    }
  }
}


void update_gbc_sound(u32 cpu_ticks)
{
  u32 i, i2;
  GBCSoundStruct *gs = gbc_sound_channel;

  FIXED08_24 sample_index, frequency_step;
  FIXED08_24 tick_counter;
  u32 buffer_index, buffer_ticks;

  s32 volume_left, volume_right;
  u32 envelope_volume;

  s32 current_sample;
  s8 *sample_data;

  u64 count_ticks = delta_ticks(cpu_ticks, gbc_sound_last_cpu_ticks) * SOUND_FREQUENCY;

  buffer_ticks = FP08_24_TO_U32(count_ticks);
  gbc_sound_partial_ticks += FP08_24_FRACTIONAL_PART(count_ticks);

  if (gbc_sound_partial_ticks > 0x00FFFFFF)
  {
    buffer_ticks++;
    gbc_sound_partial_ticks &= 0x00FFFFFF;
  }

  u16 sound_status = pIO_REG(REG_SOUNDCNT_X) & 0xFFF0;

  if (sound_on == 1)
  {
    // Sound Channel 1 - Tone & Sweep
    gs = gbc_sound_channel + 0;

    if (gs->active_flag != 0)
    {
      sample_data = gs->sample_data;
      envelope_volume = gs->envelope_volume;

      GBC_SOUND_RENDER_CHANNEL(SAMPLES, 8, ENVELOPE, SWEEP);

      if (gs->active_flag != 0)
        sound_status |= 0x01;
    }

    // Sound Channel 2 - Tone
    gs = gbc_sound_channel + 1;

    if (gs->active_flag != 0)
    {
      sample_data = gs->sample_data;
      envelope_volume = gs->envelope_volume;

      GBC_SOUND_RENDER_CHANNEL(SAMPLES, 8, ENVELOPE, NOSWEEP);

      if (gs->active_flag != 0)
        sound_status |= 0x02;
    }

    // Sound Channel 3 - Wave Output
    gs = gbc_sound_channel + 2;

    GBC_SOUND_UPDATE_WAVE_RAM();

    if ((gs->active_flag & master_enable) != 0)
    {
      sample_data = wave_samples;

      if (wave_type != 0)
      {
        GBC_SOUND_RENDER_CHANNEL(SAMPLES, 64, NOENVELOPE, NOSWEEP);
      }
      else
      {
        if (wave_bank != 0)
          sample_data += 32;

        GBC_SOUND_RENDER_CHANNEL(SAMPLES, 32, NOENVELOPE, NOSWEEP);
      }

      if (gs->active_flag != 0)
        sound_status |= 0x04;
    }

    // Sound Channel 4 - Noise
    gs = gbc_sound_channel + 3;

    if (gs->active_flag != 0)
    {
      envelope_volume = gs->envelope_volume;

      if (noise_type != 0)
      {
        GBC_SOUND_RENDER_CHANNEL(NOISE, HALF, ENVELOPE, NOSWEEP);
      }
      else
      {
        GBC_SOUND_RENDER_CHANNEL(NOISE, FULL, ENVELOPE, NOSWEEP);
      }

      if (gs->active_flag != 0)
        sound_status |= 0x08;
    }
  }

  pIO_REG(REG_SOUNDCNT_X) = sound_status;

  gbc_sound_last_cpu_ticks = cpu_ticks;
  gbc_sound_buffer_index = (gbc_sound_buffer_index + (buffer_ticks << 1)) % RING_BUFFER_SIZE;

  sound_thread_wakeup();
}


// Special thanks to blarrg for the LSFR frequency used in Meridian, as posted
// on the forum at http://meridian.overclocked.org:
// http://meridian.overclocked.org/cgi-bin/wwwthreads/showpost.pl?Board=merid
// angeneraldiscussion&Number=2069&page=0&view=expanded&mode=threaded&sb=4
// Hope you don't mind me borrowing it ^_-

static void init_noise_table(u32 *table, u32 period, u32 bit_length)
{
  u32 shift_register = 0xFF;
  u32 mask = ~(1 << bit_length);
  s32 table_pos, bit_pos;
  u32 current_entry;
  s32 table_period = (period + 31) / 32;

  // Bits are stored in reverse order so they can be more easily moved to
  // bit 31, for sign extended shift down.

  for (table_pos = 0; table_pos < table_period; table_pos++)
  {
    current_entry = 0;
    for (bit_pos = 31; bit_pos >= 0; bit_pos--)
    {
      current_entry |= (shift_register & 0x01) << bit_pos;

      shift_register = ((1 & (shift_register ^ (shift_register >> 1))) << bit_length) | ((shift_register >> 1) & mask);
    }

    table[table_pos] = current_entry;
  }
}


void set_sound_volume(void)
{
  sound_volume = PSP_AUDIO_VOLUME_MAX * (option_sound_volume * 10) / 100;
}

static void fill_sound_buffer(s16 *stream, u16 frames, u32 factor)
{
  fast_forward_audio_compress_ring(sound_buffer, RING_BUFFER_SIZE,
   &sound_buffer_base, stream, frames, factor,
   option_sound_volume != 0 && reg[CPU_HALT_STATE] != CPU_STOP);
}

static void sound_thread_wakeup(void)
{
  if((sound_sleep != 0) &&
   ((sound_fast_forward == 0) || (sound_fast_forward_factor != 0) ||
    (sound_active == 0)))
    sceKernelWakeupThread(sound_thread);
}

static int sound_update_thread(SceSize args, void *argp)
{
  u32 buf_idx = 0;
  int output_volume = PSP_AUDIO_VOLUME_MAX;
  int output_failed = 0;
  u64 next_recovery_attempt = 0;

  while (sound_active != 0)
  {
    u32 factor;
    u32 required_samples;
    u32 ready_samples;

    if (sleep_flag != 0 || sound_power_suspended)
    {
      sceKernelDelayThread(10 * 1000);
      continue;
    }

    if(!sound_output_ready)
    {
      u64 now = sceKernelGetSystemTimeWide();

      sound_buffer_base = gbc_sound_buffer_index;
      if(now >= next_recovery_attempt && sound_sema >= 0)
      {
        SceUInt timeout = SOUND_OPERATION_TIMEOUT;

        next_recovery_attempt = now + SOUND_RECOVERY_RETRY_US;
        if(sceKernelWaitSema(sound_sema, 1, &timeout) >= 0)
        {
          psp_sound_release();
          if(psp_sound_reserve(SOUND_SAMPLES, SOUND_FREQUENCY) >= 0)
          {
            sound_perf_recovery_pending = 1;
            output_failed = 0;
          }
          else
            sound_perf_underrun_pending = 1;
          __builtin_allegrex_sync();
          sceKernelSignalSema(sound_sema, 1);
        }
      }
      sceKernelDelayThread(10 * 1000);
      continue;
    }

    factor = sound_fast_forward ? sound_fast_forward_factor : 1;
    if(sound_fast_forward && factor == 0)
    {
      sound_buffer_base = gbc_sound_buffer_index;
      sound_sleep = 1;
      if(sound_fast_forward && sound_fast_forward_factor == 0)
        sceKernelSleepThread();
      sound_sleep = 0;
      continue;
    }

    required_samples = fast_forward_audio_required_samples(SOUND_SAMPLES,
     factor);
    ready_samples = required_samples;
    if(sound_fast_forward && factor > 1 &&
       !sound_fast_forward_audio_started)
      ready_samples *= 2;

    if (SOUND_BUFFER_LENGTH < ready_samples)
    {
      if(sound_fast_forward && factor > 1 &&
         sound_fast_forward_audio_started)
      {
        u64 now = sceKernelGetSystemTimeWide();

        if(sound_fast_forward_starvation_since == 0)
          sound_fast_forward_starvation_since = now;
        else if(now - sound_fast_forward_starvation_since >= 100000)
          sound_fast_forward_fallback_pending = 1;
      }
      gbc_sound_update = 1;
      sound_sleep = 1;
      sceKernelSleepThread();

      sound_sleep = 0;
      sceKernelDelayThread(1);
      continue;
    }
    sound_fast_forward_starvation_since = 0;

    if (output_volume != sound_volume) output_volume = sound_volume;

    if (!sound_pause)
      fill_sound_buffer(psp_sound_buffer[buf_idx], SOUND_SAMPLES, factor);
    else
      memset(psp_sound_buffer[buf_idx], 0, SOUND_BUFFER_SIZE * sizeof(s16));

    SceUInt timeout = SOUND_OPERATION_TIMEOUT;
    if(sceKernelWaitSema(sound_sema, 1, &timeout) < 0)
      continue;
    if(sceAudioSRCOutputBlocking(output_volume,
       psp_sound_buffer[buf_idx]) < 0)
    {
      sound_perf_underrun_pending = 1;
      sound_output_ready = 0;
      output_failed = 1;
      __builtin_allegrex_sync();
    }
    else
    {
      if(output_failed)
      {
        sound_perf_recovery_pending = 1;
        output_failed = 0;
        __builtin_allegrex_sync();
      }
      if(sound_fast_forward && factor > 1)
        sound_fast_forward_audio_started = 1;
    }
    sceKernelSignalSema(sound_sema, 1);

    buf_idx ^= 1;
  }

  sceKernelExitThread(0);

  return 0;
}

static int psp_sound_release(void)
{
  int result;

  if(!sound_channel_reserved)
    return 0;

  result = sceAudioSRCChRelease();
  sound_channel_reserved = 0;
  sound_output_ready = 0;
  __builtin_allegrex_sync();
  return result;
}

static int psp_sound_reserve(u16 sample_count, u16 frequency)
{
  int result = sceAudioSRCChReserve(sample_count, frequency, 2);

  sound_channel_reserved = result >= 0;
  sound_output_ready = result >= 0;
  __builtin_allegrex_sync();
  return result;
}

int psp_sound_frequency(u16 sample_count, u16 freq)
{
  SceUInt timeout = SOUND_OPERATION_TIMEOUT;
  int ret;

  switch (freq)
  {
    case  8000: case 11025: case 12000:
    case 16000: case 22050: case 24000:
    case 32000: case 44100: case 48000:
      break;

    default:
      return -1;
  }

  if(sceKernelWaitSema(sound_sema, 1, &timeout) < 0)
    return -1;

  psp_sound_release();
  ret = psp_sound_reserve(sample_count, freq);

  sceKernelSignalSema(sound_sema, 1);

  return ret;
}

static int start_sound_thread(void)
{
  sound_active = 0;
  sound_thread = -1;

  memset(psp_sound_buffer, 0, sizeof(psp_sound_buffer));

  sound_sema = sceKernelCreateSema("Sound semaphore", 0, 1, 1, 0);
  if(sound_sema < 0)
  {
    error_msg(MSG[MSG_ERR_RESERVE_AUDIO_CHANNEL], CONFIRMATION_QUIT);
    return -1;
  }

  if (psp_sound_frequency(SOUND_SAMPLES, SOUND_FREQUENCY) < 0)
  {
    error_msg(MSG[MSG_ERR_RESERVE_AUDIO_CHANNEL], CONFIRMATION_QUIT);

    return -1;
  }
  sound_power_suspended = 0;

  sound_thread = sceKernelCreateThread("Sound thread",
                                       sound_update_thread,
                                       0x08,
                                       0x10000,
                                       0,
                                       NULL);
  if (sound_thread < 0)
  {
    error_msg(MSG[MSG_ERR_START_SOUND_THREAD], CONFIRMATION_QUIT);
    psp_sound_release();

    return -1;
  }

  sound_active = 1;
  sceKernelStartThread(sound_thread, 0, 0);

  return 0;
}

void sound_term(void)
{
  SceUInt timeout = SOUND_OPERATION_TIMEOUT;

  sound_active = 0;

  if (sound_thread >= 0)
  {
    sound_thread_wakeup();

    if(sceKernelWaitThreadEnd(sound_thread, &timeout) < 0)
      sceKernelTerminateDeleteThread(sound_thread);
    else
      sceKernelDeleteThread(sound_thread);
    sound_thread = -1;

    psp_sound_release();
  }

  sound_output_ready = 0;
  sound_channel_reserved = 0;
  sound_power_suspended = 0;
  if(sound_sema >= 0)
  {
    sceKernelDeleteSema(sound_sema);
    sound_sema = -1;
  }
}


void reset_sound(void)
{
  u32 i;
  DirectSoundStruct *ds = direct_sound_channel;
  GBCSoundStruct    *gs = gbc_sound_channel;

  sound_on = 0;
  sound_link_wait_active = 0;
  sound_link_saved_pause = 0;
  sound_perf_underrun_pending = 0;
  sound_perf_recovery_pending = 0;
  sound_fast_forward = 0;
  sound_fast_forward_factor = 1;
  sound_fast_forward_fallback_pending = 0;
  sound_fast_forward_audio_started = 0;
  sound_fast_forward_starvation_since = 0;
  sound_buffer_base = 0;

  memset(sound_buffer, 0, sizeof(sound_buffer));

  for (i = 0; i < 2; i++, ds++)
  {
    ds->buffer_index = 0;
    ds->status = DIRECT_SOUND_INACTIVE;
    ds->fifo_top  = 0;
    ds->fifo_base = 0;
    ds->fifo_fractional = 0;
    memset(ds->fifo, 0, 32);
  }

  gbc_sound_buffer_index   = 0;
  gbc_sound_last_cpu_ticks = 0;
  gbc_sound_partial_ticks  = 0;

  gbc_sound_master_volume_left  = 0;
  gbc_sound_master_volume_right = 0;
  gbc_sound_master_volume = 0;

  for (i = 0; i < 4; i++, gs++)
  {
    gs->status = GBC_SOUND_INACTIVE;
    gs->sample_data = square_pattern_duty[2];
    gs->active_flag = 0;
  }

  master_enable = 0;
  gbc_sound_wave_update = 0;
  wave_type = 0;
  wave_bank = 0;
  wave_bank_user = 0x00;
  wave_volume = 0;
  memset(wave_ram_data, 0x88, sizeof(wave_ram_data));
  memset(wave_samples,  0x00, sizeof(wave_samples));

  noise_type = 0;
  noise_index = 0;

  gbc_sound_update = 0;
}

void init_sound(void)
{
  if (start_sound_thread() < 0)
    quit();

  gbc_sound_tick_step = sound_math_tick_step(SOUND_FREQUENCY);

  init_noise_table(noise_table15, 32767, 14);
  init_noise_table(noise_table7, 127, 6);

  reset_sound();
}


#define SOUND_SAVESTATE_BODY(type)                                          \
{                                                                           \
  FILE_##type##_VARIABLE(savestate_file, sound_on);                         \
  FILE_##type##_VARIABLE(savestate_file, sound_buffer_base);                \
                                                                            \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_buffer_index);           \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_last_cpu_ticks);         \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_partial_ticks);          \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_master_volume_left);     \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_master_volume_right);    \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_master_volume);          \
                                                                            \
  FILE_##type##_VARIABLE(savestate_file, master_enable);                    \
  FILE_##type##_VARIABLE(savestate_file, wave_type);                        \
  FILE_##type##_VARIABLE(savestate_file, wave_bank);                        \
  FILE_##type##_VARIABLE(savestate_file, wave_bank_user);                   \
  FILE_##type##_VARIABLE(savestate_file, wave_volume);                      \
  FILE_##type##_ARRAY(savestate_file, wave_samples);                        \
  FILE_##type##_ARRAY(savestate_file, wave_ram_data);                       \
                                                                            \
  FILE_##type##_VARIABLE(savestate_file, noise_type);                       \
  FILE_##type##_VARIABLE(savestate_file, noise_index);                      \
                                                                            \
  FILE_##type##_ARRAY(savestate_file, direct_sound_channel);                \
  FILE_##type##_ARRAY(savestate_file, gbc_sound_channel);                   \
}                                                                           \

void sound_read_savestate(SceUID savestate_file)
{
  u32 i;

  memset(sound_buffer, 0, sizeof(sound_buffer));

  SOUND_SAVESTATE_BODY(READ);

  for (i = 0; i < 4; i++)
    gbc_sound_channel[i].sample_data = square_pattern_duty[2];
}

void sound_write_mem_savestate(SceUID savestate_file)
{
  SOUND_SAVESTATE_BODY(WRITE_MEM);
}
