# Hardware tests

The tests a GBA Rush PSP build goes through on real consoles, each under a
stable id. Every row of [HARDWARE-RESULTS.md](HARDWARE-RESULTS.md) names the id
of the test it ran, and `make check` fails when a row names an id missing
here. Keep the ids as they are: recorded rows refer to them.

A release lists the tests it needs in `docs/releases/<version>.txt`, on a line
such as `tests: save-sram-roundtrip, sleep-auto-20x`. Before a session, print
the sheet to follow at the console, with the steps, the models and the
estimated time:

```sh
python3 scripts/hardware-session.py --release 1.4.4
python3 scripts/hardware-session.py --tests save-sram-roundtrip,sleep-auto-20x
python3 scripts/hardware-session.py --check
```

The last command checks this catalog and the results table, as `make check`
does.

Each test has four fields:

- Models: the model classes it must pass on. `PSP-1000` is the PSP-1000,
  with the least memory; `second model` is any other one: PSP-2000,
  PSP-3000, PSP Go (PSP-N1000) or PSP Street (PSP-E1000).
- Minutes: the estimated time for the whole test, on every model it needs.
- Pass: what must hold for a `pass` row.
- Steps: what to do. Decode the `logs/` folder of every PSP used with
  `tools/decode_runtime_log.py` and keep the decoded log as the row's
  evidence.

## Saves

### save-sram-roundtrip

- Models: PSP-1000, second model
- Minutes: 15
- Pass: on each model the game loads its own save after the restart, and the
  `.sav` keeps the size and the CRC it had after the in-game save.
- Steps: start an SRAM game with an existing `.sav`, save in the game, note
  the size and CRC of the `.sav`, exit to the file browser, start the game
  again and load the save. Repeat on the other model.

### save-powerloss-10x

- Models: PSP-1000, second model
- Minutes: 40
- Pass: after each of the 10 cuts on each model the `.sav` is neither empty
  nor short, and the game loads it; at most the last save written before the
  cut is lost.
- Steps: run a game that keeps writing its save, cut the power 10 times in
  the middle of a write (on the PSP-1000 remove the battery, on a PSP Go hold
  POWER), restart and check the `.sav` each time.

### save-rom

- Models: PSP-1000, second model
- Minutes: 30
- Pass: on each model every target of the save test ROM saves and rereads
  its own generation (`read_back` and `write_verify` pass, and `flash_id` on
  the Flash targets), each `.sav` has its chip's size, and no bus case that
  is green on the previous release is red on the candidate.
- Steps: build the targets of `tests/save-rom`. On each model start each
  target, wait for GEN to change, leave it right after, start it again, and
  decode its `.sav` with `tools/decode_test_rom.py`.

## Link

### link-frlg-both-roles

- Models: PSP-1000, second model
- Minutes: 40
- Pass: in both roles the trades and the battle complete, both games save
  and reload with the traded Pokemon, and neither decoded log shows a link
  error.
- Steps: FireRed on one PSP and LeafGreen on the other. Enter the Cable Club,
  trade 3 times and fight one battle, save in the game on both PSPs and
  reload. Repeat with the other PSP as host.

### link-ruby-fr

- Models: PSP-1000, second model
- Minutes: 40
- Pass: in both roles the trades and the battle complete, both games save
  and reload with the traded Pokemon, and neither decoded log shows a link
  error.
- Steps: Ruby on one PSP and FireRed on the other, with the Network Machine
  on One Island already repaired in the FireRed save. Enter the Cable Club,
  trade 3 times and fight one battle, save in the game on both PSPs and
  reload. Repeat with the other PSP as host.

### link-mksc-race

- Models: PSP-1000, second model
- Minutes: 20
- Pass: the race finishes on both PSPs in both roles without a
  communication error.
- Steps: Mario Kart: Super Circuit on both PSPs, which links in lockstep
  mode. Run one Multi-Pak race with Show FPS on, then another with the other
  PSP as host. Note the frame rate of each race from the decoded logs.

### link-mixed-pair

