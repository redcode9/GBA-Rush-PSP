# Hardware results

Every run on real consoles of a test from the
[hardware test catalog](hardware-tests.md), one row each. A row is tied to the
sha256 of the `EBOOT.PBP` it ran, not of the ELF: of the binaries, the release
zip ships only `EBOOT.PBP` and `exception.prx`, and the ELF of a published
build cannot be checked. Rows recorded before builds had a manifest say
`unknown` there, and a row with an `unknown` digest never validates a
release.

The columns:

- Date: the day of the run, as `YYYY-MM-DD`.
- Version: the version of the build, such as `1.4.3`, or `1.4.3 dev` for a
  development build.
- EBOOT sha256: the first 12 hex digits, in lower case, of the sha256 of
  `EBOOT.PBP`, or `unknown`. The full digest is in the release manifest, or
  in the builds table below for a test build.
- Model: `PSP-1000`, `PSP-2000`, `PSP-3000`, `PSP-N1000` (PSP Go) or
  `PSP-E1000` (PSP Street). A link session is one row that names both
  consoles, such as `PSP-2000 + PSP-1000`.
- Firmware/CFW: the system software and custom firmware, or `unknown`.
- Storage: the Memory Stick or internal storage the emulator ran from, or
  `unknown`.
- Test id: an id from the catalog.
- Result: `pass`, `fail` or `partial`, for a run that covered only part of
  the test. Only `pass` counts for a release.
- Minutes: the minutes the run took, or `unknown`.
- Evidence: the decoded log or report the row comes from.

`make check` fails when a row is malformed or names a test id missing from
the catalog, or when a row of the builds table has a short digest that is not
the start of its full one; `python3 scripts/hardware-session.py --check`
checks the same on its own. A cell cannot contain `|`.

## Builds

The test builds that ran on the PSPs, with the full SHA-256 of their
`EBOOT.PBP`. Logs and Diagnostics reports behind each row stay out of Git, in
the `artifacts/hardware/` folder of the main checkout that
`scripts/install-test-build.sh` wrote when it collected them.

