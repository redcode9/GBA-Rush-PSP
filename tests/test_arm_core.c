/*
 * Host test of the ARM7TDMI core (arm_decode.c, arm_interp.c) on a small
 * memory map: a BIOS whose SWI vector returns at once, work RAM mapped
 * directly, and a ROM holding the programs below (assembled with clang
 * -target armv4t-none-eabi). The thorough checks are tests/core/: the
 * SingleStepTests suite and the lockstep runs against mGBA.
 */

#include <stdio.h>
#include <string.h>

#include "arm_core.h"

#define ROM_SIZE 0x40000u

static ArmCore core;
static uint8_t bios[0x4000];
static uint8_t iwram[0x8000];
static uint8_t rom[ROM_SIZE];
static int failures;

#define CHECK(condition) check((condition), #condition, __LINE__)

static void check(int ok, const char *text, int line)
{
  if(!ok)
  {
    printf("test_arm_core:%d: %s\n", line, text);
    failures++;
  }
}

/* start, at ROM 0: ARM, then a Thumb call, then SWI.
     mov r0, #5; subs r1, r0, #5; ldr r2, =0x12345678; adr r3, thumb+1
     mov lr, pc; bx r3; add r5, r0, #1; swi 0x10; stop: b stop
   thumb (24h): r0 = 10 + 9 + ... + 1
     movs r0, #0; movs r4, #10; loop: adds r0, r0, r4; subs r4, #1
     bne loop; bx lr; .word 0x12345678
   smc (34h, copied to work RAM): rewrites "mov r0, #2" at target to
   "mov r0, #3" from a later block, then runs it again.
     mov r0, #0; ldr r1, new; adr r2, target; target: mov r0, #2
     cmp r0, #3; beq done; str r1, [r2]; b target; done: b done
     new: .word 0xe3a00003
   flags (5Ch): adds r0, r1, r2; adds r3, r4, r5; bx lr
   literal (68h, Thumb, also copied to 200h): ldr r0, [pc, #4]
     ldrh r1, [r0, #4]; bx lr; nop; .word 0x04000000 */
static const uint32_t program[] =
{
  0xe3a00005, 0xe2501005, 0xe59f2020, 0xe28f3011,
  0xe1a0e00f, 0xe12fff13, 0xe2805001, 0xef000010,
  0xeafffffe, 0x240a2000, 0x3c011900, 0x4770d1fc,
  0x12345678, 0xe3a00000, 0xe59f1018, 0xe24f2004,
  0xe3a00002, 0xe3500003, 0x0a000001, 0xe5821000,
  0xeafffffa, 0xeafffffe, 0xe3a00003, 0xe0910002,
  0xe0943005, 0xe12fff1e, 0x88814801, 0x46c04770,
  0x04000000
};

/* call (ROM 300h), a wait with a call in it; f's read raises an IRQ
   when irq_armed:
     loop: bl f; cmp r0, #0; beq loop
     f: mov r0, #0x04000000; ldr r0, [r0]; bx lr
   irq (BIOS 18h), acknowledging it with a write:
     mov sp, #0x04000000; str sp, [sp, #0x200]; subs pc, lr, #4 */
static const uint32_t call[] =
{
  0xeb000001, 0xe3500000, 0x0afffffc, 0xe3a00301, 0xe5900000, 0xe12fff1e
};
static const uint32_t irq[] = { 0xe3a0d301, 0xe58dd200, 0xe25ef004 };
static int irq_armed;

static uint32_t load(const uint8_t *memory, uint32_t offset, unsigned size)
{
  uint32_t value = 0;

  memcpy(&value, memory + offset, size);
  return value;
}

static uint32_t read_any(uint32_t address, unsigned size)
{
  switch(address >> 24)
  {
    case 0: return address < sizeof(bios) ? load(bios, address, size) : 0;
    case 8: return load(rom, address & (ROM_SIZE - 1), size);
    default: return 0;
  }
}

static uint32_t read8(void *context, uint32_t address)
{
  (void)context;
  return read_any(address, 1);
}

static uint32_t read16(void *context, uint32_t address)
{
  (void)context;
  return read_any(address, 2);
}

static uint32_t read32(void *context, uint32_t address)
{
  (void)context;
  if((address >> 24) == 4)
  {
    core.irq_line |= irq_armed;
    irq_armed = 0;
  }
  return read_any(address, 4);
}

