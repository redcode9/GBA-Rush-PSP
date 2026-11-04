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
 * Runs a GBA test ROM (tests/core-rom, tests/save-rom) on the New core's
 * interpreter (arm_decode.c, arm_interp.c), for make test-roms. It shows
 * that a ROM boots, runs every case and leaves a readable signature in
 * its save chip, which it writes to a .sav file for
 * tools/decode_test_rom.py. It is not an emulator and not a reference:
 * the ROMs take their expected values from GBATEK, and a case passing
 * here only says the runner agrees with them.
 *
 * Modelled, after GBATEK: a 28-bit address bus (the upper 4 bits ignored);
 * the BIOS (the built-in image, or --bios) with read protection, where a
 * read from outside it returns the word 8 bytes past the last ARM
 * instruction it ran; work RAM, palette, VRAM and OAM as memory; the
 * ROM, mirrored to the next power of two; IE, IF, IME, HALT (woken by
 * IE & IF whatever IME and the CPSR say), VCOUNT, DISPSTAT and the VBlank
 * IRQ; DMA3 immediate transfers; the save chip the ROM's ID string names
 * (SRAM without one), with SRAM growing to 64 KiB on a write above 32 KiB
 * as GBA Rush does, and EEPROM to 8 KiB on any request with a 14-bit
 * address (GBA Rush only on a read request); Flash with
 * GBATEK's commands and the IDs GBA Rush reports (Panasonic 64 KiB,
 * Sanyo 128 KiB), writes only clearing bits. The .sav file gets the chip
 * as GBA Rush would have last saved it: as it stood once 10 frames had
 * passed without a write, so stopping in the middle of a write is a power
 * cut that keeps the previous state.
 *
 * Not modelled: timers, sound, serial, keys (none held), the PPU, HBlank
 * and VCOUNT IRQs, DMA0-2, DMA start modes other than immediate and DMA
 * timing, open bus outside the BIOS (reads 0), STOP, cycle-exact
 * interrupt entry (it waits for the end of a block), the time the BIOS
 * itself takes, and EEPROM bit by bit (it is handled per DMA request, the
 * way the ROMs use it).
 *
 * Usage: rom_run [--bios FILE] [--frames N] [--save FILE] [--screen FILE]
 *        ROM
 *   --bios    a 16 KiB BIOS image instead of the built-in one; the BIOS
 *             open bus cases expect the official BIOS
 *   --frames  GBA frames to run, 300 (5 seconds) by default
 *   --save    the save chip: loaded first if the file exists, written
 *             back at the end
 *   --screen  the mode 3 frame buffer at the end, as a binary PPM
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm_core.h"

typedef uint8_t u8;
#include "open_bios.h"

#define FRAME_CYCLES 280896
#define LINE_CYCLES 1232
#define VBLANK_START (160 * LINE_CYCLES)
#define HBLANK_START 960
/* GBA Rush saves the chip to its .sav file after this many frames
   without a write to it. */
#define SAVE_DELAY_FRAMES 10
#define MAX_ROM 0x2000000u

enum { SAVE_SRAM, SAVE_FLASH, SAVE_EEPROM };

static ArmCore core;
static u8 bios[0x4000];
static u8 ewram[0x40000];
static u8 iwram[0x8000];
static u8 io[0x400];
static u8 palette[0x400];
static u8 vram[0x18000];
static u8 oam[0x400];
static u8 *rom;
static uint32_t rom_size;
static u8 save[0x20000];
static int save_kind = SAVE_SRAM;
static uint32_t save_size = 0x8000;
/* The chip as GBA Rush last saved it. */
static u8 saved[0x20000];
static uint32_t saved_size;
static long long save_written_at = -1;
/* What a protected BIOS read returns once the BIOS has started the
   cartridge (GBATEK). */
static uint32_t bios_protect = 0xE129F000u;
static long long now;
static int halted;

static int flash_step;
static char flash_mode;
static uint32_t flash_bank;
static uint32_t eeprom_block;

static uint32_t load(const u8 *memory, uint32_t offset, unsigned size)
{
  uint32_t value = 0;

  memcpy(&value, memory + offset, size);
  return value;
}

