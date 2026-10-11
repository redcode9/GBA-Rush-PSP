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
make package VERSION=1.4.0
make artifact-smoke VERSION=1.4.0
```

The optional desktop launcher has its own
[build instructions](source/single-game_launcher/tool/README.md).

## Source tree

The emulator and PSP integration are in `source/src/`; `source/prx/` contains
helper modules. Keep the vendored SDK libraries in `source/SDK/` and the artwork
in `source/res/`. Portable tests live in `tests/`, checks and packaging scripts
in `scripts/`. Original usage notes are retained in `docs/legacy/`.

Link models are in `models/link/`, C proof harnesses in `proofs/cbmc/` and
log-analysis utilities in `tools/`.

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

See the [link implementation notes](docs/link-cable.md) and the
[remaining work for 1.4.0](docs/ROADMAP-1.4.0.md). Run these additional checks
for protocol, queue or recovery changes:

```sh
make check-cbmc
make check-link-model TLA2TOOLS_JAR=/path/to/tla2tools.jar TLC_WORKERS=2
python3 tools/generate_link_transitions.py --check
```

CBMC checks require CBMC; model checks require Java and the TLA+ tools JAR.
The four model `.cfg` files are source inputs. Journal transitions are generated
from `models/link/link_journal_transitions.def`.

Keep paired PSP logs with the build hashes and reproduction steps. Store local
results in `artifacts/`, build output in `build/` and packages in `dist/`.
These directories are ignored. BIOS files, game ROMs, saves and local settings
stay out of Git; diagnostic ROM sources must be redistributable.

Open release pull requests against `develop`, then promote the reviewed source
to `master`. Hardware acceptance remains required for a stable release; an
untested console build must be published as a prerelease.
