#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "codebreaker.h"

static int failures;
static uint8_t memory_image[0x400];

#define CHECK(condition) \
  do \
  { \
    if(!(condition)) \
    { \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
       #condition); \
      failures++; \
    } \
  } while(0)

static uint32_t memory_offset(uint32_t address)
{
  return address & (sizeof(memory_image) - 1);
}

uint16_t codebreaker_memory_read16(uint32_t address)
{
  uint32_t offset = memory_offset(address) & ~1u;
  return memory_image[offset] | ((uint16_t)memory_image[offset + 1] << 8);
}

uint32_t codebreaker_memory_read32(uint32_t address)
{
  uint32_t offset = memory_offset(address) & ~3u;
  return memory_image[offset] | ((uint32_t)memory_image[offset + 1] << 8) |
   ((uint32_t)memory_image[offset + 2] << 16) |
   ((uint32_t)memory_image[offset + 3] << 24);
}

void codebreaker_memory_write8(uint32_t address, uint8_t value)
{
  memory_image[memory_offset(address)] = value;
}

void codebreaker_memory_write16(uint32_t address, uint16_t value)
{
  uint32_t offset = memory_offset(address) & ~1u;
  memory_image[offset] = value;
  memory_image[offset + 1] = value >> 8;
}

void codebreaker_memory_write32(uint32_t address, uint32_t value)
{
  uint32_t offset = memory_offset(address) & ~3u;
  memory_image[offset] = value;
  memory_image[offset + 1] = value >> 8;
  memory_image[offset + 2] = value >> 16;
  memory_image[offset + 3] = value >> 24;
}

static void reset_memory(void)
{
  memset(memory_image, 0, sizeof(memory_image));
}

static void test_reset(void)
{
  CODEBREAKER_STATE state;
  CODEBREAKER_STATE empty;

  memset(&state, 0xA5, sizeof(state));
  memset(&empty, 0, sizeof(empty));
  codebreaker_state_reset(&state);
  CHECK(memcmp(&state, &empty, sizeof(state)) == 0);
}

static void test_unpack(void)
{
  uint8_t bytes[6];
  static const uint8_t expected[6] =
   { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };

  codebreaker_unpack(bytes, 0x12345678, 0x9ABC);
  CHECK(memcmp(bytes, expected, sizeof(bytes)) == 0);
}

static void test_payload_lengths(void)
{
  CHECK(codebreaker_payload_lines(0x40000000, 0) == 1);
  CHECK(codebreaker_payload_lines(0x50000000, 0) == 0);
  CHECK(codebreaker_payload_lines(0x50000000, 1) == 1);
  CHECK(codebreaker_payload_lines(0x50000000, 3) == 1);
  CHECK(codebreaker_payload_lines(0x50000000, 4) == 2);
  CHECK(codebreaker_payload_lines(0x80000000, 4) == 0);
}

static void test_seed_and_decrypt(void)
{
  CODEBREAKER_STATE state;
  uint32_t op1;
  uint16_t op2;
  static const uint32_t expected_seeds[4] =
   { 0x44336493, 0x5998F8E3, 0x50BB6344, 0xD8888A77 };
  static const uint8_t expected_table[48] =
  {
    0x17, 0x01, 0x03, 0x10, 0x23, 0x22, 0x1F, 0x19,
    0x1D, 0x09, 0x06, 0x25, 0x21, 0x2B, 0x0F, 0x16,
    0x2E, 0x2A, 0x0E, 0x28, 0x0C, 0x1C, 0x27, 0x12,
    0x0A, 0x15, 0x18, 0x1E, 0x04, 0x11, 0x0B, 0x26,
    0x1A, 0x05, 0x29, 0x1B, 0x13, 0x2F, 0x14, 0x07,
    0x0D, 0x02, 0x08, 0x24, 0x2C, 0x2D, 0x20, 0x00
  };

  codebreaker_state_reset(&state);
  codebreaker_reseed(&state, 0x9ABCDEF0, 0x1234);

  CHECK(state.master == 0x9ABCDEF0);
  CHECK(memcmp(state.seeds, expected_seeds, sizeof(expected_seeds)) == 0);
  CHECK(state.rng_state == 0xF8E30E9C);
  CHECK(memcmp(state.table, expected_table, sizeof(expected_table)) == 0);

  op1 = 0x12345678;
  op2 = 0x9ABC;
  codebreaker_decrypt(&state, &op1, &op2);
  CHECK(op1 == 0x7B80713C);
  CHECK(op2 == 0x5CD3);

  op1 = 0xFFFFFFFF;
  op2 = 0xFFFF;
  codebreaker_decrypt(&state, &op1, &op2);
  CHECK(op1 == 0x096B3328);
  CHECK(op2 == 0x0A80);
}

