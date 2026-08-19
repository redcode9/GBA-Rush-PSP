# Link cable diagnostic ROMs

These programs exercise the three serial modes supported by GBA Rush PSP:

- `link-normal8.gba`: Normal 8-bit mode;
- `link-normal32.gba`: Normal 32-bit mode;
- `link-multiplayer.gba`: Multiplayer 16-bit mode.

Each screen reports the configured mode, local role, data sent and received,
raw `SIOCNT` status, serial interrupt count, and completion order. The ROMs are
small original test programs and contain no game assets, firmware, or BIOS.

## Build

Install the current `gba-dev` group with devkitPro pacman, then run:

```sh
make
```

The build expects `DEVKITPRO` and `DEVKITARM` to be configured by the devkitPro
environment. The diagnostics use direct GBA register access and do not require
libgba at runtime. Generated `.gba`, `.elf`, map, and object files are
intentionally ignored by Git.

## Use

Load the same diagnostic ROM on both PSPs and establish a link cable session
before starting a transfer.

For Normal 8 and Normal 32:

1. Select the same clock speed on both PSPs with Up or Down.
2. On Player 1, press L to select the internal clock.
3. On Player 2, press R to select the external clock.
4. Press A on Player 2 to arm reception, then press A on Player 1 to transfer.
5. Repeat step 4 for each exchange.

Reverse the L and R roles to test Player 2 as the internal clock owner.

For Multiplayer:

1. Select the same baud rate on both PSPs with Up or Down.
2. Press A on Player 1 to transfer. Player 2 answers automatically.
3. Repeat to verify increasing values and matching completion counters.

A serial error, mismatched completion number, missing interrupt, or different
received values indicates a failed test.