static void store(u8 *memory, uint32_t offset, unsigned size,
 uint32_t value)
{
  memcpy(memory + offset, &value, size);
}

static uint32_t io16(uint32_t reg)
{
  return load(io, reg, 2);
}

static void raise_irq(uint32_t bits)
{
  store(io, 0x202, 2, io16(0x202) | bits);
}

/* ---- Time ------------------------------------------------------------- */

static long long vblanks_by(long long t)
{
  return (t + FRAME_CYCLES - VBLANK_START) / FRAME_CYCLES;
}

static void advance(long long cycles)
{
  long long before = now;

  now += cycles;
  if(vblanks_by(now) != vblanks_by(before) && (io16(0x004) & 0x0008))
    raise_irq(0x0001);
  if(save_written_at >= 0 &&
     now / FRAME_CYCLES - save_written_at / FRAME_CYCLES >= SAVE_DELAY_FRAMES)
  {
    memcpy(saved, save, save_size);
    saved_size = save_size;
    save_written_at = -1;
  }
}

static long long next_vblank(void)
{
  long long t = now - now % FRAME_CYCLES + VBLANK_START;

  return t > now ? t : t + FRAME_CYCLES;
}

static uint32_t line_now(void)
{
  return (uint32_t)(now % FRAME_CYCLES / LINE_CYCLES);
}

/* ---- Save chip -------------------------------------------------------- */

static void save_written(void)
{
  save_written_at = now;
}

static uint32_t flash_id(void)
{
  return save_size == 0x20000 ? 0x1362 : 0x1B32;
}

static uint32_t backup_read8(uint32_t address)
{
  address &= 0xFFFF;
  if(save_kind == SAVE_FLASH)
  {
    if(flash_mode == 'I' && address < 2)
      return (flash_id() >> (address * 8)) & 0xFF;
    return save[flash_bank * 0x10000 + address];
  }
  return save[address];
}

static void flash_write(uint32_t address, uint32_t value)
{
  u8 *bank = save + flash_bank * 0x10000;

  if(flash_mode == 'W')
  {
    bank[address] &= (u8)value;
    flash_mode = 0;
    save_written();
    return;
  }
  if(flash_mode == 'B' && address == 0)
  {
    flash_bank = value & 1;
    flash_mode = 0;
    return;
  }
  if(flash_step == 0 && address == 0x5555 && value == 0xAA)
  {
    flash_step = 1;
    return;
  }
  if(flash_step == 1 && address == 0x2AAA && value == 0x55)
  {
    flash_step = 2;
    return;
  }
  if(flash_step != 2)
  {
    flash_step = 0;
    return;
  }
  flash_step = 0;
  if(flash_mode == 'E')
  {
    if(address == 0x5555 && value == 0x10)
      memset(save, 0xFF, save_size);
    else if(value == 0x30)
      memset(bank + (address & 0xF000), 0xFF, 0x1000);
    flash_mode = 0;
    save_written();
    return;
  }
  if(address != 0x5555)
    return;
  switch(value)
  {
    case 0x90: flash_mode = 'I'; break;
    case 0xF0: flash_mode = 0; break;
    case 0x80: flash_mode = 'E'; break;
    case 0xA0: flash_mode = 'W'; break;
    case 0xB0: flash_mode = save_size == 0x20000 ? 'B' : 0; break;
  }
}

static void backup_write8(uint32_t address, uint32_t value)
{
  address &= 0xFFFF;
  value &= 0xFF;
  if(save_kind == SAVE_FLASH)
    flash_write(address, value);
  else if(save_kind == SAVE_SRAM)
  {
    save[address] = (u8)value;
    if(address >= 0x8000)
      save_size = 0x10000;
    save_written();
  }
}

static uint32_t mem_read(uint32_t address, unsigned size);
static void mem_write(uint32_t address, unsigned size, uint32_t value);

/* An EEPROM request sent by DMA3: 2 command bits, a 6- or 14-bit block
   number, for a write 64 data bits, a stop bit; one bit per halfword. */
