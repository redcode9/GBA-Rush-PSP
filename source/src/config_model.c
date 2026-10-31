/* Settings file format shared by tempgba.cfg and the per-game .cfg files.
   Current files: a 16-byte header ("GBRC", u16 version, u16 value count,
   u32 checksum, u32 payload size) then little-endian u32 values in
   model_to_values() order. Headerless files from older releases are still
   read. Out-of-range values decode to their defaults. */

#include "config_model.h"

#include <string.h>

#define CONFIG_MODEL_MAGIC "GBRC"
/* decode_versioned() reads only these two layouts: bumping the version
   drops files of the older one unless it learns that layout too.
   Adding a setting: append it to model_to_values() and values_to_model()
   (limit and default), add the default to config_model_defaults(), raise
   CONFIG_MODEL_VALUE_COUNT in config_model.h and bump the version. Keep
   every released layout readable (version 1 shipped in 1.3.0): the current
   pair becomes PREVIOUS_*, and each older one needs its own value-count
   constant and a case in decode_versioned(); then update its note and the
   migration test in tests/test_config_model.c. Finally map it in gui.c's capture_*_config()
   and apply_*_config() and add its option_ global, menu row and messages. */
#define CONFIG_MODEL_VERSION 2U
#define CONFIG_MODEL_PREVIOUS_VERSION 1U
#define CONFIG_MODEL_PREVIOUS_VALUE_COUNT 39U
#define CONFIG_MODEL_CHECKSUM_OFFSET 8U

static const uint32_t default_gamepad[CONFIG_MODEL_GAMEPAD_COUNT] =
{
  11, 4, 5, 18, 6, 7, 1, 2,
  0, 3, 9, 8, 0, 1, 2, 3
};

static void write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)value;
  output[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)value;
  output[1] = (uint8_t)(value >> 8);
  output[2] = (uint8_t)(value >> 16);
  output[3] = (uint8_t)(value >> 24);
}

static uint16_t read_u16(const uint8_t *input)
{
  return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8));
}

