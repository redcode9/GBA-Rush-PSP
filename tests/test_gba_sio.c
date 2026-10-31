/*
 * Host tests for gba_sio.c, the emulated GBA serial port. Each test writes
 * the registers the way a game does and checks what the game reads back,
 * the transfer length in GBA cycles and the serial IRQ. The test plays the
 * link itself: it takes the transfer the game started and commits the data
 * the other console would have sent.
 *
 * Register values used throughout (GBATEK names):
 *   SIOCNT  bits 0-1 clock (normal) or baud (multiplayer), 2 SI, 3 SD,
 *           4-5 multiplayer ID, 6 error, 7 start/busy, 12-13 mode
 *           (normal 8, normal 32, multiplayer, UART), 14 IRQ enable
 *   RCNT    0x8000 GPIO, 0xC000 JOY bus, bit 15 clear lets SIOCNT choose
 */

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

/* gba_sio_prepare() binds the register bank on first use only; it must
   leave a running game's serial state and the other I/O registers alone. */
static void test_prepare_cold_preserves_other_registers(void)
{
  uint32_t index;

  memset(&sio, 0, sizeof(sio));
  for(index = 0; index < GBA_SIO_REGISTER_BYTES / 2; index++)
    registers[index] = (uint16_t)(0xA500u + index);

  CHECK(gba_sio_prepare(&sio, registers) == 1);
  CHECK(sio.registers == registers);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_GPIO);
  CHECK(reg16(GBA_SIO_REG_RCNT) == 0x800Fu);
  CHECK(reg16(GBA_SIO_REG_CONTROL) == 0x0004u);
  for(index = 0; index < GBA_SIO_REGISTER_BYTES / 2; index++)
  {
    if(index != GBA_SIO_REG_RCNT / 2 && index != GBA_SIO_REG_CONTROL / 2)
      CHECK(registers[index] == (uint16_t)(0xA500u + index));
  }
}

static void test_prepare_is_noop_for_loaded_game(void)
{
  GbaSio before;
  uint16_t registers_before[GBA_SIO_REGISTER_BYTES / 2];
  uint32_t attempt;

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  CHECK(gba_sio_set_cycle(&sio, 0x12345678u));
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0xBEEFu);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x6083u);
  CHECK(sio.pending_start != 0);
  memcpy(&before, &sio, sizeof(before));
  memcpy(registers_before, registers, sizeof(registers_before));
  for(attempt = 0; attempt < 3; attempt++)
  {
    CHECK(gba_sio_prepare(&sio, registers) == 1);
    CHECK(memcmp(&before, &sio, sizeof(sio)) == 0);
    CHECK(memcmp(registers_before, registers, sizeof(registers)) == 0);
  }

  gba_sio_reset(&sio);
  memcpy(&before, &sio, sizeof(before));
  memcpy(registers_before, registers, sizeof(registers_before));
  CHECK(gba_sio_prepare(&sio, registers) == 1);
  CHECK(memcmp(&before, &sio, sizeof(sio)) == 0);
  CHECK(memcmp(registers_before, registers, sizeof(registers)) == 0);
}

static void test_prepare_rejects_invalid_storage(void)
{
  GbaSio before;
  uint16_t registers_before[GBA_SIO_REGISTER_BYTES / 2];
  uint16_t other[GBA_SIO_REGISTER_BYTES / 2];
  uint16_t other_before[GBA_SIO_REGISTER_BYTES / 2];

  memset(&sio, 0, sizeof(sio));
  memcpy(&before, &sio, sizeof(before));
  CHECK(gba_sio_prepare(NULL, NULL) == 0);
  CHECK(gba_sio_prepare(&sio, NULL) == 0);
  CHECK(memcmp(&before, &sio, sizeof(sio)) == 0);
  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  memset(other, 0x5A, sizeof(other));
  memcpy(&before, &sio, sizeof(before));
  memcpy(registers_before, registers, sizeof(registers_before));
  memcpy(other_before, other, sizeof(other_before));
  CHECK(gba_sio_prepare(NULL, registers) == 0);
  CHECK(gba_sio_prepare(&sio, NULL) == 0);
  CHECK(gba_sio_prepare(&sio, other) == 0);
  CHECK(memcmp(&before, &sio, sizeof(sio)) == 0);
  CHECK(memcmp(registers_before, registers, sizeof(registers)) == 0);
  CHECK(memcmp(other_before, other, sizeof(other)) == 0);
}

