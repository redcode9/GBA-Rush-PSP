<p align="center">
  <img src="source/res/PIC1.png" alt="GBA Rush PSP" width="720">
</p>

<h1 align="center">GBA Rush PSP</h1>

<p align="center">
  A performance-focused Game Boy Advance emulator for PlayStation Portable.
</p>

> [!IMPORTANT]
> **GBA Rush PSP is an independent fork of
> [andymcca/TempGBA4PSP-mod](https://github.com/andymcca/TempGBA4PSP-mod).**
> It exists because of the emulator work maintained there and throughout the
> gpSP, gpSP Kai, TempGBA, and TempGBA4PSP-mod projects. Please credit and
> support the original authors and contributors.

## About this fork

GBA Rush PSP continues the real-hardware PSP work from TempGBA4PSP-mod under a
distinct name and identity. It retains the upstream emulator core, interface,
renderers, compatibility work, and single-game launcher while adding a safer
cheat runtime, timed CodeBreaker hooks, configurable hold-to-fast-forward, and
repeatable release checks.

This repository is independently maintained. It is not the original
TempGBA4PSP-mod project, and its authors do not imply endorsement by upstream
maintainers. See [Project lineage and acknowledgements](docs/LINEAGE.md) for
the full development path and links.

You must provide your own legally obtained GBA BIOS and ROM dumps. They are not
part of this repository or its release packages.

## Highlights

- Plaintext and encrypted CodeBreaker codes, including timed master hooks.
- Existing GameShark, Pro Action Replay, and direct-write formats.
- Manual, transactional `.cht` loading with clear filename, count, entry names,
  and `ON`/`OFF` state.
- Up to 640 cheat entries with active-entry and sorted-hook indexes that avoid
  scanning inactive lists in hot paths.
- Configurable hold-to-fast-forward with unthrottled emulation, paced display
  updates, and immediate audio recovery.
- Two-PSP GBA link cable emulation over local ad-hoc Wi-Fi, under development
  for 1.1.0-beta.1.
- Old and new video renderers, multiple scale modes, themes, box art, recent
  ROMs, configurable dynarec policies, and multilingual menus.
- Japanese, English, Simplified Chinese, Traditional Chinese, and Italian UI.
- Optional source-based single-game launcher builder.

## Installation

Copy the release folder to `PSP/GAME/GBARushPSP/`, add your own
`gba_bios.bin`, and start **GBA Rush PSP** from the PSP Game menu.

```text
PSP/GAME/GBARushPSP/
├── EBOOT.PBP
├── exception.prx
└── gba_bios.bin       user supplied
```

See [Installation and upgrade notes](docs/INSTALL.md) for configuration,
fast-forward, legacy-folder, and update guidance.

## Cheats

Open **Cheats options**, choose **Load cheat file**, and select a `.cht` file.
Lists are selected manually so the user always knows which region and revision
they are applying.

For a timed CodeBreaker list, enable its master entry and then enable the
ordinary entries you want. Multiple ordinary entries can run together. If
masters with different hook addresses conflict, the most recently enabled
master takes ownership and incompatible master entries are disabled.

CodeBreaker codes remain dependent on the exact game region and revision.
Device-specific extensions that cannot be interpreted safely are not accepted.
The complete syntax and compatibility notes are in
[Cheat files](docs/CHEATS.md).

## Fast-forward

Fast-forward is active only while its configured physical button is held. The
default mapping is **Triangle**. Reassign **Fast forward** from the button
configuration menu to use any supported digital or analog direction.

The emulator bypasses normal throttling while held, targets roughly 30 visible
frames per second, and discards queued audio instead of allowing a backlog to
stall emulation. Releasing the button restores the selected frameskip and audio
behavior immediately.

## Link cable

The development branch can connect two PSP systems as Player 1 and Player 2
over local ad-hoc Wi-Fi. It implements GBA Normal 8-bit, Normal 32-bit, and
Multiplayer 16-bit cable transfers with synchronized timing and serial
interrupts.

This work is not part of the 1.0.0 release. It remains a beta candidate until
the two-console hardware test matrix passes. See [Link cable](docs/LINK_CABLE.md)
for setup, supported modes, current restrictions, diagnostics, and recovery
steps.

## Build

The PSP build requires a current [PSPDev](https://pspdev.github.io/) toolchain
with `psp-config` available in `PATH`.

```sh
make check
make psp-build
```

Docker users can follow [BUILD-DOCKER.md](BUILD-DOCKER.md). Build outputs are
created under `source/` and ignored by Git.

### Host tests

`make check` builds the portable production modules with the host C compiler.
It runs deterministic regression vectors for CodeBreaker handling, GBA serial
timing, framed link traffic, two-core session synchronization, malformed input,
timeouts, and thousands of transfers with variable delay. It also verifies
translation order and that private runtime data or generated executables are
not tracked.

### Release package

After a successful PSP build:

```sh
make package VERSION=1.0.0
```

This creates `GBA-Rush-PSP-1.0.0.zip` and its SHA-256 checksum in `dist/`.
The package contains only `EBOOT.PBP`, `exception.prx`, installation notes,
changelog, and license. It excludes BIOS files, ROMs, saves, local settings,
and cheat databases.

### Single-game launcher

The launcher and graphical builder remain available as source. Generate the
launcher template before running the builder:

```sh
make launcher-stub
python3 -m pip install -r source/single-game_launcher/tool/requirements.txt
python3 source/single-game_launcher/tool/gui.py
```

External audio tools are not committed to the repository. See the
[builder documentation](source/single-game_launcher/tool/README.md) for its
requirements and third-party notices.

## Repository layout

| Path | Purpose |
|---|---|
| `source/` | Emulator source, PSP Makefile, assets, and inherited SDK imports |
| `source/src/codebreaker.c` | CodeBreaker decoder and operation interpreter |
| `tests/` | Host regression tests |
| `docs/CHEATS.md` | Cheat format and master-code behavior |
| `docs/LINK_CABLE.md` | Two-PSP link setup, compatibility, and diagnostics |
| `docs/LINEAGE.md` | Upstream history, links, and acknowledgements |
| `docs/BRAND.md` | Naming, visual identity, and attribution rules |
| `source/single-game_launcher/` | Optional PSP launcher and desktop builder |
| `scripts/` | Source checks and repeatable release packaging |

## Thanks and lineage

GBA Rush PSP stands on work by:

- [Exophase](https://github.com/Exophase) and gpSP contributors;
- [notaz](https://github.com/notaz) and the wider gpSP community;
- Takka and the gpSP Kai contributors;
- Nebuleon, Normmatt, BassAceGold, and the TempGBA community;
- [Phoe-nix](https://github.com/phoe-nix) and earlier TempGBA4PSP-mod
  contributors;
- [andymcca](https://github.com/andymcca),
  [JxPv2](https://github.com/JxPv2), and everyone who contributed to the
  [direct upstream repository](https://github.com/andymcca/TempGBA4PSP-mod);
- [libretro/gpsp](https://github.com/libretro/gpsp) contributors whose fixes
  are credited in the inherited source and history.

Thank you to every developer and tester who kept this emulator lineage useful
on real PSP hardware. Detailed historical and asset attribution remains in
[`source/copyright`](source/copyright), [NOTICE.md](NOTICE.md), and
[docs/LINEAGE.md](docs/LINEAGE.md).

## License

The emulator is distributed under GPL-2.0-or-later. See [LICENSE](LICENSE).
The optional desktop launcher builder is GPL-3.0-or-later and carries its own
license and third-party notices.

This project is not affiliated with or endorsed by Nintendo, Sony, CodeBreaker,
or the maintainers of the upstream projects.
