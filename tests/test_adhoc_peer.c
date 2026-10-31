/*
 * Host tests for adhoc_peer_select(), which picks the link partner from the
 * ad-hoc scan list: the console's own MAC is never chosen, and a remembered
 * preferred MAC wins over scan order when it is present.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "adhoc_peer.h"

static int failures;

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

static void test_single_remote(void)
{
  static const uint8_t local[ADHOC_PEER_MAC_SIZE] = { 1, 2, 3, 4, 5, 6 };
  static const uint8_t peers[][ADHOC_PEER_MAC_SIZE] =
  {
    { 7, 8, 9, 10, 11, 12 }
  };
  uint8_t selected[ADHOC_PEER_MAC_SIZE] = { 0 };

  CHECK(adhoc_peer_select(local, NULL, &peers[0][0], 1, selected));
  CHECK(memcmp(selected, peers[0], ADHOC_PEER_MAC_SIZE) == 0);
}

static void test_preferred_remote(void)
{
  static const uint8_t local[ADHOC_PEER_MAC_SIZE] = { 1, 2, 3, 4, 5, 6 };
  static const uint8_t preferred[ADHOC_PEER_MAC_SIZE] =
   { 13, 14, 15, 16, 17, 18 };
  static const uint8_t peers[][ADHOC_PEER_MAC_SIZE] =
  {
    { 7, 8, 9, 10, 11, 12 },
    { 13, 14, 15, 16, 17, 18 }
  };
  uint8_t selected[ADHOC_PEER_MAC_SIZE] = { 0 };

  CHECK(adhoc_peer_select(local, preferred, &peers[0][0], 2, selected));
  CHECK(memcmp(selected, preferred, ADHOC_PEER_MAC_SIZE) == 0);
}

static void test_local_is_ignored(void)
{
  static const uint8_t local[ADHOC_PEER_MAC_SIZE] = { 1, 2, 3, 4, 5, 6 };
  static const uint8_t peers[][ADHOC_PEER_MAC_SIZE] =
  {
    { 1, 2, 3, 4, 5, 6 },
    { 7, 8, 9, 10, 11, 12 }
  };
  uint8_t selected[ADHOC_PEER_MAC_SIZE] = { 0 };

  /* Even when "preferred" is our own MAC, the scan entry for it is skipped. */
  CHECK(adhoc_peer_select(local, local, &peers[0][0], 2, selected));
  CHECK(memcmp(selected, peers[1], ADHOC_PEER_MAC_SIZE) == 0);
}

static void test_no_remote(void)
{
  static const uint8_t local[ADHOC_PEER_MAC_SIZE] = { 1, 2, 3, 4, 5, 6 };
  static const uint8_t peers[][ADHOC_PEER_MAC_SIZE] =
  {
    { 1, 2, 3, 4, 5, 6 }
  };
  uint8_t selected[ADHOC_PEER_MAC_SIZE] = { 0 };

  /* Empty list, a list holding only ourselves, and no local MAC at all. */
  CHECK(!adhoc_peer_select(local, NULL, &peers[0][0], 0, selected));
  CHECK(!adhoc_peer_select(local, NULL, &peers[0][0], 1, selected));
  CHECK(!adhoc_peer_select(NULL, NULL, &peers[0][0], 1, selected));
}

int main(void)
{
  test_single_remote();
  test_preferred_remote();
  test_local_is_ignored();
  test_no_remote();

  if(failures != 0)
  {
    fprintf(stderr, "%d adhoc peer test(s) failed\n", failures);
    return 1;
  }

  puts("adhoc peer tests passed");
  return 0;
}