static void eeprom_request(uint32_t source, uint32_t count)
{
  unsigned bits[81];
  unsigned address_bits = (count == 17 || count == 81) ? 14 : 6;
  uint32_t block = 0;
  unsigned i;

  if(count != 9 && count != 17 && count != 73 && count != 81)
    return;
  if(address_bits == 14)
    save_size = 0x2000;
  for(i = 0; i < count; i++)
    bits[i] = mem_read(source + i * 2, 2) & 1;
  for(i = 0; i < address_bits; i++)
    block = block << 1 | bits[2 + i];
  block &= save_size / 8 - 1;
  if(bits[0] == 1 && bits[1] == 1 && count < 73)
    eeprom_block = block;
  else if(bits[0] == 1 && bits[1] == 0 && count >= 73)
    for(i = 0, save_written(); i < 64; i++)
    {
      u8 *byte = &save[block * 8 + i / 8];
      unsigned shift = 7 - i % 8;

      *byte = (u8)((*byte & ~(1u << shift)) |
       bits[2 + address_bits + i] << shift);
    }
}

/* The answer to a read request: 4 bits to ignore, then 64 data bits. */
static void eeprom_answer(uint32_t destination, uint32_t count)
{
  uint32_t i;

  for(i = 0; i < count; i++)
  {
    uint32_t bit = 0;

    if(i >= 4 && i < 68)
      bit = (save[eeprom_block * 8 + (i - 4) / 8] >> (7 - (i - 4) % 8)) & 1;
    mem_write(destination + i * 2, 2, bit);
  }
}

/* ---- I/O -------------------------------------------------------------- */

static void dma3(void)
{
  uint32_t source = load(io, 0x0D4, 4) & 0x0FFFFFFE;
  uint32_t destination = load(io, 0x0D8, 4) & 0x0FFFFFFE;
  uint32_t count = io16(0x0DC) ? io16(0x0DC) : 0x10000;
  uint32_t control = io16(0x0DE);
  unsigned size = (control & 0x0400) ? 4 : 2;
  int step[4] = { (int)size, -(int)size, 0, (int)size };
  uint32_t i;

  store(io, 0x0DE, 2, control & 0x7FFF);
  if(control & 0x3000)
    return;
  if(save_kind == SAVE_EEPROM && (destination >> 24) == 0x0D)
    eeprom_request(source, count);
  else if(save_kind == SAVE_EEPROM && (source >> 24) == 0x0D)
    eeprom_answer(destination, count);
  else
    for(i = 0; i < count; i++)
    {
      mem_write(destination, size, mem_read(source, size));
      source += (uint32_t)step[(control >> 7) & 3];
      destination += (uint32_t)step[(control >> 5) & 3];
    }
  if(control & 0x4000)
    raise_irq(0x0800);
}

static uint32_t io_read16(uint32_t reg)
{
  uint32_t value;

  switch(reg)
  {
    case 0x004:
      value = io16(0x004) & 0xFFF8;
      if(line_now() >= 160 && line_now() < 227)
        value |= 1;
      if(now % LINE_CYCLES >= HBLANK_START)
        value |= 2;
      if(line_now() == value >> 8)
        value |= 4;
      return value;
    case 0x006:
      return line_now();
    case 0x130:
      return 0x03FF;
    default:
      return io16(reg);
  }
}

static void halt(void)
{
  halted = 1;
  arm_core_request_exit(&core);
}

static void io_write16(uint32_t reg, uint32_t value)
{
  value &= 0xFFFF;
  switch(reg)
  {
    case 0x004:
      store(io, reg, 2, (io16(reg) & 7) | (value & 0xFFF8));
      return;
    case 0x202:
      store(io, reg, 2, io16(reg) & ~value);
      return;
    case 0x300:
      store(io, reg, 2, value);
      halt();
      return;
  }
  store(io, reg, 2, value);
  if(reg == 0x204)
    arm_core_set_waitcnt(&core, (uint16_t)value);
  if(reg == 0x0DE && (value & 0x8000))
    dma3();
}

