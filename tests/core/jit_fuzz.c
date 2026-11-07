/*
 * Differential fuzzing of the MIPS backend (source/src/arm_mips.c) against
 * the interpreter, the generated code running in Unicorn (mips_unicorn.c).
 *
 * Each case builds a small GBA-like world twice, one copy per core: a
 * BIOS, work RAM and ROM filled with random instructions (ARM in the BIOS
 * and the first half of the ROM, Thumb in the second half), and I/O that
 * reads back a hash of the address. Every bus access is logged with the
 * cycle and instruction it came at; a store to HALTCNT asks the core to
 * stop and one to WAITCNT changes the waitstates, flushing the code. The
 * registers point into that memory or hold random values, in a random mode
 * and state; in half the cases some of the program's branches are idle
 * loops.
 *
 * The case then runs some blocks one at a time (arm_mips_run_block against
 * arm_core_run_block) and then linked code with random budgets, waitstates
 * and interrupt requests (arm_mips_run against arm_core_run). After each
 * step the two cores must agree on every register of every mode, the CPSR
 * and SPSRs, the cycle count, the prefetch buffer, the instructions run,
 * memory and the bus log.
 *
 * Usage: jit_fuzz [--seed N] [--cases N] [-v]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm_mips.h"
#include "mips_unicorn.h"

#define BIOS_SIZE 0x4000u
#define EWRAM_SIZE 0x40000u
#define IWRAM_SIZE 0x8000u
#define ROM_SIZE 0x100000u
#define VRAM_SIZE 0x1000u
#define LOG_SIZE 4096u
#define CODE_BYTES (8u << 20)

typedef struct
{
  uint32_t kind;      /* 'r' or 'w' << 8 | size */
  uint32_t address;
  uint32_t value;
  int32_t cycles;
  uint32_t pc;
} Access;

typedef struct
{
  ArmCore *core;
  uint8_t *ewram;
  uint8_t *iwram;
  uint8_t vram[VRAM_SIZE];  /* only for vram_case, through the bus */
  Access log[LOG_SIZE];
  unsigned log_count;
} World;

static uint8_t *bios;
static uint8_t *rom;
static World worlds[2];
static uint8_t *ewram_pages;  /* the backend's work RAM, placed by seed */
static ArmCore reference;
static ArmMips *jit;
static int vram_on;
static uint32_t rng_state;
static int verbose;
static unsigned long long guest_instructions;

