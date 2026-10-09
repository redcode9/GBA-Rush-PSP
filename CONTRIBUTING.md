# Contributing

Changes should preserve compatibility and performance on real PSP hardware.
Prefer fixes that apply to the GBA or PSP platform generally. Keep any
game-specific workaround explicit and narrowly scoped, and preserve inherited
copyright, license and attribution notices.

## Project layout

| Directory | Contents |
|---|---|
| `source/src/` | Emulator core, renderers, PSP integration and portable link modules |
| `source/prx/` | Kernel bridge and exception-handler sources |
| `source/SDK/` | Required vendored PSP headers and static libraries |
| `source/res/` | Application artwork and editable SVG sources |
| `source/single-game_launcher/` | PSP launcher stub and desktop builder |
| `tests/` | Portable regression tests and original diagnostic ROM sources |
| `models/link/` | TLA+ models, TLC configurations and journal transition definitions |
| `proofs/cbmc/` | Bounded C proof harnesses |
| `scripts/` | Source checks, performance comparison and release packaging |
| `tools/` | Runtime-log analysis and transition generation |
| `docs/` | Link specification, release roadmap and inherited text references |

The maintained documentation is the root README, changelog, this guide,
[link specification](docs/link-cable.md),
[1.4.0 roadmap](docs/ROADMAP-1.4.0.md) and
[launcher README](source/single-game_launcher/tool/README.md).
Update these documents when behavior or acceptance changes. Git history holds
the detailed change record; avoid duplicate plans and progress diaries.
`docs/legacy/` preserves the original Japanese usage notes and acknowledgements.

## Build and verification

Host checks require a C compiler and Python 3. PSP builds require a configured
PSPDev toolchain with `psp-config` in `PATH`. Start with:

```sh
make -j4 check HOST_CC='cc -O1'
make -j4 test-sanitize HOST_CC='cc -O1'
make -j4 psp-build
```

`make check` runs portable module regressions, schedule exploration, Python
analysis-tool tests and source-tree guards. `make test-sanitize` applies
AddressSanitizer and UndefinedBehaviorSanitizer to the host harnesses. Host
tests do not execute PSP firmware, GE, MIPS assembly or physical Wi-Fi.

Link protocol, recovery, queue and timing changes also require:

```sh
make check-cbmc
make check-link-model TLA2TOOLS_JAR=/path/to/tla2tools.jar TLC_WORKERS=2
python3 tools/generate_link_transitions.py --check
```

The bounded-proof gate requires CBMC. The model gate requires Java and a local
TLA+ tools JAR; its four `.cfg` files are versioned verification inputs.
Generated journal files come from `models/link/link_journal_transitions.def`.
Change that definition and regenerate both C and TLA+ outputs together.

Hot-path changes need a focused regression that exercises production code when
practical, plus hardware comparison when performance may change. Thread,
audio, storage, network and timing changes need menu, suspend, resume and exit
checks on PSP. Keep emulator timing, executable-memory invalidation and raster
effects correct while measuring improvements.

Build fresh candidates when compiler or diagnostic flags change; old object
files do not establish the identity of the new build. Keep source revision,
toolchain, flags and EBOOT hashes with the measured results.

## Link development

The [link specification](docs/link-cable.md) defines layer ownership, protocol
semantics, recovery invariants and guest timing requirements. The
[release roadmap](docs/ROADMAP-1.4.0.md) lists the remaining work in dependency
order and gives the physical acceptance thresholds.

Preserve the first failed paired logs and reproduce a defect before selecting
a repair. Do not infer a completed transfer from a socket connection, replayed
packet, missing record or successful process exit. No test may invent a remote
result, repeat a serial IRQ, accept an old generation or wait indefinitely.

Build diagnostic ROMs from `tests/link-rom/` with a configured devkitPro GBA
toolchain. These original sources can be versioned; compiled ROMs stay local.

## Runtime diagnostics

Gameplay diagnostics use separate fixed binary RAM rings for the main and
network threads. Recording must not allocate, format text, take a kernel lock
or touch storage. Aggregate repetitive events and persist versioned,
checksummed snapshots only at safe lifecycle boundaries. Retain at most four
sessions; logging failure must never interrupt emulation.

Decode logs and compare a paired benchmark with:

```sh
python3 tools/decode_runtime_log.py /path/to/PSP/GAME/GBARushPSP/logs
python3 tools/analyze_link_benchmark.py host.gbrlog joiner.gbrlog --json
```

Interpret fields against the production emitters and their schema. Error
domain, internal code, PSP API phase and original PSP return value are separate
facts. A full ring or dropped records means incomplete evidence.

## Hardware performance comparison

Collect at least three reports per build for the same repeatable workload:

```sh
python3 scripts/compare-benchmarks.py \
  --baseline old-1.txt old-2.txt old-3.txt \
  --candidate new-1.txt new-2.txt new-3.txt
```

Record PSP model/firmware, clock, storage, ROM and initial-state hashes,
renderer, scale, VSync, frameskip, audio, fast-forward and actual ROM-cache
capacity. Separate resident and paged workloads. Compare like-for-like runs
and measure normal variation and instrumentation overhead before attributing
a benefit. A comparator pass establishes only its stated non-regression gate.

## Release packaging

After the roadmap gates pass and the final source build is verified:

```sh
make package VERSION=1.4.0
make artifact-smoke VERSION=1.4.0
```

Packaging writes a ZIP and SHA-256 checksum under `dist/` and checks its
manifest. Generated builds and proof output belong in `build/`. Local hardware
evidence belongs in `artifacts/`; all three directories are ignored by Git.

Do not commit BIOS files, game ROMs, saves, savestates, cheat databases, local
configuration or generated executables. Synthetic sources and test vectors
must be original and redistributable. Vendored SDK libraries, artwork and
component license files remain required repository inputs.