static void ignore(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  (void)address;
  (void)value;
}

static void acknowledge(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  (void)value;
  if((address >> 24) == 4)
    core.irq_line = 0;
}

static uint32_t code32(void *context, uint32_t address)
{
  (void)context;
  if((address >> 24) == 3)
    return load(iwram, address & 0x7FFF, 4);
  return read_any(address, 4);
}

static void reset(uint32_t pc, uint32_t cpsr)
{
  arm_core_reset(&core);
  arm_core_set_waitcnt(&core, 0x4317);
  arm_core_set_cpsr(&core, cpsr);
  core.v[13] = 0x03007F00;
  core.v[15] = pc;
}

static const ArmIrOp *find_op(const ArmIrOp *ops, unsigned count,
 unsigned code, unsigned nth)
{
  unsigned i;

  for(i = 0; i < count; i++)
    if(ops[i].code == code && nth-- == 0)
      return &ops[i];
  return NULL;
}

static void test_program(void)
{
  reset(0x08000000, ARM_MODE_SYSTEM);
  arm_core_run(&core, 2000);
  CHECK(core.v[15] == 0x08000020);
  CHECK(core.v[0] == 55);
  CHECK(core.v[1] == 0);
  CHECK(core.v[2] == 0x12345678);
  CHECK(core.v[5] == 56);
  CHECK(core.v[14] == 0x08000018);
  /* SWI went through supervisor mode and returned to system mode. */
  CHECK((arm_core_cpsr(&core) & ARM_CPSR_MODE) == ARM_MODE_SYSTEM);
  CHECK(*arm_core_register(&core, ARM_MODE_SUPERVISOR, 14) == 0x08000020);
}

static void test_self_modifying_code(void)
{
  memcpy(iwram, (const uint8_t *)program + 0x34, 0x58 - 0x34 + 4);
  reset(0x03000000, ARM_MODE_SYSTEM);
  arm_core_run(&core, 2000);
  CHECK(core.v[0] == 3);
  CHECK(core.v[15] == 0x03000020);
}

static void test_flag_liveness(void)
{
  ArmIrOp ops[ARM_BLOCK_MAX_OPS];
  unsigned n = arm_translate(&core, 0x0800005C, 0, ops, ARM_BLOCK_MAX_OPS,
   ARM_BLOCK_MAX_INSNS, NULL);
  const ArmIrOp *first = find_op(ops, n, ARM_IR_ADD, 0);
  const unsigned all = ARM_IR_SET_NZ | ARM_IR_SET_C | ARM_IR_SET_V;

  /* The second ADDS overwrites every flag the first one sets. */
  CHECK(first && (first->flags & all) == 0);
  CHECK(find_op(ops, n, ARM_IR_ADD, 1) &&
   (find_op(ops, n, ARM_IR_ADD, 1)->flags & all) == all);
  CHECK(find_op(ops, n, ARM_IR_EXIT_REG, 0) &&
   find_op(ops, n, ARM_IR_EXIT_REG, 0)->d == ARM_EXIT_RETURN);
}

static void test_literal_pool(void)
{
  ArmIrOp ops[ARM_BLOCK_MAX_OPS];
  unsigned n = arm_translate(&core, 0x08000200, 1, ops, ARM_BLOCK_MAX_OPS,
   ARM_BLOCK_MAX_INSNS, NULL);
  const ArmIrOp *literal = NULL;
  const ArmIrOp *io = find_op(ops, n, ARM_IR_LD16, 0);
  unsigned i;

  for(i = 0; i < n; i++)
    if(ops[i].code == ARM_IR_MOV && ops[i].d == 0)
      literal = &ops[i];

  /* The literal load became a constant, and the load through it knows
     its address (I/O), so its cycles are counted at translate time. */
  CHECK(!find_op(ops, n, ARM_IR_LD32, 0));
  CHECK(literal && literal->d == 0 && (literal->flags & ARM_IR_IMM) &&
   literal->b == 0x04000000);
  CHECK(io && (io->flags & ARM_IR_FOLDED) && io->b == 4);
}

