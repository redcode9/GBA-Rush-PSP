/*
 * Runs the ARM7TDMI core against the SingleStepTests ARM7TDMI suite
 * (github.com/SingleStepTests/ARM7TDMI, the .json.bin files in v1): each
 * test sets the registers, runs one instruction and lists the expected
 * registers and memory transactions.
 *
 * Usage: single_step [-v] FILE.json.bin...
 * Prints passed/total per file; -v prints the first failures in detail.
 * Built with SINGLE_STEP_JIT (and Unicorn, see mips_unicorn.h), the MIPS
 * backend runs every test as well and must end as the interpreter does.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm_core.h"
#ifdef SINGLE_STEP_JIT
#include "mips_unicorn.h"
#endif

typedef struct
{
  uint32_t r[16];
  uint32_t fiq[7];
  uint32_t svc[2], abt[2], irq[2], und[2];
  uint32_t cpsr;
  uint32_t spsr[5];   /* fiq, svc, abt, irq, und */
  uint32_t pipeline[2];
} State;

typedef struct
{
  uint32_t kind, size, address, data;
} Transaction;

typedef struct
{
  State initial;
  State final;
  Transaction transactions[64];
  unsigned count;
  uint32_t opcode;
  uint32_t base;
} Test;

static ArmCore interpreter;
static ArmCore *core = &interpreter;
static const Test *current;
static unsigned used[64];
static int bus_error;
static char bus_message[160];

static uint32_t get32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
   (uint32_t)p[3] << 24;
}

static size_t parse_state(const uint8_t *p, State *state)
{
  uint32_t v[40];
  unsigned i;

  for(i = 0; i < 40; i++)
    v[i] = get32(p + 8 + 4 * i);
  memcpy(state->r, v, sizeof(state->r));
  memcpy(state->fiq, v + 16, sizeof(state->fiq));
  memcpy(state->svc, v + 23, 8);
  memcpy(state->abt, v + 25, 8);
  memcpy(state->irq, v + 27, 8);
  memcpy(state->und, v + 29, 8);
  state->cpsr = v[31];
  memcpy(state->spsr, v + 32, sizeof(state->spsr));
  memcpy(state->pipeline, v + 37, 8);
  return get32(p);
}

static size_t parse_test(const uint8_t *p, Test *test)
{
  size_t size = get32(p);
  const uint8_t *q = p + 4;
  unsigned i;

  q += parse_state(q, &test->initial);
  q += parse_state(q, &test->final);
  test->count = get32(q + 8);
  if(test->count > 64)
    test->count = 64;
  for(i = 0; i < test->count; i++)
  {
    const uint8_t *t = q + 12 + 24 * i;

    test->transactions[i].kind = get32(t);
    test->transactions[i].size = get32(t + 4);
    test->transactions[i].address = get32(t + 8);
    test->transactions[i].data = get32(t + 12);
  }
  q += get32(q);
  test->opcode = get32(q + 8);
  test->base = get32(q + 12);
  return size;
}

static int thumb_state(const State *state)
{
  return (state->cpsr & ARM_CPSR_T) != 0;
}

static uint32_t instruction_address(const State *state)
{
  return state->r[15] - (thumb_state(state) ? 4 : 8);
}

/* Finds the next unused transaction of a kind at an address. */
static int find(unsigned kind, uint32_t address, unsigned size)
{
  unsigned i;

  for(i = 0; i < current->count; i++)
  {
    const Transaction *t = &current->transactions[i];

    if(!used[i] && t->kind == kind && t->size == size &&
       (t->address & ~(size - 1)) == address)
      return (int)i;
  }
  return -1;
}

static uint32_t bus_read(uint32_t address, unsigned size)
{
  int i = find(1, address, size);

  if(i < 0)
  {
    if(!bus_error)
      snprintf(bus_message, sizeof(bus_message),
       "unexpected read%u at %08X", size * 8, address);
    bus_error = 1;
    return address;
  }
  used[i] = 1;
  return current->transactions[i].data;
}

