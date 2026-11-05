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
 * The raster test ROM. It runs the HBlank cases (cases_hblank.c) with
 * the result protocol of the test ROMs.
 */

#include "raster.h"

extern const TestCase cases_hblank[];

static const TestCase *const areas[] =
{
  cases_hblank,
  NULL
};

int main(void)
{
  result_run(areas);
  for(;;)
    wait_vblank();
}
