# Two-PSP link cable specification

This document defines the maintained protocol and correctness requirements.
The [1.4.0 roadmap](ROADMAP-1.4.0.md) owns remaining work and release acceptance;
the [contributor guide](../CONTRIBUTING.md) owns build and verification commands.

## Scope and implementation status

The target is a local GBA cable between two physical PSP systems over ad-hoc
Wi-Fi. The supported implementation paths are Normal 8-bit, Normal 32-bit and
two-player Multiplayer 16-bit. Host and joiner roles must work on PSP-1000 and
PSP-2000. Three/four-player sessions, Wireless Adapter, internet relay, UART,
JoyBus, multiboot and general-purpose GPIO are separate scopes.

Protocol v3 uses a two-message stream for Multiplayer 16-bit. Normal 8/32-bit
modes retain the transfer journal. Software coverage exists for both paths,
but real-hardware cable acceptance and a conservative guest timing horizon
remain open. A connected PSP socket is not evidence of a usable GBA cable.

## Ownership and safety

| Layer | Responsibility |
|---|---|
| PSP transport | Peer discovery, bounded datagram I/O, delivery timing, acknowledgements and backpressure |
| Link session | Session identity, generations, transfer state and recovery coordination |
| GBA SIO | Registers, modes, latched data, guest completion cycles and serial IRQ delivery |

Transport cannot decide a GBA transfer, SIO cannot interpret network retries,
and session transitions cannot rebuild sockets. One recovery coordinator owns
quiescence, transport repair and resumption.

Required invariants are:

1. At most one causally dependent transfer is active; sequence ordering remains
   valid across bounded integer wrap.
2. A transfer has one immutable canonical result. Neither retries nor recovery
   may apply its registers or serial IRQ more than once.
3. A decided journal result cannot regress to an arm or become a local abort.
4. Old session nonces, epochs and stream generations cannot mutate current
   state. Incompatible protocol, identity or SIO mode cannot expose readiness.
5. Recovery preserves shared checkpoints and known results or closes the
   session; it never guesses a missing decision.
6. Queues, reply caches, recovery work and diagnostic buffers remain bounded.
   Backpressure cannot create duplicate logical work.
7. Hot emulation cannot cross the earliest possible unreceived serial event.
   Wall-clock packet delay must not change the GBA-visible transfer duration.
8. UI, audio policy and power handling remain serviceable during waits.
   Suspend and exit have fixed monotonic deadlines.

Conditional progress assumes both processes continue running and the channel
eventually delivers packets after a finite fault burst. Permanent peer or
radio failure must produce bounded, clean shutdown.

## Protocol-v3 multiplayer stream

Both peers advertise protocol major 3 and `LINK_CAPABILITY_STREAM_TRANSFER`.
Protocol-v2 builds fail negotiation with v3 rather than exchanging transfers.

The host sends one immutable `TRANSFER_START`; the joiner latches its outgoing
word on the emulation thread and sends one `TRANSFER_REPLY`. Receipt of
`REPLY(N)` acknowledges `START(N)`. `START(N+1)` or an idle heartbeat
acknowledges the cached reply. Duplicates replay bounded state without repeating
a register update or serial IRQ.

This path sends no `PREPARE`, `ACCEPT`, `COMMIT`, `COMMIT_ACK`, `RELEASE` or
`RELEASE_ACK` exchange. Clock anchors travel in stream records and low-rate
heartbeats, with no standalone high-frequency CLOCK stream.

A START for the next word does not prove the joiner's guest executed the
previous serial interrupt handler. The implementation retains one unlatched
START while the previous SIO event is busy, then maps the master's inter-word
cycle delta onto the prior local start before reading `SIOMLT_SEND`.
Duplicate STARTs cannot reset this wait or latch data again.

Recovery retains a pending start or cached reply as semantic state and
re-encodes it for the new generation. Old-generation datagrams are rejected
before session mutation. An uncertain reply timeout requests coordinated
recovery rather than cancelling a result already scheduled by the peer.

The first word still uses a local timing base. Later START mapping cannot
rewind an advanced guest. The existing freshness-limited pacing is advisory;
it does not satisfy the generic safe-horizon requirement below.

## Transport and presence

PDP remains the transport. Bounded lanes separate immediate ACK/close control,
reliable critical records and replaceable heartbeat/clock/SIO presence.
Critical replies wake the PSP network worker; bounded hot drains give them
priority, while cold waits retain low CPU use.

The thread handoff uses bounded single-producer/single-consumer rings with
explicit publication ordering. A local ring does not establish distributed
ordering: session identity, sequence and transfer state do that.

Retransmission uses measured RTT/variance, avoids ambiguous samples after
retries and applies bounded backoff. A probe requests acknowledgement; it does
not independently decide that a GBA operation failed. Queue sojourn time and
repeated `NO_SPACE` responses inform pressure handling.

Liveness probes are tokenized and bidirectional. Either role answers a
matching request once, replies are not echoed, duplicates are suppressed and
unmatched replies cannot complete a pending probe. Recovery resets outstanding
and duplicate-token state while retaining cumulative diagnostics.

At most one scheduled heartbeat request is allowed per
`LINK_SESSION_HEARTBEAT_US`. Immediate correlated replies bypass that request
gate. Successful stream and heartbeat publication resets the wall-clock
request interval; a GBA-cycle deadline cannot authorize extra requests.

Unchanged menu/SIO presence is refreshed once per heartbeat interval. The
latest-wins mailbox bounds queue growth, but the final update still needs
repair after loss. Presence refresh must not suppress due correlated probes.

## Guest timing requirements

