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
 * The diagnostic autorun file. Only the diagnostic profile has it
 * (make PROFILE=diagnostic): at startup main.c reads diag-autorun.txt from
 * the EBOOT folder, and a file this module accepts starts its ROM without
 * the file browser, the menu or the auto-state prompt. main.c writes the
 * outcome to diag-autorun.result in the same folder and to the runtime log.
 * The module never allocates and never touches files.
 *
 * Text format, ASCII, at most AUTORUN_CONFIG_MAX_SIZE bytes. Lines end with
 * LF or CRLF and hold at most AUTORUN_CONFIG_LINE_MAX characters, all
 * printable (0x20-0x7E). Empty lines and lines that start with '#' are
 * ignored. The first other line is exactly version=1, the next one is
 * build=<id>, and every other line is key=value with no spaces around the
 * '='. Each key appears at most once:
 *
 *   version=1
 *   build=<id>       required: the GBA_RUSH_BUILD_ID of the EBOOT the file
 *                    is for, so a forgotten file never drives another build.
 *                    A build without an id ("unknown", a tarball build)
 *                    refuses every file.
 *   rom=<path>       the ROM to start; a path without ':' is relative to
 *                    the EBOOT folder. Not added to Recent ROMs.
 *   trace=<path>     an input trace (.gbit) replayed from the first frame;
 *                    a version 2 trace loads the .svs of the same name
 *                    first. Needs rom.
 *   frames=<n>       decimal, 1 to 2147483647: the run is done after n
 *                    frames. Needs rom. Without it the game runs on.
 *   exit=0|1         1 quits the EBOOT when the run is done, or when the ROM
 *                    or the trace fails to start. Needs frames.
 *   hash=<path>      where the replay's state hashes (.gbsh, state_hash.h)
 *                    are written when the run is done; a path without ':'
 *                    is relative to the EBOOT folder. Needs trace and
 *                    frames.
 *   perturb=<n>      decimal, below frames: flips one IWRAM bit at frame n
 *                    of the replay (0 is its first), so the hashes of this
 *                    run must differ from an unperturbed one's from there
 *                    on. Needs hash. The run fails at the start when the
 *                    trace ends before frame n.
 *   testpat=<mode>|all  no ROM: eight colour bars (main.c) go through the
 *                    real presenter of one screen scale and filter, or of
 *                    every one in turn, AUTORUN_TESTPAT_HOLD_FRAMES each,
 *                    then the EBOOT quits. <mode> is <scale>-<filter>: the
 *                    scale is none, x15-gu, x15-sw, user, 16x9-gu,
 *                    16x9-sharp or 16x9-auto (SCALED_NONE..SCALED_16X9_AUTO
 *                    in main.h; user is shown at 170%), the filter nearest
 *                    or linear. all runs the 14 modes in that order, each
 *                    scale with nearest first. Goes with no other key but
 *                    version and build.
 *   net_profile=<name>  a simulated network (net_fault.h) for the ad-hoc
 *                    link: lan, adhoc, noisy, outage, terrible or dropouts.
 *                    Every datagram the link sends or receives goes through
 *                    it, and the runtime log's network ring records the
 *                    profile, its seed and what it lost, delayed and
 *                    duplicated. Needs no other key.
 *
 * Not keys yet: the power events come with the code that uses them (the
 * power event injector). Until then such a key is unknown and refuses the
 * file.
 *
 * Example, to replay a trace for one minute and quit:
 *
 *   version=1
 *   build=0123456789ab
 *   rom=roms/game.gba
 *   trace=traces/input-AXVE-r1.gbit
 *   frames=3600
 *   exit=1
 *
 * A refused file changes nothing: the EBOOT starts as usual. The result
 * file is six lines, each ending with LF (see autorun_config_result()):
 *
 *   version=1
 *   build=<the EBOOT's id>
 *   status=ignored|failed|started|done
 *   reason=<autorun_config_error_name()>
 *   line=<the line the reason refers to, 0 for none>
 *   frames=<frames run>
 *
 * A testpat= run rewrites it after each mode, adding one line per mode
 * shown so far (see autorun_config_testpat_line()):
 *
 *   testpat=<mode> used=gu|sw|sharp/nearest|linear|none
 *
 * naming the presenter and the filter psp_video.c really used: 16x9-auto
 * starts at sharp, sharp always samples the frame nearest, and sw scales
 * on the CPU with no GU filter.
 */

#ifndef AUTORUN_CONFIG_H
#define AUTORUN_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "net_fault.h"

#define AUTORUN_CONFIG_MAX_SIZE 4096U
#define AUTORUN_CONFIG_LINE_MAX 255U
#define AUTORUN_CONFIG_FRAMES_MAX 0x7FFFFFFFU
/* Room for the result text of a build id of up to 255 characters. */
#define AUTORUN_CONFIG_RESULT_SIZE 384U