static uint32_t rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static uint32_t hash(uint32_t x)
{
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

/* ---- The world's bus ---------------------------------------------------- */

static uint32_t read_raw(World *w, uint32_t address, unsigned size)
{
  uint32_t value = 0;
  unsigned region = (address >> 28) ? 1 : address >> 24;

  address &= ~(size - 1);
  switch(region)
  {
    case 0:
      if(address < BIOS_SIZE)
        memcpy(&value, bios + address, size);
      return value;
    case 2:
      memcpy(&value, w->ewram + (address & (EWRAM_SIZE - 1)), size);
      return value;
    case 3:
      memcpy(&value, w->iwram + (address & (IWRAM_SIZE - 1)), size);
      return value;
    case 6:
      if(!vram_on)
        break;
      memcpy(&value, w->vram + (address & (VRAM_SIZE - 1)), size);
      return value;
    case 8: case 9: case 10: case 11: case 12: case 13:
      memcpy(&value, rom + (address & (ROM_SIZE - 1)), size);
      return value;
    default:
      break;
  }
  value = hash(address ^ size);
  return size == 4 ? value : value & ((1u << (size * 8)) - 1);
}

static void note(World *w, uint32_t kind, uint32_t address, uint32_t value)
{
  if(w->log_count < LOG_SIZE)
  {
    Access *a = &w->log[w->log_count];

    a->kind = kind;
    a->address = address;
    a->value = value;
    a->cycles = w->core->cycles;
    a->pc = w->core->insn_pc;
  }
  w->log_count++;
}

static uint32_t bus_read(World *w, uint32_t address, unsigned size)
{
  uint32_t value = read_raw(w, address, size);

  note(w, 'r' << 8 | size, address, value);
  return value;
}

static void bus_write(World *w, uint32_t address, unsigned size,
 uint32_t value)
{
  note(w, 'w' << 8 | size, address, value);
  if(vram_on && address >> 24 == 6)
    memcpy(w->vram + (address & (VRAM_SIZE - 1) & ~(size - 1)), &value,
     size);
  if(address >> 24 != 4)
    return;
  if((address & 0xFFFFFC) == 0x300)
    arm_core_request_exit(w->core);
  if((address & 0xFFFFFC) == 0x204)
    arm_core_set_waitcnt(w->core, (uint16_t)(value & 0x5FFF));
}

static uint32_t read8(void *c, uint32_t a) { return bus_read(c, a, 1); }
static uint32_t read16(void *c, uint32_t a) { return bus_read(c, a, 2); }
static uint32_t read32(void *c, uint32_t a) { return bus_read(c, a, 4); }
static void write8(void *c, uint32_t a, uint32_t v) { bus_write(c, a, 1, v); }
static void write16(void *c, uint32_t a, uint32_t v) { bus_write(c, a, 2, v); }
static void write32(void *c, uint32_t a, uint32_t v) { bus_write(c, a, 4, v); }

static uint32_t code32(void *c, uint32_t a)
{
  return read_raw(c, a & ~3u, 4);
}

/* Past the BIOS, region 0 holds nothing: code reads 0 there. */
static int unmapped(void *c, uint32_t a)
{
  (void)c;
  return (a >> 24) == 0 && a >= BIOS_SIZE;
}

static void map_world(World *w)
{
  ArmCore *core = w->core;
  unsigned region;

  core->bus.context = w;
  core->map[2].read = core->map[2].write = w->ewram;
  core->map[2].mask = EWRAM_SIZE - 1;
  core->map[3].read = core->map[3].write = w->iwram;
  core->map[3].mask = IWRAM_SIZE - 1;
  for(region = 8; region <= 12; region++)
  {
    core->map[region].read = rom;
    core->map[region].mask = ROM_SIZE - 1;
  }
}

/* ---- Random cases ------------------------------------------------------- */

/* An ARM opcode: mostly unconditional, mostly low registers, often the
   same register twice (the flags of x - x, shifts of a register by
   itself). */
static uint32_t arm_opcode(void)
{
  uint32_t op = rng();

  if(rng() & 1)
    op = (op & 0x0FFFFFFFu) | 0xE0000000u;
  if(rng() & 1)
    op &= ~0x00088808u;
  if((rng() & 3) == 0)
    op = (op & ~15u) | ((op >> 16) & 15);
  return op;
}

static uint32_t thumb_opcode(void)
{
  uint32_t op = rng() & 0xFFFF;

  if((rng() & 3) == 0)
    op = (op & ~7u) | ((op >> 3) & 7);
  return op;
}

/* Whether an opcode may jump: the straight programs leave most of those
   out, so blocks run to their length limit and fall through into the next
   one (the one way into a block with the prefetch buffer not empty), while
   some still end in branches. */
static int jumps(uint32_t op, int thumb)
{
  if(thumb)
    return op >= 0xD000 || (op & 0xFF00) == 0x4700 ||
     (op & 0xFF00) == 0xBD00 || ((op & 0xFC00) == 0x4400 && (op & 0x87) ==
     0x87);
  return ((op >> 25) & 7) == 5 || ((op >> 25) & 7) == 4 ||
   ((op >> 12) & 15) == 15 || (op & 0x0FFFFFF0u) == 0x012FFF10u ||
   ((op >> 24) & 15) == 15;
}

static int straight;

static void fill(uint8_t *p, uint32_t bytes, int thumb)
{
  uint32_t i;

  for(i = 0; i < bytes; i += 4)
  {
    uint32_t word;

    if(thumb)
    {
      uint32_t low = thumb_opcode();
      uint32_t high = thumb_opcode();

      while(straight && jumps(low, 1) && (rng() & 15))
        low = thumb_opcode();
      while(straight && jumps(high, 1) && (rng() & 15))
        high = thumb_opcode();
      word = low | high << 16;
    }
    else
    {
      word = arm_opcode();
      while(straight && jumps(word, 0) && (rng() & 15))
        word = arm_opcode();
    }
    memcpy(p + i, &word, 4);
  }
}

/* Points the direct branches of the program of count instructions at p
   (running at pc) back into it, so it loops: links, the return stack and
   the dispatch cache get used, and code runs more than once. */
static void confine(uint8_t *p, uint32_t pc, unsigned count, int thumb)
{
  unsigned i;

  for(i = 0; i < count; i++)
  {
    uint32_t at = pc + i * (thumb ? 2u : 4u);
    uint32_t target = pc + (rng() % count) * (thumb ? 2u : 4u);
    int32_t offset = (int32_t)(target - at - (thumb ? 4u : 8u));

    if(!thumb)
    {
      uint32_t op;

      memcpy(&op, p + i * 4, 4);
      if(((op >> 25) & 7) == 5)
        op = (op & 0xFF000000u) | (((uint32_t)offset >> 2) & 0xFFFFFF);
      memcpy(p + i * 4, &op, 4);
    }
    else
    {
      uint16_t op;

      memcpy(&op, p + i * 2, 2);
      if((op >> 12) == 13 && ((op >> 8) & 15) < 14)
        op = (uint16_t)((op & 0xFF00) | (((uint32_t)offset >> 1) & 0xFF));
      else if((op >> 11) == 28)
        op = (uint16_t)(0xE000 | (((uint32_t)offset >> 1) & 0x7FF));
      else if((op >> 11) == 30 && i + 1 < count)
      {
        /* BL: lr = pc + 4 + high << 12, then lr + low << 1. */
        uint16_t low = (uint16_t)(0xF800 | (((uint32_t)offset >> 1) &
         0x7FF));

        op = (uint16_t)(0xF000 | (((uint32_t)offset >> 12) & 0x7FF));
        memcpy(p + i * 2, &op, 2);
        memcpy(p + (i + 1) * 2, &low, 2);
        i++;
        continue;
      }
      memcpy(p + i * 2, &op, 2);
    }
  }
}

/* Names some of the program's direct branches idle loops, in half the
   cases, without touching the random sequence: both cores get the list. */
static void pick_idle_loops(ArmCore *core, const uint8_t *p, uint32_t pc,
 unsigned count, int thumb, uint32_t seed)
{
  unsigned width = thumb ? 2u : 4u;
  unsigned i;

  core->idle_loop_count = 0;
  if(!(hash(seed) & 1))
    return;
  for(i = 0; i < count && core->idle_loop_count < ARM_IDLE_LOOPS; i++)
  {
    uint32_t at = pc + i * width;
    uint32_t op = 0;
    int branch;

    memcpy(&op, p + i * width, width);
    branch = thumb ? ((op >> 12) == 13 && ((op >> 8) & 15) < 14) ||
     (op >> 11) == 28 : ((op >> 25) & 7) == 5;
    if(branch && (hash(at ^ seed) & 1))
      core->idle_loops[core->idle_loop_count++] = at;
  }
}

/* A value for a register: an address in the world (or near it, or one of
   the program's own instructions, for stores over code it runs again), or
   a random number. */
static uint32_t random_value(uint32_t pc, unsigned count, int thumb)
{
  const uint32_t bases[] =
  {
    0x02000000u, 0x03000000u, 0x03007F00u, 0x08000000u, 0x080F0000u,
    0x04000000u, 0x05000000u, 0x00000000u, 0x0E000000u, pc & ~0xFFu,
    pc & ~0xFFu
  };
  uint32_t r = rng();

  if((r & 7) == 7)
    return pc + (rng() % count) * (thumb ? 2u : 4u);
  switch(r & 3)
  {
    case 0:
      return (r & 4) ? rng() : (rng() & 0x80000000u) | (rng() & 63);
    case 1:
      return rng() & 0xFF;
    default:
      return bases[(r >> 2) % (sizeof(bases) / sizeof(bases[0]))] +
       ((rng() & 0x3FF) & ((r & 0x80000000u) ? ~0u : ~3u));
  }
}

static const unsigned modes[7] =
{
  ARM_MODE_USER, ARM_MODE_FIQ, ARM_MODE_IRQ, ARM_MODE_SUPERVISOR,
  ARM_MODE_ABORT, ARM_MODE_UNDEFINED, ARM_MODE_SYSTEM
};

static void set_state(ArmCore *core, const uint32_t *values, uint32_t cpsr,
 uint32_t pc)
{
  unsigned m;
  unsigned r;
  unsigned k = 0;

  arm_core_set_cpsr(core, cpsr);
  for(m = 0; m < 7; m++)
    for(r = 8; r <= 16; r++)
      if(r != 15)
        *arm_core_register(core, modes[m], r) = values[k++];
  for(r = 0; r < 15; r++)
    core->v[r] = values[k++];
  core->v[15] = pc;
}

/* ---- Comparing ---------------------------------------------------------- */

static int compare(const char *what, uint32_t seed, unsigned step)
{
  ArmCore *a = &reference;
  ArmCore *b = &jit->core;
  char text[1024];
  size_t n = 0;
  unsigned m;
  unsigned r;
  unsigned i;

  text[0] = 0;
#define DIFF(...) n += (size_t)snprintf(text + n, n < sizeof(text) ? \
 sizeof(text) - n : 0, __VA_ARGS__)
  for(r = 0; r < 16; r++)
    if(a->v[r] != b->v[r])
      DIFF(" r%u %08X/%08X", r, a->v[r], b->v[r]);
  if(arm_core_cpsr(a) != arm_core_cpsr(b))
    DIFF(" cpsr %08X/%08X", arm_core_cpsr(a), arm_core_cpsr(b));
  for(m = 0; m < 7; m++)
    for(r = 8; r <= 16; r++)
      if(r != 15 && *arm_core_register(a, modes[m], r) !=
         *arm_core_register(b, modes[m], r))
        DIFF(" mode%02X.%u %08X/%08X", modes[m], r,
         *arm_core_register(a, modes[m], r),
         *arm_core_register(b, modes[m], r));
  if(a->cycles != b->cycles)
    DIFF(" cycles %d/%d", a->cycles, b->cycles);
  if(a->prefetch != b->prefetch)
    DIFF(" prefetch %u/%u", a->prefetch, b->prefetch);
  if(a->instructions != b->instructions)
    DIFF(" instructions %u/%u", a->instructions, b->instructions);
  if(a->waitcnt != b->waitcnt)
    DIFF(" waitcnt %04X/%04X", a->waitcnt, b->waitcnt);
  if(memcmp(worlds[0].ewram, worlds[1].ewram, EWRAM_SIZE))
    DIFF(" work RAM differs");
  if(memcmp(worlds[0].iwram, worlds[1].iwram, IWRAM_SIZE))
    DIFF(" internal RAM differs");
  if(memcmp(worlds[0].vram, worlds[1].vram, VRAM_SIZE))
    DIFF(" VRAM differs");
  if(worlds[0].log_count != worlds[1].log_count)
    DIFF(" %u/%u bus accesses", worlds[0].log_count, worlds[1].log_count);
  for(i = 0; i < worlds[0].log_count && i < worlds[1].log_count &&
      i < LOG_SIZE; i++)
    if(memcmp(&worlds[0].log[i], &worlds[1].log[i], sizeof(Access)))
    {
      const Access *x = &worlds[0].log[i];
      const Access *y = &worlds[1].log[i];

      DIFF(" access %u: %c%u %08X=%08X @%d pc %08X / %c%u %08X=%08X @%d "
       "pc %08X", i, x->kind >> 8, x->kind & 15, x->address, x->value,
       x->cycles, x->pc, y->kind >> 8, y->kind & 15, y->address, y->value,
       y->cycles, y->pc);
      break;
    }
#undef DIFF
  if(!n)
    return 1;
  printf("seed %u: %s %u differs (interpreter/backend):%s\n", seed, what,
   step, text);
  return 0;
}

