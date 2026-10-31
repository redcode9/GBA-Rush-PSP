/*
 * Host tests for dynarec_worklist.c, the growable stack of block exits the
 * dynarec still has to link (cpu.c process_deferred_exits()). It must pop in
 * LIFO order, grow past its initial capacity, and keep its buffer on clear.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "dynarec_worklist.h"

static void test_empty_worklist(void)
{
  DynarecWorklist worklist = { 0 };
  DynarecWorkItem item;

  assert(!dynarec_worklist_pop(&worklist, &item));
  dynarec_worklist_destroy(&worklist);
}

static void test_deep_chain_uses_heap_storage(void)
{
  DynarecWorklist worklist = { 0 };
  DynarecWorkItem item;
  uintptr_t i;

  /* A long chain of untranslated exits, far beyond the initial 128 slots. */
  for(i = 0; i < 10000; i++)
  {
    assert(dynarec_worklist_push(&worklist, (void *)(i + 1),
     (uint32_t)(0x08000000U + i * 2U), i & 1U));
  }

  assert(worklist.count == 10000);
  for(i = 10000; i > 0; i--)
  {
    assert(dynarec_worklist_pop(&worklist, &item));
    assert(item.branch_source == (void *)i);
    assert(item.branch_target == (uint32_t)(0x08000000U + (i - 1) * 2U));
    assert(item.thumb == ((i - 1) & 1U));
  }

  assert(!dynarec_worklist_pop(&worklist, &item));
  dynarec_worklist_destroy(&worklist);
}

static void test_clear_keeps_allocation_reusable(void)
{
  DynarecWorklist worklist = { 0 };
  DynarecWorkItem item;
  size_t capacity;

  assert(dynarec_worklist_push(&worklist, (void *)1, 2, 0));
  capacity = worklist.capacity;
  dynarec_worklist_clear(&worklist);
  assert(worklist.count == 0);
  assert(worklist.capacity == capacity);
  assert(dynarec_worklist_push(&worklist, (void *)3, 4, 1));
  assert(dynarec_worklist_pop(&worklist, &item));
  assert(item.branch_source == (void *)3);
  assert(item.branch_target == 4);
  assert(item.thumb == 1);
  dynarec_worklist_destroy(&worklist);
}

int main(void)
{
  test_empty_worklist();
  test_deep_chain_uses_heap_storage();
  test_clear_keeps_allocation_reusable();
  puts("Dynarec worklist tests passed");
  return 0;
}
