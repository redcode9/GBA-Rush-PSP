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

### save-read-matrix

- Models: PSP-1000, second model
- Minutes: 120
- Pass: on each model every start of the matrix loads the expected save, and
  the decoded log has one save-load event per start, and one more per retry,
  each with the expected result. A `.sav` changes size only to go back to its
  chip's size, and always with `<rom>.sav.orig` beside it. No `.sav` is
  written after a failed read. In the unreadable case Yes to "Try again?"
  reads it again, and after "Play without saving?" and exiting the file is
  as it was.
- Steps: four games, one each with a Flash 128 KiB save (such as Pokemon
  FireRed), a Flash 64 KiB, an SRAM and an EEPROM save. Run each with its
  own `.sav` and with a 128 KiB `.sav` written by libretro gpSP, and the SRAM
  game also with a 64 KiB `.sav`. Before each case put that `.sav` in the
  saves folder and remove the files an earlier case left beside it
  (`docs/saves.md` lists them). Start the game, check that the save loads,
  save in the game and exit GBA Rush PSP; note the size and CRC of the
  `.sav` and whether `<rom>.sav.orig` exists. `logs/` keeps only the last 4
  runs of the emulator: copy it off after every 4 cases at most. Unreadable
  case: move one game's `.sav` away, make an empty directory named
  `<rom>.sav` in the saves folder, start the game, answer Yes to "Try
  again?" once, then No, then Yes to "Play without saving?", save in the
  game and exit; the directory must keep its date and stay empty. If the
  game starts without the question, exit without saving and note it: that
  model reads the directory, and the case needs a fault injected in a
  diagnostic build. The case of a `.sav` that does not stat while the saves
  folder does not open needs another model, such as a PSP Go keeping the
  saves on its Memory Stick Micro, or an injected fault: on the PSP-1000 and
  the PSP-2000 the ROM is read from the same card just before the save, and
  a retry counts a missing file as unreadable anyway.

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

## Menu

### menu-every-page

- Models: PSP-1000, second model
- Minutes: 15
- Pass: every page and option opens and returns to the menu without a crash.
- Steps: with a game running, open every page of the menu and change every
  option at least once, then do the same from the file browser without a
  game.

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
