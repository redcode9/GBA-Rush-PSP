# Contributing

Changes should preserve compatibility and performance on real PSP hardware.
Prefer fixes that apply to the GBA or PSP platform generally; keep game-specific
workarounds documented and narrowly scoped.

Before submitting a change, run:

```sh
make check
make test-sanitize
make psp-build
```

Changes to hot paths should include a focused host harness where practical and
a three-run PSP benchmark comparison when performance may move. Test suspend,
resume and menu transitions for changes involving threads, audio, files,
networking or timing.

## Link cable development specification

This section is the canonical design and handoff record for two-PSP GBA cable
emulation. It replaces informal implementation notes and symptom-driven fixes.
Code and tests are the execution evidence; when they disagree with this
specification, stop, record the discrepancy below and resolve it explicitly.
Do not silently reinterpret the protocol.

### Goal-mode continuation contract

An agent continuing link work in goal mode must:

1. Read this entire section before changing code.
2. Inspect the working tree, recent commits, latest decoded hardware logs and
   the current execution record below.
3. Validate that the recorded phase still matches the code and tests. Correct
   the record before proceeding when it does not.
4. Work on the active milestone only. A newly discovered prerequisite may be
   inserted, but unrelated emulator work stays outside this goal.
5. Preserve the safety invariants even when that means ending a session rather
   than attempting a speculative recovery.
6. Run the gate assigned to the milestone and record the exact command, result
   and relevant hardware evidence.
7. Before ending a goal turn, update the milestone table and current execution
   record with the last verified fact, remaining risk and next concrete action.
8. Keep this record current rather than creating design diaries, progress
   files or one-document-per-failure notes. Git history is the detailed log.

Use `complete` only when the stated gate has passed. Use `blocked` only for an
external dependency that prevents further safe progress. An unsuccessful test
is evidence and normally leaves the milestone `in progress`.

### Current execution record

