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

/* SELECT+L saves, SELECT+R loads, START+SELECT opens the menu (bit i is
   gamepad index i: 4 L, 5 R, 10 SELECT, 11 START); slots 3-7 are empty. */
static const uint32_t default_shortcut[CONFIG_MODEL_SHORTCUT_COUNT] =
{
  (1u << 10) | (1u << 4), (1u << 10) | (1u << 5), (1u << 11) | (1u << 10),
  0, 0, 0, 0, 0
};

static void assert_default_shortcuts(const ConfigModel *model)
{
  assert(memcmp(model->shortcut, default_shortcut,
   sizeof(default_shortcut)) == 0);
}

/* The turbo default: 30 presses per second (speed 0), Hold (mode 0). */
static void set_default_turbo(ConfigModel *model)
{
  memset(model->turbo_speed, 0, sizeof(model->turbo_speed));
  memset(model->turbo_mode, 0, sizeof(model->turbo_mode));
}

/* Gives every turbo field a valid value that is not its default. */
static void set_custom_turbo(ConfigModel *model)
{
  model->turbo_speed[0] = 2;
  model->turbo_speed[1] = 1;
  model->turbo_speed[3] = 2;
  model->turbo_mode[0] = 1;
  model->turbo_mode[2] = 1;
  model->turbo_mode[3] = 1;
}

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

/* Forges a valid versioned file (header: magic, version, value count,
   checksum, payload bytes) holding the first value_count values of model;
   values past the current count are zeros. Returns the file size. */
static size_t forge_versioned(uint8_t *output, uint16_t version,
 uint16_t value_count, const ConfigModel *model)
{
  uint8_t current[CONFIG_MODEL_ENCODED_SIZE];
  size_t size = 16 + (size_t)value_count * 4;
  size_t copied = value_count;
  size_t written;

  if(copied > CONFIG_MODEL_VALUE_COUNT)
    copied = CONFIG_MODEL_VALUE_COUNT;
  assert(config_model_encode(model, current, sizeof(current), &written));
  memset(output, 0, size);
  memcpy(output, "GBRC", 4);
  write_u16(output + 4, version);
  write_u16(output + 6, value_count);
  write_u32(output + 12, (uint32_t)value_count * 4);
  memcpy(output + 16, current + 16, copied * 4);
  write_u32(output + 8, config_checksum(output, size));
  return size;
}

/* Per-game files use the same codec, so this also covers their shortcuts.
   Also flips a bit in the last byte: decoding must then fail and leave the
   output untouched. */