static void test_timing(void)
{
  /* WAITCNT 4317h: ROM accesses take 4 cycles non-sequential, 2
     sequential, per halfword. */
  reset(0x08000024, ARM_MODE_SYSTEM | ARM_CPSR_T);
  CHECK(arm_core_step(&core) == 2);
  reset(0x08000000, ARM_MODE_SYSTEM);
  CHECK(arm_core_step(&core) == 4);
  /* The fetch at a 128 KB boundary is non-sequential. */
  reset(0x0801FFFC, ARM_MODE_SYSTEM | ARM_CPSR_T);
  CHECK(arm_core_step(&core) == 4);
  /* MULS r0, r1: one cycle per significant byte of the multiplier r0. */
  reset(0x08000100, ARM_MODE_SYSTEM | ARM_CPSR_T);
  core.v[0] = 0x10;
  core.v[1] = 3;
  CHECK(arm_core_step(&core) == 3 && core.v[0] == 0x30);
  reset(0x08000100, ARM_MODE_SYSTEM | ARM_CPSR_T);
  core.v[0] = 0x12345678;
  CHECK(arm_core_step(&core) == 6);
  reset(0x08000100, ARM_MODE_SYSTEM | ARM_CPSR_T);
  core.v[0] = 0xFFFFFFF0;
  CHECK(arm_core_step(&core) == 3);
}

static void test_interrupt(void)
{
  reset(0x08000024, ARM_MODE_SYSTEM | ARM_CPSR_T | ARM_CPSR_Z);
  CHECK(arm_core_irq(&core) > 0);
  CHECK(core.v[15] == 0x18);
  CHECK((arm_core_cpsr(&core) & 0xFF) == (ARM_MODE_IRQ | ARM_CPSR_I));
  CHECK(core.v[14] == 0x08000028);
  CHECK(core.spsr == (ARM_MODE_SYSTEM | ARM_CPSR_T | ARM_CPSR_Z));
  /* Masked: nothing happens. */
  CHECK(arm_core_irq(&core) == 0);
}

static unsigned hook_calls;

static void count_hook(ArmCore *hooked)
{
  (void)hooked;
  hook_calls++;
}

static void test_hook(void)
{
  ArmIrOp ops[ARM_BLOCK_MAX_OPS];
  ArmIrBlockInfo info;

  /* The Thumb loop at 28h, inside the block from 24h, calls the hook on
     each of its ten passes. */
  core.hook = 0x08000028;
  core.hooked = count_hook;
  arm_core_flush(&core);
  arm_translate(&core, 0x08000024, 1, ops, ARM_BLOCK_MAX_OPS,
   ARM_BLOCK_MAX_INSNS, &info);
  CHECK(info.insns == 2);
  reset(0x08000000, ARM_MODE_SYSTEM);
  arm_core_run(&core, 2000);
  CHECK(core.v[0] == 55 && hook_calls == 10);
  core.hook = 0;
  arm_core_flush(&core);
}

/* One run of check_idle_loop: after a reset to pc (0: on from the last
   run), with f raising an IRQ if irq. */
typedef struct
{
  uint32_t pc;
  int32_t budget;
  int irq;
} IdleRun;

/* Makes the runs spinning, then again with the branch at idle named an
   idle loop, and checks that the second time each run ends on the cycle
   and at the pc it ended the first time, in fewer instructions. */
static void check_idle_loop(uint32_t idle, const IdleRun *runs,
 unsigned count)
{
  int32_t cycles[16];
  uint32_t pc[16];
  uint32_t spun = 0;
  unsigned named, i;
  int32_t ran;

  CHECK(count <= 16 && runs[0].pc);
  for(named = 0; named < 2 && count <= 16; named++)
  {
    core.idle_loops[0] = idle;
    core.idle_loop_count = named;
    core.instructions = 0;
    for(i = 0; i < count; i++)
    {
      if(runs[i].pc)
        reset(runs[i].pc, ARM_MODE_SYSTEM);
      irq_armed = runs[i].irq;
      ran = arm_core_run(&core, runs[i].budget);
      if(!named)
      {
        cycles[i] = ran;
        pc[i] = core.v[15];
      }
      else if(ran != cycles[i] || core.v[15] != pc[i])
      {
        printf("test_arm_core: idle loop %08x, run %u: cycle %d at %08x,"
         " spinning %d at %08x\n", (unsigned)idle, i, (int)ran,
         (unsigned)core.v[15], (int)cycles[i], (unsigned)pc[i]);
        failures++;
      }
    }
    CHECK(!named || core.instructions < spun);
    spun = core.instructions;
  }
  core.idle_loop_count = 0;
  arm_core_flush(&core);
}

