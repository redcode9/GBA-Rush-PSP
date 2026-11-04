/*
 * Host tests for save_format.c: the save type and size chosen from the
 * ROM's save library tag and the .sav file's size, the bytes written back,
 * and the repair of the save fields a savestate restores. The regression
 * behind it: a 64 KiB .sav of an SRAM game used to be written back as a
 * single byte.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "save_format.h"

#define KIB 1024u

typedef struct
{
  SaveTag tag;
  uint32_t file_size;
  SaveType type;
  uint32_t write_size; /* written before the game picks a type */
  int mismatch;
} ResolveCase;

/* Every tag against no file and the sizes found in the wild: 1 byte (cut
   by the old bug), each native size, 128 KiB images of smaller chips,
   128 KiB plus a 16-byte RTC footer, and an oversized file. */
static const ResolveCase resolve_cases[] =
{
  { SAVE_TAG_SRAM, 0, SAVE_TYPE_SRAM, 32 * KIB, 0 },
  { SAVE_TAG_SRAM, 1, SAVE_TYPE_SRAM, 32 * KIB, 1 },
  { SAVE_TAG_SRAM, 512, SAVE_TYPE_SRAM, 32 * KIB, 1 },
  { SAVE_TAG_SRAM, 8 * KIB, SAVE_TYPE_SRAM, 32 * KIB, 1 },
  { SAVE_TAG_SRAM, 32 * KIB, SAVE_TYPE_SRAM, 32 * KIB, 0 },
  { SAVE_TAG_SRAM, 64 * KIB, SAVE_TYPE_SRAM, 64 * KIB, 0 },
  { SAVE_TAG_SRAM, 128 * KIB, SAVE_TYPE_SRAM, 64 * KIB, 1 },
  { SAVE_TAG_SRAM, 128 * KIB + 16, SAVE_TYPE_SRAM, 64 * KIB, 1 },
  { SAVE_TAG_SRAM, 200 * KIB, SAVE_TYPE_SRAM, 64 * KIB, 1 },

  { SAVE_TAG_FLASH64, 0, SAVE_TYPE_FLASH, 64 * KIB, 0 },
  { SAVE_TAG_FLASH64, 1, SAVE_TYPE_FLASH, 64 * KIB, 1 },
  { SAVE_TAG_FLASH64, 512, SAVE_TYPE_FLASH, 64 * KIB, 1 },
  { SAVE_TAG_FLASH64, 8 * KIB, SAVE_TYPE_FLASH, 64 * KIB, 1 },
  { SAVE_TAG_FLASH64, 32 * KIB, SAVE_TYPE_FLASH, 64 * KIB, 1 },
  { SAVE_TAG_FLASH64, 64 * KIB, SAVE_TYPE_FLASH, 64 * KIB, 0 },
  { SAVE_TAG_FLASH64, 128 * KIB, SAVE_TYPE_FLASH, 64 * KIB, 1 },
  { SAVE_TAG_FLASH64, 128 * KIB + 16, SAVE_TYPE_FLASH, 64 * KIB, 1 },
  { SAVE_TAG_FLASH64, 200 * KIB, SAVE_TYPE_FLASH, 64 * KIB, 1 },

  { SAVE_TAG_FLASH128, 0, SAVE_TYPE_FLASH, 128 * KIB, 0 },
  { SAVE_TAG_FLASH128, 1, SAVE_TYPE_FLASH, 128 * KIB, 1 },
  { SAVE_TAG_FLASH128, 512, SAVE_TYPE_FLASH, 128 * KIB, 1 },
  { SAVE_TAG_FLASH128, 8 * KIB, SAVE_TYPE_FLASH, 128 * KIB, 1 },
  { SAVE_TAG_FLASH128, 32 * KIB, SAVE_TYPE_FLASH, 128 * KIB, 1 },
  { SAVE_TAG_FLASH128, 64 * KIB, SAVE_TYPE_FLASH, 128 * KIB, 1 },
  { SAVE_TAG_FLASH128, 128 * KIB, SAVE_TYPE_FLASH, 128 * KIB, 0 },
  { SAVE_TAG_FLASH128, 128 * KIB + 16, SAVE_TYPE_FLASH, 128 * KIB, 1 },
  { SAVE_TAG_FLASH128, 200 * KIB, SAVE_TYPE_FLASH, 128 * KIB, 1 },

  { SAVE_TAG_EEPROM, 0, SAVE_TYPE_EEPROM, 512, 0 },
  { SAVE_TAG_EEPROM, 1, SAVE_TYPE_EEPROM, 512, 1 },
  { SAVE_TAG_EEPROM, 512, SAVE_TYPE_EEPROM, 512, 0 },
  { SAVE_TAG_EEPROM, 8 * KIB, SAVE_TYPE_EEPROM, 8 * KIB, 0 },
  { SAVE_TAG_EEPROM, 32 * KIB, SAVE_TYPE_EEPROM, 512, 1 },
  { SAVE_TAG_EEPROM, 64 * KIB, SAVE_TYPE_EEPROM, 512, 1 },
  { SAVE_TAG_EEPROM, 128 * KIB, SAVE_TYPE_EEPROM, 512, 1 },
  { SAVE_TAG_EEPROM, 128 * KIB + 16, SAVE_TYPE_EEPROM, 512, 1 },
  { SAVE_TAG_EEPROM, 200 * KIB, SAVE_TYPE_EEPROM, 512, 1 },

  { SAVE_TAG_NONE, 0, SAVE_TYPE_NONE, 0, 0 },
  { SAVE_TAG_NONE, 1, SAVE_TYPE_NONE, 0, 1 },
  { SAVE_TAG_NONE, 512, SAVE_TYPE_EEPROM, 512, 0 },
  { SAVE_TAG_NONE, 8 * KIB, SAVE_TYPE_EEPROM, 8 * KIB, 0 },
  { SAVE_TAG_NONE, 32 * KIB, SAVE_TYPE_SRAM, 32 * KIB, 0 },
  { SAVE_TAG_NONE, 64 * KIB, SAVE_TYPE_NONE, 0, 0 },
  { SAVE_TAG_NONE, 128 * KIB, SAVE_TYPE_FLASH, 128 * KIB, 0 },
  { SAVE_TAG_NONE, 128 * KIB + 16, SAVE_TYPE_NONE, 0, 1 },
  { SAVE_TAG_NONE, 200 * KIB, SAVE_TYPE_NONE, 0, 1 }
};