static void test_write_operations(void)
{
  static const uint32_t or_code[] = { 0x20000101, 0x00F0 };
  static const uint32_t byte_code[] = { 0x30000102, 0x12AB };
  static const uint32_t fill_code[] =
  {
    0x40000100, 0x0010,
    0x00030003, 0x0002
  };
  static const uint32_t list_code[] =
  {
    0x50000120, 0x0004,
    0x11223344, 0x5566,
    0x778899AA, 0xBBCC
  };
  static const uint32_t and_code[] = { 0x60000101, 0x0FF0 };
  static const uint32_t assign_code[] = { 0x80000101, 0xBEEF };
  static const uint32_t add16_code[] = { 0xE0000100, 0x0003 };
  static const uint32_t add32_code[] = { 0xE0000101, 0xFFFE };
  static const uint8_t expected_list[] =
   { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };

  reset_memory();
  codebreaker_memory_write16(0x100, 0x0F0F);
  codebreaker_execute(or_code, 1);
  CHECK(codebreaker_memory_read16(0x100) == 0x0FFF);

  codebreaker_execute(byte_code, 1);
  CHECK(memory_image[0x102] == 0xAB);

  reset_memory();
  codebreaker_execute(fill_code, 2);
  CHECK(codebreaker_memory_read16(0x100) == 0x0010);
  CHECK(codebreaker_memory_read16(0x102) == 0x0013);
  CHECK(codebreaker_memory_read16(0x104) == 0x0016);

  reset_memory();
  codebreaker_execute(list_code, 3);
  CHECK(memcmp(memory_image + 0x120, expected_list,
   sizeof(expected_list)) == 0);

  codebreaker_memory_write16(0x100, 0xFFFF);
  codebreaker_execute(and_code, 1);
  CHECK(codebreaker_memory_read16(0x100) == 0x0FF0);

  codebreaker_execute(assign_code, 1);
  CHECK(codebreaker_memory_read16(0x100) == 0xBEEF);

  codebreaker_memory_write16(0x100, 0xFFFE);
  codebreaker_execute(add16_code, 1);
  CHECK(codebreaker_memory_read16(0x100) == 0x0001);

  codebreaker_memory_write32(0x100, 3);
  codebreaker_execute(add32_code, 1);
  CHECK(codebreaker_memory_read32(0x100) == 1);
}

static void check_condition(uint32_t op1, uint16_t op2,
 uint16_t memory_value, int expected_true)
{
  uint32_t address = op1 & 0x0FFFFFFF;
  uint32_t codes[] =
  {
    op1, op2,
    0x80000200, 0xCAFE
  };

  reset_memory();
  if((op1 >> 28) == 0xD)
    codebreaker_memory_write16(0x04000130, memory_value);
  else
    codebreaker_memory_write16(address, memory_value);

  codebreaker_execute(codes, 2);
  CHECK((codebreaker_memory_read16(0x200) == 0xCAFE) == expected_true);
}

static void test_conditions(void)
{
  check_condition(0x70000100, 5, 5, 1);
  check_condition(0x70000100, 5, 4, 0);
  check_condition(0xA0000100, 5, 4, 1);
  check_condition(0xA0000100, 5, 5, 0);
  check_condition(0xB0000100, 5, 6, 1);
  check_condition(0xB0000100, 5, 5, 0);
  check_condition(0xC0000100, 5, 4, 1);
  check_condition(0xC0000100, 5, 5, 0);
  check_condition(0xD0000020, 2, 1, 1);
  check_condition(0xD0000020, 1, 1, 0);
  check_condition(0xF0000100, 2, 3, 1);
  check_condition(0xF0000100, 2, 1, 0);
}

static void test_conditional_multiline_skip(void)
{
  static const uint32_t codes[] =
  {
    0x70000100, 0x0001,
    0x40000120, 0x0010,
    0x00010003, 0x0002,
    0x80000140, 0xBEEF
  };

  reset_memory();
  codebreaker_execute(codes, 4);
  CHECK(codebreaker_memory_read16(0x120) == 0);
  CHECK(codebreaker_memory_read16(0x122) == 0);
  CHECK(codebreaker_memory_read16(0x124) == 0);
  CHECK(codebreaker_memory_read16(0x140) == 0xBEEF);
}

static void test_truncated_multiline_operations(void)
{
  static const uint32_t truncated_fill[] = { 0x40000100, 0x0010 };
  static const uint32_t truncated_list[] =
  {
    0x50000120, 0x0004,
    0x11223344, 0x5566
  };
  static const uint8_t expected_prefix[] =
   { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

  reset_memory();
  codebreaker_execute(truncated_fill, 1);
  CHECK(codebreaker_memory_read16(0x100) == 0);

  codebreaker_execute(truncated_list, 2);
  CHECK(memcmp(memory_image + 0x120, expected_prefix,
   sizeof(expected_prefix)) == 0);
  CHECK(memory_image[0x126] == 0);
}

int main(void)
{
  test_reset();
  test_unpack();
  test_payload_lengths();
  test_seed_and_decrypt();
  test_write_operations();
  test_conditions();
  test_conditional_multiline_skip();
  test_truncated_multiline_operations();

  if(failures)
  {
    fprintf(stderr, "%d CodeBreaker test(s) failed\n", failures);
    return 1;
  }

  puts("CodeBreaker tests passed");
  return 0;
}