static void bus_write(uint32_t address, uint32_t value, unsigned size)
{
  int i = find(2, address, size);
  uint32_t mask = size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1;

  if(i < 0 || (current->transactions[i].data & mask) != (value & mask))
  {
    if(!bus_error)
      snprintf(bus_message, sizeof(bus_message),
       "write%u %08X = %08X, expected %08X", size * 8, address, value,
       i < 0 ? 0 : current->transactions[i].data);
    bus_error = 1;
    if(i < 0)
      return;
  }
  used[i] = 1;
}

static uint32_t read8(void *context, uint32_t address)
{
  (void)context;
  return bus_read(address, 1);
}

static uint32_t read16(void *context, uint32_t address)
{
  (void)context;
  return bus_read(address, 2);
}

static uint32_t read32(void *context, uint32_t address)
{
  (void)context;
  return bus_read(address, 4);
}

static void write8(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  bus_write(address, value, 1);
}

static void write16(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  bus_write(address, value, 2);
}

static void write32(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  bus_write(address, value, 4);
}

/* The instruction itself, or a literal the decoder wants to fold, which
   the test lists as a read. Other addresses read as themselves. */
static uint32_t code32(void *context, uint32_t address)
{
  uint32_t at = instruction_address(&current->initial);
  unsigned i;

  (void)context;
  if(address == (at & ~3u))
  {
    if(!thumb_state(&current->initial))
      return current->opcode;
    return (at & 2) ? current->opcode << 16 : current->opcode & 0xFFFF;
  }
  for(i = 0; i < current->count; i++)
  {
    const Transaction *t = &current->transactions[i];

    if(t->kind == 1 && (t->address & ~3u) == address)
    {
      unsigned shift = (t->address & (4 - t->size)) * 8;

      if(t->size == 4)
        return t->data;
      return t->data << shift;
    }
  }
  return address;
}

static const unsigned modes[6] =
{
  ARM_MODE_USER, ARM_MODE_FIQ, ARM_MODE_SUPERVISOR, ARM_MODE_ABORT,
  ARM_MODE_IRQ, ARM_MODE_UNDEFINED
};

/* The test's view of one mode's register 8-14 (user mode for others). */
static uint32_t *state_register(State *state, unsigned mode, unsigned reg)
{
  if(mode == ARM_MODE_FIQ && reg >= 8 && reg <= 14)
    return &state->fiq[reg - 8];
  if(reg == 13 || reg == 14)
  {
    switch(mode)
    {
      case ARM_MODE_SUPERVISOR: return &state->svc[reg - 13];
      case ARM_MODE_ABORT: return &state->abt[reg - 13];
      case ARM_MODE_IRQ: return &state->irq[reg - 13];
      case ARM_MODE_UNDEFINED: return &state->und[reg - 13];
      default: break;
    }
  }
  return &state->r[reg];
}

static uint32_t *state_spsr(State *state, unsigned mode)
{
  switch(mode)
  {
    case ARM_MODE_FIQ: return &state->spsr[0];
    case ARM_MODE_SUPERVISOR: return &state->spsr[1];
    case ARM_MODE_ABORT: return &state->spsr[2];
    case ARM_MODE_IRQ: return &state->spsr[3];
    case ARM_MODE_UNDEFINED: return &state->spsr[4];
    default: return NULL;
  }
}

/* The suite keeps every bank in its own array and r[] as user mode. */
static void load_state(State *state)
{
  unsigned m;
  unsigned reg;

  arm_core_reset(core);
  arm_core_set_cpsr(core, state->cpsr);
  for(reg = 0; reg < 16; reg++)
    core->v[reg] = state->r[reg];
  for(m = 0; m < 6; m++)
  {
    uint32_t *spsr = state_spsr(state, modes[m]);

    for(reg = 8; reg < 15; reg++)
      *arm_core_register(core, modes[m], reg) =
       *state_register(state, modes[m], reg);
    if(spsr)
      *arm_core_register(core, modes[m], 16) = *spsr;
  }
  core->v[15] = instruction_address(state);
}

