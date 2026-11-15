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

### net-fault-noisy-frlg

- Models: PSP-1000, second model
- Minutes: 30
- Pass: with the `noisy` network profile on both PSPs a FireRed and
  LeafGreen trade completes, and each decoded log shows a
  `net-fault-profile=noisy` line with its seed and the `fault-out` and
  `fault-in` lines with the datagrams lost, delayed and duplicated, some of
  them lost.
- Steps: build with `make psp-build PROFILE=diagnostic` and copy the EBOOT
  to its own folder on both PSPs. Next to it write `diag-autorun.txt` as
  `source/src/autorun_config.h` describes, with the build id that the
  Diagnostics page shows and `net_profile=noisy`. Start FireRed on the
  PSP-1000 and LeafGreen on the other, link with the PSP-1000 as host, enter
  the Cable Club and trade once. Close the link from the menu, play on for a
  few seconds, quit from the menu, and decode `logs/` on both PSPs.

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
  Copy the `logs/` folder beside the EBOOT off the PSP: it is the
  evidence of these starts. Then delete `PSP/GAME/GBARushPSP/`, install
  1.4.2 there with `gba_bios.bin` and a game with a battery save beside
  the EBOOT and no `dir.ini`, and on 1.4.2 save in the game and save a
  savestate. Extract the candidate's release ZIP over it and start that
  game: it must load its save, and the savestate must be listed and load.
  Do all of this on each model.

### install-ms-root

- Models: PSP-1000, second model
- Minutes: 30
- Pass: on the PSP-1000 from the Memory Stick `ms0:` and on a PSP Go
  (PSP-N1000) from its internal storage `ef0:`, once the release ZIP is
  extracted at the root of the storage, the XMB shows GBA Rush PSP and it
  starts to the file browser; once the ZIP is extracted over a 1.4.3
  install, its saves, savestates and configuration files are unchanged
  and in use: the game loads its save, the savestate loads and the
  settings changed on 1.4.3 are still set. The second model's pass row
  names the PSP-N1000 in its Model cell and `ef0:` in its Storage cell;
  a row from any other second model is not a pass for this test.
- Steps: the second model must be a PSP Go (PSP-N1000), with the ZIP
  extracted at the root of its internal storage `ef0:`; on the PSP-1000
  it goes to the root of the Memory Stick `ms0:`. Install by hand as a
  player does, not with `scripts/install-test-build.sh`: connect the PSP
  over USB and extract the release ZIP, not the `-diag` one, with the
  drive itself as the destination (Windows' Extract All proposes a folder
  named after the ZIP), replacing the files already there. For the clean
  install, start from storage with no `PSP/GAME/GBARushPSP/` folder, and
  copy its `logs/` folder off the PSP before you replace that install.
  For the upgrade, use a 1.4.3 install in `PSP/GAME/GBARushPSP/` with
  `gba_bios.bin`, a `dir.ini`, settings changed in the menu, and a game
  with a battery save and a savestate made on 1.4.3. Before extracting,
  note the size and modification time of its `.sav`, `.svs`, `.cfg` and
  `dir.ini` files; after, they must be the same. Then start the game.

### diag-autorun-smoke

- Models: PSP-1000
- Minutes: 15
- Pass: a `diag-autorun.txt` with the build's id, `rom=`, `frames=600` and
  `exit=1` starts the game without the browser or the menu and the EBOOT
  exits by itself, leaving `status=done` and `frames=600` in
  `diag-autorun.result`. With another build id in the file the result says
  `status=ignored` and `reason=build-mismatch`, the EBOOT starts as usual,
  and the decoded log shows a `diag-autorun` event with that reason.
- Steps: build with `make psp-build PROFILE=diagnostic` and copy the EBOOT
  to its own folder. Next to it write `diag-autorun.txt` as
  `source/src/autorun_config.h` describes, with the build id that the
  Diagnostics page shows, a ROM, `frames=600` and `exit=1`, and start the
  EBOOT. Then change one character of the build id, start the EBOOT again,
  quit from the menu, and decode `logs/`.

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

## Video

### ge-testpat-photo

