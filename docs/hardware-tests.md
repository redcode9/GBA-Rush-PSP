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
- Minutes: 130
- Pass: on each model every start of the matrix loads the expected save, and
  the decoded log has one save-load event per start, and one more per retry,
  each with the expected result. A `.sav` changes size only to go back to its
  chip's size, and always with `<rom>.sav.orig` beside it. No `.sav` is
  written after a failed read. In the unreadable case Yes to "Try again?"
  reads it again, and after "Play without saving?" and exiting the file is
  as it was. In the card-removed case "Try again?" appears and the log shows
  save-load failed, never not-found, then ok once the card is back.
- Steps: four games, one each with a Flash 128 KiB save (such as Pokemon
  FireRed), a Flash 64 KiB, an SRAM and an EEPROM save. Run each with its
  own `.sav` and with a 128 KiB `.sav` written by libretro gpSP, and the SRAM
  game also with a 64 KiB `.sav`. Before each case put that `.sav` in the
  saves folder and remove the files an earlier case left beside it
  (`docs/saves.md` lists them). Answer No whenever "Load auto-savestate?"
  appears: that state is the previous case's. Start the game, check that the
  save loads, save in the game and exit GBA Rush PSP; note the size and CRC
  of the `.sav` and whether `<rom>.sav.orig` exists. `logs/` keeps only the
  last 4 runs of the emulator: copy it off after every 4 cases at most.
  Unreadable case: move one game's `.sav` away, make an empty directory
  named `<rom>.sav` in the saves folder, start the game, answer Yes to "Try
  again?" once, then No, then Yes to "Play without saving?", save in the
  game and exit; the directory must keep its date and stay empty. If the
  game starts without the question, exit without saving and record the row
  as partial with a note: that model reads the directory, and the save
  fault for a diagnostic build (F-137) is not written yet. Card-removed
  case, a `.sav` that does not stat while the saves folder does not open:
  copy one game's `.sav` beside it as `<rom>.sav.link1`, start the game
  with Update backup on AUTO and wait a few seconds, so no write is
  pending. Open Link cable > Restore pre-link save; at "Restore save of
  ...? Game restarts." take the Memory Stick out and answer Yes: "The save
  file cannot be read. Try again?" must appear. Put the card back, wait a
  few seconds and answer Yes, again if it asks again. "The save could not
  be restored." follows and the game goes on with its own save. Exit
  without saving in the game: the `.sav` keeps its size, CRC and date.

### save-eeprom8k-wfirst

- Models: PSP-1000, second model
- Minutes: 30
- Pass: on the candidate, on each model, the first start of
  `save-eeprom8k_wfirst` with no save files lists no failed case on screen,
  the `.sav` it leaves is 8192 bytes, and `tools/decode_test_rom.py` prints
  write_first PASS 0000003F and read_back SKIP for it. read_back PASS means
  that start found an earlier save, so the run does not count.
- Steps: build the save test ROM with `make test-roms`, which needs an ARM
  compiler (`tests/gba-rom.mk`) and writes
  `build/test-roms/save-eeprom8k_wfirst.gba`. On each model run it on
  release 1.4.2 first, then on the candidate. Install 1.4.2 beside the
  test build with `sh scripts/install-test-build.sh --folder GBARush-142
  --rom build/test-roms/save-eeprom8k_wfirst.gba
  dist/GBA-Rush-PSP-1.4.2.zip`, and the candidate as usual with the same
  `--rom`. Each run: from that build's saves folder delete
  `save-eeprom8k_wfirst.sav` and every file whose name starts with it
  (`.sav.tmp`, `.sav.bak`, `.sav.orig`, `.sav.link1` to `.link3`;
  `docs/saves.md` lists them), since the candidate brings a `.bak` back as
  the `.sav`. Start the ROM, answer No if "Load auto-savestate?"
  appears, note what the screen lists, wait for GEN to change and exit GBA
  Rush PSP right after. Run the same install command again: it archives
  the `.sav` into `artifacts/hardware/`, where you check its size and
  decode it with `tools/decode_test_rom.py`. On 1.4.2 the screen is
  expected to list write_first FAIL, with FFFFFFFF when the block reads
  back blank or DEAD0000 when the chip never reports the write done:
  record that run as a `fail` row of the 1.4.2 EBOOT (`5e6e02004b2c`),
  which shows that the test catches the bug.

### save-powerloss-100x

- Models: PSP-1000, second model
- Minutes: 300
- Pass: on each model, every start after one of the 100 cuts finds a whole
  generation: its screen shows SKIP 0 and does not list read_back as FAIL
  or HUNG, and the first GEN it shows is at least the last GEN seen before
  the cut, so at most the generation being written at the cut is lost. At
  the end the `.sav` is 131072 bytes and `tools/decode_test_rom.py` prints
  read_back PASS for it.
