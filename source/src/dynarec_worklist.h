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

#ifndef DYNAREC_WORKLIST_H
#define DYNAREC_WORKLIST_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
  void *branch_source;
  uint32_t branch_target;
  uint8_t thumb;
} DynarecWorkItem;

typedef struct
{
  DynarecWorkItem *items;
  size_t count;
  size_t capacity;
} DynarecWorklist;

int dynarec_worklist_push(DynarecWorklist *worklist, void *branch_source,
 uint32_t branch_target, int thumb);
int dynarec_worklist_pop(DynarecWorklist *worklist, DynarecWorkItem *item);
void dynarec_worklist_clear(DynarecWorklist *worklist);
void dynarec_worklist_destroy(DynarecWorklist *worklist);

#endif /* DYNAREC_WORKLIST_H */