- Models: PSP-1000
- Minutes: 20
- Pass: each of the 16 modes passes on its own when its photo shows eight
  bars of equal width, left to right white, black, red, green, blue,
  yellow, cyan and magenta, at the size of its scale (none 240x160,
  x15-gu and x15-sw 360x240, user 408x272, the three 16:9 scales the
  whole 480x272 screen, int2 the whole screen too, rows 12-147 of the
  frame with every pixel a sharp 2x2 block in both of its modes).
  The EBOOT exits by itself and `diag-autorun.result` says `status=done`
  and `frames=9600`, followed by one `testpat=<mode> used=<presenter>/<filter>`
  line per mode. The bars are stored in GU_PSM_5551 with red in the low
  bits (0x7FFF, 0x0000, 0x001F, 0x03E0, 0x7C00, 0x03FF, 0x7FE0, 0x7C1F):
  red and blue trading places means a swapped channel.
- Steps: build with `make psp-build PROFILE=diagnostic` and copy the EBOOT
  to its own folder. Next to it write `diag-autorun.txt` with the lines
  `version=1`, `build=<id>` (the build id the Diagnostics page shows) and
  `testpat=all`, and start the EBOOT without touching the console. Each
  mode stays about 10 seconds, in the order `source/src/autorun_config.h`
  gives (none, x15-gu, x15-sw, user, 16x9-gu, 16x9-sharp, 16x9-auto, int2,
  each nearest then linear): photograph each one straight on, then keep the
  result file with the photos. Record one row per mode in
  `docs/HARDWARE-RESULTS.md`, 16 in all, each with Test id
  `ge-testpat-photo` and in Evidence the mode name (such as
  `16x9-sharp-linear`), its photo file and `diag-autorun.result`.

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
- Pass: on each model every cpu-clock record of the session reads
  `requested=333MHz` without MISMATCH, so the CPU read back 333 MHz and the
  bus 166 MHz, within 1 MHz, and there is at least one for each path of the
  steps: start, menu, ROM load, resume, link start and link end. After the
  Exact minute there is an me-clock record that reads `requested=333MHz`
  with a measured `me=` clock of 332 to 334 MHz and no MISMATCH;
  `me=unmeasured` does not count.
- Steps: with the clock option at 333, play, open and close the menu, go to
  the file browser, load another ROM, suspend and resume, host or join a link
  session and end it, then play a minute with the Exact renderer and exit
  the emulator, which writes the last records to `logs/`. Decode `logs/`
  with `tools/decode_runtime_log.py`. Repeat on the other model.

### memory-probe-3x

- Models: PSP-1000, second model
- Minutes: 20
- Pass: on each model the 3 runs agree on the line, the ways and the
  capacity of both caches, and the numbers are in the results table of
  "Memory system on the PSP" in `docs/DESIGN-DECISIONS.md`.
- Steps: build with `make psp-build DIAGNOSTIC_HW_PROBE=1` (it implies
  `PROFILE=diagnostic`). With the clock option at 333, start the emulator 3
  times on a PSP-1000 and 3 times on a slim. Each start rewrites
  `hw-probe.txt` beside the EBOOT, so copy it after every start and check
  that it reads `cpu-mhz=333` and `bus-mhz=166`. The 3 copies are the row's
  evidence.

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

### replay-hash-selfcheck

- Models: PSP-1000
- Minutes: 20
- Pass: two replays of the same `.gbit` give `.gbsh` files that
  `tools/state_hash_diff.py` finds identical (exit 0). A run with
  `perturb=N` exits 1 against them, naming frame N and `iwram`.
- Steps: build with `make psp-build PROFILE=diagnostic`. Record a trace of
  at least 10 seconds from Diagnostics > Start input trace and save it,
  which leaves the `.gbit` and its `.svs` in `traces/`, then copy the
  game's `.sav` aside and put it back before each run below. Write
  `diag-autorun.txt` with the build id, `rom=`, `trace=` that `.gbit`,
  `frames=600`, `exit=1` and `hash=a.gbsh`, and start the EBOOT; run it
  again with `hash=b.gbsh`, then with `hash=c.gbsh` and `perturb=300`. Each
  `diag-autorun.result` must say `status=done`. Compare `a.gbsh` with
  `b.gbsh`, then `a.gbsh` with `c.gbsh`, with `tools/state_hash_diff.py`.