Cold peers may free-run. Before cable readiness is exposed in hot mode, peers
need a shared cycle base and a conservative horizon for possible remote serial
events. Clock estimates may inform pacing but cannot authorize a past event.

The master sequences transfers and derives completion cycles from the emulated
GBA timing table. Both peers must apply the same result at the corresponding
guest completion event. Mode changes may cancel an undecided arm, but cannot
cancel an immutable result already scheduled or decided.

Verification must include guest-executed serial handlers, independently
advancing cycle bases, cycle wrap, first-word admission and late events. The
current register-level tests and atomic guest application in the stream model
do not establish arbitrary-ROM timing correctness.

## Normal-mode transfer journal and recovery

Normal 8/32-bit transfers retain one monotonic journal:

```text
EMPTY -> ARMED -> DECIDED -> RELEASED -> EMPTY
```

An arm may be cancelled before a decision. A host-issued decision is immutable;
a shared release authorizes completion at its recorded guest cycle. Returning
to EMPTY retains the released sequence and hash for recovery. Transport ACKs
and application decision acknowledgements are separate facts.

Recovery stops new arms, freezes the hot horizon and exchanges the released
checkpoint, active stage/hash and actual SIO snapshot/revision:

| Evidence | Result |
|---|---|
| Shared release, no decision | Cancel an undecided arm, republish SIO and resume |
| Host proves an issued decision | Replay the decision and release without regression |
| Both retain the same release | Resume from that checkpoint |
| Same sequence with different hashes | Close with a protocol error |
| Unproven decision or incompatible identity | Close without guessing |

Transport resync, group rebuild and stack restart are steps inside this one
recovery. They cannot reset the transfer journal or extend the overall session
deadline indefinitely. A new recovery supersedes an unfinished transport
attempt. A process restart creates a new session because volatile evidence
has been lost.

## Lifecycle and diagnostics

No-ROM connection must initialize SIO register storage before session
admission. Repeated preparation must preserve a loaded game's state.
Fast-forward and savestates are disabled while a session is open. Reset, ROM
replacement, suspend and exit disconnect through the bounded lifecycle path.

Shutdown stops admission, signals the worker, waits to a monotonic deadline,
closes PDP/control resources, then unloads modules after their users stop.
Audio drains or fades once, holds stable silence during a link barrier, and
restores on release. The emulation thread cannot block on socket polling,
worker destruction, diagnostic formatting or storage.

Each thread records into its own fixed binary RAM ring without allocation,
text formatting, locks or storage I/O. Versioned, checksummed snapshots flush
only at safe failure/disconnect/suspend/ROM-change/exit boundaries; at most four
sessions are retained. Lost-record counts make incomplete evidence visible.

First-failure records separate the internal error domain/code from the original
PSP API phase and signed/hex return value. Timeout records distinguish receive
silence, transfer, pacing, pressure, recovery, negotiation and shutdown.
Snapshots retain session/generation, sequence, hash, SIO revision, cycles,
RTT/RTO, queues, worker wakes, barrier latency, audio and teardown phase.

## Benchmark contract

The no-ROM benchmark offers 1,000 words/s for ten minutes. Acceptance requires
at least 600 completed words/s, nine-word burst p99 ≤ 20 ms, zero full recovery,
no persistent queue growth, stream timeout, stale-generation event or audio
underrun, and no main-thread starvation over 50 ms.

Both roles produce an immutable terminal summary for one common run ID with
matching word counts/digests and valid checksums. A missing terminal record is
missing evidence. Run IDs are identifiers, not timestamps; select `--run-id`
when several common runs exist. Short host workloads cannot satisfy this gate.

Manual stop, ROM change and suspend abort a benchmark. Transport failure and
lack of progress fail it, including a silent joiner before the first word.
Results are logged before teardown or replacement, with a bounded final
STOP/ACK drain. Benchmark network service must not depend on menu vblank cadence.

## Verification layers

TLC models check bounded safety and conditional progress. Production C
transitions are exercised with virtual time and explicit schedule exploration;
CBMC checks parsing, bounds, arithmetic, transitions and memory safety.
ASan/UBSan supplement those checks. Models abstract PSP firmware and guest
execution; source-pattern checks are structural guards rather than runtime
proofs.

`test_link_cable_runtime` covers the production scheduler/lanes with delayed
guest handlers, asymmetric cycle budgets and wire latency, nine-word bursts,
loss, duplicates and pressure. `test_link_pipeline` retains legacy transport
and recovery coverage. Neither replaces physical two-PSP acceptance.

Physical acceptance requires both ten-minute benchmark roles, diagnostic ROMs,
idle-hot stability, completed FireRed trades with save/reload verification, and
the lifecycle/failure matrix listed in the roadmap.

## Technical references

- [GBATEK serial I/O reference](https://mgba-emu.github.io/gbatek/)
- [mGBA SIO](https://github.com/mgba-emu/mgba/blob/master/src/gba/sio.c) and
  [lockstep](https://github.com/mgba-emu/mgba/blob/master/src/gba/sio/lockstep.c)
- [FireRed link routines](https://github.com/pret/pokefirered/blob/master/src/link.c)
- [PSPSDK ad-hoc API](https://github.com/pspdev/pspsdk/blob/master/src/net/pspnet_adhoc.h)
- [Serial-number arithmetic](https://www.rfc-editor.org/rfc/rfc1982.html) and
  [retransmission timing](https://www.rfc-editor.org/rfc/rfc6298.html)
- [TLA+ tools](https://lamport.azurewebsites.net/tla/tools.html) and
  [CBMC proof harnesses](https://model-checking.github.io/cbmc-training/)
