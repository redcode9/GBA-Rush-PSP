<p align="center">
  <img src="source/res/PIC1.png" alt="GBA Rush PSP" width="720">
</p>

# GBA Rush PSP

A Game Boy Advance emulator for PlayStation Portable, focused on real-hardware
performance and a clear handheld interface.

GBA Rush PSP is an independent fork of
[andymcca/TempGBA4PSP-mod](https://github.com/andymcca/TempGBA4PSP-mod). It
retains the emulator core, renderers, interface, compatibility work and
launcher developed across gpSP, gpSP Kai, TempGBA and TempGBA4PSP-mod. The
fork is maintained separately and does not imply upstream endorsement.

You must provide your own legally obtained GBA BIOS and ROM dumps. Neither is
included in this repository or its releases.

## Features

- Old and new gpSP renderers with sharp, aspect-correct and adaptive 16:9
  presentation modes.
- Modern, Classic and Custom fast-forward profiles, global defaults and
  optional per-game overrides.
- Full-scale 16-bit audio mixing and optional 2x/3x accelerated audio.
- Plaintext and encrypted CodeBreaker support, including timed master hooks.
- Existing GameShark, Pro Action Replay and direct-write cheat formats.
- Experimental two-PSP GBA cable emulation over local ad-hoc Wi-Fi.
- Suspend and resume recovery for audio, ROM access, saves and video state.
- Box art, recent ROMs, themes, configurable controls and per-game settings.
- Japanese, English, Simplified Chinese, Traditional Chinese and Italian UI.
- On-device performance reports and host regression, sanitizer and package
  checks.

## Install and upgrade

Copy the release folder to `PSP/GAME/GBARushPSP/`, then add your own
`gba_bios.bin`:

```text
PSP/GAME/GBARushPSP/
├── EBOOT.PBP
├── exception.prx
└── gba_bios.bin
```

Start GBA Rush PSP from the PSP Game menu and select a ROM. A `dir.ini` beside
the EBOOT can point ROMs, saves, states, screenshots, cheats and box art to
separate directories.

To upgrade, replace only `EBOOT.PBP` and `exception.prx`. Keep the BIOS, ROMs,
saves, savestates, cheat lists and configuration files. Back up important save
data before an update.

## Fast-forward

Assign **Fast forward** in the controls menu. Triangle is the default.

- **Modern** runs at maximum speed while the button is held, presents at a
  steady 30 fps target and mutes audio.
- **Classic** cycles through Normal, 2x and 3x on each press. It uses fixed
  frame presentation and muted audio.
- **Custom** selects Hold or Toggle, 2x, 3x or Maximum, three presentation
  rates and Silent or Accelerated audio.

Accelerated audio is available at 2x and 3x. If the PSP cannot keep its audio
buffer fed, the current fast-forward session falls back to silence. Opening a
menu, resetting, changing ROM, suspending, exiting or starting a link session
always returns to normal speed.

## Video

`16:9 Auto` fills the PSP display. It starts with the two-pass Sharp path,
falls back to the lighter Crisp path after sustained timing misses and returns
to Sharp after a stable interval. The FPS overlay shows the active level.

Integer and aspect-correct modes remain available when preserving GBA geometry
is more important than filling the display.

## Cheats

Open **Cheats options**, choose **Load cheat file** and select a `.cht` file.
A successful load shows the filename, entry count, names and `ON`/`OFF` state.
Lists are selected manually because codes depend on the exact game region and
revision.

Each entry starts with its format and display name, followed by code lines and
a blank line:

```text
codebreaker Example entry
82000100 1234

direct_v1 Example direct write
22000104 00000001
```

Supported headers are `codebreaker`, `codebreaker_v1`, `codebreaker_v2`, `CB`,
`gameshark_v1`, `gameshark_v2`, `gameshark_v3`, `PAR_v1`, `PAR_v2`, `PAR_v3`,
`direct_v1`, `direct_v2` and `direct_v3`.

For CodeBreaker lists with a timed master, enable the master entry before the
ordinary entries. If enabled masters use different hook addresses, the most
recent master takes ownership and incompatible masters are disabled. Keep a
backup of important saves when testing cheats.

## Link cable

The link implementation for 1.4.0 is experimental. The last documented
two-PSP FireRed trial, on September 16, 2026, failed before any recorded serial
transfer and showed long recovery waits. No paired ten-minute benchmark or
completed trade has passed acceptance. The
[1.4.0 release roadmap](docs/ROADMAP-1.4.0.md) lists the remaining work and the
hardware results required for release; the
[link specification](docs/link-cable.md) defines protocol and timing requirements.

Both PSP systems must use the same protocol-v3 EBOOT with WLAN enabled. A v2
EBOOT is intentionally incompatible and fails during negotiation. Open
**Main menu > Link cable** on both systems: Player 1 creates the session and
Player 2 joins it.

For a controlled test, turn WLAN Power Save and automatic system suspend Off;
the physical WLAN switch must remain On. Before testing a game, connect without
loading a ROM and run the built-in link benchmark from the host. It exercises the same v3 stream and PSP scheduler as
gameplay for ten minutes; repeat it with the PSP-1000 and PSP-2000 roles
reversed. A run passes only with at least 600 completed words/s, a nine-word
burst p99 no greater than 20 ms, no full recovery, no persistent queue growth,
no stream timeout or stale-generation event, no audio underrun, and no
main-thread starvation over 50 ms. A failed run blocks game testing. After
copying one `.gbrlog` from each PSP, validate their common run ID with:

```sh
python3 tools/analyze_link_benchmark.py psp1000.gbrlog psp2000.gbrlog --json
```

The offered load is 1,000 words/s, distinct from the 600 words/s acceptance
floor. The paired analyzer, not an on-screen PASS alone, determines acceptance.
A missing terminal summary is missing evidence, never a pass. If both logs
contain several runs, select one explicitly with `--run-id ID`; run IDs are
not timestamps. Start fresh emulator processes for each role assignment and
copy logs before starting another run.

For gameplay, load a game on each system before creating or joining the
session. Two-player Multiplayer 16-bit transfers use one host `START` and one
joiner `REPLY` per causally available word, with at most one word active.
Duplicates are replayed from bounded state and cannot repeat a GBA register
update or serial interrupt. The v3 multiplayer path does not send the former
`PREPARE`, `ACCEPT`, `COMMIT`, `COMMIT_ACK`, `RELEASE`, or `RELEASE_ACK`
sequence, nor a standalone high-frequency clock stream.

Normal 8-bit, Normal 32-bit and two-player Multiplayer 16-bit transfer paths
are implemented, but their hardware compatibility matrix is incomplete.
Fast-forward and savestates are disabled while a session is open.
Reset, ROM replacement, suspend and exit disconnect cleanly.

The GBA Wireless Adapter, three- or four-player sessions, UART, JoyBus,
GameCube link, multiboot and internet relay are not supported. Games using the
Wireless Adapter need a separate cable mode to work here.

Diagnostic ROM sources for the supported modes are in
`tests/link-rom/`. Build them with a configured devkitPro GBA toolchain by
running `make` in that directory.

Runtime snapshots are kept in `logs/` beside the emulator. Up to four sessions
are retained; gameplay recording stays in RAM and storage is touched only at
safe transitions. `link-failure` records keep the internal domain/code separate
from the original PSP API phase and signed/hexadecimal result. `link-timeout`
identifies receive silence, an active transfer, ahead pacing, pressure,
recovery, shutdown or negotiation; stream and benchmark records include RTT,
burst, retransmission, queue, worker-wake, barrier and audio data. Decode a card
directly with
`python3 tools/decode_runtime_log.py /path/to/GBA_RUSH_PSP/logs`.

The benchmark does not need a ROM and never creates or changes a ROM or save.
Installing a link candidate replaces only the emulator EBOOT and exception
module; keep existing ROMs and saves backed up and untouched. The full paired
benchmark, repeated-trade, save/reload and disconnect procedure is maintained
in the [release acceptance checklist](docs/ROADMAP-1.4.0.md#4-pass-release-acceptance-on-physical-hardware).

## Build and test

See [CONTRIBUTING.md](CONTRIBUTING.md) for the source layout, verification
commands and hardware-test procedure.

A current [PSPDev](https://pspdev.github.io/) toolchain must provide
`psp-config` in `PATH`.

```sh
make check
make test-sanitize
make psp-build
```

`make check` covers the portable cheat, serial, link, lifecycle, menu, video,
configuration, fast-forward, dynarec and audio modules. Generated PSP files are
written under `source/` and ignored by Git.

The official PSPDev container can replace a local toolchain:

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace/source pspdev/pspdev:latest make
```

After a successful candidate build:

```sh
make package VERSION=1.4.0
```

The package command creates a reproducible ZIP and SHA-256 checksum under
`dist/`, then verifies the archive manifest. It never includes BIOS files,
ROMs, saves, settings or cheat databases.

### Performance comparison

On PSP, use **Diagnostics > Start measurement**, play a repeatable section,
stop measurement and save the report. Collect three reports per build, then
run:

```sh
python3 scripts/compare-benchmarks.py \
  --baseline old-1.txt old-2.txt old-3.txt \
  --candidate new-1.txt new-2.txt new-3.txt
```

The comparison checks throughput, frame-time and GE-wait tails, audio
underruns, skipped frames and, when recorded, the measured fast-forward
multiplier.

## Single-game launcher

The optional launcher creates a separate XMB bubble for one ROM. Build its PSP
stub first, then run the desktop tool:

```sh
make launcher-stub
python3 -m pip install -r source/single-game_launcher/tool/requirements.txt
python3 source/single-game_launcher/tool/gui.py
```

See the [launcher README](source/single-game_launcher/tool/README.md) for asset
sizes and required external tools.

## Credits and license

GBA Rush PSP exists because of work by Exophase and gpSP contributors; notaz;
Takka and the gpSP Kai contributors; Nebuleon, Normmatt, BassAceGold and the
TempGBA community; Phoe-nix; andymcca; JxPv2; and every contributor to the
[direct upstream repository](https://github.com/andymcca/TempGBA4PSP-mod).
Relevant fixes from [libretro/gpSP](https://github.com/libretro/gpsp) are
retained with source history and notices.

The emulator is GPL-2.0-or-later; see [LICENSE](LICENSE), [NOTICE](NOTICE),
`source/copyright` and source headers. The launcher builder is
GPL-3.0-or-later and carries separate notices in its directory.

GBA Rush PSP is not affiliated with or endorsed by Nintendo, Sony,
CodeBreaker or the upstream maintainers.