/* testpat= mode m is scale m / 2 (a SCALED_* value) with filter m % 2
   (FILTER_NEAREST, FILTER_BILINEAR). */
#define AUTORUN_TESTPAT_SCALES 7U
#define AUTORUN_TESTPAT_MODES (AUTORUN_TESTPAT_SCALES * 2U)
#define AUTORUN_TESTPAT_ALL AUTORUN_TESTPAT_MODES
#define AUTORUN_TESTPAT_SCALE(mode) ((mode) / 2U)
#define AUTORUN_TESTPAT_FILTER(mode) ((mode) % 2U)
/* VBlanks each mode stays on screen: about 10 seconds, time for a photo. */
#define AUTORUN_TESTPAT_HOLD_FRAMES 600U
/* Room for one testpat= result line. */
#define AUTORUN_CONFIG_TESTPAT_LINE_SIZE 64U

/* Logged as values: append new ones at the end, never reorder. */
typedef enum
{
  AUTORUN_OK,
  AUTORUN_ERROR_TOO_LARGE,
  AUTORUN_ERROR_NOT_TEXT,
  AUTORUN_ERROR_LINE_TOO_LONG,
  AUTORUN_ERROR_VERSION,
  AUTORUN_ERROR_SYNTAX,
  AUTORUN_ERROR_UNKNOWN_KEY,
  AUTORUN_ERROR_DUPLICATE_KEY,
  AUTORUN_ERROR_BAD_VALUE,
  AUTORUN_ERROR_BUILD_MISSING,
  AUTORUN_ERROR_BUILD_MISMATCH,
  AUTORUN_ERROR_BUILD_UNKNOWN,
  AUTORUN_ERROR_NEEDS_ROM,
  AUTORUN_ERROR_NEEDS_FRAMES,
  /* Set by main.c, not by the parser. */
  AUTORUN_ERROR_READ,
  AUTORUN_ERROR_ROM,
  AUTORUN_ERROR_TRACE,
  /* Set by the parser. */
  AUTORUN_ERROR_NEEDS_TRACE,
  AUTORUN_ERROR_NEEDS_HASH,
  /* Set by main.c: the hashes did not fit in memory at the start, or
     were not written at the end. */
  AUTORUN_ERROR_NO_HASH_MEMORY,
  AUTORUN_ERROR_HASH,
  /* Set by main.c: perturb= is past the trace's last frame. */
  AUTORUN_ERROR_PERTURB_PAST_TRACE,
  /* Set by the parser: testpat= with a key it cannot go with. */
  AUTORUN_ERROR_TESTPAT_ALONE
} AutorunError;

/* Logged as values, like AutorunError. */
typedef enum
{
  AUTORUN_STATUS_IGNORED,
  AUTORUN_STATUS_FAILED,
  AUTORUN_STATUS_STARTED,
  AUTORUN_STATUS_DONE
} AutorunStatus;

typedef struct
{
  /* Empty when the file has no such key. */
  char rom[AUTORUN_CONFIG_LINE_MAX + 1];
  char trace[AUTORUN_CONFIG_LINE_MAX + 1];
  char hash[AUTORUN_CONFIG_LINE_MAX + 1];
  /* 1 when perturb_frame is set; the frame is relative to the replay's
     first. */
  uint32_t perturb;
  uint32_t perturb_frame;
  /* 1 when testpat_mode is set: a mode below AUTORUN_TESTPAT_MODES, or
     AUTORUN_TESTPAT_ALL. */
  uint32_t testpat;
  uint32_t testpat_mode;
  /* One of net_fault_profiles, or NULL for none. */
  const NetFaultProfile *net_profile;
  /* 0: no frame limit. */
  uint32_t frames;
  uint32_t exit_when_done;
  AutorunError error;
  /* The line error refers to, from 1; 0 for the whole file. */
  uint32_t error_line;
} AutorunConfig;

/* 1 when text is a file for build_id; otherwise 0 with error and
   error_line set and every other field cleared. */
int autorun_config_parse(const char *text, size_t size, const char *build_id,
 AutorunConfig *config);
/* A stable lowercase token, such as "build-mismatch"; "none" for
   AUTORUN_OK. */
const char *autorun_config_error_name(AutorunError error);
/* Writes the result text and a terminating NUL; returns its length without
   the NUL, or 0 when out_size is too small. */
size_t autorun_config_result(const char *build_id, AutorunStatus status,
 AutorunError error, uint32_t line, uint32_t frames, char *out,
 size_t out_size);
/* Writes "testpat=<mode> used=<presenter>/<filter>" with an LF and a
   terminating NUL; returns its length without the NUL, or 0 when mode is
   not below AUTORUN_TESTPAT_MODES or out_size is too small. */
size_t autorun_config_testpat_line(uint32_t mode, const char *presenter,
 const char *filter, char *out, size_t out_size);

#endif /* AUTORUN_CONFIG_H */
