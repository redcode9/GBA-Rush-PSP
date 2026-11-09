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
| mGBA 0.10.3, `tests/core/mgba_run.c` (`make test-roms`) | — | hangs | none (it models reading 2, so this shows only that the case cannot pass on open bus) |

## Memory system on the PSP (SOA-8.1, ENG-14.4)

**Status:** open. The probe exists, but it has not been built with psp-gcc
or run on a console yet: nothing below is measured.

**Question.** What the memory system of each PSP model really is: the
latency and the read and write bandwidth of the scratchpad, the D-cache and
RAM, and the line size, capacity and ways of the I-cache and the D-cache.
The code assumes the cache geometry (see below) without having measured it.

**Probe.** `source/src/hw_probe.c` finds the knees and writes the report
(host test `tests/test_hw_probe.c`); `source/src/hw_probe_psp.c` does the
timing. A build with the probe runs it once at every start, at the
configured CPU clock, before the ROM buffer takes the heap. Latencies are
dependent loads, in CPU cycles x100 per load; bandwidths are in MiB/s. Each
cache is scanned three times, with dependent loads for the D-cache and
branches from slot to slot for the I-cache:

- line: strides of 8 to 256 bytes; the line is the first stride whose time
  is at least 90% of the 256-byte one;
- capacity: blocks of 1 to 64 KiB, one access per line; the capacity is the
  size before the first time over 125% of the 1 KiB one;
- ways: 1 to 16 slots one capacity apart, so all in one set; the ways are
  the slots before the first time over 125% of the one-slot one.

A scan with no knee reports 0, and the next scan of that cache goes on with
the line and capacity the code assumes.

**Report.** `hw-probe.txt` beside the EBOOT, one `key=value` per line:

- `hw-probe=1`, then `cpu-mhz` and `bus-mhz`, the clocks it ran at;
- `<memory>-latency-cycles-x100`, `<memory>-read-mib-s` and
  `<memory>-write-mib-s`, for `scratchpad`, `dcache` and `ram`;
- `<cache>-line-bytes`, `<cache>-capacity-bytes` and `<cache>-ways`, the
  knees, for `dcache` and `icache`;
- `<cache>-stride-<bytes>`, `<cache>-size-<bytes>` and `<cache>-ways-<k>`,
  the curves the knees come from, in cycles x100 per access.

If the `icache-ways` knee looks wrong, read the `icache-ways-<k>` values
and see where the time jumps.

**How to run.** Hardware test `memory-probe-3x` in
[hardware-tests.md](hardware-tests.md). Build with
`make psp-build DIAGNOSTIC_HW_PROBE=1`, which implies `PROFILE=diagnostic`:
a release build refuses the probe in `source/src/build_profile.h`, and
`scripts/check-elf.py` fails on a release ELF that holds it. With the clock
option at 333, start the emulator 3 times on a PSP-1000 and 3 times on a
slim, and on a PSP Go if there is one. Each start rewrites `hw-probe.txt`,
so copy it after every start, and check that it reads `cpu-mhz=333` and
`bus-mhz=166`.

**What the code assumes.**

- `CACHE_LINE_SIZE`, 64, in `source/src/cpu.c`: the step of the loop that
  makes each newly translated block visible to the I-cache.
- The cache instructions of `invalidate_icache_region` and
  `invalidate_all_cache` in `source/src/mips_stub.S` step by 64 bytes, and
  `invalidate_all_cache` reaches the second way at 0x2000: two ways of
  8 KiB, so 16 KiB caches.
- The 16 KiB I-cache of the hot code budget in PRF-10.4.

**Criterion.** On each model the 3 runs agree on the line, capacity and
ways of both caches. A value that differs from what the code assumes opens
a story that fixes the strides or the budget (ENG-14.4). SOA-8.2-8.5 start
from the latencies and bandwidths measured here.

**Results.** The three `hw-probe.txt` files of each model are the evidence
of its `memory-probe-3x` row in [HARDWARE-RESULTS.md](HARDWARE-RESULTS.md).

| Model | `memory-probe-3x` | D-cache line, capacity, ways | I-cache line, capacity, ways | Latencies and bandwidths |
|---|---|---|---|---|
| PSP-1000 | not run yet | | | |
| PSP-2000/3000 | not run yet | | | |
| PSP Go | not run yet | | | |
