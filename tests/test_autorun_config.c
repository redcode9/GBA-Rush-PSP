/*
 * Host tests for autorun_config.c, the diagnostic autorun file: the
 * version line, the build id gate, the keys and their limits, the replay
 * hash keys, the network profile, hostile input, and the exact result text.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "autorun_config.h"

#define ID "0123456789ab-dirty"
#define HEAD "version=1\nbuild=" ID "\n"

static AutorunConfig config;
static char big[AUTORUN_CONFIG_MAX_SIZE + 2];

static int parse_size(const char *text, size_t size)
{
  return autorun_config_parse(text, size, ID, &config);
}

static int parse(const char *text)
{
  return parse_size(text, strlen(text));
}

/* A refused file reports error at line and leaves nothing to apply. */
static void assert_refused(AutorunError error, uint32_t line)
{
  assert(config.error == error);
  assert(config.error_line == line);
  assert(config.rom[0] == '\0' && config.trace[0] == '\0');
  assert(config.hash[0] == '\0');
  assert(config.frames == 0 && config.exit_when_done == 0);
  assert(config.perturb == 0 && config.perturb_frame == 0);
  assert(config.testpat == 0 && config.testpat_mode == 0);
  assert(config.net_profile == NULL);
}

static void refused(const char *text, AutorunError error, uint32_t line)
{
  assert(parse(text) == 0);
  assert_refused(error, line);
}

static void test_valid_files(void)
{
  assert(parse(HEAD "rom=roms/game.gba\n"));
  assert(config.error == AUTORUN_OK && config.error_line == 0);
  assert(strcmp(config.rom, "roms/game.gba") == 0);
  assert(config.trace[0] == '\0' && config.frames == 0);
  assert(config.exit_when_done == 0);

  /* No rom: nothing to start, still a file for this build. */
  assert(parse(HEAD));
  assert(config.rom[0] == '\0');

  /* CRLF, comments, blank lines, no final newline. */
  assert(parse("# autorun for the smoke test\r\n\r\nversion=1\r\n"
               "# the build it is for\r\nbuild=" ID "\r\n\r\n"
               "rom=ms0:/ROMS/A Game.gba\r\n"
               "trace=traces/input-AXVE-r1.gbit\r\nframes=3600\r\nexit=1"));
  assert(strcmp(config.rom, "ms0:/ROMS/A Game.gba") == 0);
  assert(strcmp(config.trace, "traces/input-AXVE-r1.gbit") == 0);
  assert(config.frames == 3600 && config.exit_when_done == 1);

  assert(parse(HEAD "rom=a.gba\nframes=007\nexit=0\n"));
  assert(config.frames == 7 && config.exit_when_done == 0);
  assert(parse(HEAD "rom=a.gba\nframes=2147483647\n"));
  assert(config.frames == 0x7FFFFFFFU);
}

static void test_version(void)
{
  refused("", AUTORUN_ERROR_VERSION, 0);
  refused("# only a comment\n\n", AUTORUN_ERROR_VERSION, 0);
  refused("build=" ID "\nversion=1\n", AUTORUN_ERROR_VERSION, 1);
  refused("\n# first\nversion=2\nbuild=" ID "\n", AUTORUN_ERROR_VERSION, 3);
  refused("version=1 \nbuild=" ID "\n", AUTORUN_ERROR_VERSION, 1);
  refused("VERSION=1\nbuild=" ID "\n", AUTORUN_ERROR_VERSION, 1);
  refused(HEAD "version=1\n", AUTORUN_ERROR_DUPLICATE_KEY, 3);
}