static void print_block(ArmCore *core, uint32_t pc, int thumb)
{
  ArmIrOp ops[ARM_BLOCK_MAX_OPS];
  unsigned count = arm_translate(core, pc, thumb, ops, ARM_BLOCK_MAX_OPS,
   ARM_BLOCK_MAX_INSNS, NULL);
  unsigned i;

  printf("block at %08X (%s):\n", pc, thumb ? "thumb" : "arm");
  for(i = 0; i < count; i++)
    printf("  %3u %-8s f=%02X d=%3u a=%3u b=%08X\n", i,
     arm_ir_name(ops[i].code), ops[i].flags, ops[i].d, ops[i].a, ops[i].b);
}

/* ---- Cases -------------------------------------------------------------- */

/* Loops that rewrite one of their instructions every time round (mov r0
   with the next immediate, added into r3): in a block of their own after
   a branch, or later in the same block. The random cases rarely store
   over code they run again. */
static const uint32_t smc_arm[2][6] =
{
  /* str r1, [r2]; add r1, r1, #1; b 1f; 1: mov r0, #0; add r3, r3, r0;
     b loop */
  { 0xE5821000, 0xE2811001, 0xEAFFFFFF, 0xE3A00000, 0xE0833000, 0xEAFFFFF9 },
  /* str r1, [r2]; add r1, r1, #1; mov r0, #0; add r3, r3, r0; b loop */
  { 0xE5821000, 0xE2811001, 0xE3A00000, 0xE0833000, 0xEAFFFFFA, 0 }
};
static const uint16_t smc_thumb[2][6] =
{
  /* strh r1, [r2]; add r1, #1; b 1f; 1: mov r0, #0; add r3, r3, r0;
     b loop */
  { 0x8011, 0x3101, 0xE7FF, 0x2000, 0x181B, 0xE7F9 },
  /* strh r1, [r2]; add r1, #1; mov r0, #0; add r3, r3, r0; b loop */
  { 0x8011, 0x3101, 0x2000, 0x181B, 0xE7FA, 0 }
};

