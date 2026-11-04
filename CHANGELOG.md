# Changelog

## Unreleased

### Added

- GBA Rush PSP starts without `gba_bios.bin`. When the file is missing, the
  emulator uses a built-in open-source BIOS (Normmatt's, based on VBA-M, the
  one libretro gpSP also ships) instead of quitting. A `gba_bios.bin` that is
  not a 16 KB GBA BIOS is reported and the built-in BIOS is used instead. A
  valid `gba_bios.bin` beside the EBOOT is still used first and remains the
  more compatible choice. Its Div and DivArm functions return what the
  official BIOS returns, including the signed remainder, and never hang on a
  division by zero; its IntrWait also ends on DMA, keypad and Game Pak
  interrupts.
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

### Changed

- A savestate loads only with the BIOS it was saved with, `gba_bios.bin` or
  the built-in one; otherwise a message says which one it needs, instead of
  loading a game that would crash. Savestates from earlier versions count as
  saved with `gba_bios.bin`. GBA Rush PSP 1.4.3 and earlier do not check
  this, so a savestate made with the built-in BIOS can crash there.
- With the built-in BIOS the automatic savestate is `<rom>_auto-builtin.svs`,
  so it never overwrites the `<rom>_auto.svs` made with `gba_bios.bin`.
- The `bios_rom_hack_39` and `bios_rom_hack_2C` options of `game_config.txt`
  apply only to `gba_bios.bin`.

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
