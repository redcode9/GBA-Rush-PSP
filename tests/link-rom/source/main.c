/*
 * GBA Rush PSP link cable diagnostics
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * Bare-metal GBA ROM for checking a link cable, real or emulated. The
 * Makefile builds it once per mode (normal 8-bit, normal 32-bit,
 * multiplayer); it prints SIOCNT, the words sent and received and the IRQ
 * count on a bitmap screen. Interrupts stay masked: IF is polled per frame.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define LINK_TEST_NORMAL_8    0
#define LINK_TEST_NORMAL_32   1
#define LINK_TEST_MULTIPLAYER 2

#ifndef LINK_DIAGNOSTIC_MODE
#define LINK_DIAGNOSTIC_MODE LINK_TEST_MULTIPLAYER
#endif

#define REG_DISPCNT    (*(volatile u16 *)0x04000000)
#define REG_VCOUNT     (*(volatile u16 *)0x04000006)
#define REG_SIODATA32  (*(volatile u32 *)0x04000120)
#define REG_SIOMULTI0  (*(volatile u16 *)0x04000120)
#define REG_SIOMULTI1  (*(volatile u16 *)0x04000122)
#define REG_SIOMULTI2  (*(volatile u16 *)0x04000124)
#define REG_SIOMULTI3  (*(volatile u16 *)0x04000126)
#define REG_SIOCNT     (*(volatile u16 *)0x04000128)
#define REG_SIODATA8   (*(volatile u16 *)0x0400012A)
#define REG_SIOMLT_SEND (*(volatile u16 *)0x0400012A)
#define REG_KEYINPUT   (*(volatile u16 *)0x04000130)
#define REG_RCNT       (*(volatile u16 *)0x04000134)
#define REG_IE         (*(volatile u16 *)0x04000200)
#define REG_IF         (*(volatile u16 *)0x04000202)
#define REG_IME        (*(volatile u16 *)0x04000208)
#define FRAMEBUFFER    ((volatile u16 *)0x06000000)

#define KEY_A     0x0001u
#define KEY_UP    0x0040u
#define KEY_DOWN  0x0080u
#define KEY_R     0x0100u
#define KEY_L     0x0200u

#define IRQ_SERIAL 0x0080u

#define SIOCNT_CLOCK_INTERNAL 0x0001u
#define SIOCNT_FAST           0x0002u
#define SIOCNT_MULTI_CHILD    0x0004u
#define SIOCNT_MULTI_READY    0x0008u
#define SIOCNT_MULTI_ERROR    0x0040u
#define SIOCNT_START          0x0080u
#define SIOCNT_NORMAL_32      0x1000u
#define SIOCNT_MULTIPLAYER    0x2000u
#define SIOCNT_IRQ            0x4000u

#define COLOR_BACKGROUND 0x1800u
#define COLOR_TEXT       0x7FFFu
#define COLOR_ACCENT     0x03FFu
#define SCREEN_WIDTH     240u
#define SCREEN_HEIGHT    160u

typedef enum
{
  ROLE_UNSET,
  ROLE_INTERNAL,
  ROLE_EXTERNAL
} NormalRole;

/* 5x7 glyphs, one byte per row; bit 4 is the leftmost pixel. */
static const u8 digit_font[10][7] =
{
  { 14, 17, 19, 21, 25, 17, 14 },
  { 4, 12, 4, 4, 4, 4, 14 },
  { 14, 17, 1, 2, 4, 8, 31 },
  { 30, 1, 1, 14, 1, 1, 30 },
  { 2, 6, 10, 18, 31, 2, 2 },
  { 31, 16, 16, 30, 1, 1, 30 },
  { 14, 16, 16, 30, 17, 17, 14 },
  { 31, 1, 2, 4, 8, 8, 8 },
  { 14, 17, 17, 14, 17, 17, 14 },
  { 14, 17, 17, 15, 1, 1, 14 }
};