static uint32_t io_read(uint32_t address, unsigned size)
{
  uint32_t reg = address & 0x3FE;

  if(size == 4)
    return io_read16(reg) | io_read16(reg + 2) << 16;
  if(size == 2)
    return io_read16(reg);
  return (io_read16(reg) >> ((address & 1) * 8)) & 0xFF;
}

static void io_write(uint32_t address, unsigned size, uint32_t value)
{
  uint32_t reg = address & 0x3FE;

  if(size == 4)
  {
    io_write16(reg, value);
    io_write16(reg + 2, value >> 16);
  }
  else if(size == 2)
    io_write16(reg, value);
  else if((address & 0x3FF) == 0x301)
    halt();
  else if(reg == 0x202)
    io_write16(reg, (value & 0xFF) << ((address & 1) * 8));
  else
  {
    unsigned shift = (address & 1) * 8;

    io_write16(reg, (io16(reg) & ~(0xFFu << shift)) |
     (value & 0xFF) << shift);
  }
}

/* ---- Memory map ------------------------------------------------------- */

static uint32_t vram_offset(uint32_t address)
{
  address &= 0x1FFFF;
  return address >= 0x18000 ? address - 0x8000 : address;
}

static uint32_t mem_read(uint32_t address, unsigned size)
{
  uint32_t value;

  address &= 0x0FFFFFFF;
  switch(address >> 24)
  {
    case 0x0:
      if(address >= 0x4000)
        return 0;
      if(core.insn_pc < 0x4000)
        return load(bios, address, size);
      value = bios_protect >> ((address & 3) * 8);
      return size == 4 ? value : value & (size == 2 ? 0xFFFF : 0xFF);
    case 0x2:
      return load(ewram, address & 0x3FFFF, size);
    case 0x3:
      return load(iwram, address & 0x7FFF, size);
    case 0x4:
      return io_read(address, size);
    case 0x5:
      return load(palette, address & 0x3FF, size);
    case 0x6:
      return load(vram, vram_offset(address), size);
    case 0x7:
      return load(oam, address & 0x3FF, size);
    case 0xD:
      if(save_kind == SAVE_EEPROM)
        return 1;
      /* fall through */
    case 0x8: case 0x9: case 0xA: case 0xB: case 0xC:
      return load(rom, address & (rom_size - 1), size);
    case 0xE: case 0xF:
      value = backup_read8(address);
      return value * (size == 4 ? 0x01010101u : size == 2 ? 0x0101u : 1u);
    default:
      return 0;
  }
}

static void mem_write(uint32_t address, unsigned size, uint32_t value)
{
  address &= 0x0FFFFFFF;
  switch(address >> 24)
  {
    case 0x2:
      store(ewram, address & 0x3FFFF, size, value);
      arm_core_invalidate(&core, address, size);
      break;
    case 0x3:
      store(iwram, address & 0x7FFF, size, value);
      arm_core_invalidate(&core, address, size);
      break;
    case 0x4:
      io_write(address, size, value);
      break;
    case 0x5:
      store(palette, address & 0x3FF, size, value);
      break;
    case 0x6:
      store(vram, vram_offset(address), size, value);
      break;
    case 0x7:
      store(oam, address & 0x3FF, size, value);
      break;
    case 0xE: case 0xF:
      backup_write8(address, value);
      break;
  }
}

static uint32_t bus_read8(void *context, uint32_t address)
{
  (void)context;
  return mem_read(address, 1);
}

static uint32_t bus_read16(void *context, uint32_t address)
{
  (void)context;
  return mem_read(address, 2);
}

static uint32_t bus_read32(void *context, uint32_t address)
{
  (void)context;
  return mem_read(address, 4);
}

static void bus_write8(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  mem_write(address, 1, value);
}

static void bus_write16(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  mem_write(address, 2, value);
}

static void bus_write32(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  mem_write(address, 4, value);
}