static void save_state(State *state)
{
  unsigned m;
  unsigned reg;

  memset(state, 0, sizeof(*state));
  state->cpsr = arm_core_cpsr(core);
  for(reg = 0; reg < 8; reg++)
    state->r[reg] = core->v[reg];
  for(m = 0; m < 6; m++)
  {
    uint32_t *spsr = state_spsr(state, modes[m]);

    for(reg = 8; reg < 15; reg++)
      *state_register(state, modes[m], reg) =
       *arm_core_register(core, modes[m], reg);
    if(spsr)
      *spsr = *arm_core_register(core, modes[m], 16);
  }
  /* After a refill r15 reads two instructions on. */
  state->r[15] = core->v[15] + ((state->cpsr & ARM_CPSR_T) ? 4 : 8);
}

static int compare(const State *got, const State *want, char *out,
 size_t size)
{
  State a = *got;
  State b = *want;
  size_t used_bytes = 0;
  unsigned i;

  memset(a.pipeline, 0, sizeof(a.pipeline));
  memset(b.pipeline, 0, sizeof(b.pipeline));
  if(!memcmp(&a, &b, sizeof(a)))
    return 1;
  if(!out)
    return 0;
  for(i = 0; i < 16; i++)
    if(a.r[i] != b.r[i])
      used_bytes += (size_t)snprintf(out + used_bytes, size - used_bytes,
       " r%u=%08X/%08X", i, a.r[i], b.r[i]);
  for(i = 0; i < 7 && used_bytes < size; i++)
    if(a.fiq[i] != b.fiq[i])
      used_bytes += (size_t)snprintf(out + used_bytes, size - used_bytes,
       " fiq_r%u=%08X/%08X", i + 8, a.fiq[i], b.fiq[i]);
  if(used_bytes < size && a.cpsr != b.cpsr)
    used_bytes += (size_t)snprintf(out + used_bytes, size - used_bytes,
     " cpsr=%08X/%08X", a.cpsr, b.cpsr);
  for(i = 0; i < 5 && used_bytes < size; i++)
    if(a.spsr[i] != b.spsr[i])
      used_bytes += (size_t)snprintf(out + used_bytes, size - used_bytes,
       " spsr%u=%08X/%08X", i, a.spsr[i], b.spsr[i]);
  for(i = 0; i < 2 && used_bytes < size; i++)
  {
    if(a.svc[i] != b.svc[i] || a.irq[i] != b.irq[i] ||
       a.abt[i] != b.abt[i] || a.und[i] != b.und[i])
      used_bytes += (size_t)snprintf(out + used_bytes, size - used_bytes,
       " bank_r%u differs", 13 + i);
  }
  return 0;
}

static int is_multiply(const Test *test)
{
  if(thumb_state(&test->initial))
    return (test->opcode & 0xFFC0) == 0x4340;
  return (test->opcode & 0x0F0000F0u) == 0x00000090u;
}

/* ARM encodings the manual calls unpredictable: r15 where it may not
   be, a written back base that is also loaded, a user bank transfer with
   writeback, or an MSR that changes the T bit. The suite records what
   its reference emulator happens to do for them. */
