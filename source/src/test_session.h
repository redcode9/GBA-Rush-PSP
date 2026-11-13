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

/* The hardware test session of the diagnostic profile (make
   PROFILE=diagnostic, never released): it runs the measurements of a
   console session by itself. Release builds get the macros below as
   constants, so their code is the same as without this header. */

#ifndef TEST_SESSION_H
#define TEST_SESSION_H

#include "build_profile.h"

#ifdef ENABLE_DIAGNOSTIC_TEST_SESSION

/* At start-up: offers the session and loads its first game; nonzero if it
   did, and the caller runs it without the ROM browser. */
int test_session_start(void);
/* At each V-Blank of the game: nonzero if it replaced the machine, as the
   menu does. */
int test_session_frame(void);
/* GBA keys (KEYINPUT bits, pressed = 1) the current capture holds. */
u32 test_session_gba_keys(void);
/* Whether the current capture holds fast-forward. */
int test_session_fast_forward(void);
/* Whether Exact draws on the main processor although the Media Engine is
   up. */
int test_session_exact_cpu(void);
/* While the session runs, a Media Engine that falls behind keeps drawing,
   so a capture measures how far behind it is instead of the main processor
   taking over. */
int test_session_keeps_slow_me(void);
/* Ends a running session and puts the player's settings back: the menu
   and quit() call it before they show or save any setting. */
void test_session_stop(void);
/* Whether a session ran since the EBOOT started: the game then holds the
   session's state, so neither its save nor its AUTO savestate is written
   until the emulator restarts. */
int test_session_ran(void);

#define TEST_SESSION_START() test_session_start()
#define TEST_SESSION_FRAME() test_session_frame()
#define TEST_SESSION_GBA_KEYS() test_session_gba_keys()
#define TEST_SESSION_FAST_FORWARD() test_session_fast_forward()
#define TEST_SESSION_EXACT_CPU() test_session_exact_cpu()
#define TEST_SESSION_KEEPS_SLOW_ME() test_session_keeps_slow_me()
#define TEST_SESSION_STOP() test_session_stop()
#define TEST_SESSION_RAN() test_session_ran()

#else

#define TEST_SESSION_START() 0
#define TEST_SESSION_FRAME() 0
#define TEST_SESSION_GBA_KEYS() 0
#define TEST_SESSION_FAST_FORWARD() 0
#define TEST_SESSION_EXACT_CPU() 0
#define TEST_SESSION_KEEPS_SLOW_ME() 0
#define TEST_SESSION_STOP() ((void)0)
#define TEST_SESSION_RAN() 0

#endif

#endif