| Field | Value |
|---|---|
| Last updated | 2026-09-14 |
| Design state | RC3.3 is a local correction candidate, not an accepted two-PSP cable. It fixes reproduced early-START, lost-presence and benchmark-terminal defects. No new physical benchmark or FireRed trade has been performed |
| Active milestone | M8; the guest-timing prerequisite in M4 is reopened. A bounded synthetic workload is not proof of a shared safe horizon for arbitrary guest programs |
| Working tree | The RC3.3 working tree (protocol-v3 stream implementation, models, CBMC proofs, tools, tests and documentation) is committed in this handoff and the tree is clean. The historical code baseline is `790d3da`. Gate outputs and isolated count-audit copies are in `build/link-audit/`. No SD installation, file replacement or ejection was performed in this correction pass |
| Last hardware evidence | Sep-11 RC3.2, PSP-2004 host / PSP-1004 joiner: shared session nonce 1131198442, 10,434 STARTs / 10,433 REPLYs, 10,433 words displayed, then host WLAN-off. Neither log has a terminal benchmark summary or enough data to reconstruct throughput. Suspend is a hypothesis, not an established cause. Acquisition, photo, checksums, decoded logs and the original failing reproducer remain in `build/hardware-logs/20260911-rc32-benchmark.yGIP2P/` |
| Earlier evidence and rollback | Sep-10 FireRed failures: `build/hardware-logs/20260910-m8-v3-rc31-hardware-stall/`. RC3.1 no-ROM null-register crash and RC3.2 validation: `build/hardware-logs/20260911-m8-v3-rc31-no-rom-crash/`. Paired installation manifests: `build/hardware-logs/20260911-rc32-install.hXxVA2/`. Last installed RC3.2 EBOOT SHA-256: `0ac8bd009632769062025ce4df83804fdcbba37a9e6298c28e89f4d3251fc4a9`. Prior local binaries and the previous detailed handoff are preserved in `build/link-audit/rc32-binaries/` and `build/link-audit/previous-handoff.md` |
| Verified corrections | Session/SIO tests now cover delayed guest IRQ handling, unequal cycle bases, cycle wrap, recovery before latching, immutable scheduled results, periodic presence repair, all benchmark session endings and silent joiner timeout. `test_link_cable_runtime` passes 720 register-level words through the production scheduler/lanes with independently advancing guests, asymmetric latency, loss, duplication and backpressure. It does not execute a ROM, PSP threads or radio firmware |
| Software gate | Re-verified on the committed RC3.3 tree: `make -j4 check HOST_CC='cc -O1' HOST_BUILD_DIR=build/host-work` exits 0 (`build/link-audit/check-final-20260914.log`) and `make -j4 test-sanitize HOST_CC='cc -O1 -fsanitize=address,undefined' HOST_SANITIZE_DIR=build/sanitize-work` exits 0. Each explores 5,159,580 schedules / 120,593 observed signatures and runs the 720-word production-scheduler workload; native checks also pass 27 Python tests. Earlier equivalent logs: `build/link-audit/check-final.log`, `sanitize-final.log`. The original five-failure benchmark reproducer passes under ASan/UBSan (`original-benchmark-reproducer-green.log`) |
| Exploration audit | The initial full runs failed only their old count assertions. Isolated copies disable only presence refresh and reproduce exactly 5,156,572 schedules / 67,753 signatures (exit 0). Comparing all 324 recovery configurations attributes the additional 3,008 schedules to 752 post-outage packet opportunities, each explored with four faults. Zero-outage configurations are unchanged. The updated counts retain every invariant and completion check; audit script, source copies and outputs remain in `build/link-audit/` |
| Proof and static checks | `make check-cbmc` exits 0 on the committed tree (2026-09-14). `make check-link-model TLA2TOOLS_JAR=/tmp/gba-link-research/tla2tools.jar TLC_WORKERS=2 TLC_STATE_DIR="$PWD/build/link-audit/tlc"` passes all four TLC safety/liveness gates, including the cable liveness temporal check over 2,690,938 fair states: cable 7,136,984 generated / 1,345,469 distinct, stream 4,550 / 1,558 (`build/link-audit/link-model-final-20260914.log`). Clang static analysis re-run reports zero diagnostics for SIO, session, stream, benchmark, lanes and scheduler; these checks do not model PSP firmware or guest ROM execution |
| Candidate artifact | PSPDEV `v20260801`, GCC 15.2.0: clean build plus final `make -j4 psp-build` relink exit 0. EBOOT is 5,608,188 bytes, SHA-256 `8571083399536a579ede4a17bd347b1e3819cf17d055de13dd785db80add8636`; its embedded PRX byte-matches `source/TempGBA.prx`. ELF contains `1.4.0-m8-v3-rc3.3`; exception PRX is unchanged. `dist/GBA-Rush-PSP-1.4.0-m8-v3-rc3.3.zip` is 5,626,777 bytes, SHA-256 `580a29679fbd614320ea89306995a871bcd0171902e63e2ef03aae01e17b8008`; `make artifact-smoke VERSION=1.4.0-m8-v3-rc3.3` re-ran exit 0 on the committed tree (2026-09-14). Full binary hashes and package/build logs are in `build/link-audit/`. The only compiler warning concerns serial LTO compilation, not a code diagnostic |
| Remaining risk | Real PSP service latency, WLAN-switch failure origin and FireRed compatibility remain unverified. Stream clock hints are advisory, not a proven conservative execution grant; an early-START regression alone cannot establish all guest timing or mode-transition behavior. Models abstract guest execution. A ten-minute benchmark validates transport performance, not Pokémon protocol correctness |
| Immediate next action | M8 is blocked on an external dependency: obtain paired ten-minute RC3.3 hardware benchmarks in both roles, preserving terminal logs before another run. Continue with the FireRed/save-reload/lifecycle matrix only if both paired gates pass. Investigate any first divergence against guest SIO timing, not just packet counts; M4's generic safe-horizon requirement remains open. Do not call M8 complete, infer a successful trade from host tests, or install to a guessed/stale SD mount |

When updating this table, retain only the latest verified handoff. Move durable
requirements into the specification or milestone gates instead of accumulating
a chronological diary.

### Product scope

The target is a generic local cable between two physical PSP systems. The
first compatibility boundary is GBA Normal 8-bit, Normal 32-bit and two-player
Multiplayer 16-bit communication. Host and joiner must be interchangeable
across PSP-1000 and PSP-2000 hardware.

The following are separate future scopes and cannot be used to claim completion
of the two-player cable: three- or four-player sessions, Wireless Adapter/RFU,
internet relay, GameCube JoyBus, UART peripherals and multiboot. GPIO/general
purpose cable behavior is required before claiming broad commercial-game cable
compatibility, but it follows the stable two-player core.

Fast-forward, savestate load and ROM replacement are unavailable while a link
session is active. Suspend, WLAN loss, peer exit and emulator exit must either
recover at a declared safe checkpoint or close the session cleanly.

### Correctness contract

The implementation must provide two different guarantees:

- **Safety:** no pair of reachable software states may apply different records
  for one transfer, apply a transfer twice, deliver its serial IRQ twice or
  accept an old session as current.
- **Conditional liveness:** when both PSP processes continue running and the
  channel eventually delivers packets after a finite fault burst, the peers
  must complete the pending decision or converge on the same recovery
  checkpoint. Permanent radio or power failure must produce bounded, clean
  shutdown rather than a freeze or invented commit.

No two-node asynchronous protocol can guarantee progress after an unbounded
partition or permanent peer failure. User-visible reliability therefore means
that a valid session works, while an invalid session fails closed with a useful
reason. It never means guessing remote state.

### Evidence behind the reset

