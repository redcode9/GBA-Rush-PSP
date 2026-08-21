#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gba_sio.h"

static int failures;
static uint16_t registers[GBA_SIO_REGISTER_BYTES / 2];
static GbaSio sio;

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

static uint16_t reg16(uint32_t address)
{
  return registers[address >> 1];
}

static void reset_sio(void)
{
  memset(registers, 0, sizeof(registers));
  gba_sio_init(&sio, registers);
}

static void select_mode(GbaSioMode mode)
{
  uint16_t control = 0;

  if(mode == GBA_SIO_MODE_GPIO)
  {
    gba_sio_write16(&sio, GBA_SIO_REG_RCNT, 0x8000);
    return;
  }

  if(mode == GBA_SIO_MODE_JOYBUS)
  {
    gba_sio_write16(&sio, GBA_SIO_REG_RCNT, 0xC000);
    return;
  }

  gba_sio_write16(&sio, GBA_SIO_REG_RCNT, 0x0000);
  if(mode == GBA_SIO_MODE_NORMAL_32)
    control = 0x1000;
  else if(mode == GBA_SIO_MODE_MULTIPLAYER)
    control = 0x2000;
  else if(mode == GBA_SIO_MODE_UART)
    control = 0x3000;
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, control);
}

static void test_reset_and_modes(void)
{
  reset_sio();
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_GPIO);
  CHECK(reg16(GBA_SIO_REG_RCNT) == 0x800F);
  CHECK(reg16(GBA_SIO_REG_CONTROL) == 0x0004);

  select_mode(GBA_SIO_MODE_NORMAL_8);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_NORMAL_8);
  select_mode(GBA_SIO_MODE_NORMAL_32);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_NORMAL_32);
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_MULTIPLAYER);
  select_mode(GBA_SIO_MODE_UART);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_UART);
  select_mode(GBA_SIO_MODE_GPIO);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_GPIO);
  select_mode(GBA_SIO_MODE_JOYBUS);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_JOYBUS);
}

static void test_control_masks(void)
{
  GbaSioEvent event;

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0xCFFF);
  CHECK((event & GBA_SIO_EVENT_START) != 0);
  CHECK(reg16(GBA_SIO_REG_CONTROL) == 0x408F);

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0xEFFF);
  CHECK((event & GBA_SIO_EVENT_START) != 0);
  CHECK(reg16(GBA_SIO_REG_CONTROL) == 0x608F);

  gba_sio_write16(&sio, GBA_SIO_REG_MULTI0, 0x1234);
  gba_sio_write16(&sio, GBA_SIO_REG_MULTI3, 0x5678);
  CHECK(reg16(GBA_SIO_REG_MULTI0) == 0xFFFF);
  CHECK(reg16(GBA_SIO_REG_MULTI3) == 0xFFFF);
}

static void test_rcnt_masks(void)
{
  reset_sio();
  gba_sio_write16(&sio, GBA_SIO_REG_RCNT, 0x81FF);
  CHECK(reg16(GBA_SIO_REG_RCNT) == 0x81FF);

  gba_sio_write16(&sio, GBA_SIO_REG_RCNT, 0x01F0);
  CHECK(reg16(GBA_SIO_REG_RCNT) == 0x01FF);

  gba_sio_write16(&sio, GBA_SIO_REG_RCNT, 0xC1F0);
  CHECK(reg16(GBA_SIO_REG_RCNT) == 0xC1FF);
}

static void test_multiplayer_connection_state(void)
{
  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);

  gba_sio_set_connection(&sio, 0, 2);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x003C) == 0x0008);

  gba_sio_set_connection(&sio, 1, 2);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x003C) == 0x001C);

  gba_sio_set_connection(&sio, 3, 9);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x003C) == 0x003C);

  gba_sio_set_connection(&sio, 0, 0);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x003C) == 0x000C);
}

static void test_normal_timing(void)
{
  CHECK(gba_sio_transfer_cycles(GBA_SIO_MODE_NORMAL_8, 0, 2) == 512);
  CHECK(gba_sio_transfer_cycles(GBA_SIO_MODE_NORMAL_8, 1, 2) == 64);
  CHECK(gba_sio_transfer_cycles(GBA_SIO_MODE_NORMAL_32, 0, 2) == 2048);
  CHECK(gba_sio_transfer_cycles(GBA_SIO_MODE_NORMAL_32, 1, 2) == 256);
}

