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

Use a clean build after changing compiler or diagnostic flags. Release packages
are created and checked with:

```sh
make package VERSION=1.4.3
make artifact-smoke VERSION=1.4.3
```

The optional desktop launcher has its own
[build instructions](https://github.com/redcode9/GBA-Rush-PSP/blob/master/source/single-game_launcher/tool/README.md).

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
in that shape when it can be tested off the console.

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

## Test ROMs

`tests/core-rom` (CPU, interrupt, BIOS and bus cases), `tests/save-rom`
(one target per save chip) and `tests/raster-rom` (HBlank DMA and IRQ cases,
then a picture) are GBA ROMs built from source with
arm-none-eabi-gcc, from the Arm GNU Toolchain or from devkitARM through
`DEVKITARM`; the ROMs never go into Git. `make test-roms` builds them into
`build/test-roms`, runs each on `tests/core/rom_run.c` (the New core's
interpreter with a small GBA around it, not an emulator) and checks the
result; without an ARM compiler it is skipped. `BIOS=gba_bios.bin` runs the
core ROM on your dump of the official BIOS, for the BIOS open bus cases.

Each ROM reports every case on screen and in a signature at the start of
its save chip (`tests/gba-rom/result.h`), which
`python3 tools/decode_test_rom.py game.sav` prints from an emulator's save
file. A case is an entry in the table of `cases_<area>.c`, with hand-written
code in `cases_<area>_asm.s`; a new area is listed in the ROM's `Makefile`
and `source/main.c`. Expected values come from GBATEK or the ARM7TDMI
documentation, never from an emulator. The header logo is left blank, so
boot the ROMs directly: "Boot from BIOS" off, or in mGBA no BIOS or "Skip
BIOS intro".
The BIOS copy and decompression cases take their inputs and expected
outputs from `tests/core-rom/source/bios_vectors.h`, which
`python3 tools/make_bios_vectors.py` writes; run it again after changing
the script (`make test` fails while the file is out of date).

The raster ROM leaves its results on screen for three seconds, then shows a
picture that changes the affine registers and the palette in the middle of
the frame, by HBlank DMA and from an HBlank IRQ. The picture is the same in
every frame, so two screenshots in mGBA must match, and
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

Open release pull requests against `develop`, then promote the reviewed source
to `master`. Hardware acceptance remains required for a stable release; an
untested console build must be published as a prerelease.
