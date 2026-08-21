#ifndef MENU_MODEL_H
#define MENU_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define MENU_MODEL_INVALID_INDEX ((size_t)-1)

typedef enum
{
  MENU_ROW_ACTION = 0,
  MENU_ROW_SUBMENU,
  MENU_ROW_VALUE,
  MENU_ROW_STATUS
} MenuRowKind;

typedef enum
{
  MENU_ROW_SELECTABLE = 1 << 0,
  MENU_ROW_WRAP_VALUE = 1 << 1
} MenuRowFlags;

typedef enum
{
  MENU_MODEL_NO_SELECTION = 0,
  MENU_MODEL_UNAVAILABLE,
  MENU_MODEL_UNSUPPORTED,
  MENU_MODEL_AT_BOUNDARY,
  MENU_MODEL_CHANGED,
  MENU_MODEL_ACTIVATED
} MenuModelResult;

typedef int (*MenuRowPredicate)(void *context);
typedef void (*MenuRowCallback)(void *context);

typedef struct
{
  uint32_t summary_id;
  uint32_t recommendation_id;
  uint32_t impact_id;
  int restart_required;
} MenuRowHelp;

typedef struct
{
  const char *id;
  uint32_t label_id;
  MenuRowKind kind;
  unsigned int flags;
  MenuRowPredicate visible;
  MenuRowPredicate available;
  uint32_t unavailable_reason_id;
  MenuRowHelp help;
  uint32_t *value;
  uint32_t value_count;
  MenuRowCallback activate;
  MenuRowCallback changed;
  void *context;
} MenuRow;

typedef struct
{
  const MenuRow *rows;
  size_t row_count;
  size_t selected_index;
} MenuModel;

int menu_model_validate(const MenuRow *rows, size_t row_count);
void menu_model_init(MenuModel *model, const MenuRow *rows, size_t row_count);
const MenuRow *menu_model_selected(const MenuModel *model);
int menu_model_selected_available(const MenuModel *model);
int menu_model_select_id(MenuModel *model, const char *id);
int menu_model_reconcile(MenuModel *model);
int menu_model_move(MenuModel *model, int direction);
MenuModelResult menu_model_activate(MenuModel *model);
MenuModelResult menu_model_adjust(MenuModel *model, int direction);

#endif