static int unpredictable(const Test *test)
{
  uint32_t op = test->opcode;
  unsigned rn = (op >> 16) & 15;
  unsigned rd = (op >> 12) & 15;
  unsigned rs = (op >> 8) & 15;
  unsigned rm = op & 15;
  int pc = rn == 15 || rd == 15 || rs == 15 || rm == 15;

  if(thumb_state(&test->initial))
    return 0;
  if((op & 0x0FB00FF0u) == 0x01200000u &&
     ((test->initial.cpsr ^ test->final.cpsr) & ARM_CPSR_T))
    return 1;
  switch((op >> 25) & 7)
  {
    case 0:
      if((op & 0xF0) == 0x90)
      {
        if(op & (1u << 24))
          return pc || rn == rd || rn == rm;
        if(op & (1u << 23))
          return pc || rn == rd || rn == rm || rd == rm;
        return pc || rn == rm;
      }
      if((op & 0x90) == 0x90)
        return (rn == 15 && (!(op & (1u << 24)) || (op & (1u << 21)))) ||
         (!(op & (1u << 22)) && rm == 15) ||
         ((!(op & (1u << 24)) || (op & (1u << 21))) && rn == rd);
      if((op & 0x01900000u) == 0x01000000u)
        return (op & (1u << 21)) ? rm == 15 : rd == 15;
      if(op & 0x10)
        return pc;
      return 0;
    case 2:
    case 3:
      return (rn == 15 && (!(op & (1u << 24)) || (op & (1u << 21)))) ||
       ((op & (1u << 25)) && rm == 15) ||
       ((!(op & (1u << 24)) || (op & (1u << 21))) && rn == rd);
    case 4:
      return rn == 15 || ((op & (1u << 21)) && (op & (1u << 22))) ||
       ((op & (1u << 21)) && (op & (1u << 20)) && (op & (1u << rn)));
    default:
      return 0;
  }
}

/* Behaviours the core leaves as mGBA has them, where the reference
   (NanoBoyAdvance) differs:
   - an ARM r15 written with bit 1 or 0 set keeps them there, while the
     core aligns it, as it does for every write to r15;
   - MSR to an SPSR keeps mode bit 4 clear, while the core sets it as
     for the CPSR;
   - MUL and MULL set C from the multiplier's last carry, which the
     manual calls meaningless and the core leaves unchanged. */
static int known_difference(const State *got, const Test *test)
{
  State a = *got;
  State b = test->final;
  uint32_t align = (b.cpsr & ARM_CPSR_T) ? ~1u : ~3u;
  unsigned i;

  a.r[15] &= align;
  b.r[15] &= align;
  for(i = 0; i < 5; i++)
  {
    a.spsr[i] |= 0x10u;
    b.spsr[i] |= 0x10u;
  }
  if(compare(&a, &b, NULL, 0))
    return 1;
  if(is_multiply(test))
  {
    a.cpsr &= ~ARM_CPSR_C;
    b.cpsr &= ~ARM_CPSR_C;
    return compare(&a, &b, NULL, 0);
  }
  return 0;
}

