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
 * The new ARM core as the GBA's CPU. Everything around the CPU stays as
 * the Classic core has it: memory.c owns memory and I/O, update_gba() in
 * main.c the timers, video, DMA, sound and link, reg[] the CPU state the
 * rest of the emulator sees.
 *
 * Storage. The core (an ArmMips, about 1.3 MB) lives in the Classic
 * core's writable code cache and its generated code in the read-only one
 * (cpu.h): only one core runs at a time, and switching flushes both.
 *
 * Memory. Work RAM is read and written by the core directly, palette RAM
 * and OAM read directly, and the ROM too when it is all in memory with a
 * power of two size. Everything else goes through memory.c's handlers:
 * the BIOS (read protection), I/O, VRAM (its 96 KB mirror is no mask)
 * and every store to video memory, so palette, VRAM and OAM writes mark
 * video_dirty exactly as the Classic core's do. A paged ROM is read
 * through the bus as well: translated code keeps no pointer into a page.
 *
 * Events. The core runs for the cycles update_gba() hands out and checks
 * them at taken branches. A store whose handler reports a timer start,
 * DMA, halt, interrupt or link transfer stops it after that instruction,
 * so update_gba() sees the cycle of the write: SIO transfers start and
 * end at the same cycles as with the Classic core's alerts. The core
 * takes interrupts itself from its IRQ line (IME and IE & IF), which is
 * updated after every I/O store and every update_gba(); update_gba() only
 * ends a halt.
 */

#include "common.h"

#include "arm_mips.h"

typedef char ArmGbaStorageCheck[sizeof(ArmMips) <= WRITABLE_CODE_CACHE_SIZE ?
 1 : -1];

static ArmMips *jit;
/* Where CodeBreaker cheats run, 0 for none (cheats.c). */
static u32 cheat_hook;

/* ARM mode of each CPU_MODE_TYPE (user and system share MODE_USER). */
static const u8 arm_modes[MODE_INVALID] =
{
  ARM_MODE_USER, ARM_MODE_IRQ, ARM_MODE_FIQ, ARM_MODE_SUPERVISOR,
  ARM_MODE_ABORT, ARM_MODE_UNDEFINED
};

/* ---- The bus: memory.c's handlers --------------------------------------- */

/* memory.c's handlers find the running instruction in reg[]: BIOS reads
   are allowed only from the BIOS, and open bus reads the opcode after
   it. */
static void bus_sync(void)
{
  reg[REG_PC] = jit->core.insn_pc;
  reg[REG_CPSR] = jit->core.cpsr;
}

static void update_irq_line(void)
{
  jit->core.irq_line = GBA_IME_STATE &&
   (pIO_REG(REG_IE) & pIO_REG(REG_IF) & 0x3FFF) != 0;
}

/* After a store: I/O may change the interrupt line or the waitstates, and
   an alert means update_gba() must run before the next instruction. */
static void bus_stored(u32 address, CPU_ALERT_TYPE alert)
{
  ArmCore *core = &jit->core;

  if((address >> 24) == 0x04)
  {
    update_irq_line();
    arm_core_set_waitcnt(core, pIO_REG(REG_WAITCNT));
  }
  if(alert & (CPU_ALERT_IRQ | CPU_ALERT_HALT | CPU_ALERT_TIMER |
   CPU_ALERT_DMA | CPU_ALERT_LINK))
    arm_core_request_exit(core);
}

static uint32_t bus_read8(void *context, uint32_t address)
{
  (void)context;
  bus_sync();
  return read_memory8(address);
}

static uint32_t bus_read16(void *context, uint32_t address)
{
  (void)context;
  bus_sync();
  return read_memory16(address);
}

static uint32_t bus_read32(void *context, uint32_t address)
{
  (void)context;
  bus_sync();
  return read_memory32(address);
}

static void bus_write8(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  bus_sync();
  bus_stored(address, write_memory8(address, (u8)value));
}

static void bus_write16(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  bus_sync();
  bus_stored(address, write_memory16(address, (u16)value));
}

static void bus_write32(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  bus_sync();
  bus_stored(address, write_memory32(address, value));
}

/* Code and literals at translate time: the BIOS unprotected, memory that
   can hold code, nothing elsewhere (I/O and save memory reads have side
   effects). */