static void test_control_masks(void)
{
  GbaSioEvent event;

  /* Writing every bit: only writable bits and the line state read back. */
  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0xCFFF);
  CHECK((event & GBA_SIO_EVENT_START) != 0);
  CHECK(reg16(GBA_SIO_REG_CONTROL) == 0x408F);

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0xEFFF);
  CHECK((event & GBA_SIO_EVENT_START) != 0);
  CHECK(reg16(GBA_SIO_REG_CONTROL) == 0x6087);

  gba_sio_write16(&sio, GBA_SIO_REG_MULTI0, 0x1234);
  gba_sio_write16(&sio, GBA_SIO_REG_MULTI3, 0x5678);
  /* GBATEK marks SIOMULTI0-3 R/W. FireRed clears these after a handshake. */
  CHECK(reg16(GBA_SIO_REG_MULTI0) == 0x1234);
  CHECK(reg16(GBA_SIO_REG_MULTI3) == 0x5678);
  gba_sio_write32(&sio, GBA_SIO_REG_MULTI0, 0);
  CHECK(reg16(GBA_SIO_REG_MULTI0) == 0);
  CHECK(reg16(GBA_SIO_REG_MULTI1) == 0);
}

static void test_rcnt_masks(void)
{
  reset_sio();
  gba_sio_write16(&sio, GBA_SIO_REG_RCNT, 0x81FF);
  CHECK(reg16(GBA_SIO_REG_RCNT) == 0x81FF);

  gba_sio_write16(&sio, GBA_SIO_REG_RCNT, 0x01F0);
  CHECK(reg16(GBA_SIO_REG_RCNT) == 0x01FD);

  gba_sio_write16(&sio, GBA_SIO_REG_RCNT, 0xC1F0);
  CHECK(reg16(GBA_SIO_REG_RCNT) == 0xC1FD);
}

/* SIOCNT SI/SD/ID (mask 0x003C) and the RCNT line bits report this
   console's player slot and whether the other console is ready. */
static void test_multiplayer_connection_state(void)
{
  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);

  gba_sio_set_connection(&sio, 0, 2);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x003C) == 0x0000);
  gba_sio_set_peer_state(&sio, GBA_SIO_MODE_MULTIPLAYER, 0x201Cu,
   0x0007u, 1);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x003C) == 0x0008);
  CHECK((reg16(GBA_SIO_REG_RCNT) & 0x0007u) == 0x0003u);

  gba_sio_set_connection(&sio, 1, 2);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x003C) == 0x001C);
  CHECK((reg16(GBA_SIO_REG_RCNT) & 0x0007u) == 0x0007u);

  gba_sio_set_connection(&sio, 3, 9);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x003C) == 0x003C);

  gba_sio_set_connection(&sio, 0, 0);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x003C) == 0x0004);
}

/* Normal mode moves 8 or 32 bits at 256 KHz (setting 0) or 2 MHz (1). */
static void test_normal_timing(void)
{
  CHECK(gba_sio_transfer_cycles(GBA_SIO_MODE_NORMAL_8, 0, 2) == 512);
  CHECK(gba_sio_transfer_cycles(GBA_SIO_MODE_NORMAL_8, 1, 2) == 64);
  CHECK(gba_sio_transfer_cycles(GBA_SIO_MODE_NORMAL_32, 0, 2) == 2048);
  CHECK(gba_sio_transfer_cycles(GBA_SIO_MODE_NORMAL_32, 1, 2) == 256);
}

/* One multiplayer word between two players at 9600, 38400, 57600 and
   115200 bps (baud setting 0-3). */
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

/* Reference vectors:
 * - Nintendo AGB Programming Manual, sections 13.1 and 13.2: completion
 *   replaces the receive registers, clears START/BUSY, and raises SIO IRQ
 *   only when SIOCNT.14 is enabled.
 * - mGBA src/gba/sio.c: GBASIOCyclesPerTransfer,
 *   GBASIOTransferCycles, and the three *FinishTransfer functions.
 */
