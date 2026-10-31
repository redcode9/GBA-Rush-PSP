/*
 * Host tests for config_model.c, the settings file codec: round trip of the
 * current versioned "GBRC" format, migration from version 1 and from the
 * headerless word arrays older releases wrote, and fallback to defaults for
 * out-of-range fields.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_model.h"

static void write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)value;
  output[1] = (uint8_t)(value >> 8);
  output[2] = (uint8_t)(value >> 16);
  output[3] = (uint8_t)(value >> 24);
}

static void write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)value;
  output[1] = (uint8_t)(value >> 8);
}

/* Independent copy of the codec's FNV-1a checksum; the stored checksum at
   bytes 8-11 is hashed as zeros. Used to forge valid version 1 files. */
static uint32_t config_checksum(const uint8_t *data, size_t size)
{
  uint32_t value = 2166136261U;
  size_t i;

  for(i = 0; i < size; i++)
  {
    uint8_t byte = data[i];

    if(i >= 8 && i < 12)
      byte = 0;
    value ^= byte;
    value *= 16777619U;
  }
  return value;
}

static void encode_legacy_words(uint8_t *output, const uint32_t *words,
 size_t word_count)
{
  size_t i;

  for(i = 0; i < word_count; i++)
    write_u32(output + i * 4, words[i]);
}

static void assert_models_equal(const ConfigModel *left,
 const ConfigModel *right)
{
  assert(memcmp(left, right, sizeof(*left)) == 0);
}

static void test_defaults_and_versioned_round_trip(void)
{
  ConfigModel original;
  ConfigModel decoded;
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  size_t written = 0;

  config_model_defaults(&original);
  assert(original.screen_scale == 6);
  assert(original.screen_magnification == 170);
  assert(original.frameskip_value == 9);
  assert(original.clock_speed == 3);
  assert(original.sound_volume == 10);
  assert(original.ram_dynarec_policy == 2);
  assert(original.hblank_window_start == 1);
  assert(original.hblank_window_end == 160);
  assert(original.fast_forward_profile == 0);
  assert(original.fast_forward_game_override == 0);
  assert(original.fast_forward_activation == 0);
  assert(original.fast_forward_speed == 2);
  assert(original.fast_forward_presentation == 1);
  assert(original.fast_forward_audio == 0);
  assert(original.gamepad[0] == 11);

  original.screen_filter = 0;
  original.language = 4;
  original.theme = 8;
  original.gamepad[15] = 19;
  original.fast_forward_profile = 2;
  original.fast_forward_game_override = 1;
  original.fast_forward_activation = 1;
  original.fast_forward_speed = 1;
  original.fast_forward_presentation = 2;
  original.fast_forward_audio = 1;

  assert(config_model_encode(&original, encoded, sizeof(encoded), &written));
  assert(written == CONFIG_MODEL_ENCODED_SIZE);
  memset(&decoded, 0xA5, sizeof(decoded));
  assert(config_model_decode(encoded, written, &decoded) ==
         CONFIG_MODEL_DECODE_VERSIONED);
  assert_models_equal(&original, &decoded);

  encoded[CONFIG_MODEL_ENCODED_SIZE - 1] ^= 0x40;
  memset(&decoded, 0xA5, sizeof(decoded));
  assert(config_model_decode(encoded, written, &decoded) ==
         CONFIG_MODEL_DECODE_INVALID);
  assert(decoded.screen_scale == 0xA5A5A5A5U);
}

/* A hand-built version 1 file (header: magic, version, value count,
   checksum, payload bytes) carrying the first 39 values: the fast-forward
   fields that version 2 added must decode as their defaults. */