/* ROM code storing through a literal (an address the decoder knows) over
   a loop in internal RAM that jumps back:
     rom: ldr r2, =0x03001F00; str r1, [r2]; add r1, r1, #1; mov pc, r4
     ram: mov r0, #0; add r3, r3, r0; mov pc, r5
   then the same with a cheat hook on the RAM loop, which the interpreter
   runs: the compiled store must drop that block too. */
static const uint32_t smc_rom[5] =
 { 0xE59F2008, 0xE5821000, 0xE2811001, 0xE1A0F004, 0x03001F00 };
static const uint32_t smc_ram[3] = { 0xE3A00000, 0xE0833000, 0xE1A0F005 };

static unsigned hook_calls;

static void count_hook(ArmCore *core)
{
  (void)core;
  hook_calls++;
}

static int smc_case(unsigned variant)
{
  int thumb = variant < 8 && (variant & 1);
  int same_block = (variant >> 1) & 1;
  int iwram = variant >= 8 || ((variant >> 2) & 1);
  uint32_t pc = (iwram ? 0x03000000u : 0x02000000u) + 0x1F00u;
  uint32_t target = pc + (thumb ? 2u : 4u) * (same_block ? 2u : 3u);
  uint32_t dropped = jit->stats.dropped;
  unsigned w;
  unsigned i;

  for(w = 0; w < 2; w++)
  {
    uint8_t *ram = iwram ? worlds[w].iwram : worlds[w].ewram;
    ArmCore *core = w ? &jit->core : &reference;

    if(variant >= 8)
    {
      memcpy(rom + 0x1000, smc_rom, sizeof(smc_rom));
      memcpy(ram + 0x1F00, smc_ram, sizeof(smc_ram));
    }
    else if(thumb)
      memcpy(ram + 0x1F00, smc_thumb[same_block], sizeof(smc_thumb[0]));
    else
      memcpy(ram + 0x1F00, smc_arm[same_block], sizeof(smc_arm[0]));
    arm_core_reset(core);
    core->hook = variant == 9 ? 0x03001F00u : 0;
    core->hooked = count_hook;
    arm_core_set_cpsr(core, ARM_MODE_SYSTEM | (thumb ? ARM_CPSR_T : 0));
    core->v[1] = thumb ? 0x2000u : 0xE3A00000u;
    core->v[2] = target;
    core->v[3] = 0;
    core->v[4] = 0x03001F00u;
    core->v[5] = 0x08001000u;
    core->v[15] = variant >= 8 ? 0x08001000u : pc;
    core->prefetch = 0;
    core->instructions = 0;
    worlds[w].log_count = 0;
  }
  /* Under 256 rounds, before Thumb's immediate overflows into the
     register field. */
  for(i = 0; i < 6; i++)
  {
    int32_t ran = arm_core_run(&reference, 250);

    if(arm_mips_run(jit, 250) != ran || !compare("smc run", variant, i))
      return 0;
  }
  if(reference.v[3] == 0 || (variant == 9 ? hook_calls == 0 :
     jit->stats.dropped == dropped))
  {
    printf("smc case %u: no code was dropped\n", variant);
    return 0;
  }
  reference.hook = jit->core.hook = 0;
  return 1;
}