The first complete hardware log remained open for roughly 4,099 seconds and
performed about 480 transport resynchronizations without starting a single GBA
serial transfer. The peers retained asymmetric menu/gameplay and SIO presence
after recovery. A connected PSP socket was therefore not a usable GBA cable.

The next attempt reached the first serial START. The session aborted after
about five seconds, while transport recovery did not begin until roughly eight
seconds. Recovery then compared local aborted sequence 1 with remote empty
sequence 0 and violated the transaction invariant. Critical resume messages
also accumulated behind one unacknowledged reliable base record.

These records explain the repeated field symptoms:

- a game name on only one PSP came from asymmetric HELLO or stale presence;
- `0x80410001` indicated exhausted network space during retry pressure;
- `0x8041070C` indicated a disconnected ad-hoc socket;
- `0x80410715` indicated a timeout, including suspend-related waits;
- intermittent `COLLEGAM. 2G` reflected oscillating SIO readiness rather than
  a stable cable;
- one-frame-at-a-time gameplay and broken audio came from holding the emulation
  loop inside network waits;
- exit and suspend crashes came from lifecycle races and waits that outlived
  their socket or worker.

The timing model also operated at the wrong scale. The prototype publishes a
clock every `0x80000` GBA cycles, approximately 31.25 ms, and permits two frames
of lead. A two-player Multiplayer word at 115200 baud completes in about 5,755
cycles, approximately 0.343 ms. Packet delivery can be reliable and still
arrive after the receiving emulator has passed the affected serial event.

The current explicit-state harness remains useful but bounded. Its fake
transport does not model PSP scheduling, firmware socket buffers, radio,
network-module memory pressure, audio, power callbacks or storage. Passing its
existing schedule count is not hardware-completeness evidence.

### M8 protocol-v3 multiplayer refinement

The two-message stream described here supersedes the transaction-journal hot
path below for two-player Multiplayer 16-bit mode. This maintained section is
the implementation reference; it does not depend on retired draft-plan paths.
The journal remains in use for Normal 8/32-bit transfers and legacy regression
coverage; it must not be put back into v3 multiplayer.

Both peers advertise protocol major 3 and
`LINK_CAPABILITY_STREAM_TRANSFER`. Version 2 and version 3 EBOOTs are not
interoperable. The host sends one immutable `TRANSFER_START`; the joiner
latches its word on the emulation thread and returns one `TRANSFER_REPLY`.
Only one causally dependent word is active, receipt of `REPLY(N)` acknowledges
`START(N)`, and `START(N+1)` or an idle heartbeat acknowledges the cached
reply. Duplicate traffic may retransmit or replay a packet but may not repeat a
GBA result update or serial IRQ.

The stream sends no `PREPARE`, `ACCEPT`, `COMMIT`, `COMMIT_ACK`, `RELEASE` or
`RELEASE_ACK`, and carries clock anchors in start, reply and low-rate heartbeat
records instead of producing standalone hot CLOCK traffic. A critical reply
wakes the PSP network worker; bounded hot drains prioritize stream records,
while the cold regime retains low CPU use. Recovery preserves the semantic
pending start or cached reply across a generation change but rejects every old
generation datagram before session mutation.

A stream session may schedule at most one heartbeat request per
`LINK_SESSION_HEARTBEAT_US`. Immediate correlated replies bypass that request
gate. Successful START, REPLY, heartbeat-request and heartbeat-reply publication
all reset the wall-clock interval before another scheduled request. A GBA-cycle
clock deadline alone must never authorize another scheduled heartbeat; otherwise
the removed near-1 kHz CLOCK traffic is merely renamed and can exhaust the PSP
PDP send buffer before the first SIO word.

#### RC3.3 behavior and timing limits

Receipt of `START(N+1)` is not evidence that the joiner's CPU executed the
serial handler for word N. `link_session_service_stream_start` retains one
unlatched START while the previous SIO event is busy. It then maps the master's
inter-word cycle delta onto the previous local start before reading the next
`SIOMLT_SEND`. Duplicate STARTs cannot reset this wait or repeat the latch.
Recovery may discard and replay an unlatched START, but not an already
scheduled register result or IRQ. An uncertain REPLY timeout requests recovery
instead of cancelling a result the other peer may already have scheduled.

The first stream word still uses a local timing base. Mapping a later START
cannot rewind a guest that has already advanced past it. The 2 ms freshness
limit on stream pacing prevents a one-second heartbeat from imposing a
500 ms wait, but it is **not a conservative safe-horizon protocol**. Both peers
must advertise hot gameplay presence before this advisory pacing applies.
The stricter SIO-hot contract below remains a requirement, not a completed
arbitrary-guest guarantee. This is why M4 and M8 remain open.