static void test_previous_versioned_migration(void)
{
  enum { previous_count = 39, previous_size = 16 + previous_count * 4 };
  uint8_t encoded[previous_size];
  uint8_t current[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel defaults;
  ConfigModel decoded;
  size_t written;
  size_t i;

  config_model_defaults(&defaults);
  assert(config_model_encode(&defaults, current, sizeof(current), &written));
  memset(encoded, 0, sizeof(encoded));
  memcpy(encoded, "GBRC", 4);
  write_u16(encoded + 4, 1);
  write_u16(encoded + 6, previous_count);
  write_u32(encoded + 12, previous_count * 4);
  for(i = 0; i < previous_count; i++)
    memcpy(encoded + 16 + i * 4, current + 16 + i * 4, 4);
  write_u32(encoded + 8, config_checksum(encoded, sizeof(encoded)));

  assert(config_model_decode(encoded, sizeof(encoded), &decoded) ==
   CONFIG_MODEL_DECODE_VERSIONED);
  assert(decoded.fast_forward_profile == 0);
  assert(decoded.fast_forward_game_override == 0);
  assert(decoded.fast_forward_activation == 0);
  assert(decoded.fast_forward_speed == 2);
  assert(decoded.fast_forward_presentation == 1);
  assert(decoded.fast_forward_audio == 0);
}

/* Out-of-range fast-forward values reset only themselves to the default;
   valid neighbours are kept. */
static void test_fast_forward_fields_fall_back_independently(void)
{
  ConfigModel model;
  ConfigModel decoded;
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  size_t written;

  config_model_defaults(&model);
  model.screen_scale = 5;
  model.fast_forward_profile = 99;
  model.fast_forward_game_override = 1;
  model.fast_forward_activation = 99;
  model.fast_forward_speed = 1;
  model.fast_forward_presentation = 99;
  model.fast_forward_audio = 1;
  assert(config_model_encode(&model, encoded, sizeof(encoded), &written));
  assert(config_model_decode(encoded, written, &decoded) ==
   CONFIG_MODEL_DECODE_VERSIONED);
  assert(decoded.screen_scale == 5);
  assert(decoded.fast_forward_profile == 0);
  assert(decoded.fast_forward_game_override == 1);
  assert(decoded.fast_forward_activation == 0);
  assert(decoded.fast_forward_speed == 1);
  assert(decoded.fast_forward_presentation == 1);
  assert(decoded.fast_forward_audio == 1);
}

/* Headerless 39-word file from the last release before the versioned
   format: valid words are taken in place (word 9, the old RAM dynarec
   setting, is translated), then out-of-range words fall back one by one. */
static void test_current_binary_migration_and_field_validation(void)
{
  uint32_t words[CONFIG_MODEL_LEGACY_CURRENT_WORDS] = { 0 };
  uint8_t encoded[CONFIG_MODEL_LEGACY_CURRENT_WORDS * 4];
  ConfigModel decoded;
  size_t i;

  words[0] = 5;
  words[1] = 200;
  words[2] = 0;
  words[3] = 1;
  words[4] = 2;
  words[5] = 8;
  words[6] = 2;
  words[7] = 7;
  words[8] = 0;
  words[9] = 4;
  words[10] = 0;
  words[11] = 1;
  words[12] = 1;
  words[13] = 0;
  words[14] = 1;
  words[15] = 1;
  words[16] = 7;
  words[17] = 4;
  words[18] = 20;
  words[19] = 180;
  words[20] = 1;
  words[21] = 1;
  words[22] = 8;
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
    words[23 + i] = (uint32_t)i;

  encode_legacy_words(encoded, words, CONFIG_MODEL_LEGACY_CURRENT_WORDS);
  assert(config_model_decode(encoded, sizeof(encoded), &decoded) ==
         CONFIG_MODEL_DECODE_LEGACY);
  assert(decoded.screen_scale == 5);
  assert(decoded.ram_dynarec_policy == 0);
  assert(decoded.hblank_window_end == 180);
  assert(decoded.theme == 8);
  assert(decoded.gamepad[15] == 15);

  words[0] = 99;
  words[5] = 99;
  words[9] = 99;
  words[17] = 99;
  words[23] = 99;
  encode_legacy_words(encoded, words, CONFIG_MODEL_LEGACY_CURRENT_WORDS);
  assert(config_model_decode(encoded, sizeof(encoded), &decoded) ==
         CONFIG_MODEL_DECODE_LEGACY);
  assert(decoded.screen_scale == 6);
  assert(decoded.frameskip_value == 9);
  assert(decoded.ram_dynarec_policy == 2);
  assert(decoded.language == 1);
  assert(decoded.gamepad[0] == 11);
}

/* 37-word and 32-word files from older releases; settings those releases
   did not have keep their defaults. */
static void test_older_binary_layouts(void)
{
  uint32_t words[37] = { 0 };
  uint8_t encoded[37 * 4];
  ConfigModel decoded;
  size_t i;

  config_model_defaults(&decoded);
  words[0] = 1;
  words[1] = 150;
  words[4] = 1;
  words[5] = 3;
  words[6] = 3;
  words[7] = 10;
  words[8] = 1;
  words[9] = 1;
  words[10] = 1;
  words[18] = 4;
  words[19] = 100;
  words[20] = 1;
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
    words[21 + i] = 19;

  encode_legacy_words(encoded, words, 37);
  assert(config_model_decode(encoded, sizeof(encoded), &decoded) ==
         CONFIG_MODEL_DECODE_LEGACY);
  assert(decoded.ram_dynarec_policy == 2);
  assert(decoded.hblank_window_start == 4);
  assert(decoded.psp_vsync == 1);
  assert(decoded.swap_confirm_buttons == 0);
  assert(decoded.theme == 0);
  assert(decoded.gamepad[0] == 19);

  memset(words, 0, sizeof(words));
  words[0] = 2;
  words[1] = 120;
  words[4] = 0;
  words[5] = 9;
  words[6] = 3;
  words[7] = 10;
  words[8] = 1;
  words[9] = 1;
  words[10] = 1;
  words[11] = 0;
  words[12] = 1;
  words[13] = 4;
  words[14] = 1;
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
    words[15 + i] = (uint32_t)i;
  encode_legacy_words(encoded, words, 32);
  assert(config_model_decode(encoded, 32 * 4, &decoded) ==
         CONFIG_MODEL_DECODE_LEGACY);
  assert(decoded.video_renderer == 1);
  assert(decoded.oam_hijacking == 0);
  assert(decoded.ram_dynarec_policy == 2);
  assert(decoded.hblank_window_start == 1);
  assert(decoded.hblank_window_end == 160);
  assert(decoded.gamepad[15] == 15);
}

/* The 23-word per-game file: display, frameskip, clock, volume and the 16
   button mappings only. */
static void test_game_binary_layout(void)
{
  uint32_t words[23] = { 0 };
  uint8_t encoded[23 * 4];
  ConfigModel decoded;
  size_t i;

  words[0] = 6;
  words[1] = 190;
  words[2] = 0;
  words[3] = 2;
  words[4] = 4;
  words[5] = 2;
  words[6] = 8;
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
    words[7 + i] = (uint32_t)(15 - i);
  encode_legacy_words(encoded, words, 23);

  assert(config_model_decode(encoded, sizeof(encoded), &decoded) ==
         CONFIG_MODEL_DECODE_GAME_LEGACY);
  assert(decoded.screen_scale == 6);
  assert(decoded.screen_magnification == 190);
  assert(decoded.screen_filter == 0);
  assert(decoded.frameskip_type == 2);
  assert(decoded.frameskip_value == 4);
  assert(decoded.clock_speed == 2);
  assert(decoded.sound_volume == 8);
  assert(decoded.gamepad[0] == 15);
  assert(decoded.gamepad[15] == 0);
  assert(decoded.theme == 0);
}

static void test_rejects_unknown_or_truncated_data(void)
{
  uint8_t bytes[CONFIG_MODEL_ENCODED_SIZE] = { 0 };
  ConfigModel model;

  /* 7 bytes and 38 words match no known layout. */
  assert(config_model_decode(bytes, 7, &model) == CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(bytes, 38 * 4, &model) ==
         CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(NULL, 0, &model) == CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(bytes, sizeof(bytes), NULL) ==
         CONFIG_MODEL_DECODE_INVALID);
}

/* Runs every codec case; a failing assert aborts the run. */
int main(void)
{
  test_defaults_and_versioned_round_trip();
  test_previous_versioned_migration();
  test_fast_forward_fields_fall_back_independently();
  test_current_binary_migration_and_field_validation();
  test_older_binary_layouts();
  test_game_binary_layout();
  test_rejects_unknown_or_truncated_data();
  puts("config model tests passed");
  return 0;
}
