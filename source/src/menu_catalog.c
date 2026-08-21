#include "menu_catalog.h"

const MenuMainCategoryDefinition menu_main_categories[MENU_MAIN_CATEGORY_COUNT] =
{
  { MENU_MAIN_CONTINUE, "continue" },
  { MENU_MAIN_SAVE_LOAD, "save-load" },
  { MENU_MAIN_GAME, "game" },
  { MENU_MAIN_VIDEO, "video" },
  { MENU_MAIN_AUDIO, "audio" },
  { MENU_MAIN_PERFORMANCE, "performance" },
  { MENU_MAIN_CONTROLS, "controls" },
  { MENU_MAIN_LINK_CABLE, "link-cable" },
  { MENU_MAIN_INTERFACE, "interface" },
  { MENU_MAIN_DIAGNOSTICS, "diagnostics" },
  { MENU_MAIN_EXIT, "exit" }
};

const size_t menu_main_category_count = MENU_MAIN_CATEGORY_COUNT;
