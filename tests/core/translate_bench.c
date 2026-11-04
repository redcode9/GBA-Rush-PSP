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
 * What translating real game code costs the new core, on the host. The
 * bench finds the blocks of a ROM and the BIOS without running them (see
 * crawl), then times the decoder (arm_translate) and the MIPS compiler
 * (arm_mips.c, included here for its compile function) over all of them.
 * It also counts the IR ops and MIPS words per guest instruction and
 * hashes the code, so a change meant to make the compiler faster can show
 * that it emits the same words. Nothing runs: no MIPS emulator is needed.
 *
 * Usage: translate_bench [--repeat N] [--blocks N] ROM BIOS
 * scripts/check-core.sh builds and runs it when BIOS and MKSC are set.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "arm_mips.c"

#define ROM_MAX 0x2000000u
#define BIOS_SIZE 0x4000u
#define SEEN (1u << 17)
#define CODE_BYTES (8u << 20)
#define POOL_OPS (1u << 21)

static uint8_t *rom;
static uint32_t rom_size;
static uint8_t bios[BIOS_SIZE];
static uint8_t ewram[0x40000];
static uint8_t iwram[0x8000];
static uint32_t *code;
static ArmMips *jit;

static uint32_t keys[ARM_CACHE_BLOCKS];
static uint32_t first[ARM_CACHE_BLOCKS];
static ArmIrOp pool[POOL_OPS];
static unsigned key_count;
static uint32_t seen[SEEN];

static uint32_t code32(void *context, uint32_t address)
{
  uint32_t value = 0;

  (void)context;
  address &= ~3u;
  if(address < BIOS_SIZE)
    memcpy(&value, bios + address, 4);
  else if(address >> 24 >= 8 && address >> 24 <= 13 &&
   (address & 0x1FFFFFFu) < rom_size)
    memcpy(&value, rom + (address & 0x1FFFFFFu), 4);
  return value;
}

static uint32_t read_none(void *c, uint32_t a) { (void)c; (void)a; return 0; }
static void write_none(void *c, uint32_t a, uint32_t v)
{
  (void)c; (void)a; (void)v;
}

/* Target addresses as the tests map them (tests/core/jit_fuzz.c), so the
   code has the PSP's shape: work RAM not aligned to its size. */
static uint32_t target(void *context, const void *host)
{
  const uint8_t *p = host;

  (void)context;
  if(p >= (const uint8_t *)code && p < (const uint8_t *)code + CODE_BYTES)
    return 0x08800000u + (uint32_t)(p - (const uint8_t *)code);
  if(p >= (const uint8_t *)jit && p < (const uint8_t *)(jit + 1))
    return 0x10000000u + (uint32_t)(p - (const uint8_t *)jit);
  if(p >= ewram && p < ewram + sizeof(ewram))
    return 0x0A010000u + (uint32_t)(p - ewram);
  if(p >= iwram && p < iwram + sizeof(iwram))
    return 0x0A068000u + (uint32_t)(p - iwram);
  return 0x0C000000u + (uint32_t)(p - rom);
}

static void sync_none(void *context, void *start, uint32_t bytes)
{
  (void)context; (void)start; (void)bytes;
}

static int code_at(uint32_t address)
{
  return address < BIOS_SIZE || (address >> 24 >= 8 && address >> 24 <= 9 &&
   (address & 0x1FFFFFFu) < rom_size);
}

static void visit(uint32_t key, unsigned max_blocks)
{
  uint32_t h = (key * 0x9E3779B1u) >> 15;

  if(!code_at(key & ~1u) || key_count >= max_blocks)
    return;
  while(seen[h])
  {
    if(seen[h] == key + 1)
      return;
    h = (h + 1) & (SEEN - 1);
  }
  seen[h] = key + 1;
  keys[key_count++] = key;
}

/* Every block reachable without running anything from the entry points
   and from the Thumb function pointers in the ROM (odd words pointing into
   it; games reach most of their code through such tables and literals). */
static void crawl(unsigned max_blocks)
{
  static ArmIrOp ops[ARM_BLOCK_MAX_OPS];
  unsigned next;
  uint32_t at;

  visit(0x00000000u, max_blocks);      /* reset */
  visit(0x00000008u, max_blocks);      /* SWI */
  visit(0x00000018u, max_blocks);      /* IRQ */
  visit(0x08000000u, max_blocks);      /* the game */
  for(at = 0; at + 4 <= rom_size; at += 4)
  {
    uint32_t word;

    memcpy(&word, rom + at, 4);
    if(word & 1)
      visit(word, max_blocks);
  }
  for(next = 0; next < key_count; next++)
  {
    uint32_t key = keys[next];
    int thumb = (int)(key & 1);
    uint32_t insn = key & ~1u;
    unsigned count = arm_translate(&jit->core, key & ~1u, thumb, ops,
     ARM_BLOCK_MAX_OPS, ARM_BLOCK_MAX_INSNS, NULL);
    unsigned i;

    for(i = 0; i < count; i++)
    {
      const ArmIrOp *op = &ops[i];
      int to_thumb = thumb;

      if(op->code == ARM_IR_INSN)
        insn = op->b;
      if(op->code != ARM_IR_EXIT || op->d == ARM_EXIT_SYSTEM)
        continue;
      if(op->flags & ARM_IR_BX)
        to_thumb = (int)(op->b & 1);
      visit((op->b & (to_thumb ? ~1u : ~3u)) | (uint32_t)to_thumb,
       max_blocks);
      if(op->d == ARM_EXIT_CALL)
        visit((insn + (thumb ? 2u : 4u)) | (uint32_t)thumb, max_blocks);
    }
  }
}