static void test_multiplayer_timing(void)
{
  static const uint32_t expected[4] = { 63427, 16241, 10998, 5755 };
  uint32_t baud;

  for(baud = 0; baud < 4; baud++)
  {
    CHECK(gba_sio_transfer_cycles(GBA_SIO_MODE_MULTIPLAYER,
     (uint8_t)baud, 2) == expected[baud]);
  }
}

static void test_normal8_transfer(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;
  GbaSioEvent event;

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0x12AB);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083);
  CHECK(event == GBA_SIO_EVENT_START);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  CHECK(transfer.mode == GBA_SIO_MODE_NORMAL_8);
  CHECK(transfer.outgoing == 0xAB);
  CHECK(transfer.setting == 1);
  CHECK(transfer.internal_clock == 1);
  CHECK(gba_sio_take_start(&sio, &transfer) == 0);

  memset(&result, 0, sizeof(result));
  result.data[0] = 0x5A;
  result.player_count = 2;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_next_event(&sio) == 64);
  CHECK(gba_sio_advance(&sio, 63) == GBA_SIO_EVENT_NONE);
  CHECK(gba_sio_next_event(&sio) == 1);
  CHECK(gba_sio_advance(&sio, 1) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(reg16(GBA_SIO_REG_SEND) == 0x005A);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080) == 0);
}

static void test_normal32_transfer(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_32);
  gba_sio_write32(&sio, GBA_SIO_REG_DATA32_L, 0x12345678);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x5081) ==
   GBA_SIO_EVENT_START);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  CHECK(transfer.outgoing == 0x12345678);
  CHECK(transfer.setting == 0);

  memset(&result, 0, sizeof(result));
  result.data[0] = 0x89ABCDEF;
  result.player_count = 2;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_next_event(&sio) == 2048);
  CHECK(gba_sio_advance(&sio, 2048) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(reg16(GBA_SIO_REG_DATA32_L) == 0xCDEF);
  CHECK(reg16(GBA_SIO_REG_DATA32_H) == 0x89AB);
}

static void test_external_clock_transfer(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_set_connection(&sio, 1, 2);
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0x00C3);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4080) ==
   GBA_SIO_EVENT_NONE);
  CHECK(gba_sio_take_start(&sio, &transfer) == 0);
  CHECK(gba_sio_latch_remote_start(&sio, GBA_SIO_MODE_NORMAL_8, 1,
   &transfer) == 1);
  CHECK(transfer.outgoing == 0xC3);
  CHECK(transfer.internal_clock == 0);
  CHECK(transfer.setting == 1);

  memset(&result, 0, sizeof(result));
  result.data[0] = 0x3C;
  result.player_count = 2;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_advance(&sio, 64) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(reg16(GBA_SIO_REG_SEND) == 0x003C);
}

static void test_multiplayer_transfer(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;
  GbaSioEvent event;

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_set_connection(&sio, 0, 2);
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0x1234);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x6083);
  CHECK(event == GBA_SIO_EVENT_START);
  CHECK(reg16(GBA_SIO_REG_MULTI0) == 0xFFFF);
  CHECK(reg16(GBA_SIO_REG_MULTI3) == 0xFFFF);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  CHECK(transfer.outgoing == 0x1234);
  CHECK(transfer.setting == 3);
  CHECK(transfer.player_id == 0);

  memset(&result, 0xFF, sizeof(result));
  result.data[0] = 0x1234;
  result.data[1] = 0xABCD;
  result.player_count = 2;
  result.error = 0;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_next_event(&sio) == 5755);
  CHECK(gba_sio_advance(&sio, 5755) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(reg16(GBA_SIO_REG_MULTI0) == 0x1234);
  CHECK(reg16(GBA_SIO_REG_MULTI1) == 0xABCD);
  CHECK(reg16(GBA_SIO_REG_MULTI2) == 0xFFFF);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x00C0) == 0);
}

static void test_multiplayer_remote_start(void)
{
  GbaSioTransfer transfer;

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_set_connection(&sio, 1, 2);
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0xBEEF);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x6083) ==
   GBA_SIO_EVENT_NONE);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080) == 0);
  CHECK(gba_sio_latch_remote_start(&sio, GBA_SIO_MODE_MULTIPLAYER, 3,
   &transfer) == 1);
  CHECK(transfer.outgoing == 0xBEEF);
  CHECK(transfer.player_id == 1);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x009C) == 0x009C);
}