| EBOOT.PBP | Full SHA-256 | Built from | Notes |
|---|---|---|---|
| `5dfde160d28b` | `5dfde160d28be296b91890fd8be009c0fb00fb18605bb618bd08b0e91302438f` | `features/state-of-the-art-development/main` 700de75, pspdev v20260801 | Test build with the New core, the Exact renderer on the Media Engine and the built-in BIOS. `exception.prx` 75d7347e9f58…, `media_engine.prx` 10cd22cb758c…. Reports itself as 1.4.3. |
| `a496fe9c6d5c` | `a496fe9c6d5c4caa39188896bad4e441698f2e22bf5540625c0fde5805f9a995` | local integration merge ff93a23 (not on origin): `features/state-of-the-art-development/main` b5cffbf with `wip/new-core` 42dc451, `wip/event-scheduler` 0fb796c, `wip/test-roms` 5644284, `wip/open-bios` ce57645, `wip/media-engine` bfa4ff7 and `wip/hardware-validation` ee34e5d, pspdev v20260801 | Package `GBA-Rush-PSP-test-ff93a23.zip`. `exception.prx` 75d7347e9f58…, `media_engine.prx` 65248c713a0f…. Reports itself as 1.4.3. Installed with `scripts/install-test-build.sh` on the PSP-1000 and the PSP-2000; the SHA-256 read back from both cards is the same. The test ROMs come from the same tree. |
| `58cd5edb20bb` | `58cd5edb20bbf8d8eac198ae7038a42bea3dfed1894bc86ae6ce74178b604069` | local integration e7ff176 (not on origin): the merge ff93a23 above plus `wip/new-core` 8f3f260 (the `pc_high` fix: zeros where nothing is mapped are skipped in one step) and the commit "Name the fields of the BIOS HLE test's bus" (the same change as `wip/open-bios` 5e3e0d5), pspdev v20260801 | Package `GBA-Rush-PSP-test-e7ff176.zip`. `exception.prx` 75d7347e9f58…, `media_engine.prx` 65248c713a0f…, both unchanged. Reports itself as 1.4.3. Installed with `scripts/install-test-build.sh` on the PSP-1000 and the PSP-2000; the SHA-256 read back from both cards is the same (`artifacts/hardware/20261106-214344/PSP_*/installed.sha256`). The test ROMs come from the same tree. |
| `7c4d7b06ec22` | `7c4d7b06ec22473be06ca99908eff01798fa645a57512f9e6249ea81b1764c07` | `features/state-of-the-art-development/main` b5cffbf, pspdev v20260801 | Package `GBA-Rush-PSP-test-main-b5cffbf.zip`, built from main to check that main installs the same on both PSPs. `exception.prx` 75d7347e9f58…, `media_engine.prx` 10cd22cb758c…. Reports itself as 1.4.3. Installed on 2026-11-08 with `scripts/install-test-build.sh` on the PSP-1000 and the PSP-2000 in one run; the SHA-256 read back from both cards is the same (`artifacts/hardware/20261108-140736/PSP_*/installed.sha256`). No session ran on it: the next build went on right after. |
| `e70bc5f8d432` | `e70bc5f8d432cea1e5b0689f928a9ebf3fd30e4a664c4330816defa38fda7a69` | local integration 7908ef0 (not on origin): `features/state-of-the-art-development/main` b5cffbf with `wip/new-core` 5b5de54, `wip/event-scheduler` 44fbf6d, `wip/test-roms` da776c2, `wip/open-bios` f7608cb and `wip/media-engine` 6770e3b, pspdev v20260801 | Package `GBA-Rush-PSP-test-7908ef0.zip`, the build of the sessions from 2026-11-08. `exception.prx` 75d7347e9f58…, `media_engine.prx` 1a4104767a59…. Reports itself as 1.4.3. Installed on 2026-11-08 with `scripts/install-test-build.sh` on the PSP-1000 and the PSP-2000; the SHA-256 read back from both cards is the same (`artifacts/hardware/20261108-144059/PSP_*/installed.sha256`). The test ROMs come from the same tree (`dist/core-rom-7908ef0.gba`, SHA-256 efc33f16b4c5…). |

## Results

