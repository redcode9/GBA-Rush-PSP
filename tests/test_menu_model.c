#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "menu_model.h"

typedef struct
{
  int show_optional;
  int feature_ready;
  unsigned int action_count;
  unsigned int change_count;
} TestContext;

static int optional_visible(void *context)
{
  return ((TestContext *)context)->show_optional;
}

static int feature_available(void *context)
{
  return ((TestContext *)context)->feature_ready;
}

static void count_action(void *context)
{
  ((TestContext *)context)->action_count++;
}

static void count_change(void *context)
{
  ((TestContext *)context)->change_count++;
}

static void test_navigation_and_dispatch(void)
{
  TestContext context = { 0, 0, 0, 0 };
  uint32_t speed = 2;
  const MenuRow rows[] =
  {
    {
      "continue", 1, MENU_ROW_ACTION, MENU_ROW_SELECTABLE,
      NULL, NULL, 0, { 10, 11, 12, 0 },
      NULL, 0, count_action, NULL, &context
    },
    {
      "optional", 2, MENU_ROW_ACTION, MENU_ROW_SELECTABLE,
      optional_visible, NULL, 0, { 20, 21, 22, 0 },
      NULL, 0, NULL, NULL, &context
    },
    {
      "link", 3, MENU_ROW_SUBMENU, MENU_ROW_SELECTABLE,
      NULL, feature_available, 30, { 31, 32, 33, 1 },
      NULL, 0, count_action, NULL, &context
    },
    {
      "speed", 4, MENU_ROW_VALUE, MENU_ROW_SELECTABLE | MENU_ROW_WRAP_VALUE,
      NULL, NULL, 0, { 40, 41, 42, 0 },
      &speed, 3, NULL, count_change, &context
    },
    {
      "status", 5, MENU_ROW_STATUS, 0,
      NULL, NULL, 0, { 50, 51, 52, 0 },
      NULL, 0, NULL, NULL, &context
    }
  };
  MenuModel model;

  assert(menu_model_validate(rows, sizeof(rows) / sizeof(rows[0])) == 1);
  menu_model_init(&model, rows, sizeof(rows) / sizeof(rows[0]));
  assert(strcmp(menu_model_selected(&model)->id, "continue") == 0);

  assert(menu_model_move(&model, 1) == 1);
  assert(strcmp(menu_model_selected(&model)->id, "link") == 0);
  assert(menu_model_selected_available(&model) == 0);
  assert(menu_model_activate(&model) == MENU_MODEL_UNAVAILABLE);
  assert(context.action_count == 0);

  assert(menu_model_move(&model, 1) == 1);
  assert(strcmp(menu_model_selected(&model)->id, "speed") == 0);
  assert(menu_model_adjust(&model, 1) == MENU_MODEL_CHANGED);
  assert(speed == 0);
  assert(context.change_count == 1);
  assert(menu_model_adjust(&model, -1) == MENU_MODEL_CHANGED);
  assert(speed == 2);

  assert(menu_model_move(&model, 1) == 1);
  assert(strcmp(menu_model_selected(&model)->id, "continue") == 0);
  assert(menu_model_activate(&model) == MENU_MODEL_ACTIVATED);
  assert(context.action_count == 1);

  assert(menu_model_select_id(&model, "link") == 1);
  context.feature_ready = 1;
  assert(menu_model_activate(&model) == MENU_MODEL_ACTIVATED);
  assert(context.action_count == 2);

  context.show_optional = 1;
  assert(menu_model_select_id(&model, "optional") == 1);
  context.show_optional = 0;
  assert(menu_model_reconcile(&model) == 1);
  assert(strcmp(menu_model_selected(&model)->id, "link") == 0);
}

static void test_bounds_and_invalid_models(void)
{
  uint32_t value = 0;
  const MenuRow bounded[] =
  {
    {
      "bounded", 1, MENU_ROW_VALUE, MENU_ROW_SELECTABLE,
      NULL, NULL, 0, { 2, 3, 4, 0 },
      &value, 2, NULL, NULL, NULL
    }
  };
  const MenuRow duplicate[] =
  {
    { "same", 1, MENU_ROW_ACTION, MENU_ROW_SELECTABLE,
      NULL, NULL, 0, { 1, 1, 1, 0 }, NULL, 0, NULL, NULL, NULL },
    { "same", 2, MENU_ROW_ACTION, MENU_ROW_SELECTABLE,
      NULL, NULL, 0, { 2, 2, 2, 0 }, NULL, 0, NULL, NULL, NULL }
  };
  MenuModel model;

  assert(menu_model_validate(duplicate, 2) == 0);
  assert(menu_model_validate(bounded, 1) == 1);
  menu_model_init(&model, bounded, 1);
  assert(menu_model_adjust(&model, -1) == MENU_MODEL_AT_BOUNDARY);
  assert(value == 0);
  assert(menu_model_adjust(&model, 1) == MENU_MODEL_CHANGED);
  assert(value == 1);
  assert(menu_model_adjust(&model, 1) == MENU_MODEL_AT_BOUNDARY);
  assert(value == 1);
}

static void test_no_selectable_rows(void)
{
  const MenuRow rows[] =
  {
    { "status", 1, MENU_ROW_STATUS, 0,
      NULL, NULL, 0, { 2, 3, 4, 0 }, NULL, 0, NULL, NULL, NULL }
  };
  MenuModel model;

  menu_model_init(&model, rows, 1);
  assert(menu_model_selected(&model) == NULL);
  assert(menu_model_move(&model, 1) == 0);
  assert(menu_model_activate(&model) == MENU_MODEL_NO_SELECTION);
  assert(menu_model_adjust(&model, 1) == MENU_MODEL_NO_SELECTION);
}

int main(void)
{
  test_navigation_and_dispatch();
  test_bounds_and_invalid_models();
  test_no_selectable_rows();
  puts("menu model tests passed");
  return 0;
}