Unchanged SIO/menu presence is republished once per heartbeat interval. A
latest-wins mailbox prevents queue buildup, not packet loss; the final update
must remain repairable. Presence refresh also permits a due correlated probe
so its own traffic cannot indefinitely suppress liveness checks.

`test_link_cable_runtime` runs the production scheduler and transport lanes
with separate 300/700-cycle guest budgets, 300/700 microsecond wire delays,
a delayed serial handler, nine-word bursts and Timer3 spacing. Four scenarios
cover either slower role with and without loss, delayed duplicates and send
backpressure. Its 720 words check data and exact-once IRQ/application, not a
FireRed ROM, actual PSP thread concurrency or Wi-Fi throughput. The older
`test_link_pipeline` still exercises `AdhocDatagram`/`LinkTxQueue`; retain it as
legacy/recovery coverage rather than calling it the PSP runtime.

#### Benchmark and diagnostic contract

The benchmark offers 1,000 words/s; the acceptance floor is 600 words/s for
600,000 ms. Short host tests may configure a lower target, but cannot satisfy
the unchanged physical acceptance gate. A live summary measures from benchmark
start, not emulator uptime. An unbounded histogram bucket reports `UINT_MAX`,
not an optimistic 32 ms percentile. A threshold failure has an explicit reason.

User, suspend and ROM-change disconnects terminate an active benchmark as
aborted; transport failure and lack of progress terminate it as failed. The
joiner's watchdog applies even before its first START and while both peers are
in menus. It ends the benchmark, not the existing intentional menu-pause policy:
a silent menu connection may remain open for explicit disconnect. Terminal
results do not change when polled again or disconnected.
The main loop records a result before teardown, benchmark replacement or
clearing on game entry, allowing up to 250 ms for a final STOP/ACK queue drain
when the transport is still open. The retained 64-record critical ring must
hold all 14 summary records plus the 32-record shutdown checkpoint.

Start records use `link-benchmark` flags `0x80` (run, requested duration,
offered rate) and `0x81` (run, seed, role). They are not terminal summaries.
The paired analyzer requires every terminal field, one host and one joiner,
matching counts/digests and valid checksums. A missing record or unmatched run
fails rather than producing a traceback or inferring success. Run IDs are identifiers, not
ordered timestamps: use `--run-id` if multiple common runs are retained.

During the benchmark, the PSP menu services networking and power independently
of its approximately 30 Hz rendering/input cadence. Run observation and
terminal logging check cheap status/identity fields before computing summary
histograms and checksums, avoiding redundant work on every network pump. It does not wait for
vblank between benchmark pumps. These changes require on-console latency,
power and input checks; a successful PSP build does not execute that loop.

### Layer ownership

The replacement keeps three strict layers:

1. **PSP transport** discovers a peer, sends and receives bounded records,
   reports backpressure and estimates delivery timing. It cannot decide or
   abort a GBA transfer.
2. **Link session** owns identity, epochs, the transfer journal and recovery.
   It cannot rebuild a socket from inside a transfer transition.
3. **GBA SIO** owns registers, pins, mode, latched words, completion cycles and
   serial IRQ delivery. It never interprets network retries.

There is one recovery coordinator. Transport, session and SIO may report a
condition to it, but they cannot start nested recoveries independently.

### Identity and ordering

Every wire record carries these logical identifiers:

```text
session_nonce, epoch, wire_packet_number, logical_message_id
```

Every transfer record additionally carries:

```text
transfer_sequence, sio_revision, mode, setting, player_count,
start_cycle, finish_cycle, participant_data, transfer_hash
```

`session_nonce` is new for every connection attempt. `epoch` increases for a
recovery within that session. Old nonces and epochs may be acknowledged for
drain purposes but cannot mutate current state. Packet retransmission keeps the
logical message ID and receives a new wire packet number, avoiding ambiguous
RTT samples. Sequence comparison must use bounded serial-number arithmetic and
remain valid across wrap.

The same transfer sequence may have only one hash. A second hash for that
sequence is a protocol error and closes the session.

### Transport lanes

PDP remains the default transport because it permits independent delivery
policies without stream head-of-line blocking. PTP, GameMode, KCP or ENet may
be evaluated behind the transport interface later, but none may alter session
semantics or be treated as a synchronization fix.

Outgoing traffic uses three bounded lanes:

1. **Immediate control:** transport ACKs and close acknowledgements bypass all
   application queues.
2. **Reliable critical:** one logical session record is outstanding at a time.
   Duplicate sends are retransmissions of that record, not new queue entries.
3. **Replaceable state:** heartbeat, clock and non-transactional SIO presence
   are latest-wins records and never wait behind the critical lane.

A bounded single-producer/single-consumer ring transfers work between the
network and emulation threads. Acquire/release ordering is explicit. The ring
solves local contention only; distributed ordering comes from session identity,
sequence and the transfer journal.

