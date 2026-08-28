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
| Last updated | 2026-08-28 |
| Design state | Approved; replacement architecture not implemented |
| Active milestone | M1 — formal protocol model and shared transition table |
| Baseline commit | `790d3da` (`Define low-overhead runtime diagnostics`) |
| Working tree | Contains the existing link prototype and uncommitted diagnostic, transport, recovery and test work; preserve it until M1 classifies reusable parts |
| Last hardware pair | PSP-2000 host and PSP-1000 joiner |
| Last accepted build evidence | EBOOT SHA-256 `f20fb67bd216148eddd18a1dedb5c01edbd7a71f5ac8a05e670c5992aca0fab` passed host checks but did not complete a Pokémon FireRed trade |
| Last verified finding | Session and transport recovery used different timeout ownership; recovery could compare a local aborted sequence with an older remote history and regress the transaction |
| Immediate next action | Model the three-stage transfer journal, loss, duplication, reorder, backpressure and one-sided recovery before changing the production session |
| External blocker | None |

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

### Verification gates

The design is accepted in layers:

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

- both host/join role assignments;
- 10,000 consecutive transfers per synthetic supported-mode ROM;
- a 60-minute idle hot session without recovery churn;
- at least 20 complete Pokémon FireRed trades across repeated sessions;
- suspend, resume, peer exit and local exit from each exposed session phase;
- no crash, permanent freeze, divergent transfer hash or repeated serial IRQ;
- bounded exit and a single stable audio pause rather than recurring underruns.

Other PSP models are an additional compatibility gate, not a prerequisite for
developing the verified core. Public compatibility claims list the hardware
and modes actually tested.

### Milestones

| ID | State | Deliverable and exit gate |
|---|---|---|
| M0 | complete | Root-cause analysis, source research and this canonical specification |
| M1 | in progress | Formal model and one table of legal production transitions; all stated invariants checked before production rewrites |
| M2 | not started | Three transport lanes, bounded SPSC handoff, unambiguous packet numbering and adaptive probes; deterministic loss/backpressure tests pass |
| M3 | not started | Monotonic `ARMED/DECIDED/RELEASED` journal and single recovery coordinator; model and C harness traces agree |
| M4 | not started | Cycle-level SIO-hot barrier and shared finish scheduling for Normal 8/32 and two-player Multiplayer; differential traces pass |
| M5 | not started | Bounded suspend/exit, stable audio wait and complete binary failure snapshot; lifecycle fault injection passes |
| M6 | not started | Full host, sanitizer, CBMC and schedule-exploration gate with no invariant or memory failure |
| M7 | not started | PSP-1000/PSP-2000 physical acceptance matrix completed in both roles |
| M8 | deferred | GPIO/general-purpose cable behavior and a broader commercial-game matrix after M7 |

The working prototype may supply reusable parsing, logging or SIO code, but its
existing state names and recovery behavior are not compatibility constraints.
Reuse is decided only after M1 maps a component to this specification.

### Technical references

- Nintendo AGB Programming Manual, Communication Functions:
  <https://mutantstargoat.com/~nuclear/tmp/gba_prog_manual.pdf>
- mGBA SIO lockstep and timing:
  <https://github.com/mgba-emu/mgba/blob/master/src/gba/sio/lockstep.c>
- VBA-M GBA link synchronization:
  <https://github.com/visualboyadvance-m/visualboyadvance-m/blob/master/src/core/gba/gbaLink.cpp>
- Pokémon FireRed cable state and timeouts:
  <https://github.com/pret/pokefirered/blob/master/src/link.c>
- PSPDev PDP, PTP and GameMode API:
  <https://github.com/pspdev/pspsdk/blob/master/src/net/pspnet_adhoc.h>
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
