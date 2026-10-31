# Contributing

Builds and tests should keep the emulator usable on real PSP hardware. Include
reproduction steps for bug fixes and preserve the original copyright and license
notices when changing inherited code.

## Build

Install a PSPDev toolchain and put `psp-config` in `PATH`. Host tests require a C
compiler, Python 3 and PSP SDK headers; source checks also require `rg`.
`PSP_SDK_INCLUDE` can override the SDK include directory for host tests.

```sh
make -j4 check HOST_CC='cc -O1'
make -j4 test-sanitize HOST_CC='cc -O1'
make -j4 psp-build
```

Use a clean build after changing compiler or diagnostic flags. Release packages
are created and checked with:

```sh
make package VERSION=1.4.1
make artifact-smoke VERSION=1.4.1
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

## Link development

See the [link cable notes](docs/link-cable.md). `tests/test_link_cable.c`
links two simulated consoles over a simulated lossy network and runs a port of
the Pokemon serial driver plus word-by-word lockstep guests; extend it when
changing `link_cable.c` or `link_net.c`. Changes to packet parsing should also
pass the bounded proof, which requires CBMC. With an mGBA 0.10 build and your
own FireRed files, `make check-rom-link` trades between two real games:

```sh
make check-cbmc
make check-rom-link MGBA_DIR=/path/to/mgba ROM=firered.gba \
  BIOS=gba_bios.bin SAVE=firered.sav
```

Keep paired PSP logs with the build hashes and reproduction steps. Store local
results in `artifacts/`, build output in `build/` and packages in `dist/`.
These directories are ignored. BIOS files, game ROMs, saves and local settings
stay out of Git; diagnostic ROM sources must be redistributable.

Open release pull requests against `develop`, then promote the reviewed source
to `master`. Hardware acceptance remains required for a stable release; an
untested console build must be published as a prerelease.