Retransmission uses a smoothed RTT and variance, excludes ambiguous samples
after retransmit and applies bounded exponential probe backoff. A probe timeout
requests acknowledgement; it does not by itself declare a transfer lost.
Backpressure is measured by queue sojourn time and repeated `NO_SPACE` results,
not by queue length alone. It may slow delivery but cannot manufacture a
session failure or duplicate a logical record.

### SIO-hot synchronization

Both emulators free-run while the cable is cold. The session becomes SIO-hot
when either peer selects a supported cable mode or exposes cable readiness.
Entering hot mode establishes a shared cycle base before readiness is exposed
to either game.

While hot, a peer may not run past the earliest cycle at which an unreceived
remote serial event could affect it. Generic SIO has no useful lookahead once
the hardware is armed, so both peers stop at the serial barrier instead of
using the former two-frame allowance. Clock samples estimate delay and jitter;
they never authorize a past event.

The master remains the transfer sequencer. It chooses `start_cycle` in the
shared hot timeline and derives `finish_cycle` from the emulated GBA transfer
table. Both peers schedule register completion and IRQ at that same emulated
cycle. Wall-clock delay changes how long a PSP waits, not the GBA-visible
duration of the transfer.

Mode change during an armed but undecided transfer cancels that arm. Mode
change after a decision does not cancel the decision; the decided record is
finished or the session closes.

### Transfer journal

Only one transfer may be active. Its monotonic stages are:

```text
EMPTY -> ARMED -> DECIDED -> RELEASED -> EMPTY
```

- `ARMED` contains both peers' compatible SIO revisions and latched data. It
  may be cancelled without applying data or IRQ.
- `DECIDED` is an immutable host-issued record with its complete hash. Once
  either peer records this stage, abort is forbidden.
- `RELEASED` means the decision is known on both sides and may complete at its
  recorded `finish_cycle`. A release duplicate only repeats its acknowledgement.
- Returning to `EMPTY` retains `last_released_sequence` and its hash as the
  recovery checkpoint.

The normal exchange is:

```text
ARM(host) + ARM(joiner)
        -> DECISION
        <- DECISION_ACK
        -> RELEASE
        <- RELEASE_ACK
```

The peers do not admit another transfer until both observe the released
sequence. Transport ACK and application decision acknowledgement are different
facts and cannot substitute for one another. All handlers are idempotent.
Serial data and IRQ are applied exactly once, keyed by session, epoch and
transfer sequence.

### Recovery

Any credible loss first enters one quiescent recovery state. It stops new SIO
arms, freezes the hot horizon and keeps UI, power callbacks, audio policy and
the network worker responsive. A suspicion or timeout may enter quiescence but
cannot decide commit or abort.

Recovery increments the epoch and exchanges:

```text
last_released_sequence + hash
active_sequence + stage + hash
actual SIO snapshot + revision
```

Reconciliation is deterministic:

| Local and peer evidence | Action |
|---|---|
| Same released checkpoint, no decision | Cancel an arm, republish SIO and resume |
| Host has an issued decision, peer has no decision | Replay that decision |
| Peer has the same decision and host journal proves it was issued | Adopt/replay release; never regress |
| Both have the same released decision | Restore that checkpoint and resume |
| Same sequence with different hash | Close with protocol error |
| Peer claims a decision the host journal never issued | Close with protocol error |
| Nonce, ROM, build or protocol mismatch | Close as incompatible |

Transport resync, group rebuild and stack restart are implementation steps
inside this one quiescent recovery. They cannot reset the session journal. A
new recovery supersedes an unfinished transport attempt without starting a
second session recovery. On success, the actual SIO state is republished before
the GBA cores are released.

There is no transparent continuation after process restart because volatile
transfer evidence has been lost. A fresh process creates a fresh session.

### Lifecycle and performance

Network work runs outside the emulation thread. The emulation thread may pause
at a deliberate SIO barrier, but it must not poll a socket, format diagnostics
or wait for worker destruction. UI and power callbacks stay serviceable during
the barrier.

Link waits use a stable audio policy: drain or fade once, hold silence without
repeated underruns, then restore on release. Fast-forward remains disabled
while the session is hot. Thread priority, stack size, socket buffers and poll
cadence are profiled on PSP-1000 before tuning for newer hardware.

Suspend and exit use one cancellation token and this destruction order:

1. stop admission of link and SIO work;
2. signal the network worker;
3. wait with a fixed monotonic deadline;
4. close PDP and ad-hoc control resources;
5. unload network modules only after all users have stopped;
6. restore audio, power and UI lifecycle state.

No cleanup path may wait forever. When the deadline expires, record the exact
phase and fail closed without dereferencing destroyed resources.

### Safety invariants

Production assertions, the model and host harness must express all of these:

1. Transfer sequence is monotonic within an epoch.
2. At most one transfer is active.
3. A sequence has one immutable canonical hash.
4. A decision never returns to `ARMED` or becomes an abort.
5. Each peer applies data and serial IRQ at most once per sequence.
6. Neither peer admits the next transfer before the current release is shared.
7. Old nonces and epochs cannot mutate current state.
8. Recovery preserves the last shared release or closes; it never guesses.
9. Reliable and replaceable queues remain bounded under arbitrary input.
10. Backpressure cannot create duplicate logical work.
11. Hot emulation cannot cross its safe serial horizon.
12. Shutdown and suspend contain no unbounded wait.
13. ROM identity, emulator protocol and supported mode match before readiness.

### Diagnostics

The runtime logging rules below remain in force. Link records must additionally
make the first divergence reconstructable without per-packet SD writes. The
critical snapshot contains nonce, epoch, transfer sequence, stage, hash, SIO
revision, local and peer cycles, queue sojourn high-water mark, RTT estimate,
probe count, last PSP error and shutdown phase.

Each thread writes its own fixed binary ring. The hot path allocates nothing,
formats no text and touches no storage. Flush occurs only at a safe failure,
disconnect, suspend, ROM change or exit boundary. The snapshot records dropped
event counts; loss of diagnostic capacity cannot be mistaken for absence of an
event.

Failure attribution has three independent axes. `link-failure` domain values
are `none`, `psp-api`, `transport`, `session`, `lifecycle` and `internal`; the
internal signed/hex code must never be displayed as if it were the original
PSP return value. A separate API phase and signed/hex result identify the real
firmware call. `link-timeout` names `receive-silence`, `active-transfer`,
`ahead-pacing`, `pressure`, `recovery-rendezvous`, `shutdown`, `negotiation`,
`abort-ack`, `control`, `peer-discovery` or `connect-handshake`.

The versioned `link-stream-metrics` records retain start/reply/apply counts,
duplicates, cached replays, stale generations, RTT/RTO, nine-word burst and
barrier latency, worker wake/drain/yield data and audio underruns. A terminal
`link-benchmark` summary is bound to one run ID and contains status, failure,
role, duration, completed words, throughput, digest, RTT percentiles, burst
tails, retransmissions, queue state, recovery, starvation and a checksum. The
paired analyzer requires one host and one joiner record with matching run ID,
word count and digest.

### Verification gates

The following are required acceptance layers, not a claim that every layer
has been implemented. In particular, the current stream model treats guest
application atomically and does not model guest ISR execution or PSP threads.

- A TLA+/PlusCal model checks every safety invariant plus conditional liveness
  under loss, duplication, reorder, backpressure, one-sided recovery, suspend
  and sequence wrap.
- The production C transition functions run unchanged in an explicit-state
  harness with a virtual monotonic clock. The state signature includes queue,
  epoch, journal, SIO horizon and lifecycle state.
- CBMC proofs cover queue bounds, packet parsing, serial arithmetic, transition
  preconditions, memory safety and undefined behavior within declared bounds.
- Differential SIO traces match the Nintendo register behavior and mGBA timing
  for every supported mode, including mode changes and IRQ enable/disable.
- Fault injection restarts or suspends either role at every externally visible
  phase and verifies bounded recovery or bounded close.

Host acceptance requires zero invariant failures, divergent hashes, duplicate
IRQs, queue overflows and unbounded waits. Sanitizers and static lifecycle
checks are supplementary; text-pattern checks do not prove protocol behavior.

Physical acceptance on the current PSP-1000/PSP-2000 pair requires:

- a ten-minute no-ROM v3 benchmark in each host/join role assignment with at
  least 600 words/s, nine-word burst p99 at most 20 ms, zero full recoveries,
  no persistent queue, timeout, stale generation, audio underrun or more than
  50 ms main-thread starvation;
- both host/join role assignments;
- 10,000 consecutive transfers per synthetic supported-mode ROM;
- a 60-minute idle hot session without recovery churn;
- at least 20 complete Pokémon FireRed trades across repeated sessions;
- suspend, resume, peer exit and local exit from each exposed session phase;
- no crash, permanent freeze, divergent transfer hash or repeated serial IRQ;
- bounded exit and a single stable audio pause rather than recurring underruns.

For a controlled physical test:

1. Back up both saves and record PSP model, firmware, EBOOT SHA-256, clock,
   ROM revision/CRC and role. Both consoles need the same candidate. Enable
   the physical WLAN switch; disable WLAN Power Save and automatic system
   suspend for the benchmark. Screen blanking is not proof of system suspend.
2. Start fresh emulator processes without loading ROMs. Create/join, start the
   benchmark on the host and wait for a terminal result. A frozen word count,
   error, timeout or manual Stop is not a pass. Exit normally and copy both
   logs before another run can replace retained evidence.
