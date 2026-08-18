# Two-player GBA link cable design

Status: approved for implementation planning on 18 August 2026.

## Objective

Add local link cable emulation to GBA Rush PSP so that two PSP systems can use
the multiplayer features built into Game Boy Advance software. The two PSPs
communicate directly over the PSP ad-hoc wireless network while each game sees
the standard GBA serial hardware.

The implementation must operate at the SIO register and timing level. It must
not identify titles or translate game-specific protocols. Compatibility is
therefore defined by the supported GBA serial modes rather than a list of
special-cased games.

## Initial scope

The first release supports two PSP systems and these GBA serial modes:

- 16-bit multiplayer mode at all four GBA baud rates;
- 8-bit normal mode;
- 32-bit normal mode;
- serial completion timing and IRQ behavior;
- local PSP ad-hoc discovery, connection, status, and disconnection.

The packet format and player identifiers reserve space for four players, but
the initial implementation accepts exactly one peer.

The following are separate peripherals or platforms and are outside the first
release:

- the GBA Wireless Adapter and RFU protocol;
- UART, general-purpose, and JoyBus peripherals;
- GameCube connectivity;
- Game Boy and Game Boy Color link emulation;
- internet relays and infrastructure-mode networking;
- single-cartridge multiboot distribution.

The compatibility goal is every game that uses one of the supported two-player
SIO modes. Stable status does not imply that every commercial release has been
individually tested.

## Current limitation

The existing core identifies normal, multiplayer, UART, GPIO, and JoyBus SIO
modes, but multiplayer handling is a stub. It masks `SIOCNT`, reports a fixed
ready state, and never exchanges `SIOMULTI` data or schedules a serial event.
Normal transfers complete immediately. No PSP networking code is linked into
the emulator.

The new implementation replaces that behavior with a dedicated SIO state
machine and adds serial events to the core scheduler alongside video, timers,
DMA, and delayed IRQ processing.

## Components

### GBA SIO core

`gba_sio` owns the emulated serial state and provides a small interface to the
memory and scheduler code. Its responsibilities are:

- decode mode changes from `RCNT` and `SIOCNT`;
- preserve read-only and writable bits correctly;
- model parent/child state, player ID, ready, busy, and error flags;
- latch outgoing data and reset receive registers at transfer start;
- calculate transfer duration from mode, clock, baud rate, and player count;
- populate `SIOMULTI0` through `SIOMULTI3` or normal receive data;
- clear the start or busy bit at completion;
- request `IRQ_SERIAL` when enabled;
- reset cleanly on ROM changes and emulator reset;
- expose state required by native savestates when no network session is active.

`memory.c` delegates SIO register writes to this module. `main.c` asks it for
the next scheduled event and advances it using completed GBA cycles. Network
operations never occur inside memory access helpers or generated code.

### Link session

`link_session` connects the SIO state machine to an abstract transport. It owns
the session identifier, protocol negotiation, player assignment, transfer
sequence, heartbeat, barriers, and disconnect reason.

The PSP that creates the session is network coordinator and multiplayer Player
1. In normal mode, either game may own the internal serial clock; the network
coordinator and the GBA clock owner are intentionally separate concepts.

Different games, languages, and revisions are allowed. Each peer advertises
the ROM title, game code, revision, and CRC32 for diagnostics, but ROM identity
does not decide whether a session may start.

### Transport interface

The core-facing transport exposes operations to host, join, send, poll, and
disconnect. This boundary allows the protocol and SIO modules to run in host
tests without PSP libraries.

`psp_adhoc` implements the interface with PSPSDK. It loads the common and
ad-hoc network modules only when the link menu is opened, joins a named ad-hoc
group, and uses a reliable ordered PTP connection between the two PSPs.

A dedicated network thread performs all blocking PSP calls. It communicates
with the emulator thread through bounded, preallocated queues. The dynarec and
normal memory paths do not acquire network locks or allocate packets.

### User interface

The existing menu gains a Link cable section with:

- Create session;
- Join session;
- Disconnect;
- current role and connection status.

User-facing states are Wi-Fi disabled, waiting for peer, connecting, connected
as Player 1 or Player 2, link active, and disconnected with a reason. Search
and connection may be cancelled with Circle.

## Wire protocol

PTP provides ordered reliable delivery, while the application protocol adds
framing and session validation. Every message contains:

- a fixed `GBLK` magic value;
- protocol major and minor version;
- message type and payload length;
- session identifier;
- monotonically increasing transfer sequence;
- CRC32 covering the header, with its CRC field set to zero, and the payload.

All multibyte values use network byte order. The decoder validates the declared
length against a fixed protocol maximum before reserving queue space. Invalid
lengths, unknown mandatory message types, wrong session identifiers, invalid
CRC values, and decreasing sequence numbers terminate the session rather than
reaching the SIO core.

The initial message set is:

- `HELLO` and `SESSION_READY` for capabilities and ROM metadata;
- `SIO_STATE` for relevant mode and readiness changes;
- `TRANSFER_REQUEST` when the non-coordinator owns the normal-mode clock;
- `TRANSFER_PREPARE`, `TRANSFER_ACCEPT`, and `TRANSFER_COMMIT` for the barrier;
- `TRANSFER_ABORT` for a failed exchange;
- `HEARTBEAT` and `DISCONNECT` for session health.

The protocol major version must match. A minor-version mismatch is accepted
only when both peers advertise the required capabilities.

## Connection lifecycle

1. Player 1 creates the GBA Rush PSP ad-hoc group and listens for one peer.
2. Player 2 scans, joins the group, and opens the PTP connection.
3. Both peers exchange `HELLO`, protocol capabilities, and ROM metadata.
4. The coordinator assigns player identifiers and sends `SESSION_READY`.
5. Each SIO core reports mode and readiness transitions through `SIO_STATE`.
6. The session remains connected across game menus until the user disconnects,
   changes ROM, suspends the PSP, turns off Wi-Fi, or exits the emulator.

Discovery remains active until cancelled. A transfer barrier has a five-second
real-time timeout. Heartbeats are sent once per second while no other traffic
is present; five seconds without valid traffic is treated as a disconnect.

## Transfer synchronization

Network latency must consume wall-clock time, not GBA cycles. A transfer uses
a three-step barrier:

1. The clock owner sets Start. The SIO core latches its outgoing value, sets
   the correct busy state, and stops advancing the GBA virtual clock.
2. The coordinator issues `TRANSFER_PREPARE`. The peer processes it on the
   emulator thread, verifies mode and readiness, latches its outgoing value,
   enters the same paused state, and replies with `TRANSFER_ACCEPT`.
3. The coordinator validates both states and sends `TRANSFER_COMMIT` with the
   ordered receive values and transfer parameters. Both cores resume and
   schedule completion after the hardware-derived number of local GBA cycles.

During the barrier the network thread and PSP interface remain responsive, but
`cpu_ticks`, GBA timers, video, DMA, and serial time do not advance. Audio is
silenced cleanly instead of replaying a queued buffer after the barrier.

For multiplayer mode, Player 1 is the parent and is the only unit that may
start a transfer. Both peers receive the same four-register result, with unused
players represented by `FFFF`. For normal mode, the game selecting the internal
clock may initiate; the external-clock peer receives the prepare event as its
transfer start.

## Failure behavior

If a peer disconnects or the barrier times out, the emulator completes the
pending hardware operation as a failed cable transfer:

- busy or start is cleared;
- the multiplayer error flag is set;
- missing multiplayer data is `FFFF`;
- missing normal data is `FF` or `FFFFFFFF`;
- `IRQ_SERIAL` is requested when enabled;
- the session is closed and the disconnect reason is shown.

This gives the game a chance to run its own communication-error path instead of
leaving the emulator blocked. The emulator never fabricates a successful
exchange and never rolls back or edits an in-game save.