static uint32_t bus_code32(void *context, uint32_t address)
{
  (void)context;
  switch(address >> 24)
  {
    case 0x00:
      return address < 0x4000 ? ADDRESS32(bios.rom, address & 0x3FFC) : 0;
    case 0x02: case 0x03: case 0x05: case 0x06: case 0x07:
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C:
      return read_memory32(address);
    default:
      return 0;
  }
}

static const ArmBus bus =
{
  NULL, bus_read8, bus_read16, bus_read32, bus_write8, bus_write16,
  bus_write32, bus_code32
};

/* New code must reach instruction fetch: out of the data cache, and out
   of the instruction cache wherever older code was. */
static void sync_code(void *context, void *code, uint32_t bytes)
{
  (void)context;
  sceKernelDcacheWritebackRange(code, bytes);
  sceKernelIcacheInvalidateRange(code, bytes);
}

static const ArmMipsHost host = { NULL, NULL, sync_code, NULL, 0 };

/* ---- Memory map --------------------------------------------------------- */

static void map_region(ArmCore *core, unsigned region, const void *read,
 void *write, u32 mask)
{
  core->map[region].read = read;
  core->map[region].write = write;
  core->map[region].mask = mask;
}

static void map_memory(ArmCore *core)
{
  u32 rom_size = gamepak_size;
  unsigned region;

  memset(core->map, 0, sizeof(core->map));
  map_region(core, 0x02, ewram, ewram, 0x3FFFF);
  map_region(core, 0x03, iwram, iwram, 0x7FFF);
  map_region(core, 0x05, palette_ram, NULL, 0x3FF);
  map_region(core, 0x07, oam_ram, NULL, 0x3FF);

  /* The whole ROM in memory, at a power of two size so a mask finds it:
     the three ROM windows read it directly. A ROM under 16 MB repeats
     through the first 16 MB of each window, where the Classic core reads
     open bus past its end; a game that tests for that would need a bound
     check in the backend. The second 16 MB of a window is ROM only for a
     32 MB ROM. */
  if(rom_size == 0 || rom_size > gamepak_ram_buffer_size ||
     gamepak_file_large != GAMEPAK_FILE_NONE || (rom_size & (rom_size - 1)))
    return;
  for(region = 0x08; region <= 0x0C; region++)
    if(!(region & 1) || rom_size > 0x1000000)
      map_region(core, region, gamepak_rom, NULL, rom_size - 1);
}

/* ---- CPU state ---------------------------------------------------------- */

static void run_cheat_hook(ArmCore *core)
{
  (void)core;
  process_cheats_at_hook(cheat_hook);
}

/* Leaving the BIOS from an IRQ or a SWI: protected BIOS reads give the
   last opcode the BIOS fetched, two instructions after its return. With
   the official BIOS that is 0xE55EC002 after an IRQ and 0xE3A02004 after
   a SWI (GBATEK). The built-in BIOS returns from other addresses, so the
   official words are set with either BIOS, as the Classic core does. */
static void exception_returned(ArmCore *core, uint32_t from, uint32_t left)
{
  if(from < 0x4000 && core->v[15] >= 0x4000)
    bios_read_protect = left == ARM_MODE_IRQ ? 0xe55ec002 : 0xe3a02004;
}

/* Entering an IRQ: the official BIOS's handler jumps to the game's from
   0x134, so protected BIOS reads give 0xE25EF004, the word at 0x13C, while
   the game's handler runs (GBATEK). Set with either BIOS, as above. */
static void irq_entered(ArmCore *core)
{
  (void)core;
  bios_read_protect = 0xe25ef004;
}

/* Where register r (8-14, or 16 for the SPSR) of a mode not running is,
   or NULL when that mode is the one running (its registers are in v[]). */
static u32 *banked(ArmCore *core, unsigned mode, unsigned r)
{
  u32 *where = arm_core_register(core, mode, r);

  return where >= core->v && where < core->v + 16 ? NULL : where;
}

/* The core from reg[], reg_mode[] and spsr[], with nothing translated:
   at the start, after a reset and after loading a state. The flags come
   from the CPSR, as the Classic core takes them. */