/* A slide through zeroed work RAM (Thumb movs r0, r0, two blocks to a
   page) with no branch for far longer than ARM_RUNAWAY_SHIFT allows: both
   cores end the run at the same block. The second run goes through code
   already linked, where only the backend's own check can stop it. Then
   zeros past the BIOS, where nothing is mapped, which neither core
   translates: two ARM runs from 4000h, each ending on its budget at one
   cycle a zero, and a Thumb run from the end of the region into the hash
   words at 1000000h. */
static int slide_case(void)
{
  const int32_t budget = 1000;
  int32_t ran[2];
  unsigned w;
  unsigned i;

  for(i = 0; i < 2; i++)
  {
    for(w = 0; w < 2; w++)
    {
      ArmCore *core = w ? &jit->core : &reference;

      /* The reset flushes the code, so only before the first run. */
      if(i == 0)
      {
        memset(worlds[w].ewram, 0, EWRAM_SIZE);
        arm_core_reset(core);
        arm_core_set_cpsr(core, ARM_MODE_SYSTEM | ARM_CPSR_T);
      }
      core->v[15] = 0x02000000u;
      core->prefetch = 0;
      core->instructions = 0;
      worlds[w].log_count = 0;
    }
    ran[0] = arm_core_run(&reference, budget);
    ran[1] = arm_mips_run(jit, budget);
    if(!compare("slide run", 0, i))
      return 0;
    if(ran[0] != ran[1] || ran[0] - budget < 1 << ARM_RUNAWAY_SHIFT ||
       ran[0] - budget > (1 << ARM_RUNAWAY_SHIFT) + 512)
    {
      printf("slide run %u: ended %d/%d cycles past the budget\n", i,
       ran[0] - budget, ran[1] - budget);
      return 0;
    }
  }
  for(i = 0; i < 3; i++)
  {
    for(w = 0; w < 2; w++)
    {
      ArmCore *core = w ? &jit->core : &reference;

      if(i != 1)
      {
        arm_core_set_cpsr(core, ARM_MODE_SYSTEM | (i ? ARM_CPSR_T : 0));
        core->v[15] = i ? 0x00FFFF00u : 0x00004000u;
      }
      core->instructions = 0;
      worlds[w].log_count = 0;
    }
    ran[0] = arm_core_run(&reference, budget);
    ran[1] = arm_mips_run(jit, budget);
    if(!compare("zeros run", 0, i))
      return 0;
    if(i < 2 && (ran[0] != budget || ran[1] != budget))
    {
      printf("zeros run %u: ended %d/%d cycles past the budget\n", i,
       ran[0] - budget, ran[1] - budget);
      return 0;
    }
  }
  return 1;
}

/* Stores over the next two instructions, which run as the pipeline
   fetched them before the store (arm_core_window), with the store last in
   its page, so that they are in the next one, where nothing is translated
   yet: the backend must take that store to C all the same. ARM str r1,
   [pc, #-4] over mov r0, #1; Thumb strh r1, [r3, #2] over movs r0, #1;
   ARM stmia r12, {r1, r2} over add r0, r0, #1 and #2; in work RAM and in
   internal RAM. A second pass puts the code back and drops only what was
   translated after the store, so the store runs from its block compiled
   the first time; a third one, after a reset, steps one instruction at a
   time over code no block holds. */
static const uint32_t window_arm[3] = { 0xE50F1004, 0xE3A00001, 0xEAFFFFFE };
static const uint32_t window_thumb[2] = { 0x20018059, 0xE7FE };
static const uint32_t window_stm[4] =
 { 0xE88C0006, 0xE2800001, 0xE2800002, 0xEAFFFFFE };

static int window_case(unsigned variant)
{
  static const uint32_t expected[3] = { 1, 1, 3 };
  unsigned kind = variant % 3;
  int thumb = kind == 1;
  const uint32_t *code = kind == 0 ? window_arm : thumb ? window_thumb :
   window_stm;
  size_t bytes = kind == 0 ? sizeof(window_arm) : thumb ?
   sizeof(window_thumb) : sizeof(window_stm);
  uint32_t width = thumb ? 2u : 4u;
  uint32_t pc = (variant >= 3 ? 0x03002100u : 0x02002100u) - width;
  unsigned pass;
  unsigned w;
  unsigned n;

  for(pass = 0; pass < 3; pass++)
  {
    for(w = 0; w < 2; w++)
    {
      ArmCore *core = w ? &jit->core : &reference;

      memcpy((variant >= 3 ? worlds[w].iwram : worlds[w].ewram) +
       (pc & 0x7FFF), code, bytes);
      if(pass != 1)
      {
        arm_core_reset(core);
        arm_core_set_cpsr(core, ARM_MODE_SYSTEM |
         (thumb ? ARM_CPSR_T : 0));
      }
      else
        arm_core_invalidate(core, pc + width, (uint32_t)bytes - width);
      core->v[0] = 0;
      core->v[1] = thumb ? 0x2002u : kind == 0 ? 0xE3A00002u : 0xE2800010u;
      core->v[2] = 0xE2800020u;
      core->v[3] = pc;
      core->v[12] = pc + 4;
      core->v[15] = pc;
      core->prefetch = 0;
      core->instructions = 0;
      worlds[w].log_count = 0;
    }
    if(pass == 2)
      for(n = 0; n < 4; n++)
      {
        if(arm_core_step(&reference) != arm_mips_step(jit) ||
           !compare("window step", variant, n))
          return 0;
      }
    else if(arm_core_run(&reference, 100) != arm_mips_run(jit, 100) ||
       !compare("window run", variant, pass))
      return 0;
    if(reference.v[0] != expected[kind])
    {
      printf("window case %u, pass %u: r0 %u, not %u\n", variant, pass,
       reference.v[0], expected[kind]);
      return 0;
    }
  }
  return 1;
}

