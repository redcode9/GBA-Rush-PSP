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
  assert(original.gamepad[0] == 11);

  original.screen_filter = 0;
  original.language = 4;
  original.theme = 8;
  original.gamepad[15] = 19;

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

static void test_rejects_unknown_or_truncated_data(void)
{
  uint8_t bytes[CONFIG_MODEL_ENCODED_SIZE] = { 0 };
  ConfigModel model;

  assert(config_model_decode(bytes, 7, &model) == CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(bytes, 38 * 4, &model) ==
         CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(NULL, 0, &model) == CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(bytes, sizeof(bytes), NULL) ==
         CONFIG_MODEL_DECODE_INVALID);
}

int main(void)
{
  test_defaults_and_versioned_round_trip();
  test_current_binary_migration_and_field_validation();
  test_older_binary_layouts();
  test_rejects_unknown_or_truncated_data();
  puts("config model tests passed");
  return 0;
}