static void test_build_gate(void)
{
  refused("version=1\n", AUTORUN_ERROR_BUILD_MISSING, 0);
  refused("version=1\nrom=a.gba\nbuild=" ID "\n",
   AUTORUN_ERROR_BUILD_MISSING, 2);
  refused("version=1\nbuild=0123456789ab\nrom=a.gba\n",
   AUTORUN_ERROR_BUILD_MISMATCH, 2);
  refused("version=1\nbuild=" ID "x\nrom=a.gba\n",
   AUTORUN_ERROR_BUILD_MISMATCH, 2);
  refused("version=1\nbuild=\n", AUTORUN_ERROR_BUILD_MISMATCH, 2);
  /* The gate is read before any later key, valid or not. */
  refused("version=1\nbuild=fedcba987654\nframes=0\npower=1\n",
   AUTORUN_ERROR_BUILD_MISMATCH, 2);

  /* A build without an id refuses every file, its own id included. */
  assert(autorun_config_parse("version=1\nbuild=unknown\n", 24, "unknown",
   &config) == 0);
  assert_refused(AUTORUN_ERROR_BUILD_UNKNOWN, 0);
  assert(autorun_config_parse("version=1\nbuild=\n", 17, "", &config) == 0);
  assert_refused(AUTORUN_ERROR_BUILD_UNKNOWN, 0);
  assert(autorun_config_parse(HEAD, strlen(HEAD), NULL, &config) == 0);
  assert_refused(AUTORUN_ERROR_BUILD_UNKNOWN, 0);
}

