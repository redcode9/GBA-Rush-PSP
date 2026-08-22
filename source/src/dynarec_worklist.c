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

#include "dynarec_worklist.h"

#include <stdlib.h>

#define DYNAREC_WORKLIST_INITIAL_CAPACITY 128

int dynarec_worklist_push(DynarecWorklist *worklist, void *branch_source,
 uint32_t branch_target, int thumb)
{
  DynarecWorkItem *items;
  size_t capacity;

  if(worklist == NULL)
    return 0;

  if(worklist->count == worklist->capacity)
  {
    capacity = worklist->capacity == 0
     ? DYNAREC_WORKLIST_INITIAL_CAPACITY : worklist->capacity * 2;
    if(capacity < worklist->capacity ||
     capacity > (size_t)-1 / sizeof(*worklist->items))
      return 0;

    items = (DynarecWorkItem *)realloc(worklist->items,
     capacity * sizeof(*worklist->items));
    if(items == NULL)
      return 0;

    worklist->items = items;
    worklist->capacity = capacity;
  }

  worklist->items[worklist->count].branch_source = branch_source;
  worklist->items[worklist->count].branch_target = branch_target;
  worklist->items[worklist->count].thumb = thumb != 0;
  worklist->count++;
  return 1;
}

int dynarec_worklist_pop(DynarecWorklist *worklist, DynarecWorkItem *item)
{
  if(worklist == NULL || item == NULL || worklist->count == 0)
    return 0;

  worklist->count--;
  *item = worklist->items[worklist->count];
  return 1;
}

void dynarec_worklist_clear(DynarecWorklist *worklist)
{
  if(worklist != NULL)
    worklist->count = 0;
}

void dynarec_worklist_destroy(DynarecWorklist *worklist)
{
  if(worklist == NULL)
    return;

  free(worklist->items);
  worklist->items = NULL;
  worklist->count = 0;
  worklist->capacity = 0;
}
