# Upstream register: libretro/gpsp

GBA Rush PSP descends from a TempGBA snapshot and has no live upstream (see
`NOTICE`). [libretro/gpsp](https://github.com/libretro/gpsp) keeps developing
the same gpSP core, so this register records, for each part of it that
matters to us, whether we take its code, why, and where it landed.

- Last reviewed upstream commit: `5819380c2ffb0900219d700a382ee68c464ebb99`
- Last review: 2026-11-05

## Rules

- Code of libretro origin enters the tree only from an upstream commit
  recorded in this register, today `5819380`, with its headers and credits
  intact.
- Every port from libretro/gpsp cites its row here, with the upstream commit,
  in its commit message. A port that has no row adds it in the same change.
  When the port lands, its row gets our commit and becomes "already ported".
- Decisions: **port** (we take upstream's code, when the ticket named in the
  reason runs), **skip** (we do not take it; the reason says what we do
  instead), **already ported** (it is in our tree at the commit shown).
- Review, once a month (`CONTRIBUTING.md`): update a clone of libretro/gpsp,
  then run `sh scripts/upstream-review.sh <clone>`. It lists the upstream
  commits after the last reviewed one that touch `cpu*`, `gba_memory.c`,
  `video.cc`, `sound.c`, `serial*.c`, `rfu.c`, `bios/`, `gba_over.h` and
  `tests/`, then prints `Reviewed up to <commit>`. Give each listed commit a
  row, then move the two lines above to that commit and today's date.

## Register

File and line references are in libretro/gpsp at the upstream commit of the
row; F- and ticket IDs are in `docs/BACKLOG.md`.

| Upstream commit | Area | Decision | Reason | Our commit |
|---|---|---|---|---|
| 5819380 | `bios/` (open BIOS image) | already ported | F-012. `source/src/open_bios.h` embeds upstream's `bios/open_gba_bios.bin`; our local image has sha256 `661a9afb93624f2c5e77d07dab137bd8d23cff79e8639c62fe621b49bb064749`, the same as upstream's. Its source in `source/bios/` comes from Nebuleon/ReGBA `313ddd0`, which built the same image and keeps the linker script: a deliberate exception to the rule above (`source/bios/README.md`). CMP-1.3 patches IntrWait at 0x4B4 and must write the new local sha256 here. | 42888fc, fe95c9a |
| 5819380 | `gba_over.h` (game table) | port | F-013, CMP-2.1. The 200 rows per game code (111 with an idle loop, up to 3 gates, save, RTC, rumble, serial and GBA Player flags) are copied verbatim, with `ini_t` and `FLAGS_*` (`gba_memory.c:1861-1883`), so they compare with diff. Our `game_db` checks the gate limits that upstream's loop does not (`gba_memory.c:1923-1939`), and idle loops are enabled only on the ROM they were verified on (CMP-2.4). | |
| 5819380 | HALT wake-up | skip | F-014, CMP-3.2. The wake on IE&IF with IME=0 or CPSR.I=1 is written from GBATEK into our `update_gba`, which keeps its 9-cycle IRQ latency. | |
| 5819380 | 28-bit mask of the guest PC | skip | F-015, CMP-4 (behind its gate). The mask goes only into the branch that stops the emulator on a bad jump, so the hot path gains no instruction, and every masked PC leaves a runtime log record. New code from GBATEK's ARM7 address decoding. | |
| 5819380 | Stack in EWRAM on the LDM/STM fast path | port | F-016, CMP-5.2. The test of SP's region and the EWRAM trampoline (`mips/mips_emit.h:1399-1419`, `:2768-2772`) fix the silent IWRAM corruption when SP is in EWRAM. Adapted to our memory layout and cycle model (EWRAM wait states), cited in the comment. | |
| 5819380 | Mirroring of 1 MiB ROMs (Classic NES Series) | skip | F-017, CMP-6 (behind gate CMP-6.1: a Classic NES Series title must fail first). Our `MAP_REGION` already repeats blocks, so the mirror is pointers only: no RAM copied, and 0x0D stays with the EEPROM. New code. | |
| 5819380 | `gbp.c` (Game Boy Player handshake) | port | F-021, CMP-10.2, only if CMP-10.1 finds a GBP title that misbehaves without the handshake. It becomes a `gba_sio` device with its state in the savestate trailer, original header kept. The PSP has no rumble. | |
| 5819380 | `rfu.c` (Wireless Adapter) | port | F-111, LNK-14.1, behind the LNK-14 gate. Goes into `third_party/gpsp-upstream/` with its headers and a README citing this commit; its queues and timing are retuned on our scheduler (LNK-14). | |
| 5819380 | `tests/mipsgen.c` (MIPS encoder test) | skip | ENG-5.2. It tests upstream's own `mips/mips_codegen.h`. Our test covers the 61 raw macros of our `mips_emit.h` (moved to `source/src/mips_encode.h`), including Allegrex `madd`/`maddu`, against a table assembled by psp-as. New code. | |
| 5819380 | `serial_proto.c` (Advance Wars relay) | port | F-110 (Advance Wars part), LNK-11.2, behind the LNK-11 gate. The Advance Wars part goes into `third_party/gpsp-upstream/serial_aw.c` with its header. The port bounds the word count of both sends that carry data: a packet whose size byte is 255 (254 or 255 in AW2 mode) reaches 257-258 words, `pack16` writes 129 32-bit words into `pkt[2 + 128]`, one past its end, and the 8-bit count in the flags truncates (`serial_proto.c:418-426`, `:547-552`); an intersync run of key words has no bound at all (`:508-516`). The game reaches both (Proposals below). | |
| 5819380 | Div and DivArm in HLE in the dynarec | port | CMP-1.2, if open decision (e) accepts it. The translation sequence (`cpu_threaded.c:96`, `:1727-1735`, `:2298-2306`; `mips/mips_emit.h:1889-1903`), cited in the comment; the edge cases go to our pure `bios_hle` module. | |
| 5819380 | 8 KiB EEPROM detected from writes | skip | EXT-1.1. The idea of promoting the chip from a write DMA is upstream's (`gba_memory.c:987-989`), but its rule `(length & 0x1F) == 17` also accepts lengths the protocol never uses (49, 113, ...). We write our own condition, exactly 17 or 81 units. | |

## Proposals

Patches and issues for libretro/gpsp: what we have that upstream lacks, and
defects found while reading it (ENG-19.2). The maintainer opens each one;
a fix of ours goes out only after the release that contains it, and a core
fix attaches the core-rom case (CMP-3.1) that fails before it and passes
after it. Status is **ready** (verified, the maintainer may open it) or
**waiting** (for what is named). Outcome stays **not submitted** until the
maintainer opens it; then it gets the link and upstream's answer.

| Upstream commit | Proposal | Verification | Status | Outcome |
|---|---|---|---|---|
| 5819380 | Tilt sensor (patch): the X/Y registers of the tilt cartridges, started by writing 0x55 to 0x8000 and 0xAA to 0x8100 of the backup region and read at 0x8200-0x8500. | Upstream has none: "tilt" occurs in no file, the only accelerometer code is the frontend API in `libretro/libretro-common/include/libretro.h:2528-2557`, which no core file calls, and reads of region 0x0E go to `read_backup()` (`gba_memory.c:942-944`, `:451-507`), which has no sensor registers. Ours is in released builds: `read_backup` and `write_backup` (`source/src/memory.c:2088-2116`, `:2135-2148`), fed by the analog stick (`source/src/input.c:164-167`, `:365-377`). | ready | not submitted |
| 5819380 | Cycle check on indirect branches (patch, BX-dual): test the cycle counter in the indirect-branch stubs and run `update_gba` when it is spent, as ours do. | Checked that upstream does not test it elsewhere. Its stubs (`mips/mips_stub.S:297-317`) only look the block up. Every indirect exit is a jump to them with the subtraction in the delay slot (`mips/mips_emit.h:273-279`), the lookups (`cpu_threaded.c:2736-2790`) cannot see the counter, and the update at a block entry point only subtracts (`cpu_threaded.c:3242-3248`, `mips/mips_emit.h:230-235`). Only direct branches test the counter (`mips/mips_emit.h:247-266`, into `mips_update_gba`, `mips/mips_stub.S:240-256`, whose comment says every branch or jump checks it); otherwise `update_gba` runs only from the HALT loop and the VRAM interpreter (`mips/mips_stub.S:336-380`). So a guest loop whose only exits are indirect branches never reaches `update_gba`. Ours: `source/src/mips_stub.S:377-405` (BX-dual at 404-405). Found by reading; not run on upstream. | ready | not submitted |
| 5819380 | Advance Wars relay (issue): `serialaw_senddata` writes past `pkt[2 + 128]` (`serial_proto.c:418-427`). | The game reaches it. `serial.c:163-181` calls `serialaw_master_send` (`serial_proto.c:486-568`) on every transfer the game starts as the master (netplay client 0, also with no netplay session) in AW1 or AW2 mode (core option, or the game's flags at `gba_memory.c:1917-1920`); every value comes from the game's writes to SIOMLT_SEND. A packet with size byte 255 (254 or 255 in AW2) sends 257-258 halfwords (`:539-557`), and an intersync run of more than 256 key words sends them all (`:508-516`); a run of more than 512 also writes past the queue `data[MAX_FPACK]` (`:37`, `:82`). `serialaw_update` repeats both paths for a netplay client (`:570-677`). `tests/test_upstream_aw_relay.c` models the master side in our own code and shows both overflows. Not known: whether Advance Wars itself sends such a packet or run (no ROM run). | ready | not submitted |
| 5819380 | CMP-11.2 (patch): MSR to CPSR_c, CPSR_x or CPSR_s keeps the live NZCV; Rn=PC reads PC+12 with a shift by register. | Same defects upstream, read at `mips/mips_stub.S:477-490` (the stale `REG_CPSR` merged in) and `mips/mips_emit.h:1067-1068` (Rn=PC always PC+8). | waiting: the fix is in wip/classic-core and the core-rom case in wip/test-roms (CMP-3.1), neither in this tree | not submitted |
| 5819380 | CMP-11.3 (patch): BX to ARM with bit 1 set continues at `pc & ~3`. | Same defect upstream: `block_lookup_address_dual` rounds up with `(pc + 2) & ~0x03` (`cpu_threaded.c:2744`). | waiting: the fix is in wip/classic-core and the core-rom case in wip/test-roms (CMP-3.1), neither in this tree | not submitted |
| 5819380 | CMP-11.4 (patch): the whole FIQ bank in `set_cpu_mode`. | Same defect upstream, read at `cpu.cc:1403-1433`: entering FIQ loads only SP and LR from its bank, leaving it does not save its r8-r12, and r8-r12 then come from the new mode's slots, which only entering FIQ from that mode writes. | waiting: the fix is in wip/classic-core and the core-rom case in wip/test-roms (CMP-3.1), neither in this tree | not submitted |