static void test_idle_loop(void)
{
  /* Runs where a pass measured from an earlier branch would be wrong:
     through the preamble to "stop: b stop" after a short run in it, in
     the call loop from wherever the last run, short or long, left it,
     and with an IRQ taken after the call loop's branch. */
  static const IdleRun preamble[] =
  {
    { 0x08000020, 40, 0 }, { 0x08000000, 5000, 0 }
  };
  static const IdleRun calls[] =
  {
    { 0x08000300, 7, 0 }, { 0, 300, 0 }, { 0, 5, 0 }, { 0, 211, 0 },
    { 0, 13, 0 }, { 0, 1000, 0 }, { 0, 3, 0 }, { 0, 97, 0 },
    { 0, 30, 0 }, { 0, 450, 0 }
  };
  static const IdleRun interrupted[] =
  {
    { 0x08000304, 1000, 1 }, { 0, 500, 0 }
  };
  unsigned pass;
  int32_t spun;

  /* "stop: b stop" named an idle loop: two passes measure it, the
     skipped ones and the last one end the run on the cycle spinning
     ends it. */
  reset(0x08000020, ARM_MODE_SYSTEM);
  core.instructions = 0;
  spun = arm_core_run(&core, 1000);
  CHECK(spun > 1000 && core.instructions > 50);
  core.idle_loops[0] = 0x08000020;
  core.idle_loop_count = 1;
  arm_core_flush(&core);
  reset(0x08000020, ARM_MODE_SYSTEM);
  core.instructions = 0;
  CHECK(arm_core_run(&core, 1000) == spun);
  CHECK(core.instructions == 3 && core.v[15] == 0x08000020);
  /* Spent already, the budget stays as it is. */
  CHECK(arm_core_run(&core, 1) > 1);

  /* The Thumb countdown's bne, not idle: three passes per run while it
     is taken, the first two measuring one (12 cycles: adds, subs and the
     taken bne) and the last ending the run on the first bne past the
     budget, which after the preamble falls on cycle 2010 and from the
     loop's head on 2004; running on once it is not taken. */
  core.idle_loops[0] = 0x0800002C;
  arm_core_flush(&core);
  reset(0x08000000, ARM_MODE_SYSTEM);
  for(pass = 3; pass < 10; pass += 3)
  {
    CHECK(arm_core_run(&core, 2000) == (pass == 3 ? 2010 : 2004));
    CHECK(core.v[15] == 0x08000028 && core.v[4] == 10 - pass);
  }
  arm_core_run(&core, 2000);
  CHECK(core.v[0] == 55 && core.v[15] == 0x08000020);
  core.idle_loop_count = 0;
  arm_core_flush(&core);

  check_idle_loop(0x08000020, preamble, 2);
  check_idle_loop(0x08000308, calls, 10);
  check_idle_loop(0x08000308, interrupted, 2);
}

int main(void)
{
  ArmBus bus = { NULL, read8, read16, read32, ignore, ignore, acknowledge,
   code32 };
  uint32_t movs_pc_lr = 0xe1b0f00e;
  uint16_t movs_r0_0 = 0x2000;
  uint16_t muls_r0_r1 = 0x4348;

  memcpy(bios + 0x08, &movs_pc_lr, 4);
  memcpy(bios + 0x18, irq, sizeof(irq));
  memcpy(rom, program, sizeof(program));
  memcpy(rom + 0x1FFFC, &movs_r0_0, 2);
  memcpy(rom + 0x100, &muls_r0_r1, 2);
  memcpy(rom + 0x300, call, sizeof(call));
  /* The first 256 bytes of ROM, where the GPIO port may be, are never
     read at translate time; run the literal pool test further on. */
  memcpy(rom + 0x200, (const uint8_t *)program + 0x68, 12);
  arm_core_init(&core, &bus);
  core.map[3].read = iwram;
  core.map[3].write = iwram;
  core.map[3].mask = sizeof(iwram) - 1;

  test_program();
  test_self_modifying_code();
  test_flag_liveness();
  test_literal_pool();
  test_timing();
  test_interrupt();
  test_hook();
  test_idle_loop();
  if(failures)
    return 1;
  printf("test_arm_core: all checks passed\n");
  return 0;
}