static void test_reference_timing_and_register_traces(void)
{
  static const struct
  {
    GbaSioMode mode;
    uint8_t setting;
    uint32_t cycles;
  } vectors[] =
  {
    { GBA_SIO_MODE_NORMAL_8,  0, 512u },
    { GBA_SIO_MODE_NORMAL_8,  1, 64u },
    { GBA_SIO_MODE_NORMAL_32, 0, 2048u },
    { GBA_SIO_MODE_NORMAL_32, 1, 256u },
    { GBA_SIO_MODE_MULTIPLAYER, 0, 63427u },
    { GBA_SIO_MODE_MULTIPLAYER, 1, 16241u },
    { GBA_SIO_MODE_MULTIPLAYER, 2, 10998u },
    { GBA_SIO_MODE_MULTIPLAYER, 3, 5755u }
  };
  GbaSioTransfer transfer;
  GbaSioResult result;
  uint32_t irq_enabled;
  uint32_t index;

  for(irq_enabled = 0; irq_enabled < 2u; irq_enabled++)
  {
    for(index = 0; index < sizeof(vectors) / sizeof(vectors[0]); index++)
    {
      GbaSioEvent finish_event;
      uint16_t control;
      uint32_t start = 0xFFF00000u + index * 0x10000u +
       irq_enabled * 0x8000u;

      reset_sio();
      CHECK(gba_sio_set_cycle(&sio, start) == 1);
      select_mode(vectors[index].mode);
      gba_sio_set_connection(&sio, 0, 2);
      memset(&result, 0xFF, sizeof(result));
      result.player_count = 2;
      if(vectors[index].mode == GBA_SIO_MODE_MULTIPLAYER)
      {
        gba_sio_set_peer_state(&sio, GBA_SIO_MODE_MULTIPLAYER,
         0x6008u, 0x0003u, 1);
        gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0x1234u);
        control = (uint16_t)(0x2080u | vectors[index].setting |
         (irq_enabled ? 0x4000u : 0u));
        result.data[0] = 0x1234u;
        result.data[1] = 0xABCDu;
      }
      else
      {
        control = (uint16_t)((vectors[index].mode ==
         GBA_SIO_MODE_NORMAL_32 ? 0x1000u : 0u) | 0x0081u |
         (vectors[index].setting != 0 ? 0x0002u : 0u) |
         (irq_enabled ? 0x4000u : 0u));
        if(vectors[index].mode == GBA_SIO_MODE_NORMAL_32)
        {
          gba_sio_write32(&sio, GBA_SIO_REG_DATA32_L, 0x11223344u);
          result.data[0] = 0xAABBCCDDu;
        }
        else
        {
          gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0x0044u);
          result.data[0] = 0xDDu;
        }
      }

      CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, control) ==
       GBA_SIO_EVENT_START);
      CHECK(gba_sio_take_start(&sio, &transfer) == 1);
      CHECK(gba_sio_commit_at(&sio, &result, start,
       start + vectors[index].cycles) == 1);
      CHECK(gba_sio_next_event(&sio) == vectors[index].cycles);
      CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080u) != 0);
      CHECK(gba_sio_advance(&sio, vectors[index].cycles - 1u) ==
       GBA_SIO_EVENT_NONE);
      CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080u) != 0);

      if(vectors[index].mode == GBA_SIO_MODE_MULTIPLAYER)
      {
        CHECK(reg16(GBA_SIO_REG_MULTI0) == 0xFFFFu);
        CHECK(reg16(GBA_SIO_REG_MULTI1) == 0xFFFFu);
      }
      else if(vectors[index].mode == GBA_SIO_MODE_NORMAL_32)
      {
        CHECK(reg16(GBA_SIO_REG_DATA32_L) == 0x3344u);
        CHECK(reg16(GBA_SIO_REG_DATA32_H) == 0x1122u);
      }
      else
      {
        CHECK(reg16(GBA_SIO_REG_SEND) == 0x0044u);
      }

      finish_event = gba_sio_advance(&sio, 1u);
      CHECK(finish_event == (irq_enabled ? GBA_SIO_EVENT_SERIAL_IRQ :
       GBA_SIO_EVENT_NONE));
      CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080u) == 0);
      CHECK(((reg16(GBA_SIO_REG_CONTROL) & 0x4000u) != 0) ==
       (irq_enabled != 0));

      if(vectors[index].mode == GBA_SIO_MODE_MULTIPLAYER)
      {
        CHECK(reg16(GBA_SIO_REG_MULTI0) == 0x1234u);
        CHECK(reg16(GBA_SIO_REG_MULTI1) == 0xABCDu);
        CHECK(reg16(GBA_SIO_REG_MULTI2) == 0xFFFFu);
        CHECK(reg16(GBA_SIO_REG_MULTI3) == 0xFFFFu);
      }
      else if(vectors[index].mode == GBA_SIO_MODE_NORMAL_32)
      {
        CHECK(reg16(GBA_SIO_REG_DATA32_L) == 0xCCDDu);
        CHECK(reg16(GBA_SIO_REG_DATA32_H) == 0xAABBu);
      }
      else
      {
        CHECK(reg16(GBA_SIO_REG_SEND) == 0x00DDu);
      }
    }
  }
}

