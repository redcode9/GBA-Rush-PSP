#ifndef CONFIG_MODEL_H
#define CONFIG_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define CONFIG_MODEL_GAMEPAD_COUNT 16U
#define CONFIG_MODEL_VALUE_COUNT 45U
#define CONFIG_MODEL_LEGACY_CURRENT_WORDS 39U
#define CONFIG_MODEL_HEADER_SIZE 16U
#define CONFIG_MODEL_ENCODED_SIZE \
 (CONFIG_MODEL_HEADER_SIZE + CONFIG_MODEL_VALUE_COUNT * sizeof(uint32_t))

typedef enum
{
  CONFIG_MODEL_DECODE_INVALID = 0,
  CONFIG_MODEL_DECODE_VERSIONED,
  CONFIG_MODEL_DECODE_LEGACY,
  CONFIG_MODEL_DECODE_GAME_LEGACY
} ConfigModelDecodeResult;

typedef struct
{
  uint32_t screen_scale;
  uint32_t screen_magnification;
  uint32_t screen_filter;
  uint32_t show_fps;
  uint32_t frameskip_type;
  uint32_t frameskip_value;
  uint32_t clock_speed;
  uint32_t sound_volume;
  uint32_t stack_optimize;
  uint32_t ram_dynarec_policy;
  uint32_t video_renderer;
  uint32_t oam_hijacking;
  uint32_t boot_mode;
  uint32_t update_backup;
  uint32_t screen_capture_format;
  uint32_t enable_analog;
  uint32_t analog_sensitivity;
  uint32_t language;
  uint32_t hblank_window_start;
  uint32_t hblank_window_end;
  uint32_t psp_vsync;
  uint32_t swap_confirm_buttons;
  uint32_t theme;
  uint32_t gamepad[CONFIG_MODEL_GAMEPAD_COUNT];
  uint32_t fast_forward_profile;
  uint32_t fast_forward_game_override;
  uint32_t fast_forward_activation;
  uint32_t fast_forward_speed;
  uint32_t fast_forward_presentation;
  uint32_t fast_forward_audio;
} ConfigModel;

void config_model_defaults(ConfigModel *model);
int config_model_encode(const ConfigModel *model, uint8_t *output,
 size_t output_size, size_t *written);
ConfigModelDecodeResult config_model_decode(const uint8_t *data, size_t size,
 ConfigModel *model);

#endif
