# Menu footer layout

## Goal

Prevent menu options, contextual guidance, and footer controls from occupying
the same pixels. The fix must work for every menu and interface language
without changing saved settings.

## Layout

The menu screen has three fixed vertical regions:

- options: `y=28` through `y=207`, with the last text baseline at `y=196`;
- contextual guide: baselines at `y=210`, `y=222`, and `y=234`;
- controls: baseline at `y=258`.

Logical option line numbers remain unchanged. Before drawing, a viewport offset
maps them into the options region. When the selected option would fall below
the last visible line, the viewport scrolls enough to keep it visible. Rows
outside the region are not drawn.

## Text bounds

Guide and control strings are clipped at character boundaries to the available
screen width. Multibyte menu text uses the existing language-aware printer and
must not be split between bytes.

Clipping is deterministic. It does not animate, wrap into another region, or
change the underlying message tables.

## Components

A portable layout helper calculates:

- the viewport offset for a logical selection;
- the screen baseline for each logical row;
- whether a row is inside the options region;
- the maximum text width for a region.

`gui.c` uses those results only while drawing. Navigation, option actions,
menu definitions, configuration files, ROM data, and save data are unchanged.

## Failure handling

An invalid or empty menu produces no option rows. Out-of-range selections are
clamped to the nearest drawable row. A missing guide message falls back to the
existing help text.

## Verification

Host tests cover top and bottom selections, sparse line numbers, scrolling,
clamping, and row visibility. Source checks verify that menu drawing uses the
layout helper and that guide and control strings use bounded rendering.

The PSP build must pass in normal and diagnostic configurations. Hardware
validation checks the Cheats, Performance, Video, Interface, and Diagnostics
menus in Italian, including their final `Indietro` entries.

## Scope

This change fixes menu geometry only. It does not redesign the artwork
carousel, change theme assets, alter the volume overlay, or add new settings.
