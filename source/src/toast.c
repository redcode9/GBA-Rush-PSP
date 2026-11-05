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

#include "toast.h"

#include <string.h>

#include "menu_layout.h"

static Toast toast_ring[TOAST_RING_SIZE];
static uint32_t toast_head;
static uint32_t toast_count;

static Toast *toast_at(uint32_t index)
{
  return &toast_ring[(toast_head + index) % TOAST_RING_SIZE];
}

/* The next toast becomes visible now, so it gets its whole duration. */
static void toast_drop_oldest(uint32_t now_us)
{
  toast_head = (toast_head + 1) % TOAST_RING_SIZE;
  toast_count--;
  if(toast_count != 0)
    toast_ring[toast_head].start_us = now_us;
}

void toast_push(const char *text, uint32_t duration_us, int sjis,
 uint32_t now_us)
{
  size_t length;
  uint32_t index;
  Toast *toast;

  if(text == NULL)
    return;

  /* Same clipping rule as the menu text: a byte is one column. */
  length = menu_text_clip_length_for_encoding(text, TOAST_TEXT_SIZE - 1,
   sjis ? MENU_TEXT_SHIFT_JIS : MENU_TEXT_GBK);

  for(index = 0; index < toast_count; index++)
  {
    toast = toast_at(index);
    if(memcmp(toast->text, text, length) == 0 && toast->text[length] == '\0')
    {
      toast->start_us = now_us;
      toast->duration_us = duration_us;
      return;
    }
  }

  if(toast_count == TOAST_RING_SIZE)
    toast_drop_oldest(now_us);

  toast = toast_at(toast_count);
  memcpy(toast->text, text, length);
  toast->text[length] = '\0';
  toast->start_us = now_us;
  toast->duration_us = duration_us;
  toast->sjis = sjis != 0;
  toast_count++;
}

const Toast *toast_current(uint32_t now_us)
{
  while(toast_count != 0)
  {
    const Toast *toast = &toast_ring[toast_head];

    /* Unsigned elapsed time stays right across the 2^32 us wrap. */
    if((uint32_t)(now_us - toast->start_us) < toast->duration_us)
      return toast;
    toast_drop_oldest(now_us);
  }

  return NULL;
}

void toast_clear(void)
{
  toast_head = 0;
  toast_count = 0;
}