- Models: PSP-1000, second model
- Minutes: 45
- Pass: the trades and the races complete in both roles. Otherwise the row
  is a fail and its notes say what the mixed pair loses.
- Steps: the previous release on one PSP and the candidate on the other.
  Trade 3 times in the Cable Club and run one Mario Kart: Super Circuit race
  in each role, the candidate hosting first.

## Power

### sleep-auto-20x

- Models: PSP-1000, second model
- Minutes: 30
- Pass: after each of the 20 suspends the game resumes, and the AUTO
  savestate loads with the right preview.
- Steps: during play, suspend with the POWER switch, resume and check that
  the game goes on and the AUTO savestate loads. Repeat 20 times on each
  model.

## Startup

### boot-open-bios-10

- Models: PSP-1000, second model
- Minutes: 60
- Pass: each of the 10 games reaches gameplay with the built-in BIOS, and
  the decoded log reports it.
- Steps: remove `gba_bios.bin` from the emulator's folder, start 10
  different games and play each until gameplay.

### boot-no-dir-ini

- Models: PSP-1000, second model
- Minutes: 30
- Pass: on each model, with `gba_bios.bin` and no `dir.ini`, the first
  start reaches the file browser without any dialog, in `roms/` when that
  folder exists and in the EBOOT's folder otherwise; the saves and
  savestates of a 1.4.2 install without `dir.ini` are still found; a
  `dir.ini` naming a folder that does not exist still shows its warning;
  colours picked in Custom colors are still active after a restart, with
  and without a `dir.ini`.
- Steps: with no `PSP/GAME/GBARushPSP/` folder, extract the candidate's
  release ZIP at the root, so there is no `dir.ini` and no `tempgba.cfg`,
  and add `gba_bios.bin` and a `roms/` folder with a few ROMs beside the
  EBOOT, none named `game.gba` (that one starts without the browser).
  Start the emulator: the browser must list the ROMs of `roms/`. Back out
  to the menu, change two colours in Interface > Custom colors and quit
  from the menu. Over USB rename `roms/` to `roms-off/` and start again:
  the browser must list `docs/` and `roms-off/`, in the picked colours.
  Add a `dir.ini` holding only `rom_directory = missing`, with no
  `missing/` folder, and start again: the warning must call
  `rom_directory` invalid, and the colours must still be the picked ones.
  Then delete `PSP/GAME/GBARushPSP/`, install 1.4.2 there with
  `gba_bios.bin` and a game with a battery save beside the EBOOT and no
  `dir.ini`, and on 1.4.2 save in the game and save a savestate. Extract
  the candidate's release ZIP over it and start that game: it must load
  its save, and the savestate must be listed and load. Do all of this on
  each model.

### install-ms-root

- Models: PSP-1000, second model
- Minutes: 30
- Pass: on each model, once the release ZIP is extracted at the root of
  the storage, the XMB shows GBA Rush PSP and it starts to the file
  browser; once the ZIP is extracted over a 1.4.3 install, its saves,
  savestates and configuration files are unchanged and in use: the game
  loads its save, the savestate loads and the settings changed on 1.4.3
  are still set.
- Steps: the second model must be a PSP Go (PSP-N1000), with the ZIP
  extracted at the root of its internal storage `ef0:`; on the PSP-1000
  it goes to the root of the Memory Stick `ms0:`. Install by hand as a
  player does, not with `scripts/install-test-build.sh`: connect the PSP
  over USB and extract the release ZIP, not the `-diag` one, with the
  drive itself as the destination (Windows' Extract All proposes a folder
  named after the ZIP), replacing the files already there. For the clean
  install, start from storage with no `PSP/GAME/GBARushPSP/` folder. For
  the upgrade, use a 1.4.3 install in `PSP/GAME/GBARushPSP/` with
  `gba_bios.bin`, a `dir.ini`, settings changed in the menu, and a game
  with a battery save and a savestate made on 1.4.3. Before extracting,
  note the size and modification time of its `.sav`, `.svs`, `.cfg` and
  `dir.ini` files; after, they must be the same. Then start the game.

## Menu

### menu-every-page

