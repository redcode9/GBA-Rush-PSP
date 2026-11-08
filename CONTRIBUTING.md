# Contributing

Builds and tests should keep the emulator usable on real PSP hardware. Include
reproduction steps for bug fixes and preserve the original copyright and license
notices when changing inherited code.

## Preparing a machine

One command puts every tool the checks use in `ext/` (ignored by Git), at
versions pinned by sha256 or Git commit, on macOS arm64 or Linux x86_64:

```sh
sh scripts/setup-dev.sh            # --dry-run lists each step first
```

It links a pspdev or arm-none-eabi-gcc already on the machine, and the
Unicorn named by `UNICORN_DIR`, instead of downloading it (otherwise it
builds Unicorn with the PSP's Allegrex instructions), and prints the
`PSPDEV` and `PATH` lines to add to your shell. `make check-core`,
`make check-render`, `make check-rom-link` and `make check-eboot` then
find mGBA, Unicorn, the CPU test suites and PPSSPP's headless runner by
themselves; your BIOS, ROMs and saves are named by the variables each
script lists. `make check-core-quick` and `make check-render-quick` are
the short runs for every pull request.

## Build

Install a PSPDev toolchain (or run `scripts/setup-dev.sh`) and put
`psp-config` in `PATH`. Host tests require a C compiler, Python 3 and PSP
SDK headers.
`PSP_SDK_INCLUDE` can override the SDK include directory for host tests.

```sh
make -j4 check HOST_CC='cc -O1'
make -j4 test-sanitize HOST_CC='cc -O1'
make -j4 psp-build
```

Use a clean build after changing compiler flags. Release packages are created
and checked with:

```sh
make package VERSION=1.4.3
make artifact-smoke VERSION=1.4.3
```

CI (`.github/workflows/check.yml`) runs on every push, tag and pull request
in the images of `tools/ci/Dockerfile`, pinned by digest: `make check`,
`make test-sanitize`, `make check-cbmc`, both build profiles and
`make package` under a local `ci-<sha>` tag, keeping the EBOOT and its
manifest as artifacts. A tag also runs `make artifact-smoke` and checks that
a rebuild gives the manifest's build id; a tag without a suffix must pass
`make release-gate CHANNEL=stable` on the manifest of its newest rc,
downloaded from that rc's GitHub prerelease. The job summary shows whether
two consecutive release builds gave the same EBOOT and, on a stable tag,
whether the rebuild gave the rc's. To run a step as CI does:

```sh
docker build --target psp -t gba-rush-ci tools/ci
docker run --rm -v "$PWD:/workspace" -w /workspace gba-rush-ci make check
```

The optional desktop launcher has its own
[build instructions](https://github.com/redcode9/GBA-Rush-PSP/blob/master/source/single-game_launcher/tool/README.md).

### Build profiles

`psp-build`, `package` and `artifact-smoke` use the release profile, the EBOOT
players get, unless given `PROFILE=diagnostic`. The diagnostic profile adds
the features that `source/src/build_profile.h` switches on (today the input
trace; `DIAGNOSTIC_CORE_PROFILE=1` adds the New core's profile), is titled
"GBA Rush PSP [DIAG]" with its own icon, and is packaged as
`dist/GBA-Rush-PSP-<version>-diag.zip`, with its manifest and
`dist/SHA256SUMS-diag`, for the folder `PSP/GAME/GBARushPSP-DIAG/`:

```sh
make -j4 psp-build PROFILE=diagnostic
make package VERSION=1.4.3 PROFILE=diagnostic
```

Switching profile rebuilds every object. A release build given an
`ENABLE_DIAGNOSTIC_*` macro stops with an `#error`, and
`scripts/check-profiles.sh` (part of `make check`) refuses those macros
anywhere but `build_profile.h`. `DIAGNOSTIC_INPUT_TRACE=1`, the old name of
`PROFILE=diagnostic`, still works with a warning until the next release.

### Reproducing a player's bug with the diagnostic build

The emulator keeps its files in the folder its EBOOT starts from, so the
diagnostic build in `PSP/GAME/GBARushPSP-DIAG/` has its own settings, saves
and logs and leaves `PSP/GAME/GBARushPSP/` alone. To reproduce a bug, copy the
player's files from their `GBARushPSP/` into `GBARushPSP-DIAG/`:

- settings: `tempgba.cfg`, `tempgba_theme.cfg`, `game_config.txt` and the
  game's `<rom>.cfg`;
- saves: the game's `.sav` and `.svs` files, and `gba_bios.bin` if they have
  one;
- logs: the `logs/` folder, to read their sessions next to the new ones with
  `tools/decode_runtime_log.py`. Keep a copy on the PC: each new session
  renames the copied ones one slot down and deletes the oldest.

Copy a `dir.ini` only if its paths are relative (no `ms0:`), together with the
folders it names: a full path would point the diagnostic build at the player's
own folders.

## Source tree

The emulator and PSP integration are in `source/src/`; `source/prx/` contains
helper modules. Keep the vendored SDK libraries in `source/SDK/` and the artwork
in `source/res/`. Portable tests live in `tests/`, checks and packaging scripts
in `scripts/`. Original usage notes are retained in `docs/legacy/`.

C proof harnesses are in `proofs/cbmc/` and the runtime log decoder is in
`tools/`.

Every emulator file includes `source/src/common.h`, which pulls in the SDK
and all module headers. Each module explains its role at the top of its
header or source file; the main ones are:

| Area | Files |
| --- | --- |
| Entry and scheduler | `main.c`: entry point, the `update_gba` hardware scheduler, frame pacing, power callbacks |
| CPU, Classic | `cpu.c` finds, translates and caches ARM/Thumb blocks (there is no interpreter); `mips_emit.h` emits the MIPS code; `mips_stub.S` holds the runtime stubs and memory handlers that translated code calls |
| CPU, New | `arm_decode.c` turns ARM/Thumb code into the IR of `arm_ir.h`, `arm_interp.c` caches and interprets it, `arm_mips.c` compiles it to MIPS; `arm_gba.c` runs that core in the emulator. `make check-core` tests it against SingleStepTests and mGBA |
| Memory and I/O | `memory.c`: memory map, I/O registers, DMA, ROM paging, save memory, RTC; `gba_sio.c`: serial port |
| Video | `video_dispatch.c` runs `video_cc.cc` (default) or `video.c` (old renderer), each built through its `*_wrap` file; `psp_video.c` presents frames and draws the menu |
| Sound | `sound.c`, with `sound_math.c`, `fast_forward_audio.c` and `link_audio_wait.c` |
| Input | `input.c`; `input_trace.c` records and replays keys for diagnostics |
| Menu and settings | `gui.c`, `menu_*.c`, `config_model.c`, `config_store.c`, `message.c` with the translations in `text/` |
| Link cable | `link_cable.c`, `link_net.c`, `psp_adhoc.c`, `adhoc_peer.c`; see the [link cable notes](docs/link-cable.md) |
| Diagnostics | `runtime_log*.c`, `perf_*.c`, `exception.c` (crash screen) |

`zlib/`, `gbkfont.c` and `shinonomefont.c` are vendored, and so is the
built-in BIOS: `open_bios.h` holds its image, and `source/bios/` (kept byte for
byte, see its README) its source. Modules with a host
test, such as the `*_model`, `*_policy`, `*_math` and `*_store` files,
`gba_sio.c`, `link_cable.c` and `link_net.c`, avoid PSP headers; keep new logic
in that shape when it can be tested off the console. `make check` fails when
a `source/src/*.c` that includes neither `common.h` nor a PSP header has no
`tests/test_<module>.c` registered as the Makefile header explains;
`scripts/check-module-tests.sh` lists the few exceptions and why.

## Upstream

[libretro/gpsp](https://github.com/libretro/gpsp) still develops the gpSP core
this tree descends from. Give one day a month to its review: update a clone of
it, run `sh scripts/upstream-review.sh <clone>`, record a decision for each
commit it lists in the [upstream register](docs/UPSTREAM.md), then set the
register's last reviewed commit to the one the script reached and its last
review to that day's date. `sh scripts/upstream-review.sh --check-age` warns
when the last review is more than 45 days old.

## Testing changes

Add a focused regression for a reproduced bug where practical. `make check`
runs host tests and source checks; sanitizer tests cover host memory errors.
PSP rendering, MIPS assembly, radio behavior and power callbacks need testing
on the console as well.

For performance changes, compare at least three runs per build with the same
ROM, initial save or state, renderer, clocks and settings:

```sh
python3 scripts/compare-benchmarks.py \
  --baseline old-1.txt old-2.txt old-3.txt \
  --candidate new-1.txt new-2.txt new-3.txt
```

Record the PSP model, firmware, storage, ROM hash and actual ROM-cache capacity.
Test menu transitions, suspend, resume and exit when changing threads, audio,
networking or file access.

A bug fix shows that its regression test catches the bug. `make
prove-regression REF=<tag> TEST=<name>` builds the C test `TEST` (for example
`test_link_net`) from your checkout against the `source/` of an older tag or
commit that has the bug, and runs it both plain and under ASan and UBSan. The
last line reads FAIL-AS-EXPECTED when a run fails or a sanitizer reports an
error there, PASSES-ON-REF when the test does not catch the bug and
NOT-APPLICABLE when it does not build there. Paste the output into the pull
request.

## Test ROMs

`tests/core-rom` (CPU, interrupt, BIOS and bus cases), `tests/save-rom`
(one target per save chip) and `tests/raster-rom` (HBlank DMA and IRQ cases,
then a picture) are GBA ROMs built from source with
arm-none-eabi-gcc, from the Arm GNU Toolchain or from devkitARM through
`DEVKITARM`; the ROMs never go into Git. `make test-roms` builds them into
`build/test-roms`, runs each on `tests/core/rom_run.c` (the New core's
interpreter with a small GBA around it, not an emulator) and checks the
result, then runs them again in mGBA 0.10 (`tests/core/mgba_run.c`, with
the mGBA of `scripts/setup-dev.sh` or `MGBA_DIR`), the reference, where
only the cases `scripts/check-test-roms.sh` lists with mGBA's reason may
fail; without an ARM compiler it is skipped. `BIOS=gba_bios.bin` runs the
core ROM on your dump of the official BIOS, for the BIOS open bus cases.

Each ROM reports every case on screen and in a signature at the start of
its save chip (`tests/gba-rom/result.h`), which
`python3 tools/decode_test_rom.py game.sav` prints from an emulator's save
file. A case is an entry in the table of `cases_<area>.c`, with hand-written
code in `cases_<area>_asm.s`; a new area is listed in the ROM's `Makefile`
and `source/main.c`. Expected values come from GBATEK or the ARM7TDMI
documentation; only a timing those leave open takes mGBA's measurement, and
the case's comment says so. The header logo is left blank, so
boot the ROMs directly: "Boot from BIOS" off, or in mGBA no BIOS or "Skip
BIOS intro".
The BIOS copy and decompression cases take their inputs and expected
outputs from `tests/core-rom/source/bios_vectors.h`, which
`python3 tools/make_bios_vectors.py` writes; run it again after changing
the script (`make test` fails while the file is out of date).

The raster ROM leaves its results on screen for three seconds, then shows a
picture that changes the affine registers and the palette in the middle of
the frame, by HBlank DMA and from an HBlank IRQ. The picture is the same in
every frame, so in mGBA a run one frame longer must draw the same picture
(`make test-roms` checks it), and
`MGBA_DIR=... RENDER_ROMS=build/test-roms/raster-rom.gba:tests/render/raster.txt make check-render`
compares GBA Rush's renderer with mGBA's on it. `rom_run` has no PPU, HBlank
DMA or HBlank IRQ, so in `make test-roms` the raster cases fail and only the
run itself is checked.

## Link development

See the [link cable notes](docs/link-cable.md). `tests/test_link_cable.c`
links two simulated consoles over a simulated lossy network and runs a port of
the Pokemon serial driver plus word-by-word lockstep guests; extend it when
changing `link_cable.c` or `link_net.c`. Changes to packet parsing should also
pass the bounded proof, which requires CBMC. With an mGBA 0.10 build (from
`scripts/setup-dev.sh` or `MGBA_DIR`) and your own FireRed files,
`make check-rom-link` runs the harness in `tests/rom_link/` to trade between
two real games and, with `MKSC` set to a Mario Kart: Super Circuit ROM
(European release), to race two of them in lockstep mode:

```sh
make check-cbmc
make check-rom-link ROM=firered.gba BIOS=gba_bios.bin SAVE=firered.sav \
  MKSC=mariokart.gba
```

Keep paired PSP logs with the build hashes and reproduction steps. Store local
results in `artifacts/`, build output in `build/` and packages in `dist/`.
These directories are ignored. BIOS dumps (`gba_bios.bin`), game ROMs, saves
and local settings stay out of Git; diagnostic ROM sources must be
redistributable.

## Releasing

Open release pull requests against `develop`, then promote the reviewed source
to `master`. Hardware acceptance remains required for a stable release; an
untested console build must be published as a prerelease.

`docs/releases/<version>.txt` says what a release needs: the ids of the
[hardware tests](docs/hardware-tests.md) it requires, whether it fixes lost
data (`hotfix-data`), its known limits and, when the link code changed, the
link status. `docs/releases/link-compat.txt` holds the link facts that the
protocol constants of each tag do not show. An rc already carries the final
version in `source/`: tag it with `git tag -a 1.4.4-rc.1` and package that
tag, `make package VERSION=1.4.4-rc.1`. The stable release republishes that
package rather than a new build, so both gates read the manifest of the
newest `<version>-rc.N` tag, here `dist/GBA-Rush-PSP-1.4.4-rc.1.manifest.json`,
and the hardware results must name its `EBOOT.PBP`. Then check the candidate
and write `dist/RELEASE-NOTES-<version>.md`:

```sh
make release-gate VERSION=1.4.4 CHANNEL=prerelease
make release-gate VERSION=1.4.4 CHANNEL=stable
```

Both channels fail when a commit message since the previous release holds a
word listed, as a SHA-256, in `scripts/forbidden-words.sha256`; `make check`
runs `scripts/check-names.py` on the tracked files and file names the same
way. Otherwise a prerelease always passes. Its notes list the required tests
that its `EBOOT.PBP` already passed, with their models, and under "Not
validated" the model classes each test still needs; with no results yet, that
is every test.
A stable release needs, for the `EBOOT.PBP` in its manifest, a `pass` row in
[the hardware results](docs/HARDWARE-RESULTS.md) for every required test on
every model class the test asks for; 7 days since the newest
`<version>-rc.N` tag, or 72 hours with `hotfix-data: yes` (make the rc tags
with `git tag -a`: a lightweight tag has only its commit's date, so the gate
refuses it); no change under
`source/` since that tag; and, when `link_net.*`, `link_cable.*`, `gba_sio.*`,
`adhoc_peer.*` or `psp_adhoc.c` changed since the previous release, a
`link: wire-unchanged`, `wire-extended` (with a `mixed-pair:` line saying what
a pair with an earlier release loses) or `bump` line. A change of
`LINK_NET_VERSION` or `LINK_CABLE_PROTOCOL` needs `link: bump` and a passed
link test.
The notes do not replace the CHANGELOG, which stays hand-written.
