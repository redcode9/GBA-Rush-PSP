# Link cable implementation

The protocol-v3 implementation is on `feature/link-cable`; `main` retains the
older protocol. Use matching builds on both consoles. The cable is experimental
and has not completed the [1.4.0 hardware tests](ROADMAP-1.4.0.md#release-tests).

## Components

`psp_adhoc` and the transport modules handle discovery, PDP datagrams,
acknowledgements and queues. `link_session` owns connection identity, transfer
state and recovery. `gba_sio` owns guest registers, completion timing and serial
interrupts. Network retries cannot apply a guest result directly.

The network worker and emulator exchange work through bounded queues. Critical
transfer replies wake the worker and take priority over replaceable presence
and clock updates. Logging uses separate binary RAM rings per thread; storage
writes happen at disconnect, failure, suspend, ROM change or exit.

## Multiplayer stream

Protocol v3 negotiates `LINK_CAPABILITY_STREAM_TRANSFER`. The host sends one
`TRANSFER_START`; the joiner latches its word on the emulation thread and returns
`TRANSFER_REPLY`. Only one dependent word is active. A reply acknowledges its
START; the next START or an idle heartbeat acknowledges the cached reply.
Duplicates replay the existing result without applying registers or IRQ twice.

This path replaces the older multi-phase transfer exchange. Clock anchors are
carried in transfers and low-rate heartbeats rather than a separate fast clock
stream. Heartbeat requests are tokenized and bidirectional; replies are not
echoed. Presence is refreshed periodically so a lost final update can be repaired.

An early next START waits while the preceding SIO event is busy, then uses the
master's inter-word cycle delta before reading the next outgoing word. Recovery
retains pending starts and cached replies, and rejects old-generation packets.
A reply timeout requests recovery rather than cancelling a result the peer may
already have scheduled.

The first word still uses a local timing base, and current clock hints are
advisory. Later packet mapping cannot rewind a guest that has already passed
the event. A shared safe timeline and guest-executed timing tests remain open.

## Normal modes and recovery

Normal 8/32-bit transfers use the journal states
`EMPTY → ARMED → DECIDED → RELEASED → EMPTY`. An undecided arm can be cancelled;
a decided result is immutable. Released sequence/hash pairs form checkpoints.
Transport acknowledgements and application decisions are different events.

One coordinator stops new transfers, reconciles checkpoints and repairs the
transport. Known decisions are replayed without regression. Conflicting hashes
or unprovable decisions close the session. Rebuilding a socket cannot reset
transfer evidence or extend the session deadline indefinitely. Process restart
creates a new session.

No-ROM setup initializes SIO storage before session admission. Fast-forward and
savestates are disabled while connected. Reset, ROM change, suspend and exit
close the session through a bounded worker shutdown. Serial waits must keep
input, audio policy and power handling responsive.

## Tests and logs

Host tests cover protocol parsing, SIO, queues, streams, recovery and lifecycle.
`test_link_cable_runtime` uses the production scheduler with separate guest
speeds, delayed handlers, packet faults and burst transfers. TLA+ models and
CBMC harnesses add bounded state and memory checks. Hardware scheduling, radio
and guest ROM execution still need console tests.

On the development build, run the built-in no-ROM benchmark before testing games.
Start fresh emulator processes for each role assignment, leave WLAN on, disable
WLAN Power Save and automatic suspend, and copy both terminal logs before another
run. An on-screen PASS without matching terminal records is incomplete evidence.

```sh
python3 tools/decode_runtime_log.py /path/to/GBARushPSP/logs
python3 tools/analyze_link_benchmark.py host.gbrlog joiner.gbrlog --json
```

Use `--run-id ID` if the logs contain several common benchmark runs. Preserve
internal error domain/code separately from the original PSP API phase and return
value. A full log ring means packet history may be incomplete.

The supported scope is two PSPs and the three GBA cable modes. Wireless Adapter,
additional players, UART, JoyBus, multiboot and internet relay are separate work.

## References

- [GBATEK serial registers](https://mgba-emu.github.io/gbatek/)
- [mGBA SIO timing](https://github.com/mgba-emu/mgba/blob/master/src/gba/sio.c)
- [FireRed link routines](https://github.com/pret/pokefirered/blob/master/src/link.c)
- [PSPSDK ad-hoc interface](https://github.com/pspdev/pspsdk/blob/master/src/net/pspnet_adhoc.h)
