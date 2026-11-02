/*
 * Lockstep test of the ARM7TDMI core (source/src/arm_*.c) against mGBA,
 * which owns the rest of the GBA: memory, I/O, DMA, timers, video and
 * interrupts. Three modes:
 *
 *   step   mGBA runs one instruction, recording every data access; the
 *          core then runs the same instruction from the same registers
 *          with a bus that replays those reads and checks its writes
 *          against them. Registers of every mode, CPSR, SPSRs, writes
 *          and cycles are compared after each instruction.
 *   block  the same a whole cached block at a time: mGBA runs as many
 *          instructions as the core's block holds (its events held back
 *          until the block ends, as the core only stops at blocks), then
 *          the core runs the block. This exercises the block cache, the
 *          flag liveness pass and constant propagation.
 *   run    the core replaces mGBA's CPU: it runs the game against mGBA's
 *          memory and devices, mGBA only processing events between runs.
 *          An end-to-end check (play the script, look at --shot) and the
 *          speed measurement.
 *   mgba   mGBA alone, for reference screenshots and timings.
 *
 * mGBA processes its events (interrupt entry, DMA) between instructions
 * as usual; the core's registers are loaded from mGBA's before every
 * step, so interrupt entries need no special care.
 *
 * Usage: lockstep [options] ROM BIOS [SAVE]
 *   --mode step|block|run|mgba  (step)
 *   --script FILE          "run FRAMES [KEYS]" and "shot NAME" lines as
 *                          in tests/rom_link (console 0's keys)
 *   --frames N             stop after N frames (600 without a script)
 *   --bios-boot            run the BIOS intro instead of skipping it
 *   --shot FILE            write the last frame as a BMP
 *   --cycles-strict        stop at the first cycle difference too
 *   --trace FILE           run mode: a line per run (frame, cycles,
 *                          instructions, pc, register hash), so two runs
 *                          can be compared
 * Built with LOCKSTEP_JIT (and Unicorn, see mips_unicorn.h):
 *   --jit                  the MIPS backend (arm_mips.c) runs the core
 *   --count                run mode: count the MIPS instructions run, the
 *                          sound mixer's share, the backend's predictors
 *   --dump KEY             at the end, the IR and MIPS words of the block
 *                          at KEY (hex pc, | 1 for Thumb)
 */

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "arm_core.h"

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/memory.h>
#include <mgba-util/vfs.h>

typedef struct
{
  uint8_t write;
  uint8_t size;
  uint8_t used;
  uint8_t wild;       /* value not known (an STM outside RAM) */
  uint32_t address;
  uint32_t value;
} Access;

static struct mCore *mcore;
static struct GBA *gba;
static struct ARMCore *cpu;
static struct ARMMemory original;
static color_t video[256 * 160];
static ArmCore interpreter;
static ArmCore *core = &interpreter;
static FILE *trace;

#ifdef LOCKSTEP_JIT
#include "arm_mips.h"
#include "mips_unicorn.h"

#define JIT_CODE_BYTES (16u << 20)
/* The M4A sound mixer's loop in Mario Kart's internal RAM. */
#define MIXER_START 0x03002890u
#define MIXER_END 0x030028D8u

static ArmMips *jit;

static ArmExitKind run_block(void)
{
  return jit ? arm_mips_run_block(jit) : arm_core_run_block(core);
}

static int step(void)
{
  return jit ? arm_mips_step(jit) : arm_core_step(core);
}

static int32_t run(int32_t cycles)
{
  return jit ? arm_mips_run(jit, cycles) : arm_core_run(core, cycles);
}

static void print_ir(uint32_t pc, int thumb, unsigned max_insns);

/* Which block a stretch of MIPS code belongs to: blocks sit one after
   the other in the buffer, each from its key word to the next one. */
typedef struct
{
  uint32_t start;
  uint32_t entry;
  uint32_t key;
  uint32_t insns;
} Span;

static Span spans[ARM_CACHE_BLOCKS];
static unsigned span_count;
static uint32_t spans_blocks = ~0u;
static uint32_t spans_generation;
static unsigned long long block_runs;
static unsigned long long block_guest_insns;
static unsigned long long mixer_mips;
static unsigned long long mixer_runs;
static unsigned long long mixer_guest_insns;

static int by_start(const void *a, const void *b)
{
  const Span *x = a;
  const Span *y = b;

  return x->start < y->start ? -1 : x->start > y->start;
}

static void build_spans(void)
{
  unsigned i;

  span_count = 0;
  for(i = 0; i < core->block_count; i++)
    if(jit->code[i] > 1)
    {
      Span *s = &spans[span_count++];

      s->entry = jit->code[i];
      s->start = s->entry - 16;
      s->key = core->blocks[i].key;
      s->insns = core->blocks[i].insns;
    }
  qsort(spans, span_count, sizeof(Span), by_start);
  spans_blocks = jit->stats.blocks;
  spans_generation = jit->generation;
}

/* MIPS instructions run per block, by key, for the hottest blocks. */
typedef struct
{
  uint32_t key;
  uint32_t insns;
  unsigned long long mips;
  unsigned long long runs;
} Heat;

static Heat heat[65536];

static Heat *heat_of(const Span *s)
{
  unsigned i = (s->key * 2654435761u) >> 16;

  while(heat[i].key && heat[i].key != s->key + 1)
    i = (i + 1) & 65535;
  heat[i].key = s->key + 1;
  heat[i].insns = s->insns;
  return &heat[i];
}

static int by_heat(const void *a, const void *b)
{
  const Heat *x = a;
  const Heat *y = b;

  return x->mips < y->mips ? 1 : x->mips > y->mips ? -1 : 0;
}

