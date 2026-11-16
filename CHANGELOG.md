# Changelog

## Unreleased

### Added

- GBA Rush PSP starts without `gba_bios.bin`. When the file is missing, the
  emulator uses a built-in open-source BIOS (Normmatt's, based on VBA-M, the
  one libretro gpSP also ships) instead of quitting. A `gba_bios.bin` that is
  not a 16 KB GBA BIOS is reported and the built-in BIOS is used instead. A
  valid `gba_bios.bin` beside the EBOOT is still used first and remains the
  more compatible choice.
- Performance > Advanced settings > CPU core: Classic, the dynarec GBA Rush
  has always used, or New, a processor core written from scratch that runs
  each instruction and its cycles as mGBA does and compiles the game's code
  for the PSP. It is experimental and takes effect when a game starts or is
  reset; Classic stays the default. With Show FPS on, the counter ends in
  NEW while the New core runs. Savestates load in either core. The runtime
  log records the core each game starts on, and how many blocks of code the
  New core compiled.
- A third choice for Video > Video renderer, Exact: a renderer written from
  scratch for GBA Rush, which matches mGBA pixel for pixel on every frame of
  the games it is checked with. On custom firmware it draws on the PSP's
  second processor, the Media Engine, through media_engine.prx, which ships
  next to the EBOOT: the main processor only tells it what the game wrote,
  so every frame can be drawn, shown as early as with the other renderers.
  Where the Media Engine cannot be used, as on official firmware or in an
  emulator, Exact draws on the main processor and is slower than New. With
  Show FPS on, the counter ends in ME or CPU to tell which.
- Shortcuts: SELECT+L saves to the current savestate slot and SELECT+R loads
  it, as the quick save and load buttons do, and holding START+SELECT for
  half a second opens the menu, also where HOME does not reach the emulator.
  The hold never quits the game and does not count while A or B is held, so
  A+B+START+SELECT still resets the game; START and SELECT reach the game
  during the hold. A shortcut acts on the press that completes it, and the L
  or R that completes it is kept from the game while it stays held.
  Shortcuts are saved with the settings and with each game's settings;
  settings from 1.4.x start with these defaults. Single-button mappings work
  as before.
- Controls > Turbo config sets, for each rapid-fire button (A, B, L and R),
  the speed (30, 15 or 10 presses per second) and the mode. Hold fires while
  the button is held; Toggle starts firing on one press and stops on the
  next, with a "Turbo A: on" or "Turbo A: off" notice. The settings are
  saved globally and with each game's settings. Settings from 1.4.x start at
  30 presses per second in Hold mode, as before.
- A ROM folder with no games shows "No games found" and the folder to copy
  .gba or .zip files to, in the list and in the carousel; its subfolders
  stay listed.
- On a fresh install the first game shows, for 6 seconds, a notice naming
  both ways into the menu: "HOME: menu, or hold START+SELECT". It comes back
  at each start until the emulator has been closed once from the Exit menu,
  which saves the settings file.
- In the diagnostic build, Diagnostics > Pin a test toast keeps a notice on
  screen until the game is reset, to measure what drawing notices costs. It
  is available once a game is loaded.
- While a ROM loads, the loading popup shows the file name, whether it is
  reading the ROM or unpacking the ZIP, the KiB read so far and a bar that
  follows the whole read, for `.zip` archives and for loose ROMs that fit in
  memory. A loose ROM too large to load whole, which is read in pages while
  the game runs, still shows only "Now Loading...".
- The runtime log records how long each ROM load takes, stage by stage:
  opening the file, reading or unpacking the ROM, loading the game's
  settings, searching its backup ID and reading its `.sav`. Loads that fail
  are recorded too, and `tools/decode_runtime_log.py` prints the times.
- Before a link session starts, the game's save is written and a copy of it
  is kept as `<rom>.sav.link1`; the two copies before it move to `.link2`
  and `.link3`. If the save cannot be written or copied, the session does
  not start and the Link menu says why.
- Link > Restore pre-link save puts one of those copies back, chosen by its
  date and time. The current save is kept as `<rom>.sav.bak`, and the game
  restarts on the restored save. The item is not available during a
  session.