- Steps: build the save test ROM with `make test-roms`, which needs an ARM
  compiler (`tests/gba-rom.mk`), and install the candidate with
  `sh scripts/install-test-build.sh --rom
  build/test-roms/save-flash128.gba` and its zip. That target has the
  largest chip, so the longest write, and writes a new generation every
  second, which GBA Rush PSP writes to the `.sav` with Update backup on
  AUTO. Before the first start delete `save-flash128.sav` and every file
  whose name starts with it from the saves folder; that start shows
  read_back SKIP and does not count. Start the ROM, wait for GEN to change
  at least once, so the cases of the start are done, then cut the power at
  a random moment and note the last GEN seen: on the PSP-1000 take the
  battery out with no charger plugged in, on a PSP Go (the preferred
  second model) hold POWER, on other models take the battery out. Put the
  power back, start GBA Rush PSP and the ROM, answer No if "Load
  auto-savestate?" appears, and note the first GEN shown, the SKIP count
  and any case listed: each start reads the last generation saved and
  shows the next one, which it writes. Repeat 100 times on each model. At
  the end exit GBA Rush PSP from the menu once GEN has changed and run the
  same install command again: it archives the `.sav` and `logs/` into
  `artifacts/hardware/`, where you check the size of the `.sav` and decode
  it with `tools/decode_test_rom.py`. `logs/` keeps only the last 4 runs,
  and a run cut by the power holds what was logged up to the game's start,
  its save-load event included.

### save-card-full

- Models: PSP-1000
- Minutes: 20
- Pass: with the Memory Stick full, a save in the game opens the menu with
  "The save file could not be written. The game keeps it and tries again."
  once, and no later failed try shows it again. Once space is freed, the
  next try writes the save: afterwards `<rom>.sav.bak` has the size and
  CRC the `.sav` had before the card was filled, and the `.sav` has the
  same size and the new save. The decoded log shows save-write events with
  a result other than ok, then one with result=ok.
- Steps: a game that saves from its own menu, with Update backup on AUTO,
  as `docs/saves.md` describes under "How a save is written". Start it,
  save two savestates from the Savestate menu, save in the game and exit
  GBA Rush PSP. On a PC note the size and CRC of `<rom>.sav`, then copy a
  filler file to the Memory Stick that leaves less free space than the
  size of the `.sav`. Start GBA Rush PSP and the game, answer Yes if "Load
  auto-savestate?" appears, move a step and save in the game: the menu
  opens with the warning. Close it, go back to the game and play for half
  a minute: the game tries again about every 5 seconds, and the warning
  must not come back. Open the Savestate menu, delete both savestates with
  Triangle, go back to the game and wait 10 seconds, then exit GBA Rush
  PSP from the menu: "Save not written: try again?" must not appear. On a
  PC check `<rom>.sav` and `<rom>.sav.bak`, then delete the filler file.

### save-trade-fr-emerald

- Models: PSP-1000, second model
- Minutes: 40
- Pass: the trade completes without a communication error, so the link
  does not drop while a game saves; both games reload with the Pokemon
  they received, both `.sav` files are 131072 bytes, and each decoded log
  shows save-write events with result=ok for the save made during the
  session. Each log's only link-error event is the session's end: end=user
  on the PSP that exited first, end=peer-left on the other (end=user on
  both if they exit at the same moment); none has end=lost, network,
  incompatible or save-failed.
- Steps: FireRed on one PSP, with the Network Machine on One Island
  already repaired in its save, and Emerald on the other, both with
  Update backup on AUTO. Enter the Cable Club and trade once: both games
  save while the link session is still open, where a save stalls the link
  the longest. Leave the Cable Club, save in the game on both PSPs and
  exit GBA Rush PSP on one PSP, then on the other: the first exit ends the
  link session. Start each game again, answer No to "Load auto-savestate?"
  and continue from the title screen, so the game loads its `.sav`; check
  that it has the Pokemon it received, and check the size of both `.sav`
  files.

### savestate-load-auto-file

- Models: PSP-1000
- Minutes: 10
- Pass: "Load state from file" on the game's AUTO state brings back the
  state the file held before the choice; on `<rom>_0.svs` it still writes
  the AUTO state first, then loads slot 0.
- Steps: the AUTO state is `<rom>_auto.svs` with `gba_bios.bin` in the
  emulator's folder, `<rom>_auto-builtin.svs` with the built-in BIOS.
  Start a game, play to a place you will recognise (A) and save it to
  slot 0 from the Savestate menu. Play on to place B and exit GBA Rush
  PSP, which writes B to the AUTO state. Start the game again, answer No
  to "Load auto-savestate?" and stay at the title screen. In the
  Savestate menu choose "Load state from file", pick the AUTO state and
  answer Yes to "Load savestate file?": the game must come back at B.
  Play on to place C, then do the same with `<rom>_0.svs`: the game must
  come back at A. Load the AUTO slot from the Savestate menu: the game
  must be at C, since the AUTO state was written just before slot 0
  loaded.

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

### sleep-resume-question

- Models: PSP-1000
- Minutes: 10
- Pass: a suspend with "Load auto-savestate?" open leaves the AUTO state
  as it was: after the resume, Yes brings back the game where it stood at
  the last exit, and the Savestate menu shows the AUTO slot with the date
  and time of that exit.
- Steps: start a game, play to a place you will recognise, note the time
  and exit GBA Rush PSP from the menu, which writes the AUTO state
  (`<rom>_auto.svs`, or `<rom>_auto-builtin.svs` with the built-in BIOS).
  At least a minute later start GBA Rush PSP and the game. While "Load
  auto-savestate?" is on screen, suspend with the POWER switch, wait 10
  seconds and resume. Answer Yes, check the place, then open the
  Savestate menu and read the date and time of the AUTO slot.

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
