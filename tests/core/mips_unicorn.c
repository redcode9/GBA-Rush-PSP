/*
 * ArmMips host on Unicorn: see mips_unicorn.h. Generated code calls
 * arm_mips_access through a trap address, where a code hook runs the C
 * function and returns to ra; entering generated code is a uc_emu_start
 * that stops when the trampoline returns to STOP. On a stock Unicorn an
 * interrupt hook runs the four Allegrex instructions it does not know.
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

/* Whether the word branches or jumps, so the next one is its delay slot. */
static int branches(uint32_t word)
{
  unsigned op = word >> 26;

  return (op >= 1 && op <= 7) || (op >= 0x14 && op <= 0x17) ||
   (op == 0 && ((word & 0x3F) == 0x08 || (word & 0x3F) == 0x09));
}

/* Stock Unicorn treats the Allegrex's clz, clo, max and min (SPECIAL
   functions 0x16, 0x17, 0x2C and 0x2D) as reserved instructions. Each ends
   its translation block, so a block hook knows where it is, as the
   exception loses the pc; the interrupt hook then runs it and moves the pc
   on. A code hook on every instruction would also work, but slows all code. */
static uint32_t block_end;

static void on_block_end(uc_engine *u, uint64_t address, uint32_t size,
 void *data)
{
  (void)u; (void)data;
  block_end = (uint32_t)address + size;
}

static void on_allegrex(uc_engine *u, uint32_t number, void *data)
{
  uint32_t pc = block_end - 4;
  uint32_t word = 0, previous = 0, s, t, value;
  unsigned rd;

  (void)data;
  /* 20 is the reserved instruction exception. */
  if(number != 20)
  {
    fprintf(stderr, "unicorn: CPU exception %u in the block ending at %08X\n",
     number, block_end);
    exit(2);
  }
  uc_mem_read(u, pc, &word, 4);
  rd = word >> 11 & 31;
  s = reg(UC_MIPS_REG_0 + (word >> 21 & 31));
  t = reg(UC_MIPS_REG_0 + (word >> 16 & 31));
  switch(word >> 26 ? 0 : word & 0x3F)
  {
    case 0x16: value = s ? (uint32_t)__builtin_clz(s) : 32; break;
    case 0x17: value = ~s ? (uint32_t)__builtin_clz(~s) : 32; break;
    case 0x2C: value = (int32_t)s > (int32_t)t ? s : t; break;
    case 0x2D: value = (int32_t)s < (int32_t)t ? s : t; break;
    default:
      fprintf(stderr, "unicorn: reserved instruction %08X at %08X\n", word,
       pc);
      exit(2);
  }
  /* The pc can only move on, so in a delay slot (arm_mips.c puts none of
     these there) the branch would be lost: stop instead. */
  uc_mem_read(u, pc - 4, &previous, 4);
  if(branches(previous))
  {
    fprintf(stderr, "unicorn: Allegrex instruction %08X in the delay slot "
     "at %08X\n", word, pc);
    exit(2);
  }
  if(rd)
    set_reg(UC_MIPS_REG_0 + rd, value);
  set_reg(UC_MIPS_REG_PC, pc + 4);
}

/* Whether this Unicorn runs max (scripts/build-unicorn.sh patches it in),
   tried on a throwaway engine so that the real one starts clean. */
static int knows_allegrex(void)
{
  static const uint32_t max = 2u << 21 | 3u << 11 | 0x2C;
  uc_engine *probe;
  uc_err e;

  check(uc_open(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN,
   &probe), "open");
  check(uc_ctl_set_cpu_model(probe, UC_CPU_MIPS32_24KF), "cpu model");
  check(uc_mem_map(probe, TRAP, 4096, UC_PROT_ALL), "probe page");
  check(uc_mem_write(probe, TRAP, &max, 4), "probe");
  e = uc_emu_start(probe, TRAP, TRAP + 4, 0, 1);
  uc_close(probe);
  return e == UC_ERR_OK;
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
  if(!knows_allegrex())
  {
    check(uc_hook_add(uc, &hook, UC_HOOK_BLOCK, (void *)on_block_end, NULL,
     1, 0), "block end hook");
    check(uc_hook_add(uc, &hook, UC_HOOK_INTR, (void *)on_allegrex, NULL, 1,
     0), "Allegrex hook");
  }
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
