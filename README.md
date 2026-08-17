<p align="center">
  <img src="source/res/ICON0.png" alt="TempGBA4PSP" width="360">
</p>

<h1 align="center">TempGBA4PSP</h1>

<p align="center">
  A performance-focused Game Boy Advance emulator for PlayStation Portable,
  based on TempGBA and gpSP.
</p>

## About

TempGBA4PSP is a homebrew GBA emulator designed for real PSP hardware. This
fork adds a safer and more complete cheat runtime, timed CodeBreaker hooks, a
configurable hold-to-fast-forward action, and release-quality regression
checks while retaining the performance and compatibility work from its
TempGBA4PSP-mod base.

You must provide your own legally obtained GBA BIOS and ROM dumps. They are not
part of this repository or its release packages.

## Highlights

- Plaintext and encrypted CodeBreaker codes, including timed master hooks.
- Existing GameShark, Pro Action Replay, and direct-write formats.
- Manual, transactional `.cht` loading with clear filename, count, entry names,
  and `ON`/`OFF` state.
- Up to 640 cheat entries with active-entry and sorted-hook indexes that avoid
  scanning inactive lists in hot paths.
- Configurable hold-to-fast-forward, with unthrottled emulation, paced display
  updates, and audio queue recovery on release.
- Old and new video renderers, multiple scale modes, themes, box art, recent
  ROMs, configurable dynarec policies, and multilingual menus.
- Japanese, English, Simplified Chinese, Traditional Chinese, and Italian UI.
- Optional source-based single-game launcher builder.

## Installation

Copy the release folder to `PSP/GAME/TempGBA4PSP/`, add your own
`gba_bios.bin`, and start the emulator from the PSP Game menu.

```text
PSP/GAME/TempGBA4PSP/
├── EBOOT.PBP
├── exception.prx
└── gba_bios.bin       user supplied
```

See [Installation and upgrade notes](docs/INSTALL.md) for configuration,
fast-forward, and update guidance.

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
stall emulation. Releasing the button immediately restores the selected
frameskip and audio behavior.

## Build

The PSP build requires a current [PSPDev](https://pspdev.github.io/) toolchain
with `psp-config` available in `PATH`.

```sh
make check
make psp-build
```

The equivalent direct command is:

```sh
make -C source
```

Docker users can follow [BUILD-DOCKER.md](BUILD-DOCKER.md). Build outputs are
created under `source/` and are ignored by Git.

### Host tests

`make check` builds the production CodeBreaker module with the host C compiler
and runs deterministic regression vectors for encryption, operation decoding,
conditions, multiline skips, and memory writes. It also verifies that private
runtime data and generated executables are not tracked.

### Release candidate package

After a successful PSP build:

```sh
make package VERSION=20260817-rc1
```

This creates a clean ZIP and SHA-256 checksum in `dist/`. The package contains
only `EBOOT.PBP`, `exception.prx`, installation notes, changelog, and license.
It excludes BIOS files, ROMs, saves, local settings, and cheat databases.

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
| `source/single-game_launcher/` | Optional PSP launcher and desktop builder |
| `scripts/` | Source checks and deterministic release packaging |

## Lineage and credits

- gpSP by Exophase and its contributors.
- gameplaySP kai by takka and contributors.
- TempGBA by Nebuleon, Normmatt, BassAceGold, and the TempGBA community.
- TempGBA4PSP-mod and its contributors.
- libretro/gpsp and other upstream emulator projects whose fixes are credited
  in the inherited source and history.

Detailed historical and asset attribution is retained in
[`source/copyright`](source/copyright). Component licensing is summarized in
[NOTICE.md](NOTICE.md).

## License

The emulator is distributed under GPL-2.0-or-later. See [LICENSE](LICENSE).
The optional desktop launcher builder is GPL-3.0-or-later and carries its own
license and third-party notices.

This project is not affiliated with Nintendo, Sony, or CodeBreaker.
