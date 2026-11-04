# Built-in GBA BIOS

GBA Rush PSP boots with this BIOS when `gba_bios.bin` is missing from the
emulator folder, or when that file is not a GBA BIOS image (16 KB, starting
with the GBA reset vector). It is an open-source replacement for Nintendo's
BIOS, written by Normmatt on top of the VBA and VBA-M HLE BIOS:

- Copyright (c) 2002-2006 VBA Development Team
- Copyright (c) 2006-2013 VBA-M Development Team
- Copyright (c) 2013 Normmatt

It is distributed under the GNU General Public License, version 2 or (at your
option) any later version, like GBA Rush PSP (see `LICENSE` at the top of this
repository). The logo it shows at boot is Normmatt's, not Nintendo's.

## Where it comes from

The EBOOT embeds the image as the byte array in `source/src/open_bios.h`,
because this repository does not track `.bin` files. The image (16384 bytes,
sha256 `661a9afb93624f2c5e77d07dab137bd8d23cff79e8639c62fe621b49bb064749`) is
`bios/open_gba_bios.bin` of [libretro/gpsp](https://github.com/libretro/gpsp)
at commit `5819380c2ffb0900219d700a382ee68c464ebb99`, and is identical to
`bios/gba_bios.bin` of [Nebuleon/ReGBA](https://github.com/Nebuleon/ReGBA) at
commit `313ddd06932aae69daaf66d6102d77e0475de88a` (2013-09-02), the commit
that added the image together with the source it was built from.
`tests/test_open_bios.py` checks the array against that sha256.

Everything else in this folder is ReGBA's `bios/` folder at that commit, byte
for byte: `.gitattributes` keeps the original line endings (CRLF in five of
the files) and trailing spaces, so `git ls-tree` shows the same blob ids as
ReGBA. It is used rather than libretro's copy, whose sources match ReGBA's
apart from a final newline but which has no `specs/` folder and whose
Makefile drops the linker script (`specs/bios.ld`) that places the image at
address 0.

Assembled for the ARM7TDMI, the three assembly sources give the same bytes as
the image, apart from the addresses the linker fills in: `core.s` at 0x0000
(exception vectors, IRQ and SWI handlers, SWI table), `divide.s` at 0x1798
and `reset.s` at 0x1804.

## Rebuilding it

The Makefile needs devkitARM and libtonc (tonclib), which ReGBA expected in a
`tonclib` folder next to `bios/`; libtonc is not part of this repository.
`make` writes `gba_bios.bin` into this folder (Git ignores it). To embed a
new image:

1. `xxd -i gba_bios.bin` prints its bytes; put them between the braces of
   `open_bios_rom` in `source/src/open_bios.h`.
2. Update the provenance and the sha256 in this file, in
   `source/src/open_bios.h` and in `tests/test_open_bios.py`.

## How the emulator uses it

A valid `gba_bios.bin` beside the EBOOT is always used first, and remains the
more compatible choice; a copy of this image named `gba_bios.bin` (ReGBA
ships it that way) counts as the built-in BIOS. The BIOS is read once, when
the emulator starts.

Savestates record which of the two BIOSes made them and load only with the
same one, because a game paused inside one BIOS would crash in the other;
savestates from earlier versions count as made with `gba_bios.bin`, and GBA
Rush PSP 1.4.2 and earlier do not check this. The automatic savestate has one
file per BIOS (`<rom>_auto.svs` with `gba_bios.bin`, `<rom>_auto-builtin.svs`
with this one), so neither BIOS overwrites the other's. The
`bios_rom_hack_39` and `bios_rom_hack_2C` options of `game_config.txt` patch
the official BIOS and are ignored with this one.

Known differences from the official BIOS, all in the original code:

- `Div` and `DivArm` (SWI 0x06/0x07) return the remainder without the sign of
  the numerator (`divide.s`), and the SWI handler restores r3, so the absolute
  quotient is not returned there. They hang on 0, 1 or -1 divided by 0 and on
  -2^31 divided by a power of two, where the official BIOS returns.
- `IntrWait` (SWI 0x04) checks only interrupt flags 0-7 before returning
  (`tst r1, #255` at 0x4B4), so a wait for DMA, keypad or Game Pak interrupts
  alone never ends.
- `SoundBiasChange` and the sound driver and music player functions do
  nothing, except `MidiKey2Freq`; `SoundGetJumpList` fills its list with
  pointers to a function that just returns.
- `MultiBoot` (SWI 0x25) and `HardReset` (0x26) do nothing: a game that calls
  `HardReset` keeps running instead of restarting.