static uint32_t layout_write_size(const SaveLayout *layout, uint32_t type)
{
  return save_format_write_size(type, layout->sram_size, layout->flash_size,
   layout->eeprom_size);
}

/* Type, size and mismatch for each case; a known type never writes less
   than 512 bytes or more than the 128 KiB buffer, and whatever type a game
   picks when the tag leaves it open, the write is a real chip size. */
static void test_resolve_matrix(void)
{
  size_t i;

  for(i = 0; i < sizeof(resolve_cases) / sizeof(resolve_cases[0]); i++)
  {
    const ResolveCase *c = &resolve_cases[i];
    SaveLayout layout = save_format_resolve(c->tag, c->file_size);
    uint32_t written = layout_write_size(&layout, layout.type);

    assert(layout.type == c->type);
    assert(written == c->write_size);
    assert(layout.mismatch == c->mismatch);
    if(layout.type != SAVE_TYPE_NONE)
      assert(written >= 512 && written <= SAVE_FORMAT_MAX_SIZE);
    if(layout.type == SAVE_TYPE_NONE)
    {
      assert(layout_write_size(&layout, SAVE_TYPE_SRAM) >= 32 * KIB);
      assert(layout_write_size(&layout, SAVE_TYPE_FLASH) >= 64 * KIB);
      assert(layout_write_size(&layout, SAVE_TYPE_EEPROM) >= 512);
    }
  }
}

/* The bug: a game whose tag says SRAM, with a 64 KiB file, wrote 1 byte.
   Untagged, the game picks the type and either choice keeps 64 KiB. */
static void test_sram_64k_file_keeps_its_size(void)
{
  SaveLayout tagged = save_format_resolve(SAVE_TAG_SRAM, 64 * KIB);
  SaveLayout untagged = save_format_resolve(SAVE_TAG_NONE, 64 * KIB);

  assert(layout_write_size(&tagged, tagged.type) == 65536);
  assert(untagged.type == SAVE_TYPE_NONE);
  assert(layout_write_size(&untagged, SAVE_TYPE_SRAM) == 65536);
  assert(layout_write_size(&untagged, SAVE_TYPE_FLASH) == 65536);
}

/* The tag also fixes the Flash size, which sets the chip ID games read. */
static void test_tag_beats_file_size(void)
{
  assert(save_format_resolve(SAVE_TAG_FLASH64, 128 * KIB).flash_size ==
         64 * KIB);
  assert(save_format_resolve(SAVE_TAG_FLASH128, 64 * KIB).flash_size ==
         128 * KIB);
  assert(save_format_resolve(SAVE_TAG_EEPROM, 32 * KIB).type ==
         SAVE_TYPE_EEPROM);
  assert(save_format_resolve(SAVE_TAG_SRAM, 512).type == SAVE_TYPE_SRAM);
  assert(save_format_resolve(SAVE_TAG_NONE, 512).type == SAVE_TYPE_EEPROM);
}

/* Sizes a type cannot have, such as the 1 that the old size switch stored
   in sram_size, write the type's larger size, never a cut file. */
