#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "link_journal_transitions.h"

#define LINK_JOURNAL_MAKE_ROW(name, source, target, role, trigger, flags) \
  {name, source, target, role, trigger, flags},

static const LinkJournalTransition transitions[] = {
  LINK_JOURNAL_TRANSITION_ROWS(LINK_JOURNAL_MAKE_ROW)
};

#undef LINK_JOURNAL_MAKE_ROW

typedef char LinkJournalCountMatches[
  (sizeof(transitions) / sizeof(transitions[0]) ==
   LINK_JOURNAL_TRANSITION_COUNT) ? 1 : -1];

static int has_edge(LinkJournalStage source, LinkJournalStage target)
{
  size_t index;

  for(index = 0; index < LINK_JOURNAL_TRANSITION_COUNT; index++)
  {
    if(transitions[index].source == source &&
       transitions[index].target == target)
      return 1;
  }
  return 0;
}

int main(void)
{
  size_t index;
  size_t other;

  assert(LINK_JOURNAL_TRANSITION_COUNT == 14);
  assert(has_edge(LINK_JOURNAL_EMPTY, LINK_JOURNAL_ARMED));
  assert(has_edge(LINK_JOURNAL_ARMED, LINK_JOURNAL_EMPTY));
  assert(has_edge(LINK_JOURNAL_ARMED, LINK_JOURNAL_DECIDED));
  assert(has_edge(LINK_JOURNAL_DECIDED, LINK_JOURNAL_RELEASED));
  assert(has_edge(LINK_JOURNAL_RELEASED, LINK_JOURNAL_EMPTY));

  for(index = 0; index < LINK_JOURNAL_TRANSITION_COUNT; index++)
  {
    const LinkJournalTransition *transition = &transitions[index];

    assert(transition->source >= LINK_JOURNAL_EMPTY);
    assert(transition->source <= LINK_JOURNAL_RELEASED);
    assert(transition->target >= LINK_JOURNAL_EMPTY);
    assert(transition->target <= LINK_JOURNAL_RELEASED);
    assert(!(transition->source == LINK_JOURNAL_DECIDED &&
             transition->target == LINK_JOURNAL_EMPTY));
    assert(!(transition->target == LINK_JOURNAL_ARMED &&
             transition->source != LINK_JOURNAL_EMPTY));

    if(transition->source == transition->target)
      assert(transition->flags & LINK_JOURNAL_FLAG_IDEMPOTENT);
    if(transition->source == LINK_JOURNAL_RELEASED &&
       transition->target == LINK_JOURNAL_EMPTY)
      assert(transition->flags & LINK_JOURNAL_FLAG_CHECKPOINT);

    for(other = index + 1; other < LINK_JOURNAL_TRANSITION_COUNT; other++)
      assert(strcmp(transition->name, transitions[other].name) != 0);
  }

  puts("link journal transition table: ok");
  return 0;
}