/* Code in VRAM (mov r0, #n; b .), which goes through the bus: run, then
   rewritten by a store from internal RAM (str r1, [r2]; b .) and behind
   the cores' backs (DMA), and run again each time: the new version. */
static int vram_case(void)
{
  static const uint32_t old[2] = { 0xE3A00001, 0xEAFFFFFE };
  static const uint32_t store[2] = { 0xE5821000, 0xEAFFFFFE };
  const uint32_t code = 0x06000100u;
  unsigned n;
  unsigned w;

  vram_on = 1;
  for(n = 1; n <= 3; n++)
  {
    for(w = 0; w < 2; w++)
    {
      ArmCore *core = w ? &jit->core : &reference;

      if(n == 1)
      {
        memset(worlds[w].vram, 0, VRAM_SIZE);
        memcpy(worlds[w].vram + (code & (VRAM_SIZE - 1)), old, sizeof(old));
        memcpy(worlds[w].iwram + 0x3000, store, sizeof(store));
        arm_core_reset(core);
        arm_core_set_cpsr(core, ARM_MODE_SYSTEM);
      }
      if(n == 3)
      {
        worlds[w].vram[code & (VRAM_SIZE - 1)] = 3;
        arm_core_invalidate(core, code, 4);
      }
      core->v[1] = 0xE3A00002u;
      core->v[2] = code;
      core->v[15] = n == 2 ? 0x03003000u : code;
      core->prefetch = 0;
      core->instructions = 0;
      worlds[w].log_count = 0;
    }
    if(n == 2)
    {
      if(arm_core_run(&reference, 50) != arm_mips_run(jit, 50) ||
         !compare("vram store", 0, n))
        return 0;
      reference.v[15] = jit->core.v[15] = code;
    }
    if(arm_core_run(&reference, 50) != arm_mips_run(jit, 50) ||
       !compare("vram run", 0, n))
      return 0;
    if(reference.v[0] != n)
    {
      printf("vram case: version %u returned %u\n", n, reference.v[0]);
      return 0;
    }
  }
  vram_on = 0;
  return 1;
}

/* Multiplies whose operands sit at the byte boundaries of the multiplier's
   early termination, both signs, run from internal RAM and from the ROM
   with the prefetch buffer on: the cycles must agree. */
static const uint32_t multiply_arm[8] =
{
  0xE0020190,   /* mul r2, r0, r1 */
  0xE0232190,   /* mla r3, r0, r1, r2 */
  0xE0854190,   /* umull r4, r5, r0, r1 */
  0xE0A54190,   /* umlal r4, r5, r0, r1 */
  0xE0C76190,   /* smull r6, r7, r0, r1 */
  0xE0E76190,   /* smlal r6, r7, r0, r1 */
  0xE0180091,   /* muls r8, r1, r0 */
  0xEAFFFFFE    /* b . */
};
/* muls r2, r1; muls r1, r0; b . */
static const uint16_t multiply_thumb[4] = { 0x434A, 0x4341, 0xE7FE, 0 };
static const uint32_t multiply_values[] =
{
  0, 1, 0x7F, 0x80, 0xFF, 0x100, 0xFFFF, 0x10000, 0xFFFFFF, 0x1000000,
  0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFF80, 0xFFFFFF00, 0xFFFF7FFF,
  0xFFFF0000, 0xFF7FFFFF, 0xFF000000, 0x12345678
};

static int multiply_case(void)
{
  unsigned n = sizeof(multiply_values) / sizeof(multiply_values[0]);
  unsigned place;

  for(place = 0; place < 4; place++)
  {
    int thumb = place & 1;
    int in_rom = place >> 1;
    uint32_t pc = in_rom ? 0x080F0000u : 0x03007000u;
    unsigned w;
    unsigned i;

    for(w = 0; w < 2; w++)
    {
      uint8_t *at = in_rom ? rom + 0xF0000 : worlds[w].iwram + 0x7000;

      if(thumb)
        memcpy(at, multiply_thumb, sizeof(multiply_thumb));
      else
        memcpy(at, multiply_arm, sizeof(multiply_arm));
    }
    for(i = 0; i < n * n; i++)
    {
      for(w = 0; w < 2; w++)
      {
        ArmCore *core = w ? &jit->core : &reference;

        arm_core_reset(core);
        arm_core_set_waitcnt(core, 0x4317);
        arm_core_set_cpsr(core, ARM_MODE_SYSTEM | (thumb ? ARM_CPSR_T : 0));
        core->v[0] = multiply_values[i / n];
        core->v[1] = multiply_values[i % n];
        core->v[15] = pc;
        core->prefetch = 0;
        core->instructions = 0;
        core->cycles = 0;
        worlds[w].log_count = 0;
      }
      arm_core_run_block(&reference);
      arm_mips_run_block(jit);
      if(!compare("multiply block", place, i))
        return 0;
      if(arm_core_run(&reference, 60) != arm_mips_run(jit, 60) ||
         !compare("multiply run", place, i))
        return 0;
    }
  }
  return 1;
}

