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

/*
 * Writes a game's settings file with the default settings and the given
 * CPU core (0 Classic, 1 New), so scripts/check-eboot.sh can start a game
 * on either core.
 *
 * Usage: game_config CORE FILE
 */

#include <stdio.h>
#include <stdlib.h>

#include "config_model.h"

int main(int argc, char **argv)
{
  ConfigModel model;
  uint8_t data[CONFIG_MODEL_ENCODED_SIZE];
  size_t size;
  FILE *file;

  if(argc != 3)
  {
    fprintf(stderr, "usage: game_config CORE FILE\n");
    return 2;
  }
  config_model_defaults(&model);
  model.cpu_core = (uint32_t)strtoul(argv[1], NULL, 10);
  if(!config_model_encode(&model, data, sizeof(data), &size))
    return 1;
  file = fopen(argv[2], "wb");
  if(!file || fwrite(data, 1, size, file) != size || fclose(file))
  {
    perror(argv[2]);
    return 1;
  }
  return 0;
}