| Date | Version | EBOOT sha256 | Model | Firmware/CFW | Storage | Test id | Result | Minutes | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| 2026-10-31 | 1.4.0 | unknown | PSP-2000 + PSP-1000 | unknown | unknown | link-frlg-both-roles | partial | unknown | Notes: first paired run |
| 2026-11-02 | 1.4.3 dev | unknown | PSP-2000 + PSP-1000 | unknown | unknown | link-mksc-race | partial | unknown | Notes: Mario Kart races, 40 fps |
| 2026-11-02 | 1.4.3 dev | unknown | PSP-2000 + PSP-1000 | unknown | unknown | link-mksc-race | partial | unknown | Notes: Mario Kart races, 45.5 fps |
| 2026-11-02 | 1.4.3 dev | unknown | PSP-1000 | unknown | unknown | menu-every-page | fail | unknown | Notes: Performance page crash |
| 2026-11-02 | 1.4.3 dev | unknown | PSP-2000 | unknown | unknown | menu-every-page | fail | unknown | Notes: Performance page crash |
| 2026-11-06 | test build ff93a23 (reports 1.4.3) | a496fe9c6d5c | PSP-1000 | unknown | Memory Stick | core-rom | partial | unknown | `artifacts/hardware/20261106-session1/PSP_1004/GBA/SAV/core-classic.sav`; Notes: test ROM session, Classic core |
| 2026-11-06 | test build ff93a23 (reports 1.4.3) | a496fe9c6d5c | PSP-1000 | unknown | Memory Stick | core-rom | partial | unknown | `artifacts/hardware/20261106-session2/PSP_1004/GBA/SAV/CORE-NEW.sav`; Notes: test ROM session, New core |
| 2026-11-06 | test build ff93a23 (reports 1.4.3) | a496fe9c6d5c | PSP-2000 | unknown | SD card in an adapter | core-rom | partial | unknown | `artifacts/hardware/20261106-session2/PSP_2004/GBA/SAV/CORE-NEW.sav`; Notes: test ROM session, New core |
| 2026-11-06 | test build ff93a23 (reports 1.4.3) | a496fe9c6d5c | PSP-1000 | unknown | Memory Stick | save-rom | partial | unknown | `artifacts/hardware/20261106-session1/PSP_1004/GBA/SAV/save-eeprom512.sav`, `save-eeprom8k.sav`; Notes: test ROM session, EEPROM targets |
| 2026-11-06 | test build ff93a23 (reports 1.4.3) | a496fe9c6d5c | PSP-1000 | unknown | Memory Stick | save-rom | partial | unknown | `artifacts/hardware/20261106-session2/PSP_1004/GBA/SAV/save-sram32.sav`, `save-sram64.sav`, `save-flash64.sav`, `save-flash128.sav`, `save-notag.sav`; Notes: test ROM session, SRAM and Flash targets |
| 2026-11-06 | test build ff93a23 (reports 1.4.3) | a496fe9c6d5c | PSP-1000 | unknown | Memory Stick | save-rom | partial | unknown | `artifacts/hardware/20261106-session2/PSP_1004/GBA/SAV/save-sram32.sav`, `save-flash128.sav`; Notes: test ROM session, bus cases |
| 2026-11-06 | test build ff93a23 (reports 1.4.3) | a496fe9c6d5c | PSP-2000 | unknown | SD card in an adapter | save-rom | partial | unknown | `artifacts/hardware/20261106-session2/PSP_2004/GBA/SAV/save-sram32.sav`, `save-sram64.sav`, `save-flash64.sav`, `save-notag.sav`; Notes: test ROM session, SRAM and Flash targets |
| 2026-11-06 | test build ff93a23 (reports 1.4.3) | a496fe9c6d5c | PSP-2000 | unknown | SD card in an adapter | save-rom | fail | unknown | `artifacts/hardware/20261106-session2/PSP_2004/GBA/SAV/save-flash128.sav`; Notes: test ROM session, flash128 on the PSP-2000 |
| 2026-11-06 | test build ff93a23 (reports 1.4.3) | a496fe9c6d5c | PSP-2000 | unknown | SD card in an adapter | save-rom | partial | unknown | `artifacts/hardware/20261106-session2/PSP_2004/GBA/SAV/save-sram32.sav`, `save-flash128.sav`; Notes: test ROM session, bus cases |
| 2026-11-06 | test build e7ff176 (reports 1.4.3) | 58cd5edb20bb | PSP-1000 | unknown | Memory Stick | core-rom | partial | unknown | `artifacts/hardware/20261106-session3/PSP_1004/GBA/SAV/CORE-NEW.sav`, `logs/`; Notes: built-in BIOS session, core-rom |
| 2026-11-06 | test build e7ff176 (reports 1.4.3) | 58cd5edb20bb | PSP-2000 | unknown | SD card in an adapter | core-rom | partial | unknown | `artifacts/hardware/20261106-session3/PSP_2004/GBA/SAV/CORE-NEW.sav`, `logs/`; Notes: built-in BIOS session, core-rom |
| 2026-11-06 | test build e7ff176 (reports 1.4.3) | 58cd5edb20bb | PSP-1000 | unknown | Memory Stick | boot-open-bios-10 | partial | unknown | `artifacts/hardware/20261106-session3/PSP_1004/logs/session-1.gbrlog`, `GBA/SAV/mariokart_auto-builtin.svs`; Notes: built-in BIOS session, Mario Kart: Super Circuit |
| 2026-11-06 | test build e7ff176 (reports 1.4.3) | 58cd5edb20bb | PSP-2000 | unknown | SD card in an adapter | boot-open-bios-10 | partial | unknown | `artifacts/hardware/20261106-session3/PSP_2004/logs/session-1.gbrlog`, `GBA/SAV/ROSSO_auto-builtin.svs`; Notes: built-in BIOS session, Pokémon FireRed |