- Models: PSP-1000, second model
- Minutes: 15
- Pass: every page and option opens and returns to the menu without a crash.
- Steps: with a game running, open every page of the menu and change every
  option at least once, then do the same from the file browser without a
  game.

### browser-carousel-500

- Models: PSP-1000
- Minutes: 40
- Pass: the ROM browser opens in the last view used, both after leaving the
  menu and opening it again and after a restart; the savestate and cheat
  browsers open in the list; switching view writes no file to the Memory
  Stick but the covers' `.raw` caches; with 500 ROMs the median of the
  candidate's 3 carousel opens, counted in frames of a 60 fps video, is at
  most 6 frames (0.1 s) above the median of 1.4.3's.
- Steps: put the same 500 ROMs and their covers in the ROM and cover
  folders of each build, sharing a folder only where dir.ini gives it an
  absolute path (copying them is not in the minutes). Each build's ROM
  folder holds the 500 ROMs and nothing else, with no subfolder, so both
  carousels open on the first ROM; 1.4.3 reads ROMs from its own folder
  unless dir.ini's `rom_directory` names another, so give it that line.
  Time with no game loaded: back out of the browser that opens at startup
  and open the ROM browser with the menu's Load game (L). On 1.4.3 press
  SELECT for the carousel; on the candidate, after choosing the carousel
  once, the browser opens straight into it. Before timing, open the
  carousel once on each build without timing it and without moving the
  cursor, so every visible cover has its `.raw` cache, and check that both
  builds show the same five items with the first ROM in the centre. Then
  open it 3 times on each build, alternating the two, and film each open
  with a 60 fps video that shows the screen and the L and SELECT buttons.
  A stopwatch is not enough: it adds a reaction time at each visual end,
  two on 1.4.3 and one on the candidate. Count frames on the video: on the
  candidate, from the frame where L goes down to the first frame with
  every visible cover drawn; on 1.4.3, the frames from L going down to the
  first frame of the list, plus the frames from SELECT going down to the
  first frame of the carousel with every visible cover drawn. Write both
  sets of 3 frame counts and their medians in the row's notes. On the
  candidate, choose the carousel and load a game from it, which leaves the
  menu (with no game loaded the menu cannot be left). Open the menu again
  and the ROM browser with L: it must be in carousel. Back out and open
  the savestate and cheat browsers: they must start in the list. Quit from
  the menu and start the emulator again: the browser that opens at startup
  must be in carousel. For the Memory Stick, start the candidate with no
  game, wait for the browser's clock to change minute, note it and switch
  view several times, then hold the POWER switch up for at least 3 seconds
  until the PSP turns off (switched on again, it shows the XMB, not the
  emulator). A short slide only puts it to sleep, and HOME > Exit or the
  menu's exit write the config. Over USB, look only in the emulator's
  folder and the folders its dir.ini names, and leave out the files the PC
  itself writes, such as `.DS_Store`: no file there outside `logs/`, other
  than the covers' `.raw` caches, may have a modification time at or after
  the noted minute.

## Performance

### perf-baseline-5

- Models: PSP-1000, second model
- Minutes: 120
- Pass: `scripts/compare-benchmarks.py` reports no failure for any of the
  5 games on either model.
- Steps: for each of 5 games, take 3 measurements with Diagnostics > Start
  measurement on the previous release and 3 on the candidate, alternating
  the two builds, with the same save or state, renderer, clocks and
  settings. Compare each game's two sets of reports with
  `scripts/compare-benchmarks.py`.

## Core

### core-rom

- Models: PSP-1000, second model
- Minutes: 20
- Pass: no case that is green on the previous release is red on the
  candidate, on PSP-1000 and on the second model.
- Steps: run the core test ROM on the previous release and on the
  candidate, on each model, and compare the results of every case.

### suite-terzi

- Models: PSP-1000, second model
- Minutes: 40
- Pass: no test that is green on the previous release is red on the
  candidate, on PSP-1000 and on the second model.
- Steps: run the third-party test suites the repository pins on the
  previous release and on the candidate, on each model, and compare the
  results of every test.