/* With the IRQ bit clear a transfer still completes and stores the
   received byte, but raises no interrupt. */
static void test_reference_irq_disabled_trace(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0x0044u);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x0083u) ==
   GBA_SIO_EVENT_START);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  memset(&result, 0, sizeof(result));
  result.player_count = 2;
  result.data[0] = 0xDDu;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_advance(&sio, 64u) == GBA_SIO_EVENT_NONE);
  CHECK(reg16(GBA_SIO_REG_SEND) == 0x00DDu);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080u) == 0);
}

/* The IRQ enable bit counts at completion, not at start. A mode change
   cancels a transfer in flight, unless the link already locked in the
   decision to complete it (gba_sio_lock_decision). */
static void test_reference_irq_toggle_and_mode_change_traces(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;
  GbaSioEvent event;

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083u) ==
   GBA_SIO_EVENT_START);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  memset(&result, 0, sizeof(result));
  result.player_count = 2;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x0083u) ==
   GBA_SIO_EVENT_NONE);
  CHECK(gba_sio_advance(&sio, 64u) == GBA_SIO_EVENT_NONE);

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x0083u) ==
   GBA_SIO_EVENT_START);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  memset(&result, 0, sizeof(result));
  result.player_count = 2;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083u) ==
   GBA_SIO_EVENT_NONE);
  CHECK(gba_sio_advance(&sio, 64u) == GBA_SIO_EVENT_SERIAL_IRQ);

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083u) ==
   GBA_SIO_EVENT_START);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x2000u);
  CHECK(event == GBA_SIO_EVENT_MODE_CHANGE);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_MULTIPLAYER);
  CHECK(!gba_sio_is_busy(&sio));
  CHECK(gba_sio_next_event(&sio) == GBA_SIO_NO_EVENT);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080u) == 0);

  reset_sio();
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0x0044u);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083u) ==
   GBA_SIO_EVENT_START);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  CHECK(gba_sio_lock_decision(&sio) == 1);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x6000u);
  CHECK(event == GBA_SIO_EVENT_MODE_CHANGE);
  CHECK(gba_sio_mode(&sio) == GBA_SIO_MODE_MULTIPLAYER);
  CHECK(gba_sio_is_waiting(&sio));
  CHECK(sio.decision_locked != 0);
  memset(&result, 0, sizeof(result));
  result.player_count = 2;
  result.data[0] = 0xDDu;
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_advance(&sio, 64u) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(reg16(GBA_SIO_REG_SEND) == 0x00DDu);
  CHECK(!gba_sio_is_busy(&sio));
  CHECK(sio.decision_locked == 0);
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
  CHECK(gba_sio_peek_start(&sio, &transfer) == 1);
  CHECK(transfer.mode == GBA_SIO_MODE_NORMAL_8);
  CHECK(transfer.outgoing == 0xAB);
  CHECK(sio.pending_start != 0);
  CHECK(sio.waiting == 0);
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

/* With the external clock (bit 0 clear) the game only arms the transfer;
   it runs when the other console supplies the clock. */
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

/* Parent side of one multiplayer word at 115200 bps: SIOMULTI0-3 read
   0xFFFF while busy, and slots without a player stay 0xFFFF afterwards. */
