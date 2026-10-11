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

#include "link_audio_wait.h"

#include <stddef.h>

void link_audio_wait_init(LinkAudioWait *wait)
{
  if(wait == NULL)
    return;
  wait->active = 0;
  wait->saved_pause = 0;
}

int link_audio_wait_begin(LinkAudioWait *wait, uint32_t current_pause,
 uint32_t *next_pause)
{
  if(wait == NULL || next_pause == NULL || wait->active)
    return 0;
  wait->saved_pause = current_pause;
  wait->active = 1;
  *next_pause = 1;
  return 1;
}

int link_audio_wait_end(LinkAudioWait *wait, uint32_t *next_pause)
{
  if(wait == NULL || next_pause == NULL || !wait->active)
    return 0;
  *next_pause = wait->saved_pause;
  wait->active = 0;
  return 1;
}
