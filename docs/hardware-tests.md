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

## Platform

### clock-every-path

- Models: PSP-1000, second model
- Minutes: 20
- Pass: on each model the decoded log of every path shows a cpu-clock record
  without MISMATCH, and after the Exact renderer has drawn, an me-clock
  record without MISMATCH.
- Steps: with the clock option at 333, play, open and close the menu, go to
  the file browser, load another ROM, suspend and resume, host or join a link
  session and end it, then play a minute with the Exact renderer. Decode
  `logs/` with `tools/decode_runtime_log.py`. Repeat on the other model.

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