static void test_multiplayer_transfer(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;
  GbaSioEvent event;

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_set_connection(&sio, 0, 2);
  gba_sio_set_peer_state(&sio, GBA_SIO_MODE_MULTIPLAYER, 0x601Cu,
   0x0007u, 1);
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0x1234);
  event = gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x6083);
  CHECK(event == GBA_SIO_EVENT_START);
  CHECK((reg16(GBA_SIO_REG_RCNT) & 0x0001u) == 0);
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
  CHECK((reg16(GBA_SIO_REG_RCNT) & 0x0001u) != 0);
}

/* A child cannot start a transfer itself; the parent's start latches it. */
static void test_multiplayer_remote_start(void)
{
  GbaSioTransfer transfer;

  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_set_connection(&sio, 1, 2);
  gba_sio_set_peer_state(&sio, GBA_SIO_MODE_MULTIPLAYER, 0x6008u,
   0x0003u, 1);
  gba_sio_write16(&sio, GBA_SIO_REG_SEND, 0xBEEF);
  CHECK(gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x6083) ==
   GBA_SIO_EVENT_NONE);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x0080) == 0);
  CHECK(gba_sio_latch_remote_start(&sio, GBA_SIO_MODE_MULTIPLAYER, 3,
   &transfer) == 1);
  CHECK(transfer.outgoing == 0xBEEF);
  CHECK(transfer.player_id == 1);
  CHECK((reg16(GBA_SIO_REG_CONTROL) & 0x009C) == 0x009C);
  CHECK((reg16(GBA_SIO_REG_RCNT) & 0x0001u) == 0);
}