3. Run `python3 tools/analyze_link_benchmark.py host.gbrlog joiner.gbrlog
   --json` (one shell line). Preserve its exit status and JSON. Repeat the full
   ten minutes with roles reversed. An on-screen PASS alone is insufficient.
4. Only after both paired gates pass, test legally obtained FireRed copies
   with separate saves and no cheats or fast-forward. Record room entry,
   an actual completed exchange, normal room exit, in-game save and reload
   verification of both received Pokémon. Repeat entry/trade/exit without
   restarting the link and with roles reversed, until the matrix above is met.
5. Test user disconnect, WLAN loss, suspend/resume and peer exit separately
   from successful runs. Preserve the first failed pair and reproduction
   steps; stop rather than repeatedly attempting trades against an unstable
   build. Never overwrite the only copy of a save during acceptance testing.

Other PSP models are an additional compatibility gate, not a prerequisite for
developing the verified core. Public compatibility claims list the hardware
and modes actually tested.

### Milestones

| ID | State | Deliverable and exit gate |
|---|---|---|
| M0 | complete | Root-cause analysis, source research and this canonical specification |
| M1 | complete | Finite journal/stream models and one table of legal transitions; model-level safety/liveness pass, without proving the guest-timing refinement required by M4 |
| M2 | complete | Three transport lanes, bounded SPSC handoff, unambiguous packet numbering and adaptive probes; deterministic loss/backpressure tests pass |
| M3 | complete | Monotonic `ARMED/DECIDED/RELEASED` journal and single recovery coordinator; model and C harness traces agree |
| M4 | in progress | Timing prerequisite reopened by the early-START regression: local completion and deferred-latch tests pass, but arbitrary-guest safe horizons and differential ROM traces remain unverified |
| M5 | complete | Host-tested bounded teardown, audio wait and binary failure snapshots; lifecycle fault injection passes, while physical PSP lifecycle acceptance remains in M8 |
| M6 | complete | Full host, sanitizer, CBMC and schedule-exploration gate with no invariant or memory failure |
| M7 | superseded | Physical trials produced the retained failure evidence and proved the six-phase journal hot path unsuitable for Pokémon cadence; it is the rollback baseline, not an accepted cable |
| M8 | in progress | Protocol-v3 two-message multiplayer stream, full software/build gate, ten-minute benchmark in both PSP role assignments and complete FireRed matrix |
| M9 | deferred | GPIO/general-purpose cable behavior and a broader commercial-game matrix after M8 |

The working prototype may supply reusable parsing, logging or SIO code, but its
existing state names and recovery behavior are not compatibility constraints.
Reuse is decided only after M1 maps a component to this specification.

M1 classifies the prototype at component boundaries:

| Component | Decision for the replacement |
|---|---|
| `link_protocol` | Reuse bounded parsing, CRC and encoding patterns; replace the wire schema with the identity, journal and timing fields above |
| `adhoc_datagram` | Reuse nonblocking PSP PDP I/O primitives; replace its reliability, acknowledgement and backpressure policy with the three transport lanes |
| `link_tx_queue` | Replace the single mixed queue with bounded critical, replaceable-state and unreliable lanes; retain focused queue-test patterns |
| `link_session`, `adhoc_recovery`, `adhoc_resync` | Replace transaction states and independent timeout/recovery ownership with the generated journal table and one coordinator |
| `gba_sio` | Reuse register/mode primitives only after differential verification; replace frame-scale release scheduling with the cycle horizon in M4 |
| `link_clock` | Reuse cycle-conversion helpers; reject the 31.25 ms publication and two-frame lead policy for SIO-hot synchronization |
| `runtime_log`, `runtime_log_store` | Reuse fixed binary rings and bounded persistence; extend the failure snapshot with link identity, journal, horizon and queue evidence |
| `power_lifecycle` | Reuse callback plumbing after M5 proves bounded worker/socket teardown; do not reuse unbounded waits |

### Reference implementation findings

Research reviewed for this correction (2026-09-14):

- GBATEK describes writable multiplayer receive registers, their START reset
  to `FFFF`, and the four-slot serial behavior. `gba_sio` now accepts guest
  clears of `SIOMULTI0-3`; START still resets all receive slots.
- mGBA's current SIO transfer table gives 5,755 GBA cycles for two-player
  multiplayer at 115200 baud. Its local lockstep scheduler synchronizes core
  events; it is not a drop-in protocol for two PSP radio endpoints.
- FireRed's decompiled serial handler consumes a checksum plus eight command
  words and arms Timer3 between words. The regression uses the documented
  `(197 * 64)`-cycle gap. Precomputed packets alone do not test this guest code.
- libretro gpSP batches Pokémon commands and explicitly simulates a fake
  master at nine IRQs per frame for the slave. This is useful protocol-specific
  HLE research, not a generic cable fix to copy into the SIO implementation.