## Notes

### First paired run

On 2026-10-31, with the 1.4.0 EBOOT: FireRed with FireRed, a PSP-2000 hosting
a PSP-1000. Both games linked in Pokemon mode and stayed in the Cable Club for
two and a half minutes until the player left, with every frame delivered in
both directions. The consoles stalled for about 40 seconds in total, and about
30 frames, half a second, stayed queued in each direction once the room was
busy. No trade or battle was run, and only one role, so the row is partial.

### Mario Kart races

Two Mario Kart: Super Circuit Multi-Pak races between a PSP-2000 and a
PSP-1000 during the 1.4.3 work: 40 fps on the build before 1.4.3's lockstep
changes and 45.5 fps on the first build with them. They set the PSP
conditions that `make check-rom-link` simulates (see the
[link cable notes](link-cable.md)). Each race ran in one role, and which PSP
hosted was not recorded.

### Performance page crash

During the 1.4.3 work, opening the Performance page crashed both PSPs with a
blue "Reserved instruction" screen. The page's fast-forward callbacks were
nested functions, which the PSP reached through a trampoline on the stack.
1.4.3 moved them to file scope, and the PSP build now fails on any stack
trampoline. 1.4.0 to 1.4.2 have the same callbacks.

### Test ROM session

On 2026-11-06, with the test build ff93a23, official `gba_bios.bin` and Boot
from BIOS off. The firmware was not recorded. The core-rom rows are partial
because the session ran only the test build, while the catalog's pass compares
every case with the previous release; the save-rom rows are partial because
each covers some targets on one model.

- **Classic core,** core-rom (150 cases), PSP-1000: 57 PASS, 92 FAIL, `pc_high`
  HUNG: the first boot ends the emulator (back to the XMB), the second marks it
  HUNG. Same case by case and value by value as PPSSPP with this EBOOT.
- **New core,** core-rom (150 cases), PSP-1000: 132 PASS, 17 FAIL
  (`timer_start_delay`, 11 of the 15 `prefetch_*` cases (all but
  `prefetch_arm_3_*` and `prefetch_thumb_3_*`), `vram_arm_cpu`, `vram_arm_dma`,
  `vram_thumb_cpu`, `vram_thumb_dma`, `stm_vram`); `pc_high` RUNNING, not a
  pass: after the jump the PSP stopped responding (the menu did not open, the
  power switch was needed), but the emulator was still running, since auto-save
  is on and writes the `.sav` 10 frames after the last save write, so it ran at
  least 10 frames after `pc_high` started. Most likely the New core slides
  through unmapped memory, translating a new block every 64 cycles, and runs too
  slowly for the menu: this comes from the code and a PPSSPP run, not from this
  session, which no runtime log covers (the power switch prevents the flush);
  fix under way on `wip/new-core`. Same values as PPSSPP with this EBOOT. On the
  PSP-2000: the same values as the PSP-1000, 132 PASS, 17 FAIL, `pc_high`
  RUNNING (not a pass, as above).
- **EEPROM targets,** save-rom eeprom512 and eeprom8k, Classic core, PSP-1000:
  `read_back` and `write_verify` PASS on both: each reread the generation the
  boot before wrote; `.sav` of 512 bytes and 8 KiB.
- **SRAM and Flash targets,** New core. PSP-1000, sram32, sram64, flash64,
  flash128 and notag: `read_back` and `write_verify` PASS on all five;
  `flash_id` PASS 1B32 (flash64) and 1362 (flash128); `.sav` of 32, 64, 64, 128
  and 32 KiB. PSP-2000, sram32, sram64, flash64 and notag: `read_back` and
  `write_verify` PASS on all four; `flash_id` PASS 1B32 (flash64); `.sav` of
  32, 64, 64 and 32 KiB.