- The runtime log records how every `.sav` was read (its size, the save
  chip chosen and whether they disagree), how long every save write took,
  whether it worked and how many tries it needed, how long the save copy
  made before a link session took and whether it worked, and which save
  fields of a damaged savestate were repaired on load.
- A new Video > Display mode, 2x integer (crop): every GBA pixel becomes an
  exact 2x2 block and rows 12-147 of the game fill the screen. It always uses
  nearest sampling, whatever the screen filter says.
- The main menu shows a live status next to Save and Load (the state slots
  in use and whether the AUTO slot exists), Game (how many cheats are on) and
  Link Cable (the other console's game code, or waiting).
- The Save and Load page shows thumbnails of all 11 slots (0-9 and AUTO)
  with the selected slot framed. They appear one per frame as the page opens.
- Diagnostics > Log file name encoding, to find out how a firmware stores
  file names that are not ASCII: it goes through the `name-probe` folder
  beside the EBOOT and writes each name to the runtime log as the PSP
  reports it, with the long name and 8.3 alias the firmware keeps for it and
  whether the file opens by each of them, then shows how many names it
  logged. `tools/decode_runtime_log.py` shows each name in hex and as
  UTF-8, Shift-JIS and GBK text.
- The Diagnostics page shows the version and the build id, an id of the
  exact source the EBOOT was built from, ending in `-dirty` when that source
  had uncommitted changes. Performance reports, runtime logs and the
  release's build manifest record the same id, so every report can be
  traced to its build. Runtime logs move to format 6 and performance
  reports to format 7 to carry it. The log decoder still reads the older
  formats, and `scripts/compare-benchmarks.py` still compares format 6
  reports with format 7 ones but refuses reports written before 1.4.3,
  whose 64-bit counters are not valid.
- Every release comes with a build manifest (the commit, build id, compiler,
  flags and the SHA-256 of each built file), a `SHA256SUMS` file and the
  corresponding source code, `GBA-Rush-PSP-<version>-source.zip`, made from
  the release tag.
- A diagnostic build for reproducing bugs, packaged as
  `GBA-Rush-PSP-<version>-diag.zip`: it shows as "GBA Rush PSP [DIAG]" with
  its own icons and installs in `PSP/GAME/GBARushPSP-DIAG`, so it keeps its
  own settings, saves and logs and never touches the regular install. The
  regular build contains none of its diagnostic code.
- The runtime log records the memory when GBA Rush PSP starts, each time a
  game is loaded or reset, before a link session starts and when a
  performance measurement starts and stops: the size of the program, the
  free heap and its largest block, the free system memory and its largest
  block, and the size of the ROM buffer. Performance reports give the lowest
  free heap, largest heap block and largest system memory block measured
  during the capture (`heap-free-low`, `heap-largest-low`,
  `kernel-free-low`).
- The diagnostic build can hold back memory from startup, to imitate a PSP
  with less memory in an emulator: write the amount in KiB (at most 65536)
  in `memory-hold.txt` beside the EBOOT. Beside each performance report it
  also saves the emulator's last 256 memory allocations and frees as
  `perf-<time>-alloc.txt`.
- The diagnostic build can run a test with nobody at the console: a
  `diag-autorun.txt` beside the EBOOT that names the build's id starts a
  ROM, can replay an input trace for a number of frames and quit, and
  writes the outcome to `diag-autorun.result` and to the runtime log. A
  file written for another build is ignored. The same file can instead
  show colour bars through every screen scale and filter, to photograph
  the screen, or put the wireless link on a simulated bad network that
  loses, delays and duplicates packets, which the runtime log records.
- In the diagnostic build, replaying an input trace records a hash of the
  emulated machine for every frame and saves them beside the trace as a
  `.gbsh` file, so `tools/state_hash_diff.py` can show the first frame and
  the part of the machine where two replays differ.
- On GitHub, bug, game and link reports open as forms that ask for the
  version, the PSP model and firmware, the steps and the `logs/` folder, and
  warn not to attach ROMs, BIOS files or saves that are not your own.
- `docs/PERFORMANCE.md` explains how to compare the speed of two builds and
  holds a table of measured baselines for each PSP model and game, generated
  by `scripts/perf-table.py` from the reports archived under `benchmarks/`.
  No baselines are archived yet.
- `scripts/compare-benchmarks.py --baseline-dir DIR --candidate-dir DIR`
  compares folders of reports: the reports of each game, revision and PSP
  model are compared as a group of their own, and the result shows PASS or
  FAIL for every group. A group the baseline has but the candidate lacks is
  reported as missing and fails the comparison.
- Diagnostics > Unbound benchmark loads the current save state slot and
  runs 3600 frames as fast as the PSP can: nothing waits for the VBlank,
  VSync or the audio, and the game gets no input. The capture then stops by
  itself, its report is saved and the game returns to its normal speed. A
  link refuses the benchmark. The diagnostic build adds two rows that run it
  with rendering or audio off, and replays the input trace it holds instead
  of giving no input, ending the run with the trace; the report then holds
  a hash of the game's state at the end, which two runs of the same trace
  must share.
- Performance reports also record each frame's work time without the waits,
  the frames that were late, over budget or run during a lockstep link, the
  local time the capture started, the time of the main emulation phases on
  one frame in 11, and how often the emulation thread was preempted while
  working or waiting. `docs/PERFORMANCE.md` explains every key.
- `scripts/compare-benchmarks.py --order-check` tells whether the runs of the
  two builds alternated, `--strict` fails when they did not, and
  `--claim 'throughput>=+3'` says whether a claim written before the runs
  holds. `docs/PERFORMANCE.md` describes the measurement protocol they serve.

### Changed

- The game stays visible full screen behind the menu, in its display mode,
  under a veil of the theme's background colour.
- The Italian menu writes accented letters (è, più, modalità) instead of a
  backtick after the vowel or no accent at all.
- A savestate loads only with the BIOS it was saved with, `gba_bios.bin` or
  the built-in one; otherwise a message says which one it needs, instead of
  loading a game that would crash. Savestates from earlier versions count as
  saved with `gba_bios.bin`. GBA Rush PSP 1.4.3 and earlier do not check
  this, so a savestate made with the built-in BIOS can crash there.
- With the built-in BIOS the automatic savestate is `<rom>_auto-builtin.svs`,
  so it never overwrites the `<rom>_auto.svs` made with `gba_bios.bin`.
- The `bios_rom_hack_39` and `bios_rom_hack_2C` options of `game_config.txt`
  apply only to `gba_bios.bin`.
- The release ZIP extracts at the root of the Memory Stick (`ms0:`, or
  `ef0:` on a PSP Go): it installs or upgrades `PSP/GAME/GBARushPSP/` in one
  step, with the documentation in its `docs/` folder. The ZIP never contains
  saves, savestates, settings, `dir.ini`, ROMs or a BIOS, so an upgrade keeps
  them.
- The ROM browser reopens in the view last chosen with SELECT, list or
  carousel, also after a restart; the choice is saved with the other
  settings when the emulator exits. The savestate and cheat browsers still
  open in the list, and settings from 1.4.x start in the list.
- With the analog stick as d-pad, a direction is released a little nearer
  the centre than where it is pressed, so a stick resting near the
  sensitivity threshold no longer flickers. Tilt games are unchanged.
- A game with no settings of its own starts in the display mode last chosen
  in the Video menu, no longer in the one of the game played before it.
- The menu and the file browser run at the configured CPU clock (333 MHz by
  default) instead of 222 MHz: after a ROM loads (at 333 MHz) or a link
  session ends, the PSP returns to the configured clock instead of 222 MHz,
  and it sets that clock again when it resumes from sleep. The runtime log
  records the requested clock and the CPU and bus clocks the PSP reports each
  time the clock is set and, when the Exact renderer draws on the Media
  Engine, the Media Engine clock it measured under each requested clock.
- A `.sav` whose size does not match its save chip is written back at the
  chip's size the next time the game saves: SRAM 32 or 64 KB, Flash 64 or
  128 KB, EEPROM 512 bytes or 8 KB. The file as it was is kept once, as
  `<rom>.sav.orig`.
- In the diagnostic build, starting an input recording also saves
  a savestate beside the trace in `traces/`, and replaying the trace loads
  it first, so a recording replays from where it began even after the
  console is restarted, without loading a state by hand. A trace whose
  savestate is missing or has changed is refused. On games with a
  real-time clock the replay warns that it may not match the recording.
  Traces recorded by earlier versions still replay. A recording does not
  start during a link session.
- A stable release is published only after the hardware tests it requires
  have passed, with the EBOOT being released, on a PSP-1000 and on a second
  model, and after at least 7 days as a prerelease (3 days for a fix of
  lost data). Its release notes list what was validated, on which consoles,
  and its known limits; a build not yet tested on hardware is published as
  a prerelease.
- A stable release that requires the `perf-baseline-5` hardware test is
  published only when its PSP-1000 reports, archived under `benchmarks/`
  with the build id of its release candidate, pass
  `scripts/compare-benchmarks.py` against those of the newest earlier
  release there for every game; a game without reports fails.
- `scripts/compare-benchmarks.py` takes three or more reports per build, and
  the two builds may have different counts. PASS and FAIL work as before; for
  each metric it also shows the median, lowest and highest run of both builds
  and says whether the runs show a clear difference or how many runs per
  build would be needed to tell.
- `scripts/compare-benchmarks.py` also fails a build whose frame-time p99
  rises more than 5%, whose frame-time maximum is above 1.5 times the
  baseline's in two or more runs, or, with reports that have the new keys,
  whose work-time p95 rises more than 5% or whose frames over 1.5 times the
  budget per minute rise by more than max(1, 20%). A frame-time p95 rise of
  exactly 5%, and a throughput or fast-forward drop of exactly 2%, now pass.
  It refuses reports captured during a lockstep link, unless `--allow-link`
  is given and every run of both builds was in lockstep, and never compares
  the unbound benchmark with a capture the player ran.

### Fixed

- A 64 KB `.sav` of an SRAM game is no longer written back as a 1-byte file
  the first time the game saves. Saves already cut by earlier versions
  cannot be recovered.
- A `.sav` that exists but cannot be read is never written over. The
  emulator asks whether to read it again or to play without saving; while
  playing without saving, nothing is written to the file until another ROM
  is loaded.
- A `.sav` larger than 128 KB no longer overruns memory: its first 128 KB
  are loaded.
- The save type comes from the ROM's save library string, and the file size
  no longer overrides it, so a `.sav` of another size, such as a 128 KB file
  for a game with 64 KB Flash, loads with the chip the game expects.
- A damaged savestate can no longer make the save memory read or write
  outside its buffer.
- Load State File on the running game's `<rom>_auto.svs` loads that state
  instead of first overwriting it with the game in progress.
- Putting the PSP to sleep while the resume question is open no longer
  replaces the AUTO state.
- Games with an 8 KB EEPROM that save before ever reading it no longer lose
  the save.
- A power cut while the `.sav` is being written no longer loses the save:
  the previous one is kept as `<rom>.sav.bak` and restored at the next
  start.
- A save that cannot be written, for example on a full Memory Stick, is no
  longer lost: the game keeps it in memory and tries again, the menu opens
  once to say so, and before exiting, suspending or changing game the menu
  asks whether to try again.
- Saving a state no longer needs 512 KB of free memory: when memory runs
  out, the state is saved without its preview picture instead of quitting.
  A power cut while a state is being saved keeps the previous state, as a
  `.svs.bak` file.
- Pressing the power switch now always parks the game safely for standby,
  even when the PSP reports only the switch; if standby does not follow
  within 2 seconds, the game resumes with its sound and ROM. The runtime log
  records what the PSP reports at each power event.
- Closing the menu no longer waits for every button to be released, so a
  button still held never delays standby. A button held as the menu closes
  reaches neither the game nor the hotkeys until it is released.
- A damaged or truncated `.zip` no longer hangs the loader or starts a broken
  game: the ROM's size and CRC32 are checked, and the file browser says the
  ZIP file is damaged or incomplete. An encrypted `.zip`, or one whose first
  file is not a `.gba`, `.agb` or `.bin` ROM, gets the same message.
- A `.zip` written in streaming mode, with its sizes after the data, now
  loads when its ROM is compressed (deflated) and those sizes are not in
  the ZIP64 format. Any other `.zip` written that way, such as one with an
  uncompressed (stored) ROM, gets the damaged or incomplete message, like
  an encrypted one.
- A `.gba` file that cannot be read in full now fails to load instead of
  starting the game from part of the ROM.
- The emulator no longer shows an error at startup when `dir.ini` is
  missing: `dir.ini` is optional. Without it, ROMs are listed from the
  `roms/` folder next to `EBOOT.PBP` when that folder exists, otherwise from
  the emulator's folder; saves, savestates, cheats and the other files stay
  in the emulator's folder as before, so existing saves are still found. A
  `dir.ini` that names a folder that does not exist still shows its warning.
- Custom menu colours chosen with the colour picker are kept when the
  emulator restarts, with or without `dir.ini`; every start used to reset
  them to the selected preset. Choosing a preset or Default theme in the
  menu still discards them.
- `gba_bios.bin` is no longer listed as a game in the ROM browser.
- Two rapid-fire buttons held together now both fire; before, one stayed
  held and the other never pressed. Rapid fire follows the game's frames, so
  it no longer speeds up during link-cable waits. Opening the menu, loading
  a state or putting the PSP to sleep stops rapid fire; a button still held
  fires again after it is pressed again.

### Known issues

- Settings saved by this version cannot be read by 1.4.x: going back to
  1.4.x resets every setting to its default, and 1.4.x overwrites the
  settings file when it exits.

### Fixed

- The messages shown after loading a cheat file are translated into every
  menu language; the Japanese and Chinese menus showed them in English.

## 1.4.3 - 2026-11-02

### Fixed

- Opening the Performance page could crash the emulator with a blue
  "Reserved instruction" screen. The build now fails instead of producing
  code that runs from the stack.
- Performance reports saved on a PSP showed meaningless numbers for frame
  counts, durations and every other 64-bit counter.
- A Pokemon trade could stop for good at "Please wait" when the joining
  PSP froze for a moment or the radio dropped out just as the trade screen
  opened. A burst of delayed frames reached the joining game closer together
  than the hosting game had sent them, so it threw away the first part of
  the other player's party. The joining PSP now keeps the hosting game's
  spacing between frames.

### Changed

- Linked games other than Pokemon, such as a Mario Kart: Super Circuit race,
  need far fewer datagrams, each of which costs a PSP close to a millisecond
  of processor time. The PSP that receives transfers learns how far apart
  the other one's transfers come and keeps running on its own in between,
  so the other PSP reports its clock four times less often; replies that
  only serve as a check and acknowledgements ride with other datagrams; and
  a reply is announced only once the game has stopped changing it.
- The PSP that clocks transfers no longer waits for the real reply when the
  other PSP's announcement of it arrives a moment after the transfer
  started; it finishes the transfer as soon as the announcement is in.
  Replies are no longer announced during quick bursts of transfers, where
  an announcement cannot arrive in time and is more likely to be wrong.
- The PSP that receives transfers learns again every few seconds how far
  apart they come, so the burst of transfers that sets up a Mario Kart race
  no longer holds it back for the whole race.
- Once announcements have proved right, a real reply always rides with the
  next announcement, and both PSPs report their clock and acknowledge less
  often, so a Mario Kart race needs about one datagram per frame in each
  direction.
- During such a link the emulator pauses for 1 ms instead of 0.2 ms after
  each datagram, long enough for the PSP network stack to send it at once
  instead of at the end of the frame.
- A frame that runs late during such a link no longer also pauses for 2 ms
  when the emulator already slept for the link during it; below full speed
  that pause cost every frame.
- A PSP no longer sends its unacknowledged messages again before the other
  PSP has had time to acknowledge them along with its next datagram.
- During such a link the emulator draws at least every other frame,
  whatever the frameskip setting, which is left untouched and applies again
  once the link closes. AUTO frameskip skipped up to nine frames in a row
  trying to catch up with real time, which the other PSP's pace never
  allows, and showed a few frames a second.
- The session log records the GBA frames run and drawn during a link and
  the time spent waiting for VBlank.
- A PSP waiting for the other PSP's reply to a transfer asks it every 10 ms
  to answer at once, so a datagram lost on the radio holds the game for
  about a round trip instead of until the next datagram.
- During such a link the emulator runs at the lowest priority a program can
  use, so every part of the PSP network stack runs before it.
- The session log records the priority the emulator runs at during such a
  link.

## 1.4.2 - 2026-11-01

### Fixed

- Games that report a link error as soon as the cable goes quiet for a
  moment, such as Mario Kart: Super Circuit, now stay connected over Wi-Fi.
  The console that receives transfers follows the sender's timing as on a
  real cable instead of running ahead while the sender waits for replies.
- Linked games other than Pokemon, such as a Mario Kart: Super Circuit race,
  run faster over Wi-Fi. The two PSPs used to take turns, each emulating its
  frame while the other waited for its clock, because the PSP network stack
  only sends while the emulator sleeps. During such a link the emulator now
  pauses briefly after each datagram and runs at a lower priority, and
  the PSP that clocks the transfers stops waiting for replies the other PSP
  has announced in advance, once hundreds of announcements in a row have
  matched the real replies.
- The session log also records the shortest round trip, the time spent
  letting the network stack send, the transfers completed with an announced
  reply and any announcement that turned out wrong.

## 1.4.1 - 2026-10-31

### Fixed

- The Cable Club no longer falls further behind after every stall. In the
  Cable Club rooms the two PSPs now wait for each other's frames like a real
  cable, so the delay stays near one Wi-Fi round trip plus a few frames
  instead of growing to half a second or more.
- Pokemon games no longer pace on the other PSP's game clock, which made both
  consoles pause for each other after every radio dropout.
- The session log counts the datagrams handed to the PSP network stack, those
  it refused and incoming ones dropped.

### Changed

- The link protocol changed: both PSPs need GBA Rush PSP 1.4.1.

## 1.4.0 - 2026-10-31

### Added

- Modern, Classic and Custom fast-forward profiles, per-game overrides and
  optional accelerated audio at 2x/3x.
- Fast-forward measurements in performance reports and benchmark comparisons.

### Changed

- Audio mixing retains 16-bit precision with integer timing and rounded scaling.
- Dynarec branch resolution uses an iterative worklist to bound host stack use.
- Mosaic rendering avoids per-pixel division and fixes rotated bitmap sampling.
- Performance menus separate common settings from advanced core options.
- The link cable is rebuilt around two techniques chosen per pair of games.
  Pokemon Ruby, Sapphire, Emerald, FireRed and LeafGreen, in any combination,
  emulate the wire locally on each PSP and relay only the games' link frames,
  so trades and battles run at full speed. The host pairs the frames so both
  games see identical data. Every other game exchanges each serial word.
- A new reliable channel repeats unacknowledged messages in every datagram,
  and a console only waits when it runs more than four frames ahead of the
  other one.
- The ad-hoc worker only moves datagrams; the network stack and the worker
  run above the emulator thread, which stopped reception during gameplay.
- Input, the HOME menu and power events stay available while the link holds
  the game.
- The link menu shows the chosen technique and the round trip. The no-ROM
  benchmark is removed; a game must be loaded before connecting.
- Runtime logs move to format 5 with the new link events.

### Fixed

- ROMs loaded from the startup menu now run without resetting the state restored
  there. Direct launches offer the auto-savestate prompt; the browser restores
  HOME handling, and the prompt waits for the selecting button to be released.
- Aligned IWRAM stack stores invalidate translated-code tags for SMOL overlays,
  following the upstream fix while preserving stores below the stack pointer.
- Renderer wrappers rebuild when their included sources change.
- Translation headers remain UTF-8 and compile as GBK for the menu glyphs;
  filename clipping and scrolling preserve GBK pairs and Shift-JIS kana.
- Evicted ROM pages invalidate the cached read pointer. Failed and short reads
  clear unread bytes and remain uncached for retry; partial final pages are padded.
- Stopped performance reports retain the original ROM and settings. DMA counters
  exclude SRAM from ROM transfers, and clock wrap no longer drops frame samples.
- Fast-forward ends at menus, reset, ROM changes, link setup, suspend and exit;
  accelerated audio falls back to silence when output cannot stay stable.
- Version 1.3 settings migrate without discarding unrelated configuration.

### Removed

- The previous link implementation (session, journal, recovery coordinator,
  transport lanes) with its TLA+ models, proofs and paired benchmark.

### Known issues

- In Pokemon mode, frames that queue up while one console stalls are not
  caught up afterwards, so the Cable Club can lag by about half a second
  until the players leave the room.

## 1.3.0 - 2026-08-22

### Added

- A low-overhead performance monitor with live diagnostics and bounded reports.
- Diagnostic input recording and replay for development builds.
- Contextual help for settings and a benchmark comparison gate.

### Changed

- Menus are grouped by task and retain their selection when returning from a
  child screen.
- Renderer dispatch is selected once per frame instead of once per scanline.
- Audio output, suspend, resume, and shutdown use a consistent PSP SRC-channel
  lifecycle.
- Global and per-game configurations use versioned validation and recoverable
  writes while preserving existing settings.

### Fixed

- Long menus scroll within a fixed viewport instead of overlapping the guide
  and button footer.
- Invalid configuration fields no longer discard unrelated valid settings.

## 1.2.0 - 2026-08-21

### Added

- `16:9 Auto`, which starts with a sharp two-pass fullscreen renderer and
  falls back to single-pass nearest scaling after sustained timing misses.
- A manual `16:9 Sharp` mode and `SHARP`/`CRISP` status in the FPS overlay.

### Changed

- Fullscreen presentation now overlaps GE work with the remaining GBA VBlank.
- Fullscreen paths no longer clear pixels that the next frame fully replaces.
- New configurations default to `16:9 Auto`; existing scale values are kept.

### Fixed

- Video draw-buffer, viewport, texture, and filter state is rebuilt after PSP
  resume before presentation restarts.

## 1.1.0 - 2026-08-21

### Added

- Two-player GBA link cable emulation over PSP ad-hoc Wi-Fi.
- Normal 8-bit, Normal 32-bit, and Multiplayer 16-bit serial modes with
  hardware-derived completion timing and serial interrupts.
- Create, join, status, peer information, and disconnect controls in all five
  interface languages.
- A framed, versioned link protocol with ROM metadata, transfer barriers,
  heartbeats, timeouts, and clean disconnect reasons.
- Redistributable diagnostic ROM sources and deterministic two-core stress
  coverage for variable network delay and malformed traffic.

### Changed

- Savestates and fast-forward are disabled while a link session is open.
- Reset, ROM replacement, suspend, sleep, and exit now close an active link
  session before changing emulator state.
- ROM loading records exact size and CRC32 for peer diagnostics without
  requiring identical ROM files.

### Fixed

- PSP resume no longer waits for suspended audio samples to drain.
- Pending audio, save, ROM, and link recovery is completed in the foreground
  after every suspend cycle.
- Sound and ad-hoc worker shutdown paths no longer wait without a deadline.
- Ad-hoc command and event queues now enforce publication order between the
  emulator and network threads.
- Oversized transport events are rejected instead of being emitted without a
  payload.

### Limitations

- This release supports two PSP systems and GBA cable modes only.
- GBA Wireless Adapter, UART, General Purpose, JoyBus, GameCube link,
  multiboot, and internet relay modes are not supported.
- Broad compatibility and performance testing across two physical PSP systems
  and the commercial game catalog has not yet been completed.

## 1.0.0 - 2026-08-18

### Added

- Plaintext and encrypted CodeBreaker cheat support.
- Timed CodeBreaker master hooks in the dynamic recompiler.
- Configurable hold-to-fast-forward action with paced display updates.
- Host regression tests for CodeBreaker decoding and every supported operation
  family.
- Repeatable source checks and release packaging commands.

### Changed

- Rebranded the independent fork as GBA Rush PSP with a new PSP-native visual
  identity and clearer upstream attribution.
- Cheat loading is manual, transactional, and reports precise errors.
- Cheat pages show stable entry numbers, names, states, filename, and count.
- Runtime cheat work scales with active entries and configured hooks.
- Fast-forward drops queued audio and restores normal pacing immediately when
  released.
- PSP build flags now distinguish C, C++, assembler, and vendored zlib sources.
- The EBOOT uses the stable `GBA Rush PSP` title.

### Fixed

- Multiline CodeBreaker payloads no longer look like hook or seed operations.
- Conflicting timed masters no longer dispatch ordinary codes more than once.
- Signed 32-bit CodeBreaker addition and conditional skip lengths.
- Audio suspension when entering the menu during fast-forward.
- Several compiler warnings and two malformed translated strings.
