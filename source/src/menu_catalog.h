/* The main menu categories in display order, each with a stable text id
   (menu_catalog.c). */

#ifndef MENU_CATALOG_H
#define MENU_CATALOG_H

#include <stddef.h>

typedef enum
{
  MENU_MAIN_CONTINUE = 0,
  MENU_MAIN_SAVE_LOAD,
  MENU_MAIN_GAME,
  MENU_MAIN_VIDEO,
  MENU_MAIN_AUDIO,
  MENU_MAIN_PERFORMANCE,
  MENU_MAIN_CONTROLS,
  MENU_MAIN_LINK_CABLE,
  MENU_MAIN_INTERFACE,
  MENU_MAIN_DIAGNOSTICS,
  MENU_MAIN_EXIT,
  MENU_MAIN_CATEGORY_COUNT
} MenuMainCategory;

typedef struct
{
  MenuMainCategory category;
  const char *id;
} MenuMainCategoryDefinition;

extern const MenuMainCategoryDefinition
 menu_main_categories[MENU_MAIN_CATEGORY_COUNT];
extern const size_t menu_main_category_count;

#endif