static void count_block(uint32_t address, uint32_t size)
{
  Heat *h;
  unsigned low = 0;
  unsigned high;
  const Span *s;

  if(spans_blocks != jit->stats.blocks || spans_generation != jit->generation)
    build_spans();
  high = span_count;
  while(low < high)
  {
    unsigned mid = (low + high) / 2;

    if(spans[mid].start <= address)
      low = mid + 1;
    else
      high = mid;
  }
  if(!low)
    return;
  s = &spans[low - 1];
  h = heat_of(s);
  h->mips += size / 4;
  if(address == s->entry)
  {
    block_runs++;
    block_guest_insns += s->insns;
    h->runs++;
  }
  if((s->key & ~1u) >= MIXER_START && (s->key & ~1u) < MIXER_END)
  {
    mixer_mips += size / 4;
    if(address == s->entry)
    {
      mixer_runs++;
      mixer_guest_insns += s->insns;
    }
  }
}

/* The IR and the MIPS words of the block at key (pc, | 1 for Thumb), for
   a disassembler. */
static void dump_block(uint32_t key)
{
  unsigned i;

  build_spans();
  for(i = 0; i < span_count; i++)
    if(spans[i].key == key)
    {
      uint32_t end = i + 1 < span_count ? spans[i + 1].start :
       jit->buffer_target + 4 * jit->used;
      uint32_t at;

      print_ir(key & ~1u, (int)(key & 1), ARM_BLOCK_MAX_INSNS);
      printf("MIPS at %08X (main entry %08X):\n", spans[i].start,
       spans[i].entry);
      for(at = spans[i].start; at < end; at += 4)
        printf("%08X%s", jit->buffer[(at - jit->buffer_target) / 4],
         (at - spans[i].start) % 32 == 28 ? "\n" : " ");
      printf("\n");
      return;
    }
  printf("no compiled block at %08X\n", key);
}

static void print_counts(void)
{
  unsigned long long mips = mips_unicorn_executed;
  const ArmMipsStats *st = &jit->stats;
  unsigned i;

  printf("backend: %llu MIPS instructions run for %llu guest instructions "
   "in %llu block runs (%.2f per guest instruction, %llu accesses through "
   "C)\n", mips, block_guest_insns, block_runs,
   (double)mips / (double)block_guest_insns, mips_unicorn_calls);
  printf("mixer loop %08X-%08X: %llu MIPS instructions for %llu guest "
   "instructions in %llu runs (%.2f per guest instruction)\n", MIXER_START,
   MIXER_END, mixer_mips, mixer_guest_insns, mixer_runs,
   mixer_guest_insns ? (double)mixer_mips / (double)mixer_guest_insns : 0.0);
  printf("return stack: %u hits, %u misses; dispatch cache: %u hits, %u "
   "misses (%u from C)\n", jit->profile[0], jit->profile[1],
   jit->profile[2], jit->profile[3], st->misses);
  printf("code: %u blocks, %u guest instructions, %u hot and %u cold words "
   "(%.1f and %.1f per guest instruction), %u links, %u bails, %u "
   "interpreted, %u flushes\n", st->blocks, st->guest_insns,
   st->hot_words, st->cold_words,
   (double)st->hot_words / (double)st->guest_insns,
   (double)st->cold_words / (double)st->guest_insns, st->links, st->bails,
   st->interpreted, st->flushes);
  printf("accesses through C by region (loads/stores):");
  for(i = 0; i < 16; i++)
    if(mips_unicorn_region_calls[i][0] || mips_unicorn_region_calls[i][1])
      printf(" %X: %llu/%llu", i, mips_unicorn_region_calls[i][0],
       mips_unicorn_region_calls[i][1]);
  printf("\n");
  qsort(heat, 65536, sizeof(Heat), by_heat);
  for(i = 0; i < 16 && heat[i].mips; i++)
    printf("  block %08X: %llu runs of %u instructions, %.1f MIPS per "
     "run (%.2f per instruction), %.1f%% of all\n", heat[i].key - 1,
     heat[i].runs, heat[i].insns,
     heat[i].runs ? (double)heat[i].mips / (double)heat[i].runs : 0.0,
     heat[i].runs ? (double)heat[i].mips / (double)heat[i].runs /
     heat[i].insns : 0.0, 100.0 * (double)heat[i].mips / (double)mips);
}
#else
static ArmExitKind run_block(void)
{
  return arm_core_run_block(core);
}

static int step(void)
{
  return arm_core_step(core);
}

static int32_t run(int32_t cycles)
{
  return arm_core_run(core, cycles);
}
#endif

static Access accesses[8192];
static unsigned access_count;
static int recording;
static int diverged;
static char divergence[256];
static unsigned long replay_misses;

/* Run mode */
static int32_t run_base;
static double core_seconds;