static void test_write_size_never_cuts(void)
{
  assert(save_format_write_size(SAVE_TYPE_SRAM, 1, 0, 0) == 64 * KIB);
  assert(save_format_write_size(SAVE_TYPE_SRAM, 32 * KIB, 0, 0) == 32 * KIB);
  assert(save_format_write_size(SAVE_TYPE_FLASH, 0, 0, 0) == 128 * KIB);
  assert(save_format_write_size(SAVE_TYPE_FLASH, 0, 64 * KIB, 0) ==
         64 * KIB);
  assert(save_format_write_size(SAVE_TYPE_EEPROM, 0, 0, 0) == 8 * KIB);
  assert(save_format_write_size(SAVE_TYPE_EEPROM, 0, 0, 512) == 512);
  assert(save_format_write_size(SAVE_TYPE_NONE, 32 * KIB, 64 * KIB, 512) ==
         0);
  assert(save_format_write_size(7, 32 * KIB, 64 * KIB, 512) == 0);
}

static SaveState valid_state(void)
{
  SaveState state;

  state.type = SAVE_TYPE_EEPROM;
  state.sram_size = 64 * KIB;
  state.flash_size = 128 * KIB;
  state.flash_bank = 1;
  state.eeprom_size = 8 * KIB;
  state.eeprom_mode = SAVE_EEPROM_WRITE;
  state.eeprom_address_length = 14;
  state.eeprom_address = SAVE_FORMAT_MAX_SIZE - 8;
  state.eeprom_counter = 63;
  return state;
}

static int same_state(const SaveState *a, const SaveState *b)
{
  return a->type == b->type && a->sram_size == b->sram_size &&
         a->flash_size == b->flash_size && a->flash_bank == b->flash_bank &&
         a->eeprom_size == b->eeprom_size &&
         a->eeprom_mode == b->eeprom_mode &&
         a->eeprom_address_length == b->eeprom_address_length &&
         a->eeprom_address == b->eeprom_address &&
         a->eeprom_counter == b->eeprom_counter;
}

/* Valid states load unchanged, at the edge of every range, including those
   of games that never ran an EEPROM command (address width still 0). */
static void test_sanitize_keeps_valid_states(void)
{
  static const uint32_t modes[][3] =
  {
    { SAVE_EEPROM_BASE, 1, 14 }, { SAVE_EEPROM_READ, 63, 14 },
    { SAVE_EEPROM_READ_HEADER, 3, 14 }, { SAVE_EEPROM_ADDRESS, 13, 14 },
    { SAVE_EEPROM_WRITE, 63, 14 }, { SAVE_EEPROM_WRITE_ADDRESS, 13, 14 },
    { SAVE_EEPROM_ADDRESS_FOOTER, 500, 14 },
    { SAVE_EEPROM_WRITE_FOOTER, 500, 14 },
    { SAVE_EEPROM_BASE, 0, 0 }, { SAVE_EEPROM_WRITE, 63, 0 }
  };
  SaveState before = valid_state();
  size_t i;

  for(i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
  {
    SaveState state = valid_state();
    SaveState copy;

    state.eeprom_mode = modes[i][0];
    state.eeprom_counter = modes[i][1];
    state.eeprom_address_length = modes[i][2];
    copy = state;
    assert(save_format_sanitize(&state, &before) == 0);
    assert(same_state(&state, &copy));
  }
}

/* Each field out of range is put back and reported in its own bit. */
static void test_sanitize_repairs_each_field(void)
{
  SaveState before = valid_state();
  SaveState state;

  before.type = SAVE_TYPE_SRAM;
  before.sram_size = 64 * KIB;
  before.flash_size = 64 * KIB;
  before.eeprom_size = 512;

  state = valid_state();
  state.type = 9;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_TYPE);
  assert(state.type == SAVE_TYPE_SRAM);

  /* States saved by older builds hold sram_size 1 for a 64 KiB file. */
  state = valid_state();
  state.sram_size = 1;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_SRAM_SIZE);
  assert(state.sram_size == 64 * KIB);

  state = valid_state();
  state.flash_size = 0x30000;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_FLASH_SIZE);
  assert(state.flash_size == 64 * KIB);

  state = valid_state();
  state.flash_bank = 5;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_FLASH_BANK);
  assert(state.flash_bank == 0);

  state = valid_state();
  state.eeprom_mode = SAVE_EEPROM_BASE;
  state.eeprom_counter = 0;
  state.eeprom_size = 0x4000;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_EEPROM_SIZE);
  assert(state.eeprom_size == 512);

  /* The address width matters only in an address phase. */
  state = valid_state();
  state.eeprom_mode = SAVE_EEPROM_ADDRESS;
  state.eeprom_counter = 5;
  state.eeprom_address_length = 9;
  assert(save_format_sanitize(&state, &before) ==
         SAVE_FIX_EEPROM_ADDRESS_LENGTH);
  assert(state.eeprom_address_length == 14 && state.eeprom_counter == 5);

  state = valid_state();
  state.eeprom_mode = SAVE_EEPROM_WRITE_ADDRESS;
  state.eeprom_counter = 0;
  state.eeprom_address_length = 0;
  assert(save_format_sanitize(&state, &before) ==
         SAVE_FIX_EEPROM_ADDRESS_LENGTH);
  assert(state.eeprom_address_length == 14);

  state = valid_state();
  state.eeprom_address = SAVE_FORMAT_MAX_SIZE - 7;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_EEPROM_ADDRESS);
  assert(state.eeprom_address == 0);

  state = valid_state();
  state.eeprom_mode = 8;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_EEPROM_MODE);
  assert(state.eeprom_mode == SAVE_EEPROM_BASE && state.eeprom_counter == 0);

  state = valid_state();
  state.eeprom_counter = 64;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_EEPROM_COUNTER);
  assert(state.eeprom_mode == SAVE_EEPROM_BASE && state.eeprom_counter == 0);

  state = valid_state();
  state.eeprom_mode = SAVE_EEPROM_ADDRESS;
  state.eeprom_address_length = 6;
  state.eeprom_counter = 6;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_EEPROM_COUNTER);

  state = valid_state();
  state.eeprom_mode = SAVE_EEPROM_BASE;
  state.eeprom_counter = 2;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_EEPROM_COUNTER);

  state = valid_state();
  state.eeprom_mode = SAVE_EEPROM_READ_HEADER;
  state.eeprom_counter = 4;
  assert(save_format_sanitize(&state, &before) == SAVE_FIX_EEPROM_COUNTER);
}