/* Runs one 115200 bps word between two wired GbaSio instances. */
static void complete_multiplayer_word(GbaSio *parent, GbaSio *child,
 uint16_t parent_word, uint16_t child_word)
{
  GbaSioTransfer parent_transfer;
  GbaSioTransfer child_transfer;
  GbaSioResult result;
  uint32_t cycles;

  gba_sio_write16(parent, GBA_SIO_REG_SEND, parent_word);
  gba_sio_write16(child, GBA_SIO_REG_SEND, child_word);
  CHECK(gba_sio_write16(parent, GBA_SIO_REG_CONTROL, 0x6083u) ==
   GBA_SIO_EVENT_START);
  CHECK(gba_sio_take_start(parent, &parent_transfer) == 1);
  CHECK(gba_sio_latch_remote_start(child, GBA_SIO_MODE_MULTIPLAYER, 3,
   &child_transfer) == 1);
  CHECK(parent_transfer.outgoing == parent_word);
  CHECK(child_transfer.outgoing == child_word);

  memset(&result, 0xFF, sizeof(result));
  result.player_count = 2;
  result.data[0] = parent_word;
  result.data[1] = child_word;
  CHECK(gba_sio_commit(parent, &result) == 1);
  CHECK(gba_sio_commit(child, &result) == 1);
  cycles = gba_sio_transfer_cycles(GBA_SIO_MODE_MULTIPLAYER, 3, 2);
  CHECK(gba_sio_advance(parent, cycles) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(gba_sio_advance(child, cycles) == GBA_SIO_EVENT_SERIAL_IRQ);
}

/* FireRed's link handshake on two GbaSio instances: 0xB9A0 and 0x8FFF are
   the slave and master handshake words of the pret src/link.c driver. */
static void test_firered_master_driven_handshake(void)
{
  uint16_t parent_registers[GBA_SIO_REGISTER_BYTES / 2] = {0};
  uint16_t child_registers[GBA_SIO_REGISTER_BYTES / 2] = {0};
  GbaSio parent;
  GbaSio child;

  gba_sio_init(&parent, parent_registers);
  gba_sio_init(&child, child_registers);
  gba_sio_write16(&parent, GBA_SIO_REG_RCNT, 0);
  gba_sio_write16(&child, GBA_SIO_REG_RCNT, 0);
  gba_sio_write16(&parent, GBA_SIO_REG_CONTROL, 0x6003u);
  gba_sio_write16(&child, GBA_SIO_REG_CONTROL, 0x6003u);
  gba_sio_set_connection(&parent, 0, 2);
  gba_sio_set_connection(&child, 1, 2);
  gba_sio_set_peer_state(&parent, GBA_SIO_MODE_MULTIPLAYER,
   child_registers[GBA_SIO_REG_CONTROL >> 1],
   child_registers[GBA_SIO_REG_RCNT >> 1], 1);
  gba_sio_set_peer_state(&child, GBA_SIO_MODE_MULTIPLAYER,
   parent_registers[GBA_SIO_REG_CONTROL >> 1],
   parent_registers[GBA_SIO_REG_RCNT >> 1], 1);

  CHECK((parent_registers[GBA_SIO_REG_CONTROL >> 1] & 0x000Cu) ==
   0x0008u);
  CHECK((child_registers[GBA_SIO_REG_CONTROL >> 1] & 0x001Cu) ==
   0x001Cu);
  complete_multiplayer_word(&parent, &child, 0, 0);
  complete_multiplayer_word(&parent, &child, 0xB9A0u, 0xB9A0u);
  complete_multiplayer_word(&parent, &child, 0x8FFFu, 0xB9A0u);
  CHECK(parent_registers[GBA_SIO_REG_MULTI0 >> 1] == 0x8FFFu);
  CHECK(parent_registers[GBA_SIO_REG_MULTI1 >> 1] == 0xB9A0u);
  CHECK(child_registers[GBA_SIO_REG_MULTI0 >> 1] == 0x8FFFu);
  CHECK(child_registers[GBA_SIO_REG_MULTI1 >> 1] == 0xB9A0u);
}

/* Losing the link mid-transfer ends it with the error bit and an IRQ. */
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

/* An aborted normal transfer reads back all ones, like an idle line. */
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

/* A multiplayer transfer that completes with an error sets SIOCNT bit 6;
   switching to normal mode clears the error and busy bits. */
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

/* A bogus player count is clamped to 4: four players at 9600 bps. */
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

/* A second START write while busy is ignored, and gba_sio_reset cancels
   the pending transfer. */
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

/* The link commits with absolute cycle stamps; the IRQ lands exactly on
   the finish cycle. */
static void test_absolute_cycle_deadline(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;
  uint32_t start = 0x10203040u;

  reset_sio();
  CHECK(gba_sio_set_cycle(&sio, start) == 1);
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083u);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  memset(&result, 0, sizeof(result));
  result.data[0] = 0x5Au;
  result.player_count = 2;
  CHECK(gba_sio_commit_at(&sio, &result, start, start + 64u) == 1);
  CHECK(sio.current_cycle == start);
  CHECK(sio.transfer_start_cycle == start);
  CHECK(sio.transfer_finish_cycle == start + 64u);
  CHECK(gba_sio_advance(&sio, 63u) == GBA_SIO_EVENT_NONE);
  CHECK(sio.current_cycle == start + 63u);
  CHECK(gba_sio_advance(&sio, 1u) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(sio.current_cycle == start + 64u);
}

/* A plain commit whose start already lies in the past is refused. */
static void test_absolute_deadline_rejects_late_release(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;

  reset_sio();
  CHECK(gba_sio_set_cycle(&sio, 1000u) == 1);
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083u);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  CHECK(gba_sio_set_cycle(&sio, 1001u) == 1);
  memset(&result, 0, sizeof(result));
  result.player_count = 2;
  CHECK(gba_sio_commit_at(&sio, &result, 1000u, 1064u) == 0);
  CHECK(sio.waiting != 0);
  CHECK(sio.active == 0);
  CHECK(gba_sio_next_event(&sio) == GBA_SIO_NO_EVENT);
}

/* commit_at_horizon (used by lockstep mode) accepts a start in the past:
   the transfer still ends on its original deadline, or at once if that
   deadline has already been reached. */
static void test_localized_horizon_commit(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;
  GbaSioEvent event;

  reset_sio();
  CHECK(gba_sio_set_cycle(&sio, 1000u) == 1);
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083u);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  CHECK(gba_sio_set_cycle(&sio, 1024u) == 1);
  memset(&result, 0, sizeof(result));
  result.data[0] = 0x5Au;
  result.player_count = 2;
  CHECK(gba_sio_commit_at_horizon(&sio, &result, 1000u, 1064u,
   &event) == 1);
  CHECK(event == GBA_SIO_EVENT_NONE);
  CHECK(gba_sio_next_event(&sio) == 40u);
  CHECK(gba_sio_advance(&sio, 40u) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(reg16(GBA_SIO_REG_SEND) == 0x5Au);

  reset_sio();
  CHECK(gba_sio_set_cycle(&sio, 2000u) == 1);
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083u);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  CHECK(gba_sio_set_cycle(&sio, 2064u) == 1);
  result.data[0] = 0xA5u;
  CHECK(gba_sio_commit_at_horizon(&sio, &result, 2000u, 2064u,
   &event) == 1);
  CHECK(event == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(gba_sio_next_event(&sio) == GBA_SIO_NO_EVENT);
  CHECK(reg16(GBA_SIO_REG_SEND) == 0xA5u);
}