static void load_registers(void)
{
  ArmCore *core = &jit->core;
  unsigned m;
  unsigned r;
  u32 *where;

  arm_core_reset(core);
  core->hook = cheat_hook;
  core->hooked = run_cheat_hook;
  core->returned = exception_returned;
  core->interrupted = irq_entered;
  map_memory(core);
  arm_core_set_waitcnt(core, pIO_REG(REG_WAITCNT));
  arm_core_set_cpsr(core, reg[REG_CPSR]);
  core->prefetch = 0;
  for(r = 0; r < 16; r++)
    core->v[r] = reg[r];
  for(m = 0; m < MODE_INVALID; m++)
  {
    for(r = 13; r <= 14; r++)
      if((where = banked(core, arm_modes[m], r)) != NULL)
        *where = reg_mode[m][r - 8];
    if(m != MODE_USER)
      *arm_core_register(core, arm_modes[m], 16) = spsr[m];
  }
  /* r8-r12 of FIQ mode, or of the others while in FIQ mode. */
  for(r = 8; r <= 12; r++)
  {
    if((where = banked(core, ARM_MODE_FIQ, r)) != NULL)
      *where = reg_mode[MODE_FIQ][r - 8];
    if((where = banked(core, ARM_MODE_USER, r)) != NULL)
      *where = reg_mode[MODE_USER][r - 8];
  }
  core->exit_request = 0;
  update_irq_line();
}

void arm_gba_save_registers(void)
{
  ArmCore *core;
  u32 cpsr;
  unsigned m;
  unsigned r;

  if(main_cpu_core != CPU_CORE_NEW)
    return;
  core = &jit->core;
  cpsr = arm_core_cpsr(core);
  for(r = 0; r < 16; r++)
    reg[r] = core->v[r];
  reg[REG_CPSR] = cpsr;
  reg[REG_N_FLAG] = (cpsr >> 31) & 1;
  reg[REG_Z_FLAG] = (cpsr >> 30) & 1;
  reg[REG_C_FLAG] = (cpsr >> 29) & 1;
  reg[REG_V_FLAG] = (cpsr >> 28) & 1;
  reg[CPU_MODE] = MODE_USER;
  for(m = 0; m < MODE_INVALID; m++)
  {
    if(arm_modes[m] == (cpsr & ARM_CPSR_MODE))
      reg[CPU_MODE] = m;
    for(r = 13; r <= 14; r++)
      reg_mode[m][r - 8] = *arm_core_register(core, arm_modes[m], r);
    if(m != MODE_USER)
      spsr[m] = *arm_core_register(core, arm_modes[m], 16);
  }
  for(r = 8; r <= 12; r++)
  {
    reg_mode[MODE_FIQ][r - 8] = *arm_core_register(core, ARM_MODE_FIQ, r);
    reg_mode[MODE_USER][r - 8] = *arm_core_register(core, ARM_MODE_USER, r);
  }
}

/* ---- Running ------------------------------------------------------------ */

void arm_gba_written(u32 address, u32 size)
{
  u32 region = address >> 24;

  if(main_cpu_core == CPU_CORE_NEW && (region == 0x02 || region == 0x03))
    arm_core_invalidate(&jit->core, address, size);
}

void arm_gba_counters(u32 *blocks, u32 *flushes)
{
  *blocks = main_cpu_core == CPU_CORE_NEW ? jit->stats.blocks : 0;
  *flushes = main_cpu_core == CPU_CORE_NEW ? jit->stats.flushes : 0;
}

void arm_gba_set_cheat_hook(u32 address)
{
  cheat_hook = address;
  if(main_cpu_core == CPU_CORE_NEW && jit->core.hook != address)
  {
    jit->core.hook = address;
    arm_core_flush(&jit->core);
  }
}

void arm_gba_run(void)
{
  ArmCore *core;

  jit = (ArmMips *)(void *)writable_code_cache;
  arm_mips_init(jit, &bus, &host, (uint32_t *)(void *)readonly_code_cache,
   READONLY_CODE_CACHE_SIZE);
  core = &jit->core;
  load_registers();

  for(;;)
  {
    s32 ran = 0;

    /* An interrupt the last events raised. */
    if(core->irq_line && !(core->cpsr & ARM_CPSR_I))
      ran = arm_core_irq(core);
    ran += arm_mips_run(jit, (s32)reg[EXECUTE_CYCLES] - ran);
    reg[EXECUTE_CYCLES] = (u32)ran;

    update_gba();

    /* A reset or a loaded state replaced the machine. */
    if(reg[CHANGED_PC_STATUS] != 0)
    {
      reg[CHANGED_PC_STATUS] = 0;
      load_registers();
    }
    update_irq_line();
  }
}