static void test_abort_and_disconnect(void)
{
  GbaSioTransfer transfer;
  GbaSioEvent event;

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_set_connection(&sio, 0, 2);
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0x1111);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x6080);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  event = gba_sio_disconnect(&sio);
  CHECK(event == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(reg16(GBA_SIO_REG_MULTI0) == 0xFFFF);
  CHECK(reg16(GBA_SIO_REG_MULTI3) == 0xFFFF);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0040) != 0);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080) == 0);
}

static void test_normal_abort_values(void)
{
  GbaSioTransfer transfer;

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4081);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  CHECK(gba_sio_abort(&sio) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(reg16(GBA_SIO_REG_SEND) == 0x00FF);

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_32);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x5081);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  CHECK(gba_sio_abort(&sio) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(reg16(GBA_SIO_REG_DATA32_L) == 0xFFFF);
  CHECK(reg16(GBA_SIO_REG_DATA32_H) == 0xFFFF);
}

static void test_error_completion_and_mode_change(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_set_connection(&sio, 0, 2);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x2080);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  memset(&result, 0xFF, sizeof(result));
  result.player_count = 2;
  result.error = 1;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_advance(&sio, gba_sio_next_event(&sio)) ==
   GBA_SIO_EVENT_NONE);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0040) != 0);

  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x0000);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_NORMAL_8);
  CHECK(gba_sio_next_event(&sio) == GBA_SIO_NO_EVENT);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x00C0) == 0);
}

static void test_result_player_clamp(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_set_connection(&sio, 0, 2);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x2080);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  memset(&result, 0, sizeof(result));
  result.player_count = UINT8_MAX;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_next_event(&sio) == 125829);
}

static void test_repeated_start_and_reset(void)
{
  GbaSioEvent event;

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x0081);
  CHECK(event == GBA_SIO_EVENT_START);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x0081);
  CHECK(event == GBA_SIO_EVENT_NONE);
  gba_sio_reset(&sio);
  CHECK(gba_sio_next_event(&sio) == GBA_SIO_NO_EVENT);
  CHECK(gba_sio_is_waiting(&sio) == 0);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080) == 0);
}

static void test_snapshot(void)
{
  GbaSioSnapshot snapshot;
  GbaSioTransfer transfer;
  GbaSioResult result;
  uint16_t restored_registers[GBA_SIO_REGISTER_BYTES / 2];
  GbaSio restored;

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_32);
  gba_sio_write32(&sio, GBA_SIO_REG_DATA32_L, 0x11223344);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x1081);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  memset(&result, 0, sizeof(result));
  result.data[0] = 0x55667788;
  result.player_count = 1;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_advance(&sio, 100) == GBA_SIO_EVENT_NONE);
  gba_sio_save_snapshot(&sio, &snapshot);

  gba_sio_init(&restored, restored_registers);
  memcpy(restored_registers, registers, sizeof(registers));
  CHECK(gba_sio_load_snapshot(&restored, &snapshot) == 1);
  CHECK(gba_sio_next_event(&restored) == 1948);
  CHECK(gba_sio_advance(&restored, 1948) == GBA_SIO_EVENT_NONE);
  CHECK(restored_registers[GBA_SIO_REG_DATA32_L >> 1] == 0x7788);
  CHECK(restored_registers[GBA_SIO_REG_DATA32_H >> 1] == 0x5566);

  snapshot.transfer_players = 5;
  CHECK(gba_sio_load_snapshot(&restored, &snapshot) == 0);
  CHECK((restored_registers[GBA_SIO_REG_CONTROL >> 1] & 0x0080) == 0);

  memset(&snapshot, 0, sizeof(snapshot));
  restored_registers[GBA_SIO_REG_CONTROL >> 1] |= 0x0080;
  CHECK(gba_sio_load_snapshot(&restored, &snapshot) == 0);
  CHECK((restored_registers[GBA_SIO_REG_CONTROL >> 1] & 0x0080) == 0);
}

int main(void)
{
  test_reset_and_modes();
  test_control_masks();
  test_rcnt_masks();
  test_multiplayer_connection_state();
  test_normal_timing();
  test_multiplayer_timing();
  test_normal8_transfer();
  test_normal32_transfer();
  test_external_clock_transfer();
  test_multiplayer_transfer();
  test_multiplayer_remote_start();
  test_abort_and_disconnect();
  test_normal_abort_values();
  test_error_completion_and_mode_change();
  test_result_player_clamp();
  test_repeated_start_and_reset();
  test_snapshot();

  if(failures)
  {
    fprintf(stderr, "%d GBA SIO test(s) failed\n", failures);
    return 1;
  }

  puts("GBA SIO tests passed");
  return 0;
}
