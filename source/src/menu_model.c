#include "menu_model.h"

#include <string.h>

static int row_visible(const MenuRow *row)
{
  return row->visible == NULL || row->visible(row->context);
}

static int row_available(const MenuRow *row)
{
  return row->available == NULL || row->available(row->context);
}

static int row_selectable(const MenuRow *row)
{
  return (row->flags & MENU_ROW_SELECTABLE) != 0 && row_visible(row);
}

static size_t find_selectable(const MenuModel *model, size_t start,
 size_t attempts, int direction)
{
  size_t index = start;
  size_t i;

  for (i = 0; i < attempts; i++)
  {
    if (direction > 0)
      index = (index + 1) % model->row_count;
    else
      index = index == 0 ? model->row_count - 1 : index - 1;

    if (row_selectable(&model->rows[index]))
      return index;
  }

  return MENU_MODEL_INVALID_INDEX;
}

int menu_model_validate(const MenuRow *rows, size_t row_count)
{
  size_t i;
  size_t j;

  if (rows == NULL || row_count == 0)
    return 0;

  for (i = 0; i < row_count; i++)
  {
    if (rows[i].id == NULL || rows[i].id[0] == '\0')
      return 0;

    if (rows[i].kind == MENU_ROW_VALUE &&
        (rows[i].value == NULL || rows[i].value_count == 0))
      return 0;

    for (j = i + 1; j < row_count; j++)
    {
      if (rows[j].id != NULL && strcmp(rows[i].id, rows[j].id) == 0)
        return 0;
    }
  }

  return 1;
}

void menu_model_init(MenuModel *model, const MenuRow *rows, size_t row_count)
{
  size_t i;

  if (model == NULL)
    return;

  model->rows = rows;
  model->row_count = row_count;
  model->selected_index = MENU_MODEL_INVALID_INDEX;

  if (rows == NULL)
    return;

  for (i = 0; i < row_count; i++)
  {
    if (row_selectable(&rows[i]))
    {
      model->selected_index = i;
      return;
    }
  }
}

const MenuRow *menu_model_selected(const MenuModel *model)
{
  if (model == NULL || model->rows == NULL ||
      model->selected_index >= model->row_count)
    return NULL;

  if (!row_selectable(&model->rows[model->selected_index]))
    return NULL;

  return &model->rows[model->selected_index];
}

int menu_model_selected_available(const MenuModel *model)
{
  const MenuRow *row = menu_model_selected(model);

  return row != NULL && row_available(row);
}

int menu_model_select_id(MenuModel *model, const char *id)
{
  size_t i;

  if (model == NULL || model->rows == NULL || id == NULL)
    return 0;

  for (i = 0; i < model->row_count; i++)
  {
    if (strcmp(model->rows[i].id, id) == 0 && row_selectable(&model->rows[i]))
    {
      model->selected_index = i;
      return 1;
    }
  }

  return 0;
}

int menu_model_reconcile(MenuModel *model)
{
  size_t previous;
  size_t found;

  if (model == NULL || model->rows == NULL || model->row_count == 0)
    return 0;

  if (menu_model_selected(model) != NULL)
    return 0;

  previous = model->selected_index;
  if (previous >= model->row_count)
    previous = model->row_count - 1;

  found = find_selectable(model, previous, model->row_count, 1);
  model->selected_index = found;
  return found != previous;
}

int menu_model_move(MenuModel *model, int direction)
{
  size_t start;
  size_t found;

  if (model == NULL || model->rows == NULL || model->row_count == 0 ||
      direction == 0)
    return 0;

  if (model->selected_index >= model->row_count)
    start = direction > 0 ? model->row_count - 1 : 0;
  else
    start = model->selected_index;

  found = find_selectable(model, start, model->row_count,
                          direction > 0 ? 1 : -1);
  if (found == MENU_MODEL_INVALID_INDEX)
    return 0;

  model->selected_index = found;
  return 1;
}

MenuModelResult menu_model_activate(MenuModel *model)
{
  const MenuRow *row = menu_model_selected(model);

  if (row == NULL)
    return MENU_MODEL_NO_SELECTION;
  if (!row_available(row))
    return MENU_MODEL_UNAVAILABLE;
  if ((row->kind != MENU_ROW_ACTION && row->kind != MENU_ROW_SUBMENU) ||
      row->activate == NULL)
    return MENU_MODEL_UNSUPPORTED;

  row->activate(row->context);
  return MENU_MODEL_ACTIVATED;
}

MenuModelResult menu_model_adjust(MenuModel *model, int direction)
{
  const MenuRow *row = menu_model_selected(model);
  uint32_t current;
  uint32_t next;

  if (row == NULL)
    return MENU_MODEL_NO_SELECTION;
  if (!row_available(row))
    return MENU_MODEL_UNAVAILABLE;
  if (row->kind != MENU_ROW_VALUE || row->value == NULL ||
      row->value_count == 0 || direction == 0)
    return MENU_MODEL_UNSUPPORTED;

  current = *row->value;
  if (current >= row->value_count)
    return MENU_MODEL_UNSUPPORTED;

  next = current;
  if (direction > 0)
  {
    if (current + 1 < row->value_count)
      next++;
    else if ((row->flags & MENU_ROW_WRAP_VALUE) != 0)
      next = 0;
  }
  else
  {
    if (current > 0)
      next--;
    else if ((row->flags & MENU_ROW_WRAP_VALUE) != 0)
      next = row->value_count - 1;
  }

  if (next == current)
    return MENU_MODEL_AT_BOUNDARY;

  *row->value = next;
  if (row->changed != NULL)
    row->changed(row->context);
  return MENU_MODEL_CHANGED;
}