- The independent `mgba-switch-netlink` project reports two consecutive
  FireRed/LeafGreen trades between local desktop processes and documents
  pending START, mode-epoch and room-exit races. Its README explicitly leaves
  cross-machine/physical Switch acceptance open. That author's reported
  result is not an independently reproduced result here, nor PSP evidence.
- VBA-M's network link code is another timing/state-machine reference, but a
  TCP transport does not by itself align guest clocks or remove serial ISR
  dependencies. No external protocol-specific synthetic data was imported.

### Technical references

- GBATEK GBA serial I/O register reference:
  <https://mgba-emu.github.io/gbatek/>
- libretro gpSP serial engine and Pokémon protocol batching:
  <https://github.com/libretro/gpsp/blob/master/serial.c>
  <https://github.com/libretro/gpsp/blob/master/serial_proto.c>
- Independent desktop network-link prototype and its stated limitations:
  <https://github.com/senhaoran213/mgba-switch-netlink>
  <https://github.com/mgba-emu/mgba/issues/2379>

- Nintendo AGB Programming Manual, Communication Functions:
  <https://mutantstargoat.com/~nuclear/tmp/gba_prog_manual.pdf>
- mGBA SIO lockstep and timing:
  <https://github.com/mgba-emu/mgba/blob/master/src/gba/sio/lockstep.c>
  <https://github.com/mgba-emu/mgba/blob/master/src/gba/sio.c>
- VBA-M GBA link synchronization:
  <https://github.com/visualboyadvance-m/visualboyadvance-m/blob/master/src/core/gba/gbaLink.cpp>
- Pokémon FireRed cable state and timeouts:
  <https://github.com/pret/pokefirered/blob/master/src/link.c>
- PSPDev PDP, PTP and GameMode API:
  <https://github.com/pspdev/pspsdk/blob/master/src/net/pspnet_adhoc.h>
- Sony PSP WLAN power-saving setting and communication caveat:
  <https://manuals.playstation.net/document/en/psp/current/settings/wlan.html>
- MIPS Technologies MIPS32 Architecture for Programmers, Volume III,
  revision 2.50, sections 8.22-8.23 (Cause.BD and EPC interpretation):
  <https://cse.sc.edu/~jbakos/611/mips/MIPS32_Volume_III.pdf>
- PSPSDK thread lifecycle/status API:
  <https://github.com/pspdev/pspsdk/blob/master/src/user/pspthreadman.h>
- RFC 1982 serial-number arithmetic:
  <https://www.rfc-editor.org/rfc/rfc1982.html>
- RFC 6298 retransmission timing and Karn sampling:
  <https://www.rfc-editor.org/rfc/rfc6298.html>
- RFC 9002 packet numbering, RTT and probe timeout:
  <https://www.rfc-editor.org/rfc/rfc9002.html>
- Gray and Lamport, *Consensus on Transaction Commit*:
  <https://www.microsoft.com/en-us/research/publication/consensus-on-transaction-commit/>
- Fischer, Lynch and Paterson, asynchronous consensus limit:
  <https://groups.csail.mit.edu/tds/papers/Lynch/jacm85.pdf>
- TLA+ tools and TLC model checking:
  <https://lamport.azurewebsites.net/tla/tools.html>
- CBMC proof harnesses for C:
  <https://model-checking.github.io/cbmc-training/>

## Runtime diagnostics

Gameplay diagnostics use fixed binary rings in RAM. Recording an event must
not allocate, format text, take a kernel lock or access storage. Main-thread
and network-thread writers use separate rings; files are updated only at safe
lifecycle boundaries such as a link failure, disconnect, suspend, ROM change
or clean exit.

Keep events structural and bounded. Record state transitions, elapsed pause
time, clock lead, queue high-water marks, heartbeat totals, transport epochs,
recovery decisions and performance threshold crossings. Aggregate repeated
packet and frame activity instead of logging each occurrence. Persist a
versioned, checksummed snapshot and retain at most four sessions. Logging
failure must never interrupt emulation.

Transport changes need deterministic coverage for loss, delay, reordering,
duplication, sequence wrap, queue pressure and peer restart. Real-hardware
checks should cover an idle gameplay session, menu transitions, suspend and
resume, and a complete cable transfer between a PSP-1000 and PSP-2000.

Do not commit ROMs, BIOS files, saves, savestates, cheat databases, generated
executables or local configuration. Synthetic test ROM sources and test vectors
must be original and redistributable.

Keep the public documentation in the existing four Markdown files: the root
README, changelog, this guide and the launcher README. Update an existing file
instead of adding design diaries, implementation logs or one-document-per-topic
notes. Legal notices may remain plain text beside the component they cover.

Commit subjects should be short, imperative and describe the observable
change. Preserve copyright, license and attribution notices when moving or
modifying inherited code.