/* A finish cycle that wraps past 2^32 is still hit exactly. */
static void test_absolute_deadline_wrap(void)
{
  GbaSioTransfer transfer;
  GbaSioResult result;
  uint32_t start = UINT32_MAX - 31u;
  uint32_t finish = start + 64u;

  reset_sio();
  CHECK(gba_sio_set_cycle(&sio, start) == 1);
  select_mode(GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x4083u);
  CHECK(gba_sio_take_start(&sio, &transfer) == 1);
  memset(&result, 0, sizeof(result));
  result.player_count = 2;
  CHECK(gba_sio_commit_at(&sio, &result, start, finish) == 1);
  CHECK(gba_sio_advance(&sio, 64u) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(sio.current_cycle == finish);
}

/* A save state taken mid-transfer finishes on the same cycle with the same
   data after loading; invalid snapshots are refused and leave no busy bit. */
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
  CHECK(gba_sio_lock_decision(&sio) == 1);
  CHECK(gba_sio_commit(&sio, &result) == 1);
  CHECK(gba_sio_advance(&sio, 100) == GBA_SIO_EVENT_NONE);
  gba_sio_save_snapshot(&sio, &snapshot);

  gba_sio_init(&restored, restored_registers);
  memcpy(restored_registers, registers, sizeof(registers));
  CHECK(gba_sio_load_snapshot(&restored, &snapshot) == 1);
  CHECK(restored.decision_locked != 0);
  CHECK(gba_sio_next_event(&restored) == 1948);
  CHECK(gba_sio_advance(&restored, 1948) == GBA_SIO_EVENT_NONE);
  CHECK(restored.decision_locked == 0);
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

/* Every SIOCNT write with the IRQ bit clear is counted: link_cable uses
   the count to notice a game closing or reopening its link layer. */
static void test_interrupt_disable_counter(void)
{
  reset_sio();
  select_mode(GBA_SIO_MODE_MULTIPLAYER);
  CHECK(sio.interrupt_disables == 1);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x6003);
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x6083);
  CHECK(sio.interrupt_disables == 1);
  /* A Pokemon game closing its link writes SIOCNT without the IRQ bit. */
  gba_sio_write16(&sio, GBA_SIO_REG_CONTROL, 0x2000);
  CHECK(sio.interrupt_disables == 2);
}

int main(void)
{
  test_interrupt_disable_counter();
  test_prepare_cold_preserves_other_registers();
  test_prepare_is_noop_for_loaded_game();
  test_prepare_rejects_invalid_storage();
  test_reset_and_modes();
  test_control_masks();
  test_rcnt_masks();
  test_multiplayer_connection_state();
  test_normal_timing();
  test_multiplayer_timing();
  test_reference_timing_and_register_traces();
  test_reference_irq_disabled_trace();
  test_reference_irq_toggle_and_mode_change_traces();
  test_normal8_transfer();
  test_normal32_transfer();
  test_external_clock_transfer();
  test_multiplayer_transfer();
  test_multiplayer_remote_start();
  test_firered_master_driven_handshake();
  test_abort_and_disconnect();
  test_normal_abort_values();
  test_error_completion_and_mode_change();
  test_result_player_clamp();
  test_repeated_start_and_reset();
  test_absolute_cycle_deadline();
  test_absolute_deadline_rejects_late_release();
  test_localized_horizon_commit();
  test_absolute_deadline_wrap();
  test_snapshot();

  if(failures)
  {
    fprintf(stderr, "%d GBA SIO test(s) failed\n", failures);
    return 1;
  }

  puts("GBA SIO tests passed");
  return 0;
}
