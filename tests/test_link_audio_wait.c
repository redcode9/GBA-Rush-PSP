#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "link_audio_wait.h"

static void test_wait_drains_once_and_restores(void)
{
  LinkAudioWait wait;
  uint32_t pause = 0;

  link_audio_wait_init(&wait);
  assert(link_audio_wait_begin(&wait, pause, &pause));
  assert(pause == 1 && wait.active);
  assert(!link_audio_wait_begin(&wait, pause, &pause));
  assert(pause == 1 && wait.saved_pause == 0);
  assert(link_audio_wait_end(&wait, &pause));
  assert(pause == 0 && !wait.active);
  assert(!link_audio_wait_end(&wait, &pause));
  assert(pause == 0);
}

static void test_existing_pause_is_preserved(void)
{
  LinkAudioWait wait;
  uint32_t pause = 1;

  link_audio_wait_init(&wait);
  assert(link_audio_wait_begin(&wait, pause, &pause));
  assert(pause == 1);
  assert(link_audio_wait_end(&wait, &pause));
  assert(pause == 1);
}

static void test_invalid_inputs_are_noops(void)
{
  LinkAudioWait wait;
  uint32_t pause = 0;

  link_audio_wait_init(&wait);
  assert(!link_audio_wait_begin(NULL, pause, &pause));
  assert(!link_audio_wait_begin(&wait, pause, NULL));
  assert(!link_audio_wait_end(NULL, &pause));
  assert(!link_audio_wait_end(&wait, NULL));
}

int main(void)
{
  test_wait_drains_once_and_restores();
  test_existing_pause_is_preserved();
  test_invalid_inputs_are_noops();
  puts("Link audio wait tests passed");
  return 0;
}