static uint32_t read_u32(const uint8_t *input)
{
  return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
   ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

/* 32-bit FNV-1a (offset basis 2166136261, prime 16777619) over the whole
   file, reading the checksum field itself as zeros. */
static uint32_t checksum(const uint8_t *data, size_t size)
{
  uint32_t value = 2166136261U;
  size_t i;

  for(i = 0; i < size; i++)
  {
    uint8_t byte = data[i];

    if(i >= CONFIG_MODEL_CHECKSUM_OFFSET &&
       i < CONFIG_MODEL_CHECKSUM_OFFSET + sizeof(uint32_t))
      byte = 0;
    value ^= byte;
    value *= 16777619U;
  }
  return value;
}

static uint32_t valid_or_default(uint32_t value, uint32_t limit,
 uint32_t default_value)
{
  return value < limit ? value : default_value;
}

/* Headerless files numbered the RAM dynarec setting differently: 4-6 map to
   policies 0-2, 0-1 to policies 1-2, anything else to the default (2). */
static uint32_t decode_legacy_ram_policy(uint32_t value)
{
  if(value >= 4 && value <= 6)
    return value - 4;
  if(value <= 1)
    return value + 1;
  return 2;
}

/* Factory settings. Keep them equal to the defaults in values_to_model(),
   which decoding falls back to for each out-of-range value, and to the
   values the menus' menu_*_default() resets in gui.c restore. */
void config_model_defaults(ConfigModel *model)
{
  if(model == NULL)
    return;

  memset(model, 0, sizeof(*model));
  model->screen_scale = 6;              /* SCALED_16X9_AUTO */
  model->screen_magnification = 170;
  model->screen_filter = 1;
  model->frameskip_type = 0;
  model->frameskip_value = 9;
  model->clock_speed = 3;               /* PSP_CLOCK_333 */
  model->sound_volume = 10;
  model->stack_optimize = 1;
  model->ram_dynarec_policy = 2;
  model->video_renderer = 1;
  model->update_backup = 1;
  model->analog_sensitivity = 4;
  model->language = 1;                  /* LANGUAGE_ENGLISH */
  model->hblank_window_start = 1;
  model->hblank_window_end = 160;
  model->fast_forward_profile = 0;
  model->fast_forward_game_override = 0;
  model->fast_forward_activation = 0;
  model->fast_forward_speed = 2;
  model->fast_forward_presentation = 1;
  model->fast_forward_audio = 0;
  memcpy(model->gamepad, default_gamepad, sizeof(model->gamepad));
}

/* The value index is the on-disk position: append new settings, never
   reorder. values_to_model() holds each setting's limit and default. */
static void model_to_values(const ConfigModel *model, uint32_t *values)
{
  size_t i;

  values[0] = model->screen_scale;
  values[1] = model->screen_magnification;
  values[2] = model->screen_filter;
  values[3] = model->show_fps;
  values[4] = model->frameskip_type;
  values[5] = model->frameskip_value;
  values[6] = model->clock_speed;
  values[7] = model->sound_volume;
  values[8] = model->stack_optimize;
  values[9] = model->ram_dynarec_policy;
  values[10] = model->video_renderer;
  values[11] = model->oam_hijacking;
  values[12] = model->boot_mode;
  values[13] = model->update_backup;
  values[14] = model->screen_capture_format;
  values[15] = model->enable_analog;
  values[16] = model->analog_sensitivity;
  values[17] = model->language;
  values[18] = model->hblank_window_start;
  values[19] = model->hblank_window_end;
  values[20] = model->psp_vsync;
  values[21] = model->swap_confirm_buttons;
  values[22] = model->theme;
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
    values[23 + i] = model->gamepad[i];
  values[39] = model->fast_forward_profile;
  values[40] = model->fast_forward_game_override;
  values[41] = model->fast_forward_activation;
  values[42] = model->fast_forward_speed;
  values[43] = model->fast_forward_presentation;
  values[44] = model->fast_forward_audio;
}

/* Inverse of model_to_values(). A value outside [0, limit) takes its
   default, so a damaged file cannot set an out-of-range option. */
static void values_to_model(const uint32_t *values, ConfigModel *model)
{
  size_t i;

  model->screen_scale = valid_or_default(values[0], 7, 6);
  model->screen_magnification = valid_or_default(values[1], 201, 170);
  model->screen_filter = valid_or_default(values[2], 2, 1);
  model->show_fps = valid_or_default(values[3], 2, 0);
  model->frameskip_type = valid_or_default(values[4], 3, 0);
  model->frameskip_value = valid_or_default(values[5], 10, 9);
  model->clock_speed = valid_or_default(values[6], 4, 3);
  model->sound_volume = valid_or_default(values[7], 11, 10);
  model->stack_optimize = valid_or_default(values[8], 2, 1);
  model->ram_dynarec_policy = valid_or_default(values[9], 3, 2);
  model->video_renderer = valid_or_default(values[10], 2, 1);
  model->oam_hijacking = valid_or_default(values[11], 2, 0);
  model->boot_mode = valid_or_default(values[12], 2, 0);
  model->update_backup = valid_or_default(values[13], 2, 1);
  model->screen_capture_format = valid_or_default(values[14], 2, 0);
  model->enable_analog = valid_or_default(values[15], 2, 0);
  model->analog_sensitivity = valid_or_default(values[16], 10, 4);
  model->language = valid_or_default(values[17], 5, 1);
  model->hblank_window_start = valid_or_default(values[18], 228, 1);
  model->hblank_window_end = valid_or_default(values[19], 228, 160);
  model->psp_vsync = valid_or_default(values[20], 2, 0);
  model->swap_confirm_buttons = valid_or_default(values[21], 2, 0);
  model->theme = valid_or_default(values[22], 9, 0);
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
    model->gamepad[i] = valid_or_default(values[23 + i], 20,
                                         default_gamepad[i]);
  model->fast_forward_profile = valid_or_default(values[39], 3, 0);
  model->fast_forward_game_override = valid_or_default(values[40], 2, 0);
  model->fast_forward_activation = valid_or_default(values[41], 2, 0);
  model->fast_forward_speed = valid_or_default(values[42], 3, 2);
  model->fast_forward_presentation = valid_or_default(values[43], 3, 1);
  model->fast_forward_audio = valid_or_default(values[44], 2, 0);
}

/* Writes the current versioned format; output needs at least
   CONFIG_MODEL_ENCODED_SIZE bytes. Returns 1, or 0 on bad arguments. */
int config_model_encode(const ConfigModel *model, uint8_t *output,
 size_t output_size, size_t *written)
{
  uint32_t values[CONFIG_MODEL_VALUE_COUNT];
  size_t i;

  if(model == NULL || output == NULL || output_size < CONFIG_MODEL_ENCODED_SIZE)
    return 0;

  memset(output, 0, CONFIG_MODEL_ENCODED_SIZE);
  memcpy(output, CONFIG_MODEL_MAGIC, 4);
  write_u16(output + 4, CONFIG_MODEL_VERSION);
  write_u16(output + 6, CONFIG_MODEL_VALUE_COUNT);
  write_u32(output + 12, CONFIG_MODEL_VALUE_COUNT * sizeof(uint32_t));
  model_to_values(model, values);
  for(i = 0; i < CONFIG_MODEL_VALUE_COUNT; i++)
    write_u32(output + CONFIG_MODEL_HEADER_SIZE + i * 4, values[i]);
  write_u32(output + CONFIG_MODEL_CHECKSUM_OFFSET,
            checksum(output, CONFIG_MODEL_ENCODED_SIZE));

  if(written != NULL)
    *written = CONFIG_MODEL_ENCODED_SIZE;
  return 1;
}

/* Version 1 files carry the first 39 values; the rest keep their defaults. */
static ConfigModelDecodeResult decode_versioned(const uint8_t *data,
 size_t size, ConfigModel *model)
{
  uint32_t values[CONFIG_MODEL_VALUE_COUNT];
  ConfigModel decoded;
  uint16_t version;
  uint16_t value_count;
  size_t i;

  if(size < CONFIG_MODEL_HEADER_SIZE)
    return CONFIG_MODEL_DECODE_INVALID;

  version = read_u16(data + 4);
  value_count = read_u16(data + 6);
  if(!((version == CONFIG_MODEL_VERSION &&
        value_count == CONFIG_MODEL_VALUE_COUNT) ||
       (version == CONFIG_MODEL_PREVIOUS_VERSION &&
        value_count == CONFIG_MODEL_PREVIOUS_VALUE_COUNT)) ||
     size != CONFIG_MODEL_HEADER_SIZE + value_count * sizeof(uint32_t) ||
     read_u32(data + 12) != value_count * sizeof(uint32_t) ||
     read_u32(data + CONFIG_MODEL_CHECKSUM_OFFSET) != checksum(data, size))
    return CONFIG_MODEL_DECODE_INVALID;

  config_model_defaults(&decoded);
  model_to_values(&decoded, values);
  for(i = 0; i < value_count; i++)
    values[i] = read_u32(data + CONFIG_MODEL_HEADER_SIZE + i * 4);
  values_to_model(values, &decoded);
  *model = decoded;
  return CONFIG_MODEL_DECODE_VERSIONED;
}

/* Headerless files are bare u32 arrays: 32 to 39 words (no 38) for the
   global config, as releases appended settings, and 23 for a per-game file. */
static int legacy_word_count_supported(size_t word_count)
{
  return word_count == 39 || word_count == 37 || word_count == 36 ||
         word_count == 35 || word_count == 34 || word_count == 33 ||
         word_count == 32 || word_count == 23;
}

/* Settings were inserted mid-array over the releases, so the word count
   selects the layout and where the 16 button mappings start. */
static ConfigModelDecodeResult decode_legacy(const uint8_t *data, size_t size,
 ConfigModel *model)
{
  uint32_t words[CONFIG_MODEL_LEGACY_CURRENT_WORDS] = { 0 };
  uint32_t values[CONFIG_MODEL_VALUE_COUNT];
  ConfigModel decoded;
  size_t word_count = size / sizeof(uint32_t);
  size_t gamepad_offset;
  size_t i;

  if(size % sizeof(uint32_t) != 0 || !legacy_word_count_supported(word_count))
    return CONFIG_MODEL_DECODE_INVALID;

  for(i = 0; i < word_count; i++)
    words[i] = read_u32(data + i * 4);

  config_model_defaults(&decoded);
  model_to_values(&decoded, values);
  /* Per-game layout: scale, magnification, filter, frameskip type and
     value, clock, volume, then the 16 button mappings. */
  if(word_count == 23)
  {
    values[0] = words[0];
    values[1] = words[1];
    values[2] = words[2];
    values[4] = words[3];
    values[5] = words[4];
    values[6] = words[5];
    values[7] = words[6];
    for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
      values[23 + i] = words[7 + i];
    values_to_model(values, &decoded);
    *model = decoded;
    return CONFIG_MODEL_DECODE_GAME_LEGACY;
  }

  for(i = 0; i < 9; i++)
    values[i] = words[i];

  if(word_count == 32)
  {
    values[12] = words[9];
    values[13] = words[10];
    values[14] = words[11];
    values[15] = words[12];
    values[16] = words[13];
    values[17] = words[14];
    gamepad_offset = 15;
  }
  else
  {
    values[9] = decode_legacy_ram_policy(words[9]);
    if(word_count >= 34)
    {
      values[10] = words[10];
      values[11] = words[11];
      values[12] = words[12];
      values[13] = words[13];
      values[14] = words[14];
      values[15] = words[15];
      values[16] = words[16];
      values[17] = words[17];
    }
    else
    {
      values[11] = words[10];
      values[12] = words[11];
      values[13] = words[12];
      values[14] = words[13];
      values[15] = words[14];
      values[16] = words[15];
      values[17] = words[16];
    }

    switch(word_count)
    {
      case 39:
        values[18] = words[18];
        values[19] = words[19];
        values[20] = words[20];
        values[21] = words[21];
        values[22] = words[22];
        gamepad_offset = 23;
        break;
      case 37:
        values[18] = words[18];
        values[19] = words[19];
        values[20] = words[20];
        gamepad_offset = 21;
        break;
      case 36:
        values[18] = words[18];
        values[19] = words[19];
        gamepad_offset = 20;
        break;
      case 35:
        if(words[18] == 0)
        {
          values[18] = 0;
          values[19] = 0;
        }
        else
        {
          values[18] = 1;
          values[19] = words[18];
        }
        gamepad_offset = 19;
        break;
      case 34:
        gamepad_offset = 18;
        break;
      default:
        gamepad_offset = 17;
        break;
    }
  }

  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
    values[23 + i] = words[gamepad_offset + i];
  values_to_model(values, &decoded);
  *model = decoded;
  return CONFIG_MODEL_DECODE_LEGACY;
}

/* Returns the format that was read. On CONFIG_MODEL_DECODE_INVALID, *model
   is left untouched. */
ConfigModelDecodeResult config_model_decode(const uint8_t *data, size_t size,
 ConfigModel *model)
{
  if(data == NULL || model == NULL)
    return CONFIG_MODEL_DECODE_INVALID;

  if(size >= 4 && memcmp(data, CONFIG_MODEL_MAGIC, 4) == 0)
    return decode_versioned(data, size, model);
  return decode_legacy(data, size, model);
}