static uint8_t *read_file(const char *path, size_t *size)
{
  FILE *f = fopen(path, "rb");
  uint8_t *data;

  if(!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  data = malloc(*size);
  if(data && fread(data, 1, *size, f) != *size)
  {
    free(data);
    data = NULL;
  }
  fclose(f);
  return data;
}

#ifdef SINGLE_STEP_JIT
/* The MIPS backend (arm_mips.c, in Unicorn) runs every test as well and
   must end exactly as the interpreter did: same registers, cycles,
   prefetch buffer and bus traffic. A second run gives both cores work RAM
   (regions 2 and 3, mapped directly) and points the registers into it,
   which takes the backend's fast memory paths. */
#define JIT_CODE_BYTES (4u << 20)

static ArmMips *jit;
static uint8_t *ram[2][2];
static unsigned long backend_runs;
static unsigned long backend_differs;
static int backend_verbose;

static void backend_init(const ArmBus *bus)
{
  ArmMipsHost host;
  uint32_t *code;
  unsigned i;

  mips_unicorn_init();
  jit = mips_unicorn_alloc(sizeof(ArmMips));
  code = mips_unicorn_alloc(JIT_CODE_BYTES);
  for(i = 0; i < 2; i++)
  {
    ram[i][0] = mips_unicorn_alloc(0x40000);
    ram[i][1] = mips_unicorn_alloc(0x8000);
  }
  mips_unicorn_map(jit, sizeof(ArmMips), 0x10000000u);
  mips_unicorn_map(code, JIT_CODE_BYTES, 0x08800000u);
  mips_unicorn_map(ram[1][0], 0x40000, 0x0A000000u);
  mips_unicorn_map(ram[1][1], 0x8000, 0x0A040000u);
  host = mips_unicorn_host(jit);
  arm_mips_init(jit, bus, &host, code, JIT_CODE_BYTES);
}

static void map_ram(ArmCore *target, int on, unsigned which)
{
  target->map[2].read = target->map[2].write = on ? ram[which][0] : NULL;
  target->map[2].mask = 0x3FFFF;
  target->map[3].read = target->map[3].write = on ? ram[which][1] : NULL;
  target->map[3].mask = 0x7FFF;
}

/* Runs the test on the core now selected; returns its cycles and leaves
   the bus outcome in used[] and bus_error. */
static int run_test(State *initial, State *out, int backend)
{
  int cycles;

  memset(used, 0, sizeof(used));
  bus_error = 0;
  load_state(initial);
  cycles = backend ? arm_mips_step(jit) : arm_core_step(core);
  save_state(out);
  return cycles;
}

static int same_run(const State *a, const State *b, int cycles_a,
 int cycles_b, const unsigned *used_a, int error_a, unsigned prefetch_a)
{
  return !memcmp(a, b, sizeof(State)) && cycles_a == cycles_b &&
   !memcmp(used_a, used, sizeof(used)) && error_a == bus_error &&
   prefetch_a == jit->core.prefetch;
}

static int backend_agrees(const Test *test, const State *got, int cycles)
{
  State initial = test->initial;
  State mine;
  State theirs;
  unsigned their_used[64];
  unsigned first_used[64];
  int their_error = bus_error;
  int first_error = bus_error;
  unsigned their_prefetch = interpreter.prefetch;
  int agrees;
  int mine_cycles;
  unsigned r;

  memcpy(their_used, used, sizeof(used));
  memcpy(first_used, used, sizeof(used));
  core = &jit->core;
  mine_cycles = run_test(&initial, &mine, 1);
  agrees = same_run(got, &mine, cycles, mine_cycles, their_used,
   their_error, their_prefetch);
  if(!agrees && backend_verbose)
  {
    char text[512] = "";

    compare(got, &mine, text, sizeof(text));
    printf("    first run:%s cycles %d/%d error %d/%d prefetch %u/%u\n", text,
     cycles, mine_cycles, their_error, bus_error, their_prefetch,
     jit->core.prefetch);
  }

  /* Registers into work RAM: even ones at word-aligned addresses, odd ones
     small offsets (some unaligned). */
  for(r = 0; r < 15; r++)
    initial.r[r] = (r & 1) ? (test->opcode >> r) & 0x3F :
     (((test->opcode * 2654435761u) >> r) & 1 ? 0x03000000u : 0x02000000u) +
     ((test->opcode >> (r % 7)) & 0x7FFC);
  for(r = 0; r < 7; r++)
    initial.fiq[r] = initial.r[8 + r];
  memset(ram[0][0], (int)(test->opcode & 0xFF), 0x40000);
  memset(ram[0][1], (int)(test->opcode >> 8 & 0xFF), 0x8000);
  memcpy(ram[1][0], ram[0][0], 0x40000);
  memcpy(ram[1][1], ram[0][1], 0x8000);
  map_ram(&interpreter, 1, 0);
  map_ram(&jit->core, 1, 1);
  core = &interpreter;
  cycles = run_test(&initial, &theirs, 0);
  memcpy(their_used, used, sizeof(used));
  their_error = bus_error;
  their_prefetch = interpreter.prefetch;
  core = &jit->core;
  mine_cycles = run_test(&initial, &mine, 1);
  agrees = same_run(&theirs, &mine, cycles, mine_cycles, their_used,
   their_error, their_prefetch) && agrees &&
   !memcmp(ram[0][0], ram[1][0], 0x40000) &&
   !memcmp(ram[0][1], ram[1][1], 0x8000);
  map_ram(&interpreter, 0, 0);
  map_ram(&jit->core, 0, 1);
  core = &interpreter;
  /* The suite's own checks go on with the first run's bus. */
  memcpy(used, first_used, sizeof(used));
  bus_error = first_error;
  backend_runs += 2;
  if(!agrees)
    backend_differs++;
  return agrees;
}
#endif

int main(int argc, char **argv)
{
  static Test test;
  ArmBus bus = { NULL, read8, read16, read32, write8, write16, write32,
   code32 };
  int verbose = 0;
  int arg;
  unsigned long all_passed = 0;
  unsigned long all_known = 0;
  unsigned long all_undefined = 0;
  unsigned long all_total = 0;

  arm_core_init(core, &bus);
#ifdef SINGLE_STEP_JIT
  backend_init(&bus);
#endif
  for(arg = 1; arg < argc; arg++)
  {
    size_t size;
    uint8_t *data;
    size_t offset = 8;
    unsigned count;
    unsigned i;
    unsigned passed = 0;
    unsigned differences = 0;
    unsigned undefined_cases = 0;
    unsigned shown = 0;
#ifdef SINGLE_STEP_JIT
    unsigned shown_backend = 0;
#endif

    if(!strcmp(argv[arg], "-v"))
    {
      verbose = 1;
#ifdef SINGLE_STEP_JIT
      backend_verbose = 1;
#endif
      continue;
    }
    data = read_file(argv[arg], &size);
    if(!data || size < 8 || get32(data) != 0xD33DBAE0u)
    {
      fprintf(stderr, "%s: not a SingleStepTests file\n", argv[arg]);
      return 1;
    }
    count = get32(data + 4);
    for(i = 0; i < count && offset < size; i++)
    {
      State got;
      char diff[512] = "";
      int ok;
      int cycles;
      unsigned t;

      offset += parse_test(data + offset, &test);
      current = &test;
      memset(used, 0, sizeof(used));
      bus_error = 0;
      load_state(&test.initial);
      cycles = arm_core_step(core);
      (void)cycles;   /* read only by the backend check */
      save_state(&got);
#ifdef SINGLE_STEP_JIT
      if(!backend_agrees(&test, &got, cycles) && verbose && shown_backend < 8)
      {
        shown_backend++;
        printf("  #%u %s %08X at %08X cpsr %08X: the backend differs\n", i,
         thumb_state(&test.initial) ? "thumb" : "arm", test.opcode,
         instruction_address(&test.initial), test.initial.cpsr);
      }
#endif
      ok = compare(&got, &test.final, diff, sizeof(diff));
      for(t = 0; t < test.count; t++)
        if(test.transactions[t].kind == 2 && !used[t] && !bus_error)
        {
          snprintf(bus_message, sizeof(bus_message), "missing write%u %08X",
           test.transactions[t].size * 8, test.transactions[t].address);
          bus_error = 1;
        }
      if(ok && !bus_error)
        passed++;
      else if(!bus_error && known_difference(&got, &test))
        differences++;
      else if(unpredictable(&test))
        undefined_cases++;
      else if(verbose && shown < 8)
      {
        shown++;
        printf("  #%u %s %08X at %08X cpsr %08X:%s %s\n", i,
         thumb_state(&test.initial) ? "thumb" : "arm", test.opcode,
         instruction_address(&test.initial), test.initial.cpsr, diff,
         bus_error ? bus_message : "");
      }
    }
    printf("%s: %u/%u passed, %u known differences, %u unpredictable\n",
     argv[arg], passed, count, differences, undefined_cases);
    all_known += differences;
    all_undefined += undefined_cases;
    all_passed += passed;
    all_total += count;
    free(data);
  }
  printf("total: %lu/%lu passed, %lu known differences, %lu unpredictable,"
   " %lu failed\n", all_passed, all_total, all_known, all_undefined,
   all_total - all_passed - all_known - all_undefined);
#ifdef SINGLE_STEP_JIT
  printf("backend: %lu of %lu tests (each also with its registers in work "
   "RAM) differ from the interpreter\n", backend_differs, backend_runs / 2);
  if(backend_differs)
    return 1;
#endif
  return all_passed + all_known + all_undefined == all_total ? 0 : 1;
}