/* Instructions and literals: no side effects, the BIOS unprotected. */
static uint32_t bus_code32(void *context, uint32_t address)
{
  (void)context;
  address &= 0x0FFFFFFF;
  switch(address >> 24)
  {
    case 0x0:
      return address < 0x4000 ? load(bios, address & 0x3FFC, 4) : 0;
    case 0x2: case 0x3: case 0x5: case 0x6: case 0x7:
    case 0x8: case 0x9: case 0xA: case 0xB: case 0xC:
      return mem_read(address, 4);
    default:
      return 0;
  }
}

static const ArmBus bus =
{
  NULL, bus_read8, bus_read16, bus_read32, bus_write8, bus_write16,
  bus_write32, bus_code32
};

/* ---- Running ---------------------------------------------------------- */

static int irq_pending(void)
{
  return (io16(0x200) & io16(0x202) & 0x3FFF) != 0;
}

static void run(long long end)
{
  while(now < end)
  {
    if(halted)
    {
      if(!irq_pending())
      {
        advance(next_vblank() - now);
        continue;
      }
      halted = 0;
    }
    if((io16(0x208) & 1) && irq_pending() && !(core.cpsr & ARM_CPSR_I))
      advance(arm_core_irq(&core));
    if(core.v[15] < 0x4000)
    {
      /* One instruction at a time inside the BIOS, to know its last
         ARM opcode. ponytail: Thumb code there leaves the value alone. */
      uint32_t pc = core.v[15];
      int arm = !(core.cpsr & ARM_CPSR_T);

      advance(arm_core_step(&core));
      if(arm)
        bios_protect = load(bios, (pc + 8) & 0x3FFC, 4);
    }
    else
    {
      core.cycles = 0;
      arm_core_run_block(&core);
      advance(core.cycles);
    }
  }
}

/* The save type from the ROM's save library ID string, at a word
   boundary; none means SRAM, which GBA Rush settles on at the first
   access. */
static void find_save_type(void)
{
  uint32_t i;

  for(i = 0; i + 12 <= rom_size; i += 4)
  {
    const char *at = (const char *)rom + i;

    if(!memcmp(at, "EEPROM_V", 8))
    {
      save_kind = SAVE_EEPROM;
      save_size = 0x200;
      return;
    }
    if(!memcmp(at, "SRAM_V", 6))
      return;
    if(!memcmp(at, "FLASH1M_V", 9))
    {
      save_kind = SAVE_FLASH;
      save_size = 0x20000;
      return;
    }
    if(!memcmp(at, "FLASH_V", 7) || !memcmp(at, "FLASH512_V", 10))
    {
      save_kind = SAVE_FLASH;
      save_size = 0x10000;
      return;
    }
  }
}

static long file_bytes(const char *path, void *buffer, size_t max)
{
  FILE *file = fopen(path, "rb");
  size_t bytes;

  if(file == NULL)
    return -1;
  bytes = fread(buffer, 1, max, file);
  fclose(file);
  return (long)bytes;
}

static int write_file(const char *path, const void *data, size_t bytes)
{
  FILE *file = fopen(path, "wb");
  int ok = file != NULL && fwrite(data, 1, bytes, file) == bytes;

  if(file != NULL && fclose(file) != 0)
    ok = 0;
  return ok;
}

static int write_screen(const char *path)
{
  static u8 pixels[240 * 160 * 3];
  char header[32];
  FILE *file;
  unsigned i;
  int ok;

  for(i = 0; i < 240 * 160; i++)
  {
    uint32_t color = load(vram, i * 2, 2);

    pixels[i * 3] = (u8)((color & 31) << 3);
    pixels[i * 3 + 1] = (u8)(((color >> 5) & 31) << 3);
    pixels[i * 3 + 2] = (u8)(((color >> 10) & 31) << 3);
  }
  snprintf(header, sizeof(header), "P6\n240 160\n255\n");
  file = fopen(path, "wb");
  if(file == NULL)
    return 0;
  ok = fputs(header, file) >= 0 &&
   fwrite(pixels, 1, sizeof(pixels), file) == sizeof(pixels);
  return fclose(file) == 0 && ok;
}

static int usage(void)
{
  fprintf(stderr, "usage: rom_run [--bios FILE] [--frames N] "
   "[--save FILE] [--screen FILE] ROM\n");
  return 2;
}

