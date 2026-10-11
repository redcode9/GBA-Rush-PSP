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

#ifndef LINK_AUDIO_WAIT_H
#define LINK_AUDIO_WAIT_H

#include <stdint.h>

typedef struct
{
  uint32_t active;
  uint32_t saved_pause;
} LinkAudioWait;

void link_audio_wait_init(LinkAudioWait *wait);
int link_audio_wait_begin(LinkAudioWait *wait, uint32_t current_pause,
 uint32_t *next_pause);
int link_audio_wait_end(LinkAudioWait *wait, uint32_t *next_pause);

#endif /* LINK_AUDIO_WAIT_H */