static const u8 letter_font[26][7] =
{
  { 14, 17, 17, 31, 17, 17, 17 },
  { 30, 17, 17, 30, 17, 17, 30 },
  { 14, 17, 16, 16, 16, 17, 14 },
  { 30, 17, 17, 17, 17, 17, 30 },
  { 31, 16, 16, 30, 16, 16, 31 },
  { 31, 16, 16, 30, 16, 16, 16 },
  { 14, 17, 16, 23, 17, 17, 15 },
  { 17, 17, 17, 31, 17, 17, 17 },
  { 14, 4, 4, 4, 4, 4, 14 },
  { 7, 2, 2, 2, 18, 18, 12 },
  { 17, 18, 20, 24, 20, 18, 17 },
  { 16, 16, 16, 16, 16, 16, 31 },
  { 17, 27, 21, 21, 17, 17, 17 },
  { 17, 25, 21, 19, 17, 17, 17 },
  { 14, 17, 17, 17, 17, 17, 14 },
  { 30, 17, 17, 30, 16, 16, 16 },
  { 14, 17, 17, 17, 21, 18, 13 },
  { 30, 17, 17, 30, 20, 18, 17 },
  { 15, 16, 16, 14, 1, 1, 30 },
  { 31, 4, 4, 4, 4, 4, 4 },
  { 17, 17, 17, 17, 17, 17, 14 },
  { 17, 17, 17, 17, 17, 10, 4 },
  { 17, 17, 17, 21, 21, 21, 10 },
  { 17, 17, 10, 4, 10, 17, 17 },
  { 17, 17, 10, 4, 4, 4, 4 },
  { 31, 1, 2, 4, 8, 16, 31 }
};

static u32 frame_count;
static u32 irq_count;
static u32 last_irq_frame;
static u16 last_control;
static u32 next_data;
static u32 last_sent;
static u32 transfer_count;
static u32 handled_irq_count;
static u8 speed_setting;
#if LINK_DIAGNOSTIC_MODE == LINK_TEST_MULTIPLAYER
static u16 last_multi[4];
#else
static u32 last_received;
static NormalRole normal_role;
#endif

/* One 5-pixel row of a glyph. Only digits, capitals and - : / . have
   glyphs; any other character draws blank. */
static u8 glyph_row(char character, u8 row)
{
  if(character >= '0' && character <= '9')
    return digit_font[(u8)(character - '0')][row];
  if(character >= 'A' && character <= 'Z')
    return letter_font[(u8)(character - 'A')][row];

  switch(character)
  {
    case '-':
      return row == 3 ? 31 : 0;

    case ':':
      return row == 2 || row == 5 ? 4 : 0;

    case '/':
      return row < 5 ? (u8)(1u << row) : 0;

    case '.':
      return row == 6 ? 4 : 0;

    default:
      return 0;
  }
}

static void clear_screen(void)
{
  u32 pixel;

  for(pixel = 0; pixel < SCREEN_WIDTH * SCREEN_HEIGHT; pixel++)
    FRAMEBUFFER[pixel] = COLOR_BACKGROUND;
}

/* Text uses a 40x20 grid of 6x8-pixel cells. Only the glyph's set pixels
   are written, so the screen is cleared before each redraw. */
static void draw_character(u8 column, u8 line, char character, u16 color)
{
  u16 x = (u16)(column * 6u);
  u16 y = (u16)(line * 8u);
  u8 row;

  for(row = 0; row < 7; row++)
  {
    u8 bits = glyph_row(character, row);
    u8 bit;

    for(bit = 0; bit < 5; bit++)
    {
      if((bits & (1u << (4u - bit))) != 0)
        FRAMEBUFFER[(u32)(y + row) * SCREEN_WIDTH + x + bit] = color;
    }
  }
}

static void draw_text(u8 column, u8 line, const char *text, u16 color)
{
  while(*text != 0 && column < 40)
  {
    draw_character(column, line, *text, color);
    column++;
    text++;
  }
}

static void draw_hex(u8 column, u8 line, u32 value, u8 digits, u16 color)
{
  static const char hexadecimal[] = "0123456789ABCDEF";
  u8 index;

  for(index = 0; index < digits; index++)
  {
    u8 shift = (u8)((digits - index - 1u) * 4u);
    draw_character((u8)(column + index), line,
     hexadecimal[(value >> shift) & 0x0Fu], color);
  }
}

static void draw_decimal(u8 column, u8 line, u32 value, u16 color)
{
  char digits[10];
  u8 count = 0;

  do
  {
    digits[count++] = (char)('0' + value % 10u);
    value /= 10u;
  } while(value != 0 && count < sizeof(digits));

  while(count != 0)
    draw_character(column++, line, digits[--count], color);
}

/* Returns at the start of the next VBlank (VCOUNT 160), first waiting out
   one already in progress, so the main loop runs once per frame. */
static void wait_for_vblank(void)
{
  while(REG_VCOUNT >= 160)
  {
  }
  while(REG_VCOUNT < 160)
  {
  }
}

/* With IME off the serial IRQ is never taken; its IF bit still latches and
   is acknowledged by writing it back. */