/* With sampling set, the block generated code last entered (where a
   profiler would put a sample) must be a block of the cache compiled at
   that address. Runs that flushed or dropped code are left out. */
static unsigned long long sampled_runs;

static int sampled_block_ok(uint32_t flushes, uint32_t dropped)
{
  uint32_t key = arm_mips_sampled_key(jit);
  unsigned i;

  if(!jit->sampling || !jit->entered || jit->stats.flushes != flushes ||
     jit->stats.dropped != dropped)
    return 1;
  for(i = 0; i < jit->core.block_count; i++)
    if(jit->core.blocks[i].key == key && jit->code[i] == jit->entered - 8)
    {
      sampled_runs++;
      return 1;
    }
  printf("sampled block %08X at %08X is no compiled block\n", key,
   jit->entered);
  return 0;
}

static int run_case(uint32_t seed)
{
  uint32_t values[7 * 8 + 15];
  uint32_t cpsr;
  uint32_t pc;
  uint16_t waitcnt;
  uint8_t *program;
  uint32_t size;
  uint32_t offset;
  unsigned count;
  unsigned i;
  int thumb;

  rng_state = seed ? seed : 1;
  /* The backend's work RAM where the low half of its address is negative
     as an offset, or, for every other pair of seeds, where it leaves no
     room for the later words of a run, which then add the whole address.
     The core's reset below drops the code built for the other place. */
  worlds[1].ewram = ewram_pages + ((seed & 2) ? 0xFFCu : 0x8000u);
  map_world(&worlds[1]);
  fill(bios, BIOS_SIZE, 0);
  fill(rom, ROM_SIZE / 2, 0);
  fill(rom + ROM_SIZE / 2, ROM_SIZE / 2, 1);
  fill(worlds[0].ewram, EWRAM_SIZE, rng() & 1);
  fill(worlds[0].iwram, IWRAM_SIZE, rng() & 1);

  /* The program: up to 256 instructions whose branches stay inside, or
   in every other case no branches at all. */
  thumb = (int)(rng() & 1);
  straight = (int)(seed & 1);
  count = 8 + rng() % 248;
  switch(rng() % 3)
  {
    case 0:
      program = rom + (thumb ? ROM_SIZE / 2 : 0);
      size = ROM_SIZE / 2;
      pc = 0x08000000u + (uint32_t)(program - rom);
      break;
    case 1:
      program = worlds[0].iwram;
      size = IWRAM_SIZE;
      pc = 0x03000000u;
      break;
    default:
      program = worlds[0].ewram;
      size = EWRAM_SIZE;
      pc = 0x02000000u;
      break;
  }
  offset = rng() % (size - 4 * count) & ~3u;
  if(thumb)
    offset += rng() & 2;
  program += offset;
  pc += offset;
  fill(program, (count * (thumb ? 2 : 4) + 3) & ~3u, thumb);
  confine(program, pc, count, thumb);
  straight = 0;
  memcpy(worlds[1].ewram, worlds[0].ewram, EWRAM_SIZE);
  memcpy(worlds[1].iwram, worlds[0].iwram, IWRAM_SIZE);

  for(i = 0; i < sizeof(values) / sizeof(values[0]); i++)
    values[i] = random_value(pc, count, thumb);
  cpsr = (rng() & 0xF00000C0u) | modes[rng() % 7] |
   (thumb ? ARM_CPSR_T : 0);
  waitcnt = (uint16_t)(rng() & 0x5FFF);

  for(i = 0; i < 2; i++)
  {
    ArmCore *core = i ? &jit->core : &reference;

    arm_core_reset(core);
    arm_core_set_waitcnt(core, waitcnt);
    pick_idle_loops(core, program, pc, count, thumb, seed);
    set_state(core, values, cpsr, pc);
    core->prefetch = 0;
    core->instructions = 0;
    core->cycles = 0;
    core->irq_line = 0;
    worlds[i].log_count = 0;
  }

  for(i = 0; i < 6; i++)
  {
    uint32_t at = reference.v[15];
    int in_thumb = (reference.cpsr & ARM_CPSR_T) != 0;

    arm_core_run_block(&reference);
    arm_mips_run_block(jit);
    if(!compare("block", seed, i))
    {
      if(verbose)
        print_block(&reference, at, in_thumb);
      return 0;
    }
  }
  for(i = 0; i < 48; i++)
  {
    int32_t budget = (int32_t)(rng() % 1500) + 1;
    int irq = (rng() & 7) == 0;
    int32_t ran[2];
    uint32_t flushes;
    uint32_t dropped;

    reference.irq_line = jit->core.irq_line = irq;
    if((rng() & 15) == 0)
    {
      waitcnt = (uint16_t)(rng() & 0x5FFF);
      arm_core_set_waitcnt(&reference, waitcnt);
      arm_core_set_waitcnt(&jit->core, waitcnt);
    }
    flushes = jit->stats.flushes;
    dropped = jit->stats.dropped;
    jit->entered = 0;
    ran[0] = arm_core_run(&reference, budget);
    ran[1] = arm_mips_run(jit, budget);
    if(!sampled_block_ok(flushes, dropped))
      return 0;
    if(!compare("run", seed, i) || ran[0] != ran[1])
    {
      if(ran[0] != ran[1])
        printf("seed %u: run %u returned %d/%d\n", seed, i, ran[0], ran[1]);
      return 0;
    }
  }
  guest_instructions += reference.instructions;
  return 1;
}