static void test_keys(void)
{
  refused(HEAD "power=suspend\n", AUTORUN_ERROR_UNKNOWN_KEY, 3);
  refused(HEAD "rom=a.gba\n\n# net next\nnet=psp-stalls\n",
   AUTORUN_ERROR_UNKNOWN_KEY, 6);
  refused(HEAD "x=\n", AUTORUN_ERROR_UNKNOWN_KEY, 3);
  refused(HEAD "=a.gba\n", AUTORUN_ERROR_UNKNOWN_KEY, 3);
  refused(HEAD "rom =a.gba\n", AUTORUN_ERROR_UNKNOWN_KEY, 3);
  refused(HEAD "rom\n", AUTORUN_ERROR_SYNTAX, 3);
  refused(HEAD "rom=a.gba\nrom=b.gba\n", AUTORUN_ERROR_DUPLICATE_KEY, 4);
  refused(HEAD "build=" ID "\n", AUTORUN_ERROR_DUPLICATE_KEY, 3);
  refused(HEAD "rom=\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "rom=a.gba\ntrace=t.svs\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\ntrace=.gbit\n", AUTORUN_ERROR_BAD_VALUE, 4);
}

static void test_values(void)
{
  refused(HEAD "rom=a.gba\nframes=0\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes=4294967296\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes=2147483648\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes=99999999999999999999\n",
   AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes=+5\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes=-5\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes=5x\n", AUTORUN_ERROR_BAD_VALUE, 4);
  /* The characters just past each end of '0'-'9'. */
  refused(HEAD "rom=a.gba\nframes=6:\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes=6/\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes= 5\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes=\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "rom=a.gba\nframes=9\nexit=2\n", AUTORUN_ERROR_BAD_VALUE, 5);
  refused(HEAD "rom=a.gba\nframes=9\nexit=\n", AUTORUN_ERROR_BAD_VALUE, 5);
  refused(HEAD "rom=a.gba\nframes=9\nexit=10\n", AUTORUN_ERROR_BAD_VALUE, 5);

  refused(HEAD "trace=t.gbit\n", AUTORUN_ERROR_NEEDS_ROM, 3);
  refused(HEAD "frames=60\n", AUTORUN_ERROR_NEEDS_ROM, 3);
  refused(HEAD "rom=a.gba\nexit=1\n", AUTORUN_ERROR_NEEDS_FRAMES, 4);
  refused(HEAD "rom=a.gba\nexit=0\n", AUTORUN_ERROR_NEEDS_FRAMES, 4);
}

#define RUN HEAD "rom=a.gba\ntrace=t.gbit\nframes=600\n"

static void test_hash_keys(void)
{
  assert(parse(RUN "hash=hashes/a.gbsh\n"));
  assert(strcmp(config.hash, "hashes/a.gbsh") == 0);
  assert(config.perturb == 0 && config.perturb_frame == 0);
  /* Any order; a flip from the first replayed frame to the last one
     before frames runs out. */
  assert(parse(HEAD "perturb=0\nhash=ms0:/h.gbsh\nrom=a.gba\n"
               "trace=t.gbit\nframes=600\nexit=1\n"));
  assert(strcmp(config.hash, "ms0:/h.gbsh") == 0);
  assert(config.perturb == 1 && config.perturb_frame == 0);
  assert(parse(RUN "hash=h.gbsh\nperturb=599\n"));
  assert(config.perturb == 1 && config.perturb_frame == 599);
  assert(parse(RUN "hash=h.gbsh\nperturb=0120\n"));
  assert(config.perturb_frame == 120);

  refused(RUN "hash=\n", AUTORUN_ERROR_BAD_VALUE, 6);
  refused(HEAD "rom=a.gba\nframes=600\nhash=h.gbsh\n",
   AUTORUN_ERROR_NEEDS_TRACE, 5);
  refused(HEAD "rom=a.gba\ntrace=t.gbit\nhash=h.gbsh\n",
   AUTORUN_ERROR_NEEDS_FRAMES, 5);
  refused(RUN "perturb=5\n", AUTORUN_ERROR_NEEDS_HASH, 6);
  refused(RUN "hash=h.gbsh\nperturb=600\n", AUTORUN_ERROR_BAD_VALUE, 7);
  refused(RUN "hash=h.gbsh\nperturb=4294967295\n", AUTORUN_ERROR_BAD_VALUE,
   7);
  refused(RUN "hash=h.gbsh\nperturb=\n", AUTORUN_ERROR_BAD_VALUE, 7);
  refused(RUN "hash=h.gbsh\nperturb=-1\n", AUTORUN_ERROR_BAD_VALUE, 7);
  refused(RUN "hash=h.gbsh\nperturb=5:\n", AUTORUN_ERROR_BAD_VALUE, 7);
  refused(RUN "hash=h.gbsh\nhash=i.gbsh\n", AUTORUN_ERROR_DUPLICATE_KEY, 7);
  refused(RUN "hash=h.gbsh\nperturb=1\nperturb=2\n",
   AUTORUN_ERROR_DUPLICATE_KEY, 8);
}

/* Mode m is scale m / 2 (SCALED_* order) with filter m % 2. */
static const char *const testpat_modes[AUTORUN_TESTPAT_MODES] =
{
  "none-nearest", "none-linear", "x15-gu-nearest", "x15-gu-linear",
  "x15-sw-nearest", "x15-sw-linear", "user-nearest", "user-linear",
  "16x9-gu-nearest", "16x9-gu-linear", "16x9-sharp-nearest",
  "16x9-sharp-linear", "16x9-auto-nearest", "16x9-auto-linear"
};

static void test_testpat_key(void)
{
  char text[128];
  uint32_t mode;

  for(mode = 0; mode < AUTORUN_TESTPAT_MODES; mode++)
  {
    snprintf(text, sizeof(text), HEAD "testpat=%s\n", testpat_modes[mode]);
    assert(parse(text));
    assert(config.testpat == 1 && config.testpat_mode == mode);
    assert(config.rom[0] == '\0' && config.frames == 0);
  }
  assert(AUTORUN_TESTPAT_SCALE(13U) == 6 && AUTORUN_TESTPAT_FILTER(13U) == 1);
  assert(parse("version=1\r\nbuild=" ID "\r\n# all of them\r\ntestpat=all"));
  assert(config.testpat == 1 && config.testpat_mode == AUTORUN_TESTPAT_ALL);

  refused(HEAD "testpat=\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "\ntestpat=x15\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "testpat=ALL\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=al\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=alll\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=x15-gu\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=x15-gu-\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=x15-gu-bilinear\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=x15-gu_nearest\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=x15-gu-nearestx\n", AUTORUN_ERROR_BAD_VALUE, 3);
  /* A filter of the right length is compared whole. */
  refused(HEAD "testpat=x15-gu-nearesx\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=none-lineaR\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=xx15-gu-nearest\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=linear-none\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=16x9-nearest\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat= all\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "testpat=all\ntestpat=all\n", AUTORUN_ERROR_DUPLICATE_KEY, 4);
  /* It never names a mode for another build. */
  refused("version=1\nbuild=fedcba987654\ntestpat=all\n",
   AUTORUN_ERROR_BUILD_MISMATCH, 2);

  /* No ROM runs and it quits by itself: every run key is refused, on its
     own line, whichever comes first. */
  refused(HEAD "testpat=all\nrom=a.gba\n", AUTORUN_ERROR_TESTPAT_ALONE, 4);
  refused(HEAD "rom=a.gba\ntestpat=all\n", AUTORUN_ERROR_TESTPAT_ALONE, 3);
  refused(HEAD "testpat=all\ntrace=t.gbit\n", AUTORUN_ERROR_TESTPAT_ALONE, 4);
  refused(HEAD "testpat=all\nframes=60\n", AUTORUN_ERROR_TESTPAT_ALONE, 4);
  refused(HEAD "testpat=all\n\nexit=1\n", AUTORUN_ERROR_TESTPAT_ALONE, 5);
  refused(HEAD "testpat=user-linear\nhash=h.gbsh\n",
   AUTORUN_ERROR_TESTPAT_ALONE, 4);
  refused(HEAD "testpat=all\nperturb=1\n", AUTORUN_ERROR_TESTPAT_ALONE, 4);
  refused(RUN "testpat=all\n", AUTORUN_ERROR_TESTPAT_ALONE, 3);
}

/* Each of the six profiles by name, alone or with a run; anything else is
   refused on its own line. */
static void test_net_profile_key(void)
{
  char text[128];
  uint32_t i;

  for(i = 0; i < NET_FAULT_PROFILE_COUNT; i++)
  {
    snprintf(text, sizeof(text), HEAD "net_profile=%s\n",
     net_fault_profiles[i].name);
    assert(parse(text));
    assert(config.net_profile == &net_fault_profiles[i]);
    assert(config.rom[0] == '\0' && config.frames == 0);
  }
  assert(parse(RUN "exit=1\r\nnet_profile=noisy"));
  assert(config.net_profile == &net_fault_profiles[2]);
  assert(strcmp(config.rom, "a.gba") == 0 && config.exit_when_done == 1);
  assert(parse(HEAD "rom=a.gba\n"));
  assert(config.net_profile == NULL);

  refused(HEAD "net_profile=\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "\nnet_profile=Noisy\n", AUTORUN_ERROR_BAD_VALUE, 4);
  refused(HEAD "net_profile=nois\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "net_profile=noisy \n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "net_profile=psp-stalls\n", AUTORUN_ERROR_BAD_VALUE, 3);
  /* 15 and 16 characters: one fits the name buffer, one does not. */
  refused(HEAD "net_profile=dropoutsdropout\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(HEAD "net_profile=dropoutsdropouts\n", AUTORUN_ERROR_BAD_VALUE, 3);
  refused(RUN "net_profile=lan\nnet_profile=lan\n",
   AUTORUN_ERROR_DUPLICATE_KEY, 7);
  refused(HEAD "net_profile=adhoc\nnet_profile=noisy\n",
   AUTORUN_ERROR_DUPLICATE_KEY, 4);
  /* The test pattern runs no link. */
  refused(HEAD "net_profile=noisy\ntestpat=all\n",
   AUTORUN_ERROR_TESTPAT_ALONE, 3);
  refused("version=1\nbuild=fedcba987654\nnet_profile=noisy\n",
   AUTORUN_ERROR_BUILD_MISMATCH, 2);
}

static void test_testpat_line(void)
{
  static const char line[] = "testpat=16x9-auto-linear used=sharp/nearest\n";
  char out[AUTORUN_CONFIG_TESTPAT_LINE_SIZE];
  char expected[AUTORUN_CONFIG_TESTPAT_LINE_SIZE];
  uint32_t mode;

  assert(autorun_config_testpat_line(13, "sharp", "nearest", out,
   sizeof(out)) == sizeof(line) - 1);
  assert(strcmp(out, line) == 0);
  for(mode = 0; mode < AUTORUN_TESTPAT_MODES; mode++)
  {
    snprintf(expected, sizeof(expected), "testpat=%s used=gu/linear\n",
     testpat_modes[mode]);
    assert(autorun_config_testpat_line(mode, "gu", "linear", out,
     sizeof(out)) == strlen(expected));
    assert(strcmp(out, expected) == 0);
  }
  assert(autorun_config_testpat_line(4, "sw", "none", out, sizeof(out)) > 0);
  assert(strcmp(out, "testpat=x15-sw-nearest used=sw/none\n") == 0);

  /* Room for the line and its NUL, not one byte less; no mode past the
     last one, all included. */
  memset(out, 'x', sizeof(out));
  assert(autorun_config_testpat_line(13, "sharp", "nearest", out,
   sizeof(line)) == sizeof(line) - 1);
  assert(strcmp(out, line) == 0);
  assert(autorun_config_testpat_line(13, "sharp", "nearest", out,
   sizeof(line) - 1) == 0);
  assert(autorun_config_testpat_line(0, "gu", "linear", out, 0) == 0);
  assert(autorun_config_testpat_line(0, "gu", "linear", NULL, 64) == 0);
  assert(autorun_config_testpat_line(AUTORUN_TESTPAT_ALL, "gu", "linear", out,
   sizeof(out)) == 0);
  assert(autorun_config_testpat_line(0, NULL, "linear", out, sizeof(out))
   == 0);
  assert(autorun_config_testpat_line(0, "gu", NULL, out, sizeof(out)) == 0);
}

static void test_hostile_input(void)
{
  static const char nul[] = HEAD "rom=a\0.gba\n";
  size_t head = strlen(HEAD);

  /* 255 characters fit, 256 do not; the CR of CRLF is not counted. */
  memcpy(big, HEAD "rom=", head + 4);
  memset(big + head + 4, 'r', AUTORUN_CONFIG_LINE_MAX - 4);
  memcpy(big + head + AUTORUN_CONFIG_LINE_MAX, "\r\n", 2);
  assert(parse_size(big, head + AUTORUN_CONFIG_LINE_MAX + 2));
  assert(strlen(config.rom) == AUTORUN_CONFIG_LINE_MAX - 4);
  big[head + AUTORUN_CONFIG_LINE_MAX] = 'r';
  assert(parse_size(big, head + AUTORUN_CONFIG_LINE_MAX + 2) == 0);
  assert_refused(AUTORUN_ERROR_LINE_TOO_LONG, 3);
  /* A long comment is refused too. */
  memset(big, '#', 300);
  assert(parse_size(big, 300) == 0);
  assert_refused(AUTORUN_ERROR_LINE_TOO_LONG, 1);

  assert(parse_size(nul, sizeof(nul) - 1) == 0);
  assert_refused(AUTORUN_ERROR_NOT_TEXT, 3);
  refused(HEAD "rom=a\tb.gba\n", AUTORUN_ERROR_NOT_TEXT, 3);
  refused(HEAD "rom=a\rb.gba\n", AUTORUN_ERROR_NOT_TEXT, 3);
  refused(HEAD "# \x7f\n", AUTORUN_ERROR_NOT_TEXT, 3);
  refused(HEAD "rom=\xe8.gba\n", AUTORUN_ERROR_NOT_TEXT, 3);
  refused("version=1\r\r\n", AUTORUN_ERROR_NOT_TEXT, 1);

  /* The size limit holds before anything is read; a full-size file of
     comments and blank lines is still read. */
  memset(big, '\n', sizeof(big));
  memcpy(big, HEAD, head);
  assert(parse_size(big, AUTORUN_CONFIG_MAX_SIZE));
  assert(parse_size(big, AUTORUN_CONFIG_MAX_SIZE + 1) == 0);
  assert_refused(AUTORUN_ERROR_TOO_LARGE, 0);
  assert(parse_size(big, (size_t)-1) == 0);
  assert_refused(AUTORUN_ERROR_TOO_LARGE, 0);
}

static void test_result_text(void)
{
  static const char done[] =
   "version=1\nbuild=" ID "\nstatus=done\nreason=none\nline=0\n"
   "frames=3600\n";
  static const char ignored[] =
   "version=1\nbuild=" ID "\nstatus=ignored\nreason=build-mismatch\n"
   "line=2\nframes=0\n";
  char out[AUTORUN_CONFIG_RESULT_SIZE];
  size_t length;

  length = autorun_config_result(ID, AUTORUN_STATUS_DONE, AUTORUN_OK, 0,
   3600, out, sizeof(out));
  assert(length == sizeof(done) - 1 && strcmp(out, done) == 0);
  length = autorun_config_result(ID, AUTORUN_STATUS_IGNORED,
   AUTORUN_ERROR_BUILD_MISMATCH, 2, 0, out, sizeof(out));
  assert(length == sizeof(ignored) - 1 && strcmp(out, ignored) == 0);

  /* Room for the text and its NUL, not one byte less. */
  memset(out, 'x', sizeof(out));
  assert(autorun_config_result(ID, AUTORUN_STATUS_DONE, AUTORUN_OK, 0, 3600,
   out, sizeof(done)) == sizeof(done) - 1);
  assert(strcmp(out, done) == 0);
  assert(autorun_config_result(ID, AUTORUN_STATUS_DONE, AUTORUN_OK, 0, 3600,
   out, sizeof(done) - 1) == 0);
  assert(autorun_config_result(ID, AUTORUN_STATUS_DONE, AUTORUN_OK, 0, 3600,
   out, 0) == 0);
  assert(autorun_config_result(ID, (AutorunStatus)4, AUTORUN_OK, 0, 0, out,
   sizeof(out)) == 0);

  length = autorun_config_result(ID, AUTORUN_STATUS_FAILED,
   AUTORUN_ERROR_TRACE, 4294967295U, 4294967295U, out, sizeof(out));
  assert(length > 0 && strstr(out, "\nstatus=failed\nreason=trace\n"
   "line=4294967295\nframes=4294967295\n") != NULL);

  assert(strcmp(autorun_config_error_name(AUTORUN_OK), "none") == 0);
  assert(strcmp(autorun_config_error_name(AUTORUN_ERROR_TOO_LARGE),
   "too-large") == 0);
  assert(strcmp(autorun_config_error_name(AUTORUN_ERROR_TRACE), "trace")
   == 0);
  assert(strcmp(autorun_config_error_name(AUTORUN_ERROR_NEEDS_TRACE),
   "needs-trace") == 0);
  assert(strcmp(autorun_config_error_name(AUTORUN_ERROR_NEEDS_HASH),
   "needs-hash") == 0);
  assert(strcmp(autorun_config_error_name(AUTORUN_ERROR_NO_HASH_MEMORY),
   "no-hash-memory") == 0);
  assert(strcmp(autorun_config_error_name(AUTORUN_ERROR_HASH), "hash") == 0);
  assert(strcmp(autorun_config_error_name(AUTORUN_ERROR_PERTURB_PAST_TRACE),
   "perturb-past-trace") == 0);
  assert(strcmp(autorun_config_error_name(AUTORUN_ERROR_TESTPAT_ALONE),
   "testpat-alone") == 0);
  assert(strcmp(autorun_config_error_name(
   (AutorunError)(AUTORUN_ERROR_TESTPAT_ALONE + 1)), "invalid") == 0);
  assert(strcmp(autorun_config_error_name((AutorunError)99), "invalid")
   == 0);
}

/* Runs every parse and result case; a failing assert aborts the run. */
int main(void)
{
  test_valid_files();
  test_version();
  test_build_gate();
  test_keys();
  test_values();
  test_hash_keys();
  test_testpat_key();
  test_net_profile_key();
  test_testpat_line();
  test_hostile_input();
  test_result_text();
  puts("Autorun config tests passed");
  return 0;
}