int main(int argc, char **argv)
{
  const char *bios_path = NULL;
  const char *save_path = NULL;
  const char *screen_path = NULL;
  const char *rom_path = NULL;
  long frames = 300;
  long bytes;
  unsigned sum = 0;
  unsigned i;
  int arg;

  for(arg = 1; arg < argc; arg++)
  {
    if(!strcmp(argv[arg], "--bios") && arg + 1 < argc)
      bios_path = argv[++arg];
    else if(!strcmp(argv[arg], "--frames") && arg + 1 < argc)
      frames = strtol(argv[++arg], NULL, 0);
    else if(!strcmp(argv[arg], "--save") && arg + 1 < argc)
      save_path = argv[++arg];
    else if(!strcmp(argv[arg], "--screen") && arg + 1 < argc)
      screen_path = argv[++arg];
    else if(argv[arg][0] != '-' && rom_path == NULL)
      rom_path = argv[arg];
    else
      return usage();
  }
  if(rom_path == NULL || frames <= 0)
    return usage();

  rom = malloc(MAX_ROM);
  if(rom == NULL)
    return 1;
  memset(rom, 0xFF, MAX_ROM);
  bytes = file_bytes(rom_path, rom, MAX_ROM);
  if(bytes < 0xC0)
  {
    fprintf(stderr, "rom_run: %s: no ROM\n", rom_path);
    return 1;
  }
  for(rom_size = 0x8000; rom_size < (uint32_t)bytes; rom_size *= 2)
    ;
  /* What the BIOS checks before it starts a cartridge, the logo apart. */
  for(i = 0xA0; i < 0xBD; i++)
    sum += rom[i];
  if(rom[0xB2] != 0x96 || rom[0xBD] != ((0u - sum - 0x19) & 0xFF))
  {
    fprintf(stderr, "rom_run: %s: bad cartridge header\n", rom_path);
    return 1;
  }
  find_save_type();

  if(bios_path == NULL)
    memcpy(bios, open_bios_rom, sizeof(bios));
  else if(file_bytes(bios_path, bios, sizeof(bios)) != (long)sizeof(bios))
  {
    fprintf(stderr, "rom_run: %s: not a 16 KiB BIOS\n", bios_path);
    return 1;
  }

  memset(save, 0xFF, sizeof(save));
  if(save_path != NULL)
  {
    bytes = file_bytes(save_path, save, sizeof(save));
    if(save_kind == SAVE_SRAM && bytes == 0x10000)
      save_size = 0x10000;
    if(save_kind == SAVE_EEPROM && bytes == 0x2000)
      save_size = 0x2000;
  }
  memcpy(saved, save, sizeof(saved));
  saved_size = save_size;

  arm_core_init(&core, &bus);
  core.map[0x2] = (ArmRegion){ ewram, ewram, 0x3FFFF };
  core.map[0x3] = (ArmRegion){ iwram, iwram, 0x7FFF };
  core.map[0x5] = (ArmRegion){ palette, palette, 0x3FF };
  core.map[0x7] = (ArmRegion){ oam, oam, 0x3FF };
  for(i = 0x8; i <= 0xC; i++)
    core.map[i] = (ArmRegion){ rom, NULL, rom_size - 1 };
  /* Started directly, as GBA Rush does unless "Boot from BIOS" is on. */
  core.v[15] = 0x08000000;

  run((long long)frames * FRAME_CYCLES);

  if(save_path != NULL && !write_file(save_path, saved, saved_size))
  {
    fprintf(stderr, "rom_run: cannot write %s\n", save_path);
    return 1;
  }
  if(screen_path != NULL && !write_screen(screen_path))
  {
    fprintf(stderr, "rom_run: cannot write %s\n", screen_path);
    return 1;
  }
  printf("rom_run: %s: %ld frames, %u instructions, %s of %u bytes\n",
   rom_path, frames, core.instructions,
   save_kind == SAVE_SRAM ? "SRAM" : save_kind == SAVE_FLASH ? "Flash" :
   "EEPROM", save_size);
  return 0;
}