static void poll_serial_interrupt(void)
{
  if((REG_IF & IRQ_SERIAL) == 0)
    return;

  REG_IF = IRQ_SERIAL;
  last_control = REG_SIOCNT;
#if LINK_DIAGNOSTIC_MODE == LINK_TEST_NORMAL_8
  last_received = REG_SIODATA8 & 0x00FFu;
#elif LINK_DIAGNOSTIC_MODE == LINK_TEST_NORMAL_32
  last_received = REG_SIODATA32;
#else
  last_multi[0] = REG_SIOMULTI0;
  last_multi[1] = REG_SIOMULTI1;
  last_multi[2] = REG_SIOMULTI2;
  last_multi[3] = REG_SIOMULTI3;
#endif
  irq_count++;
  last_irq_frame = frame_count;
}

#if LINK_DIAGNOSTIC_MODE != LINK_TEST_MULTIPLAYER
static const char *normal_mode_name(void)
{
#if LINK_DIAGNOSTIC_MODE == LINK_TEST_NORMAL_8
  return "NORMAL 8-BIT";
#else
  return "NORMAL 32-BIT";
#endif
}

static const char *normal_role_name(void)
{
  if(normal_role == ROLE_INTERNAL)
    return "INTERNAL CLOCK";
  if(normal_role == ROLE_EXTERNAL)
    return "EXTERNAL CLOCK";
  return "NOT SELECTED";
}

/* Easy-to-read patterns that differ by side and count up per transfer:
   8-bit 0x40-0x7F (the external-clock side starts 0x20 ahead), 32-bit an
   0xA1 or 0xB2 tag in the top byte above the transfer count. */
static void prepare_next_data(void)
{
#if LINK_DIAGNOSTIC_MODE == LINK_TEST_NORMAL_8
  next_data = 0x40u + ((transfer_count +
   (normal_role == ROLE_EXTERNAL ? 0x20u : 0u)) & 0x3Fu);
#else
  next_data = (normal_role == ROLE_EXTERNAL ? 0xB2000000u : 0xA1000000u) |
   (transfer_count & 0x00FFFFFFu);
#endif
}

static void write_normal_data(void)
{
#if LINK_DIAGNOSTIC_MODE == LINK_TEST_NORMAL_8
  REG_SIODATA8 = (u16)next_data;
#else
  REG_SIODATA32 = next_data;
#endif
}

static void configure_normal(void)
{
  u16 control = SIOCNT_IRQ;

#if LINK_DIAGNOSTIC_MODE == LINK_TEST_NORMAL_32
  control |= SIOCNT_NORMAL_32;
#endif
  if(speed_setting != 0)
    control |= SIOCNT_FAST;
  if(normal_role == ROLE_INTERNAL)
    control |= SIOCNT_CLOCK_INTERNAL;

  REG_RCNT = 0;
  REG_SIOCNT = control;
  write_normal_data();
}

static void start_normal_transfer(void)
{
  if(normal_role == ROLE_UNSET || (REG_SIOCNT & SIOCNT_START) != 0)
    return;

  last_sent = next_data;
  REG_SIOCNT |= SIOCNT_START;
}

/* keys holds the buttons newly pressed this frame. Role and clock speed
   change only while idle; each serial IRQ loads the next pattern. */
static void update_normal(u16 keys)
{
  if((REG_SIOCNT & SIOCNT_START) == 0)
  {
    if((keys & KEY_UP) != 0)
      speed_setting = 1;
    if((keys & KEY_DOWN) != 0)
      speed_setting = 0;
    if((keys & KEY_L) != 0)
      normal_role = ROLE_INTERNAL;
    if((keys & KEY_R) != 0)
      normal_role = ROLE_EXTERNAL;
    configure_normal();
  }

  if((keys & KEY_A) != 0)
    start_normal_transfer();

  if(handled_irq_count != irq_count)
  {
    handled_irq_count = irq_count;
    transfer_count = irq_count;
    prepare_next_data();
    configure_normal();
  }
}