static double now(void)
{
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void silent_log(struct mLogger *logger, int category,
 enum mLogLevel level, const char *format, va_list args)
{
  (void)logger; (void)category; (void)level; (void)format; (void)args;
}

static struct mLogger silent_logger = { .log = silent_log };

static uint32_t ror32(uint32_t value, unsigned amount)
{
  amount &= 31;
  return amount ? (value >> amount) | (value << (32 - amount)) : value;
}

static uint32_t peek(uint32_t address, unsigned size)
{
  /* The EEPROM sits in the top ROM mirror and reading it has effects. */
  if((address >> 24) == 0x0D)
    return 0;
  if(size == 4)
    return GBAView32(cpu, address);
  if(size == 2)
    return GBAView16(cpu, address);
  return GBAView8(cpu, address);
}

/* ---- Recording mGBA's accesses ------------------------------------------ */

static void record(int write, unsigned size, uint32_t address,
 uint32_t value, int wild)
{
  Access *a;

  if(access_count >= sizeof(accesses) / sizeof(accesses[0]))
    return;
  a = &accesses[access_count++];
  a->write = (uint8_t)write;
  a->size = (uint8_t)size;
  a->used = 0;
  a->wild = (uint8_t)wild;
  a->address = address & ~(size - 1);
  a->value = size == 4 ? value : value & ((1u << (size * 8)) - 1);
}

/* Writes by anything but the instruction being compared (DMA) may hit
   translated code; the core must hear of them. */
static void system_write(uint32_t address, unsigned size)
{
  if(!recording)
    arm_core_invalidate(core, address, size);
}

static uint32_t hook_load32(struct ARMCore *c, uint32_t address, int *cycles)
{
  uint32_t value = original.load32(c, address, cycles);

  /* mGBA hands back unaligned words rotated; keep the raw word. */
  if(recording)
    record(0, 4, address, ror32(value, 32 - (address & 3) * 8), 0);
  return value;
}

static uint32_t hook_load16(struct ARMCore *c, uint32_t address, int *cycles)
{
  uint32_t value = original.load16(c, address, cycles);

  if(recording)
    record(0, 2, address, ror32(value, 32 - (address & 1) * 8), 0);
  return value;
}

static uint32_t hook_load8(struct ARMCore *c, uint32_t address, int *cycles)
{
  uint32_t value = original.load8(c, address, cycles);

  if(recording)
    record(0, 1, address, value, 0);
  return value;
}

static void hook_store32(struct ARMCore *c, uint32_t address, int32_t value,
 int *cycles)
{
  if(recording)
    record(1, 4, address, (uint32_t)value, 0);
  system_write(address, 4);
  original.store32(c, address, value, cycles);
}

static void hook_store16(struct ARMCore *c, uint32_t address, int16_t value,
 int *cycles)
{
  if(recording)
    record(1, 2, address, (uint16_t)value, 0);
  system_write(address, 2);
  original.store16(c, address, value, cycles);
}

static void hook_store8(struct ARMCore *c, uint32_t address, int8_t value,
 int *cycles)
{
  if(recording)
    record(1, 1, address, (uint8_t)value, 0);
  system_write(address, 1);
  original.store8(c, address, value, cycles);
}

/* The words an LDM or STM touches, as mGBA's interface describes them. */
static unsigned multiple_addresses(uint32_t address, int mask,
 enum LSMDirection direction, uint32_t *out)
{
  unsigned count = (unsigned)__builtin_popcount((unsigned)mask & 0xFFFF);
  unsigned i;

  if(direction & LSM_D)
    address -= (count << 2) - 4;
  if(direction & LSM_B)
    address += (direction & LSM_D) ? (uint32_t)-4 : 4;
  if(!count)
  {
    out[0] = address & ~3u;
    return 1;
  }
  for(i = 0; i < count; i++)
    out[i] = (address & ~3u) + 4 * i;
  return count;
}

static uint32_t hook_load_multiple(struct ARMCore *c, uint32_t address,
 int mask, enum LSMDirection direction, int *cycles)
{
  if(recording)
  {
    uint32_t list[16];
    unsigned n = multiple_addresses(address, mask, direction, list);
    unsigned i;

    for(i = 0; i < n; i++)
      record(0, 4, list[i], peek(list[i], 4), 0);
  }
  return original.loadMultiple(c, address, mask, direction, cycles);
}

static uint32_t hook_store_multiple(struct ARMCore *c, uint32_t address,
 int mask, enum LSMDirection direction, int *cycles)
{
  uint32_t list[16];
  unsigned n = multiple_addresses(address, mask, direction, list);
  uint32_t result = original.storeMultiple(c, address, mask, direction,
   cycles);
  unsigned i;

  for(i = 0; i < n; i++)
  {
    if(recording)
    {
      /* Memory only shows what was stored in RAM; I/O, the BIOS and the
         cartridge keep their own value. */
      unsigned region = list[i] >> 24;
      int ram = region == 2 || region == 3 || (region >= 5 && region <= 7);

      record(1, 4, list[i], peek(list[i], 4), !ram);
    }
    system_write(list[i], 4);
  }
  return result;
}

/* ---- The core's bus in step and block mode: replay ---------------------- */

static void diverge(const char *text)
{
  if(!diverged)
    snprintf(divergence, sizeof(divergence), "%s", text);
  diverged = 1;
}

static uint32_t replay_read(uint32_t address, unsigned size)
{
  unsigned i;

  for(i = 0; i < access_count; i++)
  {
    Access *a = &accesses[i];

    if(!a->used && !a->write && a->size == size && a->address == address)
    {
      a->used = 1;
      return a->value;
    }
  }
  replay_misses++;
  return peek(address, size);
}

static void replay_write(uint32_t address, unsigned size, uint32_t value)
{
  char text[160];
  uint32_t mask = size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1;
  unsigned i;

  for(i = 0; i < access_count; i++)
  {
    Access *a = &accesses[i];

    if(!a->used && a->write && a->size == size && a->address == address)
    {
      a->used = 1;
      if(!a->wild && a->value != (value & mask))
      {
        snprintf(text, sizeof(text), "write%u %08X = %08X, mGBA wrote %08X",
         size * 8, address, value & mask, a->value);
        diverge(text);
      }
      return;
    }
  }
  snprintf(text, sizeof(text), "write%u %08X = %08X that mGBA did not do",
   size * 8, address, value & mask);
  diverge(text);
}

static void check_writes_done(void)
{
  char text[160];
  unsigned i;

  for(i = 0; i < access_count; i++)
    if(accesses[i].write && !accesses[i].used)
    {
      snprintf(text, sizeof(text), "missing write%u %08X = %08X",
       accesses[i].size * 8, accesses[i].address, accesses[i].value);
      diverge(text);
      return;
    }
}

static uint32_t replay_read8(void *c, uint32_t a)
{
  (void)c;
  return replay_read(a, 1);
}

static uint32_t replay_read16(void *c, uint32_t a)
{
  (void)c;
  return replay_read(a, 2);
}

static uint32_t replay_read32(void *c, uint32_t a)
{
  (void)c;
  return replay_read(a, 4);
}

static void replay_write8(void *c, uint32_t a, uint32_t v)
{
  (void)c;
  replay_write(a, 1, v);
}

static void replay_write16(void *c, uint32_t a, uint32_t v)
{
  (void)c;
  replay_write(a, 2, v);
}

static void replay_write32(void *c, uint32_t a, uint32_t v)
{
  (void)c;
  replay_write(a, 4, v);
}

static uint32_t code32(void *c, uint32_t address)
{
  (void)c;
  return peek(address & ~3u, 4);
}

/* ---- The core's bus in run mode: mGBA's memory -------------------------- */

static void run_sync(void)
{
  uint32_t width = (core->cpsr & ARM_CPSR_T) ? 2 : 4;

  cpu->cycles = run_base + core->cycles;
  /* mGBA lets only code running in the BIOS read the BIOS. */
  cpu->gprs[ARM_PC] = (int32_t)(core->insn_pc + 2 * width);
  if((uint32_t)gba->memory.activeRegion != core->insn_pc >> 24)
    original.setActiveRegion(cpu, core->insn_pc);
}

static void update_irq_line(void)
{
  core->irq_line = gba->memory.io[REG_IME >> 1] &&
   (gba->memory.io[REG_IE >> 1] & gba->memory.io[REG_IF >> 1]);
}

/* After a store: new interrupt state, waitstates, or an event mGBA wants
   handled now (HALTCNT, DMA) make the core return. */
static void run_after_write(uint32_t address)
{
  if((address >> 24) == 4)
  {
    update_irq_line();
    if((address & 0x00FFFFFCu) == 0x204)
      arm_core_set_waitcnt(core, gba->memory.io[REG_WAITCNT >> 1]);
  }
  if(cpu->halted || cpu->nextEvent <= cpu->cycles)
    arm_core_request_exit(core);
}

static uint32_t run_read8(void *c, uint32_t a)
{
  int wait = 0;

  (void)c;
  run_sync();
  return original.load8(cpu, a, &wait);
}

static uint32_t run_read16(void *c, uint32_t a)
{
  int wait = 0;

  (void)c;
  run_sync();
  return original.load16(cpu, a, &wait) & 0xFFFF;
}

static uint32_t run_read32(void *c, uint32_t a)
{
  int wait = 0;

  (void)c;
  run_sync();
  return original.load32(cpu, a, &wait);
}

static void run_write8(void *c, uint32_t a, uint32_t v)
{
  int wait = 0;

  (void)c;
  run_sync();
  original.store8(cpu, a, (int8_t)v, &wait);
  run_after_write(a);
}

static void run_write16(void *c, uint32_t a, uint32_t v)
{
  int wait = 0;

  (void)c;
  run_sync();
  original.store16(cpu, a, (int16_t)v, &wait);
  run_after_write(a);
}

static void run_write32(void *c, uint32_t a, uint32_t v)
{
  int wait = 0;

  (void)c;
  run_sync();
  original.store32(cpu, a, (int32_t)v, &wait);
  run_after_write(a);
}

/* ---- Register files ----------------------------------------------------- */

static const unsigned modes[6] =
{
  ARM_MODE_USER, ARM_MODE_FIQ, ARM_MODE_IRQ, ARM_MODE_SUPERVISOR,
  ARM_MODE_ABORT, ARM_MODE_UNDEFINED
};

static unsigned bank_of(unsigned mode)
{
  unsigned i;

  for(i = 1; i < 6; i++)
    if(modes[i] == mode)
      return i;
  return 0;
}

/* mGBA's register 8-14 or SPSR (16) of a mode: the live one for the
   current bank, the stored one otherwise. Its banks are numbered as the
   core's. */
static uint32_t mgba_register(unsigned mode, unsigned reg)
{
  unsigned current = bank_of((unsigned)cpu->cpsr.priv);
  unsigned wanted = bank_of(mode);

  if(reg == 16)
    return (uint32_t)(current == wanted ? cpu->spsr.packed :
     cpu->bankedSPSRs[wanted]);
  if(reg >= 13 && current != wanted)
    return (uint32_t)cpu->bankedRegisters[wanted][reg - 13];
  if(reg >= 8 && reg <= 12 && (current == 1) != (wanted == 1))
    return (uint32_t)cpu->bankedRegisters[wanted == 1][reg - 6];
  return (uint32_t)cpu->gprs[reg];
}

static uint32_t mgba_next_pc(void)
{
  return (uint32_t)cpu->gprs[ARM_PC] -
   (cpu->executionMode == MODE_THUMB ? 2 : 4);
}

static void load_core_from_mgba(void)
{
  unsigned m;
  unsigned r;

  arm_core_set_cpsr(core, (uint32_t)cpu->cpsr.packed);
  for(m = 0; m < 6; m++)
    for(r = 8; r <= 16; r++)
      if(r != 15)
        *arm_core_register(core, modes[m], r) = mgba_register(modes[m], r);
  for(r = 0; r < 15; r++)
    core->v[r] = (uint32_t)cpu->gprs[r];
  core->v[15] = mgba_next_pc();
}

/* Compares every register of every mode; returns a description of the
   first differences, or NULL. */
static const char *compare_registers(void)
{
  static char text[512];
  size_t n = 0;
  unsigned m;
  unsigned r;

  text[0] = 0;
  for(r = 0; r < 15; r++)
    if(core->v[r] != (uint32_t)cpu->gprs[r])
      n += (size_t)snprintf(text + n, sizeof(text) - n, " r%u %08X/%08X", r,
       core->v[r], (uint32_t)cpu->gprs[r]);
  if(core->v[15] != mgba_next_pc() && n < sizeof(text))
    n += (size_t)snprintf(text + n, sizeof(text) - n, " pc %08X/%08X",
     core->v[15], mgba_next_pc());
  if(arm_core_cpsr(core) != (uint32_t)cpu->cpsr.packed && n < sizeof(text))
    n += (size_t)snprintf(text + n, sizeof(text) - n, " cpsr %08X/%08X",
     arm_core_cpsr(core), (uint32_t)cpu->cpsr.packed);
  for(m = 0; m < 6 && n < sizeof(text); m++)
    for(r = 8; r <= 16 && n < sizeof(text); r++)
    {
      uint32_t mine;
      uint32_t theirs;

      if(r == 15)
        continue;
      mine = *arm_core_register(core, modes[m], r);
      theirs = mgba_register(modes[m], r);
      if(mine != theirs)
        n += (size_t)snprintf(text + n, sizeof(text) - n,
         " mode%02X.%s%u %08X/%08X", modes[m], r == 16 ? "spsr" : "r",
         r == 16 ? 0 : r, mine, theirs);
    }
  return n ? text : NULL;
}

static void print_mgba_state(const char *title, const struct ARMCore *c)
{
  int r;

  printf("%s:", title);
  for(r = 0; r < 16; r++)
    printf("%s r%d=%08X", r % 8 ? "" : "\n ", r, (uint32_t)c->gprs[r]);
  printf("\n cpsr=%08X spsr=%08X\n", (uint32_t)c->cpsr.packed,
   (uint32_t)c->spsr.packed);
}

static void print_core_state(void)
{
  int r;

  printf("core after:");
  for(r = 0; r < 16; r++)
    printf("%s r%d=%08X", r % 8 ? "" : "\n ", r, core->v[r]);
  printf("\n cpsr=%08X spsr=%08X\n", arm_core_cpsr(core), core->spsr);
}

static void print_ir(uint32_t pc, int thumb, unsigned max_insns)
{
  ArmIrOp ops[ARM_BLOCK_MAX_OPS];
  unsigned n = arm_translate(core, pc, thumb, ops, ARM_BLOCK_MAX_OPS,
   max_insns, NULL);
  unsigned i;

  printf("IR:\n");
  for(i = 0; i < n; i++)
    printf("  %-8s f=%02X d=%3u a=%3u b=%08X\n", arm_ir_name(ops[i].code),
     ops[i].flags, ops[i].d, ops[i].a, ops[i].b);
}

static void print_accesses(void)
{
  unsigned i;

  for(i = 0; i < access_count; i++)
    printf("  mGBA %s%u %08X = %08X%s\n", accesses[i].write ? "write" :
     "read", accesses[i].size * 8, accesses[i].address, accesses[i].value,
     accesses[i].used ? "" : " (unused)");
}

/* ---- Cycle statistics --------------------------------------------------- */

typedef struct
{
  uint32_t key;
  int32_t mine;
  int32_t theirs;
  uint32_t example_pc;
  uint32_t example_opcode;
  unsigned long count;
} CycleBucket;

static CycleBucket buckets[4096];
static unsigned bucket_count;
static unsigned long cycle_mismatches;
static unsigned long long cycles_mine;
static unsigned long long cycles_theirs;

static uint32_t opcode_at(uint32_t pc, int thumb)
{
  uint32_t word = peek(pc & ~3u, 4);

  if(!thumb)
    return word;
  return (pc & 2) ? word >> 16 : word & 0xFFFF;
}

/* Groups differences by instruction class, code region and both counts. */
static void note_cycles(uint32_t pc, int thumb, int32_t mine,
 int32_t theirs)
{
  uint32_t opcode = opcode_at(pc, thumb);
  uint32_t key;
  unsigned i;

  cycles_mine += (unsigned long long)mine;
  cycles_theirs += (unsigned long long)theirs;
  if(mine == theirs)
    return;
  cycle_mismatches++;
  if(thumb)
    key = 0x10000u | (opcode >> 8);
  else
    key = ((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 15);
  key |= (pc >> 24) << 20;
  for(i = 0; i < bucket_count; i++)
    if(buckets[i].key == key && buckets[i].mine == mine &&
       buckets[i].theirs == theirs)
    {
      buckets[i].count++;
      return;
    }
  if(bucket_count < 4096)
  {
    CycleBucket *b = &buckets[bucket_count++];

    b->key = key;
    b->mine = mine;
    b->theirs = theirs;
    b->example_pc = pc;
    b->example_opcode = opcode;
    b->count = 1;
  }
}

static int by_count(const void *a, const void *b)
{
  const CycleBucket *x = a;
  const CycleBucket *y = b;

  return x->count < y->count ? 1 : x->count > y->count ? -1 : 0;
}

static void print_cycle_summary(void)
{
  unsigned i;

  printf("cycles: core %llu, mGBA %llu, %lu instructions or blocks differ\n",
   cycles_mine, cycles_theirs, cycle_mismatches);
  qsort(buckets, bucket_count, sizeof(buckets[0]), by_count);
  for(i = 0; i < bucket_count && i < 16; i++)
    printf("  %8lu x %s %08X at %08X: core %d, mGBA %d\n", buckets[i].count,
     (buckets[i].key & 0x10000u) ? "thumb" : "arm  ",
     buckets[i].example_opcode, buckets[i].example_pc, buckets[i].mine,
     buckets[i].theirs);
}

/* ---- Script ------------------------------------------------------------ */

static uint32_t parse_keys(const char *text)
{
  static const char *names[] = { "A", "B", "SEL", "START", "RIGHT", "LEFT",
   "UP", "DOWN", "R", "L" };
  uint32_t keys = 0;
  char buffer[128];
  char *token;
  unsigned i;

  if(!text || strcmp(text, "-") == 0)
    return 0;
  snprintf(buffer, sizeof(buffer), "%s", text);
  for(token = strtok(buffer, "+"); token; token = strtok(NULL, "+"))
    for(i = 0; i < 10; i++)
      if(strcmp(token, names[i]) == 0)
        keys |= 1u << i;
  return keys;
}

typedef struct
{
  unsigned frames;
  uint32_t keys;
  char shot[64];      /* screenshot to write when the segment ends */
} Segment;

static Segment segments[4096];
static unsigned segment_count;

static void add_segment(unsigned frames, uint32_t keys)
{
  if(segment_count < 4096)
  {
    segments[segment_count].frames = frames;
    segments[segment_count].keys = keys;
    segments[segment_count].shot[0] = 0;
    segment_count++;
  }
}

/* Console 0's keys from a tests/rom_link script; other commands are for
   the link test and are skipped. */
static void load_script(const char *path)
{
  FILE *f = fopen(path, "r");
  char line[512];

  if(!f)
  {
    perror(path);
    exit(2);
  }
  while(fgets(line, sizeof(line), f))
  {
    char *cmd = strtok(line, " \t\n");
    char *a = strtok(NULL, " \t\n");
    char *b = strtok(NULL, " \t\n");
    char *c = strtok(NULL, " \t\n");

    if(!cmd || cmd[0] == '#' || !a)
      continue;
    if(strcmp(cmd, "run") == 0)
      add_segment((unsigned)atoi(a), parse_keys(b));
    else if(strcmp(cmd, "tap") == 0)
    {
      add_segment(c ? (unsigned)atoi(c) : 6u, atoi(a) == 0 ?
       parse_keys(b) : 0);
      add_segment(10, 0);
    }
    else if(strcmp(cmd, "shot") == 0)
    {
      add_segment(0, 0);
      snprintf(segments[segment_count - 1].shot,
       sizeof(segments[0].shot), "%s.bmp", a);
    }
  }
  fclose(f);
}

/* ---- Main loops --------------------------------------------------------- */

static unsigned long long compared;
static int strict_cycles;

static void process_events(void)
{
  while(cpu->cycles >= cpu->nextEvent)
    cpu->irqh.processEvents(cpu);
}

static void report_divergence(const struct ARMCore *before, uint32_t pc,
 int thumb, unsigned insns, const char *registers)
{
  printf("DIVERGENCE after %llu instructions at %08X (%s %0*X)\n", compared,
   pc, thumb ? "thumb" : "arm", thumb ? 4 : 8, opcode_at(pc, thumb));
  if(registers)
    printf("registers (core/mGBA):%s\n", registers);
  if(diverged)
    printf("bus: %s\n", divergence);
  print_mgba_state("before", before);
  print_mgba_state("mGBA after", cpu);
  print_core_state();
  print_accesses();
  print_ir(pc, thumb, insns);
}

static unsigned long multiply_carries;

static int is_multiply(uint32_t opcode, int thumb)
{
  if(thumb)
    return (opcode & 0xFFC0) == 0x4340;
  return (opcode & 0x0F0000F0u) == 0x00000090u;
}

/* MUL and MULL leave C as the multiplier's last carry, which the manual
   calls meaningless; mGBA models it, the core leaves C alone. A run of
   instructions differing only there is counted and taken as matching. */
static int multiply_carry_only(uint32_t pc, int thumb, unsigned insns)
{
  unsigned i;

  if((arm_core_cpsr(core) ^ (uint32_t)cpu->cpsr.packed) != ARM_CPSR_C)
    return 0;
  arm_core_set_cpsr(core, (uint32_t)cpu->cpsr.packed);
  if(compare_registers())
    return 0;
  for(i = 0; i < insns; i++)
    if(is_multiply(opcode_at(pc + i * (thumb ? 2 : 4), thumb), thumb))
    {
      multiply_carries++;
      return 1;
    }
  return 0;
}

/* One instruction (step mode) or one block; returns 0 on a divergence. */
static int compare_once(int block_mode)
{
  struct ARMCore before;
  uint32_t pc;
  int thumb;
  unsigned insns = 1;
  int32_t mgba_start;
  int32_t mgba_cycles;
  int32_t core_cycles;
  const char *registers;
  unsigned i;

  process_events();
  load_core_from_mgba();
  memcpy(&before, cpu, sizeof(before));
  pc = core->v[15];
  thumb = (core->cpsr & ARM_CPSR_T) != 0;
  if(block_mode)
    insns = arm_core_block(core)->insns;

  access_count = 0;
  diverged = 0;
  replay_misses = 0;
  recording = 1;
  mgba_start = cpu->cycles;
  if(block_mode)
  {
    int32_t next = cpu->nextEvent;

    cpu->nextEvent = INT32_MAX;
    for(i = 0; i < insns; i++)
    {
      ARMRun(cpu);
      if(cpu->nextEvent != INT32_MAX)
      {
        if(cpu->nextEvent < next)
          next = cpu->nextEvent;
        cpu->nextEvent = INT32_MAX;
      }
    }
    cpu->nextEvent = next;
  }
  else
    ARMRun(cpu);
  recording = 0;
  mgba_cycles = cpu->cycles - mgba_start;

  core->cycles = 0;
  if(block_mode)
  {
    uint32_t start = core->instructions;

    /* A store to translated code makes the core leave the block early; a
       block starting there could run past this one's end, so the rest
       goes one instruction at a time. */
    run_block();
    while(core->instructions - start < insns && !diverged)
      step();
  }
  else
    step();
  core_cycles = core->cycles;
  check_writes_done();
  compared += insns;

  registers = compare_registers();
  if(registers && !diverged && multiply_carry_only(pc, thumb, insns))
    registers = NULL;
  if(registers || diverged)
  {
    report_divergence(&before, pc, thumb, insns, registers);
    return 0;
  }
  note_cycles(pc, thumb, core_cycles, mgba_cycles);
  if(strict_cycles && core_cycles != mgba_cycles)
  {
    printf("CYCLES differ after %llu instructions at %08X: core %d, mGBA "
     "%d\n", compared, pc, core_cycles, mgba_cycles);
    print_mgba_state("before", &before);
    print_ir(pc, thumb, insns);
    return 0;
  }
  return 1;
}

/* The core runs the game on mGBA's devices until the next mGBA event. */
static void run_once(void)
{
  int32_t budget;
  double start;

  while(cpu->halted || cpu->cycles >= cpu->nextEvent)
  {
    cpu->irqh.processEvents(cpu);
    if(cpu->halted && !(gba->memory.io[REG_IME >> 1] &&
       gba->memory.io[REG_IE >> 1]))
      cpu->cycles = cpu->nextEvent;
  }
  update_irq_line();
  budget = cpu->nextEvent - cpu->cycles;
  run_base = cpu->cycles;
  start = now();
  cpu->cycles = run_base + run(budget);
  core_seconds += now() - start;
  core->cycles = 0;
  if(trace)
  {
    uint32_t sum = arm_core_cpsr(core);
    unsigned r;

    for(r = 0; r < 16; r++)
      sum = sum * 31 + core->v[r];
    fprintf(trace, "%u %d %u %08X %08X\n", mcore->frameCounter(mcore),
     cpu->cycles, core->instructions, core->v[15], sum);
  }
}

static void write_bmp(const char *path)
{
  FILE *f = fopen(path, "wb");
  uint8_t header[54] = { 'B', 'M' };
  uint32_t size = 54 + 240 * 160 * 3;
  int x;
  int y;

  if(!f)
    return;
  header[2] = (uint8_t)size;
  header[3] = (uint8_t)(size >> 8);
  header[4] = (uint8_t)(size >> 16);
  header[10] = 54;
  header[14] = 40;
  header[18] = 240;
  header[22] = 160;
  header[26] = 1;
  header[28] = 24;
  fwrite(header, 1, 54, f);
  for(y = 159; y >= 0; y--)
    for(x = 0; x < 240; x++)
    {
      color_t c = video[y * 256 + x];
      uint8_t bgr[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };

      fwrite(bgr, 1, 3, f);
    }
  fclose(f);
}

static void *read_file(const char *path, size_t *size)
{
  FILE *f = fopen(path, "rb");
  void *data;

  if(!f)
  {
    perror(path);
    exit(2);
  }
  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  data = malloc(*size);
  if(!data || fread(data, 1, *size, f) != *size)
    exit(2);
  fclose(f);
  return data;
}

static void boot(const char *rom, const char *bios, const char *save,
 int bios_boot)
{
  mLogSetDefaultLogger(&silent_logger);
  mcore = GBACoreCreate();
  mcore->init(mcore);
  mCoreInitConfig(mcore, NULL);
  mCoreConfigSetValue(&mcore->config, "skipBios", bios_boot ? "0" : "1");
  /* mGBA would otherwise halt in loops it believes idle. */
  mCoreConfigSetValue(&mcore->config, "idleOptimization", "ignore");
  mcore->loadConfig(mcore, &mcore->config);
  mcore->setVideoBuffer(mcore, video, 256);
  if(!mcore->loadROM(mcore, VFileOpen(rom, O_RDONLY)))
  {
    fprintf(stderr, "%s: cannot load\n", rom);
    exit(2);
  }
  mcore->loadBIOS(mcore, VFileOpen(bios, O_RDONLY), 0);
  if(save)
  {
    size_t size;
    void *data = read_file(save, &size);

    mcore->loadSave(mcore, VFileMemChunk(data, size));
  }
  mcore->reset(mcore);
  gba = mcore->board;
  cpu = gba->cpu;

  original = cpu->memory;
  cpu->memory.load32 = hook_load32;
  cpu->memory.load16 = hook_load16;
  cpu->memory.load8 = hook_load8;
  cpu->memory.store32 = hook_store32;
  cpu->memory.store16 = hook_store16;
  cpu->memory.store8 = hook_store8;
  cpu->memory.loadMultiple = hook_load_multiple;
  cpu->memory.storeMultiple = hook_store_multiple;
}

int main(int argc, char **argv)
{
  const char *mode = "step";
  const char *script = NULL;
  const char *shot = NULL;
  const char *paths[3] = { NULL, NULL, NULL };
  unsigned path_count = 0;
  unsigned frames = 0;
  int bios_boot = 0;
  unsigned s;
  int arg;
  uint32_t frame_end;
  uint32_t start_frame;
  clock_t start;
  double seconds;
  int ok = 1;
  ArmBus replay = { NULL, replay_read8, replay_read16, replay_read32,
   replay_write8, replay_write16, replay_write32, code32 };
  ArmBus direct = { NULL, run_read8, run_read16, run_read32, run_write8,
   run_write16, run_write32, code32 };
#ifdef LOCKSTEP_JIT
  int use_jit = 0;
  int count = 0;
  uint32_t dump = 0;
#endif

  for(arg = 1; arg < argc; arg++)
  {
    if(!strcmp(argv[arg], "--mode") && arg + 1 < argc)
      mode = argv[++arg];
    else if(!strcmp(argv[arg], "--script") && arg + 1 < argc)
      script = argv[++arg];
    else if(!strcmp(argv[arg], "--frames") && arg + 1 < argc)
      frames = (unsigned)atoi(argv[++arg]);
    else if(!strcmp(argv[arg], "--shot") && arg + 1 < argc)
      shot = argv[++arg];
    else if(!strcmp(argv[arg], "--bios-boot"))
      bios_boot = 1;
    else if(!strcmp(argv[arg], "--cycles-strict"))
      strict_cycles = 1;
    else if(!strcmp(argv[arg], "--trace") && arg + 1 < argc)
    {
      trace = fopen(argv[++arg], "w");
      if(!trace)
      {
        perror(argv[arg]);
        return 2;
      }
    }
#ifdef LOCKSTEP_JIT
    else if(!strcmp(argv[arg], "--jit"))
      use_jit = 1;
    else if(!strcmp(argv[arg], "--count"))
      count = 1;
    else if(!strcmp(argv[arg], "--dump") && arg + 1 < argc)
      dump = (uint32_t)strtoul(argv[++arg], NULL, 16);
#endif
    else if(path_count < 3)
      paths[path_count++] = argv[arg];
  }
  if(path_count < 2)
  {
    fprintf(stderr, "usage: lockstep [--mode step|block|run] [--script FILE] "
     "[--frames N] [--bios-boot] [--shot FILE] ROM BIOS [SAVE]\n");
    return 2;
  }
  if(script)
    load_script(script);
  if(!segment_count)
    add_segment(frames ? frames : 600, 0);

  boot(paths[0], paths[1], paths[2], bios_boot);
#ifdef LOCKSTEP_JIT
  if(use_jit)
  {
    ArmMipsHost host;
    uint32_t *code;

    mips_unicorn_init();
    jit = mips_unicorn_alloc(sizeof(ArmMips));
    code = mips_unicorn_alloc(JIT_CODE_BYTES);
    mips_unicorn_map(jit, sizeof(ArmMips), 0x10000000u);
    mips_unicorn_map(code, JIT_CODE_BYTES, 0x08800000u);
    /* Work RAM and ROM at addresses aligned to their size, as the PSP
       build should allocate them. */
    mips_unicorn_map(gba->memory.wram, 0x40000, 0x0A000000u);
    mips_unicorn_map(gba->memory.iwram, 0x8000, 0x0A040000u);
    mips_unicorn_map(gba->memory.rom, gba->memory.romSize, 0x0C000000u);
    host = mips_unicorn_host(jit);
    arm_mips_init(jit, strcmp(mode, "run") ? &replay : &direct, &host,
     code, JIT_CODE_BYTES);
    core = &jit->core;
    /* Step and block mode count instructions to know where a block
       ended; a run counts them for --trace. */
    jit->count_instructions = !count;
    jit->profiling = count;
    if(count)
      mips_unicorn_count(count_block);
  }
  else
#endif
  arm_core_init(core, strcmp(mode, "run") ? &replay : &direct);
  arm_core_set_waitcnt(core, gba->memory.io[REG_WAITCNT >> 1]);
  if(!strcmp(mode, "run"))
  {
    unsigned region;

    /* Work RAM and ROM are plain memory, read and written directly as an
       emulator would map them; the rest goes through mGBA. */
    core->map[2].read = core->map[2].write = (uint8_t *)gba->memory.wram;
    core->map[2].mask = 0x3FFFF;
    core->map[3].read = core->map[3].write = (uint8_t *)gba->memory.iwram;
    core->map[3].mask = 0x7FFF;
    if(gba->memory.romSize && !(gba->memory.romSize &
       (gba->memory.romSize - 1)))
      for(region = 8; region <= 12; region++)
      {
        core->map[region].read = (const uint8_t *)gba->memory.rom;
        core->map[region].mask = (uint32_t)gba->memory.romSize - 1;
      }
    load_core_from_mgba();
    /* mGBA no longer runs instructions; keep it from entering interrupts
       itself; the core does that from irq_line. */
    cpu->cpsr.i = 1;
  }

  start = clock();
  start_frame = mcore->frameCounter(mcore);
  frame_end = start_frame;
  for(s = 0; s < segment_count && ok; s++)
  {
    frame_end += segments[s].frames;
    if(frames && frame_end - start_frame > frames)
      frame_end = start_frame + frames;
    mcore->setKeys(mcore, segments[s].keys);
    while(ok && (int32_t)(mcore->frameCounter(mcore) - frame_end) < 0)
    {
      if(!strcmp(mode, "run"))
        run_once();
      else if(!strcmp(mode, "mgba"))
        mcore->runFrame(mcore);
      else
      {
        ok = compare_once(!strcmp(mode, "block"));
        /* The core's waitstates follow WAITCNT. */
        arm_core_set_waitcnt(core, gba->memory.io[REG_WAITCNT >> 1]);
      }
    }
    if(ok && segments[s].shot[0])
      write_bmp(segments[s].shot);
    if(frames && frame_end - start_frame >= frames)
      break;
  }
  seconds = (double)(clock() - start) / CLOCKS_PER_SEC;

  if(!strcmp(mode, "run"))
    printf("run: %u frames, %u guest instructions in %.2f s (%.1f M/s "
     "with mGBA's devices), %.2f s in the core and its bus calls (%.1f "
     "M/s), %u translation flushes\n",
     mcore->frameCounter(mcore) - start_frame, core->instructions, seconds,
     core->instructions / seconds / 1e6, core_seconds,
     core->instructions / core_seconds / 1e6, core->flushes);
  else if(!strcmp(mode, "mgba"))
    printf("mgba: %u frames in %.2f s\n",
     mcore->frameCounter(mcore) - start_frame, seconds);
  else
  {
    printf("%s: %llu instructions compared over %u frames in %.1f s%s\n",
     mode, compared, mcore->frameCounter(mcore) - start_frame, seconds,
     ok ? ", no divergence" : "");
    if(multiply_carries)
      printf("C after a multiply differed %lu times (not compared)\n",
       multiply_carries);
    print_cycle_summary();
  }
#ifdef LOCKSTEP_JIT
  if(jit && count)
    print_counts();
  if(jit && dump)
    dump_block(dump);
#endif
  if(trace)
    fclose(trace);
  if(shot)
    write_bmp(shot);
  mcore->deinit(mcore);
  return ok ? 0 : 1;
}
