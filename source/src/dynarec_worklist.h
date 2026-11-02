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
 * Growable LIFO of translated branches whose target block is still to be
 * looked up. cpu.c queues direct exits while translating and drains the list
 * in process_deferred_exits once the outermost translation is done.
 */

#ifndef DYNAREC_WORKLIST_H
#define DYNAREC_WORKLIST_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
  void *branch_source;     /* emitted jump placeholder to patch */
  uint32_t branch_target;  /* GBA address the branch goes to */
  uint8_t thumb;           /* target is Thumb code */
} DynarecWorkItem;

typedef struct
{
  DynarecWorkItem *items;
  size_t count;
  size_t capacity;
} DynarecWorklist;

/* push returns 0 if the list cannot grow; pop returns 0 when it is empty. */
int dynarec_worklist_push(DynarecWorklist *worklist, void *branch_source,
 uint32_t branch_target, int thumb);
int dynarec_worklist_pop(DynarecWorklist *worklist, DynarecWorkItem *item);
void dynarec_worklist_clear(DynarecWorklist *worklist);
void dynarec_worklist_destroy(DynarecWorklist *worklist);

#endif /* DYNAREC_WORKLIST_H */