/* Whatever a state holds, after the repair every access memory.c makes
   with these fields stays inside the 128 KiB buffer: the EEPROM bit at
   address + counter / 8, a Flash bank of 64 KiB, the 8-byte EEPROM
   command buffer, the shift by (1 - counter) of the command phase, and
   the bytes written back. */
static void test_sanitize_bounds_any_state(void)
{
  static const uint32_t interesting[] =
  {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 63, 64, 65, 512, 0x2000,
    0x8000, 0x10000, 0x1FFF8, 0x1FFF9, 0x20000, 0x30000, 0x7FFFFFFF,
    0xFFFFFFFF
  };
  const size_t count = sizeof(interesting) / sizeof(interesting[0]);
  SaveState before = valid_state();
  uint32_t seed = 12345;
  int i;

  for(i = 0; i < 200000; i++)
  {
    SaveState state;
    uint32_t limit = 0;

#define PICK() (seed = seed * 1103515245u + 12345u, \
                 interesting[(seed >> 16) % count])
    state.type = PICK();
    state.sram_size = PICK();
    state.flash_size = PICK();
    state.flash_bank = PICK();
    state.eeprom_size = PICK();
    state.eeprom_mode = PICK();
    state.eeprom_address_length = PICK();
    state.eeprom_address = PICK();
    state.eeprom_counter = PICK();
#undef PICK
    save_format_sanitize(&state, &before);

    assert(state.type <= SAVE_TYPE_NONE);
    assert(state.flash_bank <= 1);
    assert(state.eeprom_mode <= SAVE_EEPROM_WRITE_FOOTER);
    if(state.eeprom_mode == SAVE_EEPROM_BASE)
      limit = 2;
    if(state.eeprom_mode == SAVE_EEPROM_READ ||
       state.eeprom_mode == SAVE_EEPROM_WRITE)
      limit = 64;
    if(state.eeprom_mode == SAVE_EEPROM_ADDRESS ||
       state.eeprom_mode == SAVE_EEPROM_WRITE_ADDRESS)
    {
      assert(state.eeprom_address_length == 6 ||
             state.eeprom_address_length == 14);
      limit = state.eeprom_address_length;
    }
    if(state.eeprom_mode == SAVE_EEPROM_READ_HEADER)
      limit = 4;
    if(limit != 0)
      assert(state.eeprom_counter < limit);
    assert(state.eeprom_address + 63 / 8 < SAVE_FORMAT_MAX_SIZE);
    assert((state.flash_bank << 16) + 0xFFFF < SAVE_FORMAT_MAX_SIZE);
    assert(save_format_write_size(state.type, state.sram_size,
     state.flash_size, state.eeprom_size) <= SAVE_FORMAT_MAX_SIZE);
  }
}

int main(void)
{
  test_resolve_matrix();
  test_sram_64k_file_keeps_its_size();
  test_tag_beats_file_size();
  test_write_size_never_cuts();
  test_sanitize_keeps_valid_states();
  test_sanitize_repairs_each_field();
  test_sanitize_bounds_any_state();
  puts("save format tests passed");
  return 0;
}