static void test_defaults_and_versioned_round_trip(void)
{
  ConfigModel original;
  ConfigModel decoded;
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  size_t written = 0;
  size_t i;

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
  assert(original.cpu_core == 0);
  assert(original.browser_view == 0);
  assert(original.gamepad[0] == 11);
  assert_default_shortcuts(&original);
  for(i = 0; i < CONFIG_MODEL_TURBO_COUNT; i++)
    assert(original.turbo_speed[i] == 0 && original.turbo_mode[i] == 0);

  original.screen_filter = 0;
  original.video_renderer = 2;
  original.language = 4;
  original.theme = 8;
  original.gamepad[15] = 19;
  original.fast_forward_profile = 2;
  original.fast_forward_game_override = 1;
  original.fast_forward_activation = 1;
  original.fast_forward_speed = 1;
  original.fast_forward_presentation = 2;
  original.fast_forward_audio = 1;
  original.cpu_core = 1;
  original.browser_view = 1;
  original.shortcut[0] = (1u << 11) | (1u << 0);
  original.shortcut[2] = 0;
  original.shortcut[7] = 0xF000;
  set_custom_turbo(&original);

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

/* A version 1 file carrying the first 39 values: the fast-forward fields
   that version 2 added must decode as their defaults. */
static void test_previous_versioned_migration(void)
{
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel defaults;
  ConfigModel decoded;
  size_t size;

  config_model_defaults(&defaults);
  size = forge_versioned(encoded, 1, 39, &defaults);

  assert(config_model_decode(encoded, size, &decoded) ==
   CONFIG_MODEL_DECODE_VERSIONED);
  assert(decoded.fast_forward_profile == 0);
  assert(decoded.fast_forward_game_override == 0);
  assert(decoded.fast_forward_activation == 0);
  assert(decoded.fast_forward_speed == 2);
  assert(decoded.fast_forward_presentation == 1);
  assert(decoded.fast_forward_audio == 0);
  assert(decoded.cpu_core == 0);
  assert(decoded.browser_view == 0);
  assert_default_shortcuts(&decoded);
}

/* A version 2 file (1.4.x) carries 45 values: the CPU core, the browser
   view, the shortcuts and the turbo settings that version 3 added decode
   as Classic, the list, the default shortcuts and 30 per second Hold;
   every value the file has is kept. */
static void test_version_2_migration(void)
{
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel model;
  ConfigModel expected;
  ConfigModel decoded;
  size_t size;

  config_model_defaults(&model);
  model.screen_scale = 5;
  model.theme = 8;
  model.gamepad[15] = 19;
  model.fast_forward_audio = 1;
  model.cpu_core = 1;
  model.browser_view = 1;
  model.shortcut[0] = 0x0003;
  model.shortcut[3] = 0x0003;
  set_custom_turbo(&model);
  size = forge_versioned(encoded, 2, 45, &model);

  assert(config_model_decode(encoded, size, &decoded) ==
   CONFIG_MODEL_DECODE_VERSIONED);
  expected = model;
  expected.cpu_core = 0;
  expected.browser_view = 0;
  memcpy(expected.shortcut, default_shortcut, sizeof(default_shortcut));
  set_default_turbo(&expected);
  assert_models_equal(&expected, &decoded);
}

/* Earlier builds of version 3 wrote 46 values, ending at the CPU core: the
   browser view decodes as the list, the shortcuts as their defaults and
   the turbo settings as 30 per second Hold; the rest is kept. */
static void test_version_3_without_browser_view(void)
{
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel model;
  ConfigModel expected;
  ConfigModel decoded;
  size_t size;

  config_model_defaults(&model);
  model.theme = 3;
  model.fast_forward_speed = 1;
  model.cpu_core = 1;
  model.browser_view = 1;
  model.shortcut[1] = 0x0003;
  set_custom_turbo(&model);
  size = forge_versioned(encoded, 3, 46, &model);

  assert(config_model_decode(encoded, size, &decoded) ==
   CONFIG_MODEL_DECODE_VERSIONED);
  expected = model;
  expected.browser_view = 0;
  memcpy(expected.shortcut, default_shortcut, sizeof(default_shortcut));
  set_default_turbo(&expected);
  assert_models_equal(&expected, &decoded);
}

/* Builds of version 3 with the browser view wrote 47 values: it is kept
   and the shortcuts decode as their defaults. */
static void test_version_3_without_shortcuts(void)
{
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel model;
  ConfigModel expected;
  ConfigModel decoded;
  size_t size;

  config_model_defaults(&model);
  model.browser_view = 1;
  model.shortcut[2] = 0x0003;
  size = forge_versioned(encoded, 3, 47, &model);

  assert(config_model_decode(encoded, size, &decoded) ==
   CONFIG_MODEL_DECODE_VERSIONED);
  expected = model;
  memcpy(expected.shortcut, default_shortcut, sizeof(default_shortcut));
  assert_models_equal(&expected, &decoded);
}

/* Builds of version 3 with the shortcuts wrote 55 values: they are kept
   and the turbo settings decode as 30 per second Hold. */
static void test_version_3_without_turbo(void)
{
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel model;
  ConfigModel expected;
  ConfigModel decoded;
  size_t size;

  config_model_defaults(&model);
  model.browser_view = 1;
  model.shortcut[2] = 0x0003;
  set_custom_turbo(&model);
  size = forge_versioned(encoded, 3, 55, &model);

  assert(config_model_decode(encoded, size, &decoded) ==
   CONFIG_MODEL_DECODE_VERSIONED);
  expected = model;
  set_default_turbo(&expected);
  assert_models_equal(&expected, &decoded);
}

/* A turbo speed is 0-2 and a mode 0-1, per button; any other stored value
   reads back as 30 per second or Hold and leaves the other fields alone. */
static void test_turbo_values(void)
{
  static const uint32_t stored[] = { 0, 1, 2, 3, 99, 0xFFFFFFFFU };
  static const uint32_t speed[] = { 0, 1, 2, 0, 0, 0 };
  static const uint32_t mode[] = { 0, 1, 0, 0, 0, 0 };
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel model;
  ConfigModel expected;
  ConfigModel decoded;
  size_t written;
  size_t button;
  size_t i;

  for(button = 0; button < CONFIG_MODEL_TURBO_COUNT; button++)
  {
    for(i = 0; i < sizeof(stored) / sizeof(stored[0]); i++)
    {
      config_model_defaults(&model);
      set_custom_turbo(&model);
      model.shortcut[7] = 0x0003;
      expected = model;
      model.turbo_speed[button] = stored[i];
      expected.turbo_speed[button] = speed[i];
      assert(config_model_encode(&model, encoded, sizeof(encoded), &written));
      assert(config_model_decode(encoded, written, &decoded) ==
       CONFIG_MODEL_DECODE_VERSIONED);
      assert_models_equal(&expected, &decoded);

      model = expected;
      model.turbo_mode[button] = stored[i];
      expected.turbo_mode[button] = mode[i];
      assert(config_model_encode(&model, encoded, sizeof(encoded), &written));
      assert(config_model_decode(encoded, written, &decoded) ==
       CONFIG_MODEL_DECODE_VERSIONED);
      assert_models_equal(&expected, &decoded);
    }
  }
}

/* A stored shortcut is kept when it is 0 or 1-4 of the 16 inputs; a mask
   past bit 15 or with 5 or more keys reads back as that slot's default and
   leaves the other slots alone. */
static void test_shortcut_values(void)
{
  static const uint32_t stored[] =
  {
    0, 0x0001, 0x8421, 0xF000, 0x10000, 0x10001, 0x001F, 0xFFFF, 0xFFFFFFFFU
  };
  static const int kept[] = { 1, 1, 1, 1, 0, 0, 0, 0, 0 };
  static const size_t slots[] = { 0, 2, 5 };
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel model;
  ConfigModel decoded;
  size_t written;
  size_t i;
  size_t s;

  for(s = 0; s < sizeof(slots) / sizeof(slots[0]); s++)
  {
    for(i = 0; i < sizeof(stored) / sizeof(stored[0]); i++)
    {
      config_model_defaults(&model);
      model.browser_view = 1;
      model.shortcut[slots[s]] = stored[i];
      assert(config_model_encode(&model, encoded, sizeof(encoded), &written));
      assert(config_model_decode(encoded, written, &decoded) ==
       CONFIG_MODEL_DECODE_VERSIONED);
      if(!kept[i])
        model.shortcut[slots[s]] = default_shortcut[slots[s]];
      assert_models_equal(&model, &decoded);
    }
  }
}

/* The browser view is 0 (list) or 1 (carousel); any other stored value
   reads back as the list and leaves its neighbours alone. */
static void test_browser_view_values(void)
{
  static const uint32_t stored[] = { 0, 1, 2, 7 };
  static const uint32_t expected[] = { 0, 1, 0, 0 };
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel model;
  ConfigModel decoded;
  size_t written;
  size_t i;

  for(i = 0; i < sizeof(stored) / sizeof(stored[0]); i++)
  {
    config_model_defaults(&model);
    model.cpu_core = 1;
    model.browser_view = stored[i];
    assert(config_model_encode(&model, encoded, sizeof(encoded), &written));
    assert(config_model_decode(encoded, written, &decoded) ==
     CONFIG_MODEL_DECODE_VERSIONED);
    assert(decoded.browser_view == expected[i]);
    assert(decoded.cpu_core == 1);
  }
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
  words[10] = 3;
  words[17] = 99;
  words[23] = 99;
  encode_legacy_words(encoded, words, CONFIG_MODEL_LEGACY_CURRENT_WORDS);
  assert(config_model_decode(encoded, sizeof(encoded), &decoded) ==
         CONFIG_MODEL_DECODE_LEGACY);
  assert(decoded.screen_scale == 6);
  assert(decoded.frameskip_value == 9);
  assert(decoded.ram_dynarec_policy == 2);
  assert(decoded.video_renderer == 1);
  assert(decoded.language == 1);
  assert(decoded.gamepad[0] == 11);
  assert_default_shortcuts(&decoded);
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
  assert_default_shortcuts(&decoded);
}

static void test_rejects_unknown_or_truncated_data(void)
{
  uint8_t bytes[CONFIG_MODEL_ENCODED_SIZE] = { 0 };
  uint8_t longer[CONFIG_MODEL_ENCODED_SIZE + 4];
  ConfigModel defaults;
  ConfigModel model;

  /* 7 bytes and 38 words match no known layout. */
  assert(config_model_decode(bytes, 7, &model) == CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(bytes, 38 * 4, &model) ==
         CONFIG_MODEL_DECODE_INVALID);

  /* Version 3 files hold 46 to CONFIG_MODEL_VALUE_COUNT values. */
  config_model_defaults(&defaults);
  assert(config_model_decode(longer,
   forge_versioned(longer, 3, 45, &defaults), &model) ==
   CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(longer,
   forge_versioned(longer, 3, CONFIG_MODEL_VALUE_COUNT + 1, &defaults),
   &model) == CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(NULL, 0, &model) == CONFIG_MODEL_DECODE_INVALID);
  assert(config_model_decode(bytes, sizeof(bytes), NULL) ==
         CONFIG_MODEL_DECODE_INVALID);
}

/* Runs every codec case; a failing assert aborts the run. */
int main(void)
{
  test_defaults_and_versioned_round_trip();
  test_previous_versioned_migration();
  test_version_2_migration();
  test_version_3_without_browser_view();
  test_version_3_without_shortcuts();
  test_version_3_without_turbo();
  test_browser_view_values();
  test_shortcut_values();
  test_turbo_values();
  test_fast_forward_fields_fall_back_independently();
  test_current_binary_migration_and_field_validation();
  test_older_binary_layouts();
  test_game_binary_layout();
  test_rejects_unknown_or_truncated_data();
  puts("config model tests passed");
  return 0;
}