static void draw_normal(void)
{
  const char *state = (REG_SIOCNT & SIOCNT_START) != 0 ?
   (normal_role == ROLE_EXTERNAL ? "ARMED" : "BUSY") : "IDLE";

  clear_screen();
  draw_text(0, 0, "GBA RUSH PSP LINK TEST", COLOR_ACCENT);
  draw_text(0, 2, "MODE:", COLOR_TEXT);
  draw_text(7, 2, normal_mode_name(), COLOR_TEXT);
  draw_text(0, 3, "ROLE:", COLOR_TEXT);
  draw_text(7, 3, normal_role_name(), COLOR_TEXT);
  draw_text(0, 4, "CLOCK:", COLOR_TEXT);
  draw_text(8, 4, speed_setting ? "2 MHZ" : "256 KHZ", COLOR_TEXT);
  draw_text(0, 5, "STATE:", COLOR_TEXT);
  draw_text(8, 5, state, COLOR_TEXT);
  draw_text(0, 6, "SIOCNT:", COLOR_TEXT);
  draw_hex(9, 6, REG_SIOCNT, 4, COLOR_TEXT);
  draw_text(16, 6, "SI:", COLOR_TEXT);
  draw_decimal(20, 6, (REG_SIOCNT >> 2) & 1u, COLOR_TEXT);
  draw_text(0, 8, "SENT:", COLOR_TEXT);
#if LINK_DIAGNOSTIC_MODE == LINK_TEST_NORMAL_8
  draw_hex(11, 8, last_sent, 2, COLOR_TEXT);
  draw_text(0, 9, "RECEIVED:", COLOR_TEXT);
  draw_hex(11, 9, last_received, 2, COLOR_TEXT);
  draw_text(0, 10, "NEXT:", COLOR_TEXT);
  draw_hex(11, 10, next_data, 2, COLOR_TEXT);
#else
  draw_hex(11, 8, last_sent, 8, COLOR_TEXT);
  draw_text(0, 9, "RECEIVED:", COLOR_TEXT);
  draw_hex(11, 9, last_received, 8, COLOR_TEXT);
  draw_text(0, 10, "NEXT:", COLOR_TEXT);
  draw_hex(11, 10, next_data, 8, COLOR_TEXT);
#endif
  draw_text(0, 12, "IRQ COUNT:", COLOR_TEXT);
  draw_decimal(16, 12, irq_count, COLOR_TEXT);
  draw_text(0, 13, "COMPLETE ORDER:", COLOR_TEXT);
  draw_decimal(18, 13, irq_count, COLOR_TEXT);
  draw_text(0, 14, "LAST IRQ FRAME:", COLOR_TEXT);
  draw_decimal(18, 14, last_irq_frame, COLOR_TEXT);
  draw_text(0, 15, "LAST STATUS:", COLOR_TEXT);
  draw_hex(18, 15, last_control, 4, COLOR_TEXT);
  draw_text(0, 17, "UP/DOWN: CLOCK SPEED", COLOR_ACCENT);
  draw_text(0, 18, "CLOCK OWNER: L  PEER: R", COLOR_ACCENT);
  draw_text(0, 19, "PEER A, THEN CLOCK OWNER A", COLOR_ACCENT);
}
#endif

#if LINK_DIAGNOSTIC_MODE == LINK_TEST_MULTIPLAYER
/* SIOCNT bits 4-5 hold this console's player number: player 1 sends
   0x1000 + count, the others 0x2000 + count. */
static void prepare_next_data(void)
{
  next_data = ((REG_SIOCNT >> 4) & 3u) == 0 ?
   0x1000u + transfer_count : 0x2000u + transfer_count;
  REG_SIOMLT_SEND = (u16)next_data;
}

static void configure_multiplayer(void)
{
  REG_RCNT = 0;
  /* speed_setting is the baud rate field, SIOCNT bits 0-1. */
  REG_SIOCNT = (u16)(SIOCNT_MULTIPLAYER | SIOCNT_IRQ | speed_setting);
  prepare_next_data();
}

static void update_multiplayer(u16 keys)
{
  if((REG_SIOCNT & SIOCNT_START) == 0)
  {
    if((keys & KEY_UP) != 0)
      speed_setting = (u8)((speed_setting + 1u) & 3u);
    if((keys & KEY_DOWN) != 0)
      speed_setting = (u8)((speed_setting - 1u) & 3u);
    configure_multiplayer();
  }

  /* Only the parent (SI low) can start a multiplayer transfer. */
  if((keys & KEY_A) != 0 &&
     (REG_SIOCNT & (SIOCNT_MULTI_CHILD | SIOCNT_START)) == 0)
  {
    last_sent = next_data;
    REG_SIOCNT |= SIOCNT_START;
  }

  if(handled_irq_count != irq_count)
  {
    handled_irq_count = irq_count;
    last_sent = next_data;
    transfer_count = irq_count;
    prepare_next_data();
  }
}

