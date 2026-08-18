# GBA Rush PSP installation

This package contains the emulator executable and its exception handler. It
does not include a Game Boy Advance BIOS, ROMs, saves, configuration files, or
cheat databases.

## Install on PSP

1. Copy the `GBARushPSP` folder to `PSP/GAME/` on the Memory Stick or PSP Go
   internal storage.
2. Add your own legally obtained `gba_bios.bin` to that folder.
3. Start **GBA Rush PSP** from the PSP Game menu.
4. Use the file browser to select your ROM.

The minimum layout is:

```text
PSP/GAME/GBARushPSP/
├── EBOOT.PBP
├── exception.prx
└── gba_bios.bin       user supplied
```

The emulator creates its own settings and save data as needed. You can place a
`dir.ini` beside the EBOOT when you want separate directories for ROMs, saves,
states, per-game settings, screenshots, cheats, and box art.

## Cheats

Cheat files are not loaded automatically. Open **Cheats options**, choose
**Load cheat file**, and select a `.cht` list. See the repository's
`docs/CHEATS.md` for the file structure, CodeBreaker masters, and safety notes.

## Fast-forward

Fast-forward is a hold action. The default mapping is **Triangle**. Change it
from the button configuration menu by assigning **Fast forward** to another
physical control. Releasing the button restores normal frame pacing and audio.

For best performance on real hardware, use the 333 MHz clock option. Keep
automatic frameskip enabled unless a specific game behaves better with another
setting.

## Upgrading

Replace `EBOOT.PBP` and `exception.prx`. Keep your BIOS, ROMs, saves, states,
cheat lists, and configuration files. Back up save data before upgrading.

The emulator itself can run from a legacy folder named `TempGBA4PSP`. The
single-game launcher now looks for `GBARushPSP` by default; set an explicit
path in `emulator_path.txt` when a launcher should use a legacy installation.

GBA Rush PSP is an independent fork of
[andymcca/TempGBA4PSP-mod](https://github.com/andymcca/TempGBA4PSP-mod). It is
not affiliated with or endorsed by Nintendo, Sony, CodeBreaker, or the upstream
maintainers.