/* A clock that ticks once per reading, so each translation adds one to
   the decode and compile times. */
static uint32_t clock_ticks;

static uint32_t tick(void *context)
{
  (void)context;
  return clock_ticks++;
}

int main(int argc, char **argv)
{
  ArmBus bus = { NULL, read8, read16, read32, write8, write16, write32,
   code32, unmapped };
  ArmMipsHost host;
  uint32_t *code;
  uint32_t seed = 1;
  unsigned cases = 1000;
  unsigned passed = 0;
  unsigned i;
  unsigned n;
  int arg;

  for(arg = 1; arg < argc; arg++)
  {
    if(!strcmp(argv[arg], "--seed") && arg + 1 < argc)
      seed = (uint32_t)strtoul(argv[++arg], NULL, 0);
    else if(!strcmp(argv[arg], "--cases") && arg + 1 < argc)
      cases = (unsigned)strtoul(argv[++arg], NULL, 0);
    else if(!strcmp(argv[arg], "-v"))
      verbose = 1;
  }

  mips_unicorn_init();
  jit = mips_unicorn_alloc(sizeof(ArmMips));
  code = mips_unicorn_alloc(CODE_BYTES);
  bios = mips_unicorn_alloc(BIOS_SIZE);
  rom = mips_unicorn_alloc(ROM_SIZE);
  worlds[0].ewram = mips_unicorn_alloc(EWRAM_SIZE);
  ewram_pages = mips_unicorn_alloc(EWRAM_SIZE + 0x9000u);
  worlds[1].ewram = ewram_pages + 0x8000u;
  for(i = 0; i < 2; i++)
    worlds[i].iwram = mips_unicorn_alloc(IWRAM_SIZE);
  mips_unicorn_map(jit, sizeof(ArmMips), 0x10000000u);
  mips_unicorn_map(code, CODE_BYTES, 0x08800000u);
  mips_unicorn_map(rom, ROM_SIZE, 0x0C000000u);
  /* Work RAM not aligned to its size, for the code that adds the offset:
     at 0x0A00F000 or 0x0A007FFC (see run_case); internal RAM aligned but
     not to 64 KB, for the code that inserts it; ROM aligned. */
  mips_unicorn_map(ewram_pages, EWRAM_SIZE + 0x9000u, 0x0A007000u);
  mips_unicorn_map(worlds[1].iwram, IWRAM_SIZE, 0x0A068000u);

  host = mips_unicorn_host(jit);
  host.clock = tick;
  arm_core_init(&reference, &bus);
  arm_mips_init(jit, &bus, &host, code, CODE_BYTES);
  jit->count_instructions = 1;
  worlds[0].core = &reference;
  worlds[1].core = &jit->core;
  map_world(&worlds[0]);
  map_world(&worlds[1]);

  if(!multiply_case())
    return 1;
  printf("jit_fuzz: multiplies agree on %u operand pairs\n",
   (unsigned)(4 * sizeof(multiply_values) / sizeof(multiply_values[0]) *
   sizeof(multiply_values) / sizeof(multiply_values[0])));
  for(i = 0; i < 10; i++)
    if(!smc_case(i))
      return 1;
  printf("jit_fuzz: 10 self-modifying loops agree, %u blocks dropped\n",
   jit->stats.dropped);
  for(i = 0; i < 6; i++)
    if(!window_case(i))
      return 1;
  if(!vram_case())
    return 1;
  printf("jit_fuzz: 6 stores over the prefetch window and code in VRAM"
   " agree\n");
  if(!slide_case())
    return 1;
  printf("jit_fuzz: a slide past the budget ends on the same cycle, zeros"
   " where nothing is mapped on the budget\n");
  /* Host instructions per guest instruction over the random cases, a
     before and after number for changes to the generated code. */
  mips_unicorn_count(NULL);
  mips_unicorn_executed = 0;
  for(i = 0; i < cases; i++)
  {
    if(run_case(seed + i))
      passed++;
    else if(verbose)
      break;
  }
  printf("jit_fuzz: %u/%u cases agree (seeds %u-%u), %llu guest "
   "instructions; %u blocks compiled, %u links, %u dispatch misses, %u "
   "bails, %u dropped, %u flushes, %llu slow accesses; %.2f MIPS "
   "instructions per guest instruction\n", passed, i, seed, seed + i - 1,
   guest_instructions, jit->stats.blocks, jit->stats.links,
   jit->stats.misses, jit->stats.bails, jit->stats.dropped,
   jit->stats.flushes, mips_unicorn_calls, guest_instructions ?
   (double)mips_unicorn_executed / (double)guest_instructions : 0.0);
  /* The first cases again with blocks noting their entry, as for the
     profiler: same results, and the notes point at the blocks. */
  jit->sampling = 1;
  for(n = 0; n < 4 && n < cases; n++)
    if(!run_case(seed + n))
      return 1;
  printf("jit_fuzz: with sampling, %llu runs left from the block noted\n",
   sampled_runs);
  if(!sampled_runs)
    return 1;
  if(jit->stats.compile_ticks != jit->stats.blocks + jit->stats.interpreted)
  {
    printf("jit_fuzz: %u compile ticks for %u blocks compiled and %u "
     "interpreted\n", jit->stats.compile_ticks, jit->stats.blocks,
     jit->stats.interpreted);
    return 1;
  }
  return passed == i ? 0 : 1;
}