- **flash128 on the PSP-2000,** New core: `flash_id` PASS 1362 and
  `write_verify` PASS, `read_back` FAIL 00005F0E: the image the boot before
  left differs from offset 5F0Eh. Probable cause: the ROM was left while it
  was still writing a generation, which takes long on a 128 KiB Flash, leaving
  a partial image. To be run again, leaving right after GEN changes.
- **Bus cases,** save-rom sram32 and flash128, New core, the same values on the
  PSP-1000 and the PSP-2000: 6 PASS, 6 FAIL: `sram_ldrh_odd` A50000A5,
  `sram_ldrsh_odd` FFFFFFA5, `sram_ldr_1`, `sram_ldr_2` and `sram_ldr_3`
  A5A5A5A5, `flash_ldrh_device` 62000062, the cases `scripts/check-test-roms.sh`
  allows on rom_run.

### Built-in BIOS session

On 2026-11-06, with the test build e7ff176, the New core, no `gba_bios.bin`
(built-in BIOS), Boot from BIOS off and the Video renderer New (not Exact).
The firmware was not recorded.

- **core-rom** (150 cases), PSP-1000: 132 PASS, the same 17 FAIL as with the
  official BIOS (`timer_start_delay`, 11 of the 15 `prefetch_*` cases, the four
  `vram_*` cases, `stm_vram`), `pc_high` HUNG on boot 2. All 29 BIOS cases
  pass, among them the four Div cases, `intrwait_dma0` and the five arithmetic
  cases with the official CRCs, and so do `halt_ime0`, `halt_cpsr_i` and
  `bios_openbus_swi`. Against the official BIOS run only the detail of
  `div_cycles` differs (000011B4 there, 00001505 here, PASS both), besides
  `pc_high`. The PSP no longer stops at `pc_high`: both core-rom sessions ended
  through the menu (runtime log `menu-presence`, then `session-end` with
  `flush=exit`). The user saw the built-in BIOS's boot logo during the "second
  boot": most likely the slide wrapped from FFFFFFFFh to 0, the built-in BIOS
  restarted the cartridge and the ROM marked `pc_high` HUNG in the same
  session. Same case by case and value by value as PPSSPP with the New core,
  the built-in BIOS and this EBOOT. On the PSP-2000: the same values as the
  PSP-1000, 132 PASS, 17 FAIL, `pc_high` HUNG on boot 2; both core-rom
  sessions ended through the menu. Partial for the same reason as in the test
  ROM session.
- **Mario Kart: Super Circuit,** PSP-1000: started (the user said the game
  started, not how far it got). The runtime log has one session of the game,
  on the New core: `rom-open` `AMKP` `core=new` at 12.2 s, the menu
  (`menu-presence`) about 22 s later, `session-end` with `flush=exit` at
  38.1 s. No Classic run is recorded: the log keeps 4 sessions, the card holds
  only 3 since the install, and there is no other `rom-open` of `AMKP`. The
  auto savestate was written as `mariokart_auto-builtin.svs`, apart from the
  official BIOS one. Partial: one game of the ten, and gameplay not confirmed.
- **Pokémon FireRed,** PSP-2000: started (the user said the game started, not
  how far it got). The runtime log has one session of the game, on the New
  core: `rom-open` `BPRI` `core=new` at 11.4 s, the menu (`menu-presence`)
  about 19 s later, `session-end` with `flush=exit` at 33.5 s. No Classic run
  is recorded: the log keeps 4 sessions, the card holds only 3 since the
  install, and there is no other `rom-open` of `BPRI`. The auto savestate was
  written as `ROSSO_auto-builtin.svs`, apart from the official BIOS one.
  Partial: one game of the ten, and gameplay not confirmed.
