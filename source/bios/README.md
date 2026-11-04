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
because this repository does not track `.bin` files. It starts from
`bios/open_gba_bios.bin` of [libretro/gpsp](https://github.com/libretro/gpsp)
at commit `5819380c2ffb0900219d700a382ee68c464ebb99` (16384 bytes, sha256
`661a9afb93624f2c5e77d07dab137bd8d23cff79e8639c62fe621b49bb064749`), which is
identical to `bios/gba_bios.bin` of
[Nebuleon/ReGBA](https://github.com/Nebuleon/ReGBA) at commit
`313ddd06932aae69daaf66d6102d77e0475de88a` (2013-09-02), the commit that
added the image together with the source it was built from. GBA Rush PSP
changed Div and DivArm (see below); the embedded image has sha256
`16c7932e2cf42d031a405d4ca6a886c78379e0dc6db69e20609761df8d63a3f2`.
`tests/test_open_bios.py` checks both: putting back the original bytes of
the changes gives libretro's image exactly.

Everything else in this folder is ReGBA's `bios/` folder at that commit, byte
for byte, apart from `source/divide.s` and `source/softwareinterrupts.c`,
which carry a dated notice of the change: `.gitattributes` keeps the
original line endings (CRLF in five of the files) and trailing spaces, so
`git ls-tree` shows the same blob ids as ReGBA for the unchanged files. It is
used rather than libretro's copy, whose sources match ReGBA's apart from a
final newline but which has no `specs/` folder and whose Makefile drops the
linker script (`specs/bios.ld`) that places the image at address 0.

## Rebuilding it

`python3 scripts/build-open-bios.py` rewrites `source/src/open_bios.h`, and
`--check` only says whether it is up to date (`make check` runs that when
`arm-none-eabi-gcc` is on the `PATH`). It needs the Arm GNU Toolchain
(`arm-none-eabi-gcc`, 15.2 here) and nothing else:

- The three assembly sources are assembled and linked at their places:
  `core.s` at 0x0000 (exception vectors, IRQ and SWI handlers, SWI table),
  `reset.s` at 0x1804, and `divide.s` at 0x2440, in the space the original
  image leaves empty after its code. Given the addresses of the C functions,
  ReGBA's unchanged `core.s`, `divide.s` (at its old place, 0x1798) and
  `reset.s` give back the original image byte for byte; the script checks
  that `core.s` and `reset.s` still do, apart from the two SWI table entries
  that point to the new `divide.s`.
- The C sources (`softwareinterrupts.c`, `logo.c`, `logo_data.c`,
  `helper.arm.c`) cannot be rebuilt byte for byte: they include libtonc's
  `tonc.h`, which is not part of this repository, and were compiled in 2013
  by the GCC of devkitARM, whose code generation GCC 15 does not reproduce.
  So the image keeps their original bytes, with one word patched to do what
  their source change does: at 0x17A8, the old Div that ArcTan2 calls, a
  branch to the new `Div`.

The script takes the original image from `open_bios.h` itself, by putting
back the original bytes of its changes, or from a copy of libretro's file
given with `--base`; either way it checks the sha256 above. ReGBA's
`Makefile` still builds the whole BIOS with devkitARM and libtonc (in a
`tonclib` folder next to `bios/`), but that image differs from the embedded
one wherever the compiler does.

## How the emulator uses it

A valid `gba_bios.bin` beside the EBOOT is always used first, and remains the
more compatible choice. A `gba_bios.bin` that is a copy of this BIOS (ReGBA
ships it that way), with or without GBA Rush PSP's changes, counts as the
built-in BIOS, and the built-in image is used. The BIOS is read once, when
the emulator starts.

Savestates record which of the two BIOSes made them and load only with the
same one, because a game paused inside one BIOS would crash in the other;
savestates from earlier versions count as made with `gba_bios.bin`, and GBA
Rush PSP 1.4.3 and earlier do not check this. The automatic savestate has one
file per BIOS (`<rom>_auto.svs` with `gba_bios.bin`, `<rom>_auto-builtin.svs`
with this one), so neither BIOS overwrites the other's. The
`bios_rom_hack_39` and `bios_rom_hack_2C` options of `game_config.txt` patch
the official BIOS and are ignored with this one.

## Changes by GBA Rush PSP

- `Div` and `DivArm` (SWI 0x06/0x07, `divide.s`) return what the official
  BIOS returns: the quotient in r0, the remainder with the sign of the
  numerator in r1 and the absolute quotient in r3, with r2, r12 and the
  flags unchanged. ReGBA's routine returned the remainder without its sign,
  lost r3 to the SWI handler, and looped forever on any division by zero and
  on -2^31 divided by a power of two. The official BIOS returns for 0, 1 and
  -1 divided by 0 (r0 = 1 or -1, the sign of the numerator, r1 = the
  numerator, r3 = 1) and hangs for any other numerator; this BIOS gives every
  division by zero the same rule. `source/src/bios_hle.c` holds the same
  results in C, recorded from the official BIOS, and `tests/test_bios_hle.c`
  runs the image on the New core to check 2,000 pairs and the edge cases
  against it, from ARM and Thumb code.

## Known differences from the official BIOS

- `IntrWait` (SWI 0x04) checks only interrupt flags 0-7 before returning
  (`tst r1, #255` at 0x4B4), so a wait for DMA, keypad or Game Pak interrupts
  alone never ends.
- `SoundBiasChange` and the sound driver and music player functions do
  nothing, except `MidiKey2Freq`; `SoundGetJumpList` fills its list with
  pointers to a function that just returns.
- `MultiBoot` (SWI 0x25) and `HardReset` (0x26) do nothing: a game that calls
  `HardReset` keeps running instead of restarting.