Queue exhaustion, malformed messages, incompatible protocol versions, network
module failures, and a disabled WLAN switch are explicit session errors. All
network resources and loaded modules are released in reverse initialization
order after failure or normal disconnection.

## Runtime restrictions

Fast-forward is unavailable while a link session is connected. Save-state
creation and loading are also unavailable because restoring only one peer
would invalidate the serial sequence. Ordinary game saves continue to work.
Video frameskip remains available because it does not change emulated time.

Cheats remain available and are not synchronized. If a cheat changes data used
by a game's link protocol, the game may reject the exchange just as it could
with modified software on physical hardware.

## Performance requirements

With no link session, serial polling and PSP network modules remain inactive.
Single-player performance must stay within one percent of the 1.0.0 baseline
in repeated real-hardware measurements.

With an idle link session, both games must retain their normal frame rate. The
only expected visible delay is a short pause at a transfer barrier. Packet
buffers and queues are fixed-size, and the implementation must not log from
the transfer hot path in release builds.

## Verification

### Host tests

The production SIO module is compiled for host tests with a fake register bank,
scheduler, and transport. Tests cover:

- mode transitions and register masks;
- parent, child, ready, ID, busy, and error flags;
- every supported baud and clock selection;
- exact scheduling and serial IRQ behavior;
- successful Normal 8, Normal 32, and Multiplayer transfers;
- timeout, disconnect, duplicate, stale, and malformed messages;
- sequence wrap and session replacement;
- clean reset between ROMs.

A two-core harness connects two independent SIO instances and injects variable
wall-clock delay without advancing their GBA clocks during barriers.

### Diagnostic ROMs

Small redistributable GBA programs exercise all supported modes and compare
sent values, received values, identifiers, status bits, and completion order.
No commercial ROM, BIOS, or save data is stored in the repository.

### Real PSP tests

At least two physical PSP systems are required before a stable release. The
test matrix includes:

- Pokémon trading and battling with compatible GBA versions;
- a two-player racing game;
- a two-player strategy game;
- different compatible ROM revisions or languages;
- ten consecutive completed exchanges;
- a continuous thirty-minute session;
- Wi-Fi loss while idle, during a barrier, and during disconnect;
- suspend, resume, ROM change, and normal emulator exit;
- single-player performance with the feature disabled.

PPSSPP or host simulation can accelerate development but cannot replace the
final physical-WLAN test.

## Acceptance criteria

The first stable release is ready when:

1. All host SIO and protocol tests pass.
2. Diagnostic ROMs pass every supported mode in both directions.
3. Two real PSPs complete the representative game matrix without emulator
   crashes, stuck busy bits, or corrupted save files.
4. A failed connection returns control with a clear error within five seconds.
5. Different compatible games can connect without a same-ROM restriction.
6. Link-disabled performance remains within one percent of the 1.0.0 baseline.
7. The implementation contains no title-specific protocol logic.

## Delivery

Work proceeds on the `feature/link-cable` branch. Core emulation, transport,
interface, and compatibility hardening remain separate reviewable commits.
Commit subjects and public documentation use the same concise human style as
the existing project history.

The first installable build is `1.1.0-beta.1`. Version `1.1.0` is published
only after the physical-PSP acceptance criteria pass. The 1.0.0 tag and release
remain unchanged.

## Technical references

- GBATEK GBA communication registers:
  <https://problemkaputt.de/gbatek.htm>
- mGBA SIO implementation:
  <https://github.com/mgba-emu/mgba/blob/master/src/gba/sio.c>
- mGBA lockstep implementation:
  <https://github.com/mgba-emu/mgba/blob/master/src/gba/sio/lockstep.c>
- PSPSDK ad-hoc networking interface:
  <https://pspdev.github.io/pspsdk/pspnet__adhoc_8h.html>
- PSPSDK ad-hoc control interface:
  <https://pspdev.github.io/pspsdk/pspnet__adhocctl_8h.html>
- libretro gpSP serial implementation:
  <https://github.com/libretro/gpsp/blob/master/serial.c>
