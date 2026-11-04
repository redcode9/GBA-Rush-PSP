/*
 * ArmMips host on Unicorn: see mips_unicorn.h. Generated code calls
 * arm_mips_access through a trap address, where a code hook runs the C
 * function and returns to ra; entering generated code is a uc_emu_start
 * that stops when the trampoline returns to STOP.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include "mips_unicorn.h"

#define TRAP 0x08700000u
#define STOP 0x08700100u
#define STACK_TOP 0x08680000u
#define PAGE 16384u

typedef struct
{
  const uint8_t *host;
  size_t bytes;
  uint32_t target;
} Mapping;

static uc_engine *uc;
static Mapping mappings[32];
static unsigned mapping_count;
static ArmMips *running;
static void (*block_callback)(uint32_t address, uint32_t size);
unsigned long long mips_unicorn_executed;
unsigned long long mips_unicorn_calls;
unsigned long long mips_unicorn_region_calls[16][2];

static void check(uc_err e, const char *what)
{
  if(e != UC_ERR_OK)
  {
    fprintf(stderr, "unicorn: %s: %s\n", what, uc_strerror(e));
    exit(2);
  }
}

static uint32_t reg(int id)
{
  uint32_t value = 0;

  uc_reg_read(uc, id, &value);
  return value;
}

static void set_reg(int id, uint32_t value)
{
  uc_reg_write(uc, id, &value);
}

static void on_trap(uc_engine *u, uint64_t address, uint32_t size,
 void *data)
{
  (void)u; (void)address; (void)size; (void)data;
  mips_unicorn_calls++;
  mips_unicorn_region_calls[(reg(UC_MIPS_REG_A1) >> 24) & 15]
   [(reg(UC_MIPS_REG_A3) & 0xFF) >= ARM_IR_ST8]++;
  set_reg(UC_MIPS_REG_V0, arm_mips_access(&running->core,
   reg(UC_MIPS_REG_A1), reg(UC_MIPS_REG_A2), reg(UC_MIPS_REG_A3)));
  set_reg(UC_MIPS_REG_PC, reg(UC_MIPS_REG_RA));
}

static void on_block(uc_engine *u, uint64_t address, uint32_t size,
 void *data)
{
  (void)u; (void)data;
  mips_unicorn_executed += size / 4;
  if(block_callback)
    block_callback((uint32_t)address, size);
}

void *mips_unicorn_alloc(size_t bytes)
{
  void *p = NULL;

  bytes = (bytes + PAGE - 1) & ~(size_t)(PAGE - 1);
  if(posix_memalign(&p, PAGE, bytes))
  {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  memset(p, 0, bytes);
  return p;
}

void mips_unicorn_map(void *host, size_t bytes, uint32_t target)
{
  Mapping *m = &mappings[mapping_count++];

  bytes = (bytes + 4095) & ~(size_t)4095;
  check(uc_mem_map_ptr(uc, target, bytes, UC_PROT_ALL, host), "map");
  m->host = host;
  m->bytes = bytes;
  m->target = target;
}

static uint32_t target(void *context, const void *host)
{
  const uint8_t *p = host;
  unsigned i;

  (void)context;
  for(i = 0; i < mapping_count; i++)
    if(p >= mappings[i].host && p < mappings[i].host + mappings[i].bytes)
      return mappings[i].target + (uint32_t)(p - mappings[i].host);
  fprintf(stderr, "unicorn: host address %p is not mapped\n", host);
  exit(2);
}

static void sync_code(void *context, void *code, uint32_t bytes)
{
  uint32_t at = target(context, code);

  check(uc_ctl_remove_cache(uc, at, at + bytes), "remove cache");
}

static uint32_t enter(void *context, uint32_t entry, uint32_t core,
 uint32_t code)
{
  uc_err e;

  (void)context;
  set_reg(UC_MIPS_REG_A0, core);
  set_reg(UC_MIPS_REG_A1, code);
  set_reg(UC_MIPS_REG_SP, STACK_TOP);
  set_reg(UC_MIPS_REG_RA, STOP);
  e = uc_emu_start(uc, entry, STOP, 0, 0);
  if(e != UC_ERR_OK)
  {
    fprintf(stderr, "unicorn: %s at pc %08X (entered at %08X)\n",
     uc_strerror(e), reg(UC_MIPS_REG_PC), code);
    exit(2);
  }
  return reg(UC_MIPS_REG_V0);
}

void mips_unicorn_init(void)
{
  uc_hook hook;

  check(uc_open(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN, &uc),
   "open");
  /* A MIPS32r2 core, for ext, ins, rotr and rotrv. */
  check(uc_ctl_set_cpu_model(uc, UC_CPU_MIPS32_24KF), "cpu model");
  check(uc_mem_map(uc, TRAP, 4096, UC_PROT_ALL), "trap page");
  check(uc_mem_map(uc, STACK_TOP - 65536, 65536, UC_PROT_ALL), "stack");
  check(uc_hook_add(uc, &hook, UC_HOOK_CODE, (void *)on_trap, NULL, TRAP,
   TRAP + 3), "trap hook");
}

ArmMipsHost mips_unicorn_host(ArmMips *jit)
{
  ArmMipsHost host;

  running = jit;
  host.context = NULL;
  host.target = target;
  host.sync = sync_code;
  host.enter = enter;
  host.access = TRAP;
  return host;
}

void mips_unicorn_count(void (*block)(uint32_t address, uint32_t size))
{
  static uc_hook hook;
  static int counting;

  block_callback = block;
  if(!counting)
    check(uc_hook_add(uc, &hook, UC_HOOK_BLOCK, (void *)on_block, NULL, 1,
     0), "block hook");
  counting = 1;
}