static double now(void)
{
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static long load(const char *path, uint8_t *to, long max)
{
  FILE *f = fopen(path, "rb");
  long n;

  if(!f)
  {
    perror(path);
    exit(1);
  }
  n = (long)fread(to, 1, (size_t)max, f);
  fclose(f);
  return n;
}

int main(int argc, char **argv)
{
  ArmBus bus = { NULL, read_none, read_none, read_none, write_none,
   write_none, write_none, code32 };
  ArmMipsHost host = { NULL, target, sync_none, NULL, 0x08700000u };
  static ArmIrOp ops[ARM_BLOCK_MAX_OPS];
  unsigned repeat = 20;
  unsigned max_blocks = ARM_CACHE_BLOCKS;
  unsigned long long insns = 0, ir_ops = 0, hot = 0, cold = 0;
  double decode = 0, compile_time = 0, t;
  unsigned failed = 0;
  uint32_t digest = 0x811C9DC5u;
  uint32_t mask;
  unsigned r, i;
  int arg = 1;

  for(; arg + 1 < argc && argv[arg][0] == '-'; arg += 2)
  {
    if(!strcmp(argv[arg], "--repeat"))
      repeat = (unsigned)strtoul(argv[arg + 1], NULL, 0);
    else if(!strcmp(argv[arg], "--blocks"))
      max_blocks = (unsigned)strtoul(argv[arg + 1], NULL, 0);
  }
  if(argc - arg != 2 || !repeat || max_blocks > ARM_CACHE_BLOCKS)
  {
    fprintf(stderr, "usage: translate_bench [--repeat N] [--blocks N] ROM "
     "BIOS\n");
    return 1;
  }
  rom = calloc(1, ROM_MAX);
  code = calloc(1, CODE_BYTES);
  jit = calloc(1, sizeof(ArmMips));
  if(!rom || !code || !jit)
    return 1;
  rom_size = (uint32_t)load(argv[arg], rom, ROM_MAX);
  load(argv[arg + 1], bios, BIOS_SIZE);

  arm_mips_init(jit, &bus, &host, code, CODE_BYTES);
  /* Waitstates as most games set them: 3,1 ROM with prefetch. */
  arm_core_set_waitcnt(&jit->core, 0x4317);
  jit->core.map[2].read = jit->core.map[2].write = ewram;
  jit->core.map[2].mask = sizeof(ewram) - 1;
  jit->core.map[3].read = jit->core.map[3].write = iwram;
  jit->core.map[3].mask = sizeof(iwram) - 1;
  for(mask = 1; mask < rom_size; mask <<= 1)
    ;
  for(i = 8; i <= 12; i += 2)
  {
    jit->core.map[i].read = rom;
    jit->core.map[i].mask = mask - 1;
  }
  crawl(max_blocks);

  /* Sizes and the code hash, then the timed passes over every block. */
  for(i = 0; i < key_count; i++)
  {
    ArmIrBlockInfo info;
    unsigned count = arm_translate(&jit->core, keys[i] & ~1u,
     (int)(keys[i] & 1), ops, ARM_BLOCK_MAX_OPS, ARM_BLOCK_MAX_INSNS, &info);
    uint32_t w;

    jit->used = jit->blocks_start;
    jit->stats.hot_words = jit->stats.cold_words = 0;
    if(!compile(jit, ops, count, keys[i], 0, MODE_LINKED, jit->used,
       jit->words - ARM_MIPS_SCRATCH_WORDS))
      failed++;
    /* A hash of the code, to tell a faster compiler that emits the same
       words from one that changed them. */
    for(w = jit->blocks_start; w < jit->used; w++)
      digest = (digest ^ code[w]) * 0x01000193u;
    insns += info.insns;
    ir_ops += count;
    hot += jit->stats.hot_words;
    cold += jit->stats.cold_words;
  }
  if(ir_ops > POOL_OPS)
    return 1;
  for(r = 0; r < repeat; r++)
  {
    unsigned at = 0;

    t = now();
    for(i = 0; i < key_count; i++)
    {
      first[i] = at;
      at += arm_translate(&jit->core, keys[i] & ~1u, (int)(keys[i] & 1),
       pool + at, ARM_BLOCK_MAX_OPS, ARM_BLOCK_MAX_INSNS, NULL);
    }
    decode += now() - t;
    t = now();
    for(i = 0; i < key_count; i++)
    {
      jit->used = jit->blocks_start;
      compile(jit, pool + first[i], (i + 1 < key_count ? first[i + 1] : at) -
       first[i], keys[i], 0, MODE_LINKED, jit->used,
       jit->words - ARM_MIPS_SCRATCH_WORDS);
    }
    compile_time += now() - t;
  }
  printf("translate_bench: %u blocks (%u not compiled), %llu guest "
   "instructions: %.2f IR ops, %.2f hot and %.2f cold MIPS words per "
   "instruction (code hash %08X); decode %.0f ns, compile %.0f ns per "
   "instruction\n", key_count, failed, insns, (double)ir_ops /
   (double)insns, (double)hot / (double)insns, (double)cold / (double)insns,
   digest,
   decode * 1e9 / (double)insns / repeat,
   compile_time * 1e9 / (double)insns / repeat);
  return 0;
}
