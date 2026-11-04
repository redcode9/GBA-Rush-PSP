# Design decisions

Decisions that rest on a test or a measurement: what is decided, what is
still open, the sources, and what would settle or change it.

## Guest PC above 0FFFFFFFh

**Status:** open. Nothing changes in the emulator until a result below
decides.

**Question.** What a jump to an address with any of bits 28-31 set, such as
18000000h, does on a GBA. Today the Classic core treats it as an unmappable
jump and ends the emulator after showing "bad jump"; the New core finds
nothing mapped there and runs the zeros it reads as no-ops.

**Two readings.**

1. *28-bit bus.* GBATEK's memory map lists 10000000h-FFFFFFFFh as not used
   because the upper 4 bits of the address bus are not used. Then 18000000h
   is 08000000h and the jump runs ROM code.
2. *Open bus.* mGBA, used as a reference tool, returns open bus for reads
   at 10000000h and above. The CPU would run whatever the bus returns, and
   the game would crash.

**Test.** The core test ROM's `pc_high` case
(`tests/core-rom/source/cases_pc.c`) makes a BX to 10000000h plus the
address of ROM code, in ARM and in Thumb state; up there the code also
loads its literals through the high addresses. With reading 1 it passes
with detail 3 (bit 0 ARM, bit 1 Thumb). With reading 2 it hangs or
crashes, and the next boot of the ROM records it as hung. The case runs
last, because on the Classic core it ends the emulator.

**Criterion.** A real GBA decides; an emulator does not. If the case passes
on hardware, masking the PC to 28 bits, only in the branch that today ends
the emulator, is faithful and costs nothing on the hot path. If it hangs,
there is no mask, and the bad jump should become a contained guest fault
that keeps the save. A bad-jump record with high bits in a player's
runtime log, or a game that needs the jump, shows a mask would help, not
that it is right: either one only starts a hardware run.

**Results.**

| Machine | Build | `pc_high` | Detail |
|---|---|---|---|
| Real GBA | core-rom from this repository | not run yet | |
| New core interpreter, `tests/core/rom_run.c` (`make test-roms`) | — | PASS | 3 (it models reading 1, so this checks only the case's code) |