static void draw_multiplayer(void)
{
  static const u32 rates[4] = { 9600, 38400, 57600, 115200 };
  u16 control = REG_SIOCNT;

  clear_screen();
  draw_text(0, 0, "GBA RUSH PSP LINK TEST", COLOR_ACCENT);
  draw_text(0, 2, "MODE: MULTIPLAYER 16-BIT", COLOR_TEXT);
  draw_text(0, 3, "PLAYER:", COLOR_TEXT);
  draw_decimal(9, 3, ((control >> 4) & 3u) + 1u, COLOR_TEXT);
  draw_text(0, 4, "BAUD:", COLOR_TEXT);
  draw_decimal(7, 4, rates[speed_setting], COLOR_TEXT);
  draw_text(0, 5, "SIOCNT:", COLOR_TEXT);
  draw_hex(9, 5, control, 4, COLOR_TEXT);
  draw_text(0, 6, "READY:", COLOR_TEXT);
  draw_decimal(8, 6, (control & SIOCNT_MULTI_READY) != 0, COLOR_TEXT);
  draw_text(12, 6, "BUSY:", COLOR_TEXT);
  draw_decimal(18, 6, (control & SIOCNT_START) != 0, COLOR_TEXT);
  draw_text(22, 6, "ERROR:", COLOR_TEXT);
  draw_decimal(29, 6, (control & SIOCNT_MULTI_ERROR) != 0, COLOR_TEXT);
  draw_text(0, 8, "SENT:", COLOR_TEXT);
  draw_hex(7, 8, last_sent, 4, COLOR_TEXT);
  draw_text(15, 8, "NEXT:", COLOR_TEXT);
  draw_hex(22, 8, next_data, 4, COLOR_TEXT);
  draw_text(0, 9, "P1:", COLOR_TEXT);
  draw_hex(5, 9, last_multi[0], 4, COLOR_TEXT);
  draw_text(12, 9, "P2:", COLOR_TEXT);
  draw_hex(17, 9, last_multi[1], 4, COLOR_TEXT);
  draw_text(0, 10, "P3:", COLOR_TEXT);
  draw_hex(5, 10, last_multi[2], 4, COLOR_TEXT);
  draw_text(12, 10, "P4:", COLOR_TEXT);
  draw_hex(17, 10, last_multi[3], 4, COLOR_TEXT);
  draw_text(0, 12, "IRQ COUNT:", COLOR_TEXT);
  draw_decimal(16, 12, irq_count, COLOR_TEXT);
  draw_text(0, 13, "COMPLETE ORDER:", COLOR_TEXT);
  draw_decimal(18, 13, irq_count, COLOR_TEXT);
  draw_text(0, 14, "LAST IRQ FRAME:", COLOR_TEXT);
  draw_decimal(18, 14, last_irq_frame, COLOR_TEXT);
  draw_text(0, 15, "LAST STATUS:", COLOR_TEXT);
  draw_hex(18, 15, last_control, 4, COLOR_TEXT);
  draw_text(0, 17, "UP/DOWN: BAUD RATE", COLOR_ACCENT);
  draw_text(0, 18, "PLAYER 1: A TO TRANSFER", COLOR_ACCENT);
  draw_text(0, 19, "PLAYER 2 ANSWERS AUTOMATICALLY", COLOR_ACCENT);
}
#endif

int main(void)
{
  u16 previous_keys = 0;

  REG_IME = 0;
  REG_IE = IRQ_SERIAL;
  REG_IF = 0xFFFFu;
  REG_DISPCNT = 0x0403u;   /* mode 3 (240x160 bitmap), BG2 on */

#if LINK_DIAGNOSTIC_MODE == LINK_TEST_MULTIPLAYER
  speed_setting = 3;
  configure_multiplayer();
#else
  speed_setting = 0;
  prepare_next_data();
  configure_normal();
#endif

  for(;;)
  {
    u16 held_keys;
    u16 pressed_keys;

    wait_for_vblank();
    frame_count++;
    poll_serial_interrupt();
    /* KEYINPUT bits read 0 while a button is held. */
    held_keys = (u16)(~REG_KEYINPUT) & 0x03FFu;
    pressed_keys = (u16)(held_keys & ~previous_keys);
    previous_keys = held_keys;

#if LINK_DIAGNOSTIC_MODE == LINK_TEST_MULTIPLAYER
    update_multiplayer(pressed_keys);
    draw_multiplayer();
#else
    update_normal(pressed_keys);
    draw_normal();
#endif
  }
}
