# Two-player GBA link cable implementation plan

Status: approved for implementation on 18 August 2026.

This plan implements the design in `docs/design/link-cable.md` without changing
the 1.0.0 tag or release. Development stays on `feature/link-cable` until the
two-PSP acceptance tests are complete.

## Working rules

- Keep GBA serial emulation, session protocol, and PSP networking in separate
  modules.
- Keep all PSP headers out of the portable SIO and session code.
- Write host tests before connecting a new module to the emulator.
- Do not call network APIs, allocate memory, or write logs from generated code
  or memory access helpers.
- Keep generated ROMs, PSP binaries, saves, BIOS files, and test captures out of
  Git.
- Run `make check` after every stage and build the PSP target whenever a stage
  changes PSP-facing code.
- Use small commits whose subjects describe the completed change.

## Stage 1: portable SIO core

### Files

- Add `source/src/gba_sio.h`.
- Add `source/src/gba_sio.c`.
- Add `tests/test_gba_sio.c`.
- Update the root `Makefile` and `source/Makefile`.

### Work

1. Define explicit register offsets for `SIODATA32`, `SIOMULTI0` through
   `SIOMULTI3`, `SIOMLT_SEND`, `SIOCNT`, and `RCNT`.
2. Give the module a pointer to the 16-bit I/O register bank rather than a
   dependency on `memory.c` globals.
3. Implement mode decoding from `RCNT` and `SIOCNT` and preserve writable and
   read-only bits for Normal 8, Normal 32, and Multiplayer modes.
4. Track connection state, zero-based hardware player ID, ready, busy, error,
   interrupt enable, outgoing data, received data, pending start, and remaining
   transfer cycles.
5. Treat a Start write as a pending event. The write path latches the register
   values but does not decrement the new transfer by CPU cycles that elapsed
   before that write.
6. Use hardware-derived cycle counts for both normal clock rates and all four
   multiplayer baud rates. Verify the values independently against GBATEK and
   the current mGBA timing tables.
7. Complete transfers by updating the receive registers, clearing Start or
   Busy, setting the correct error state, and returning an `IRQ_SERIAL` request
   to the caller.
8. Provide explicit reset, disconnect-failure, commit, advance, next-event,
   pending-start, and serialization interfaces.
9. Keep unsupported UART, GPIO, and JoyBus modes inert without changing their
   current register storage behavior.

### Tests

- Reset values and every mode transition.
- `RCNT` and `SIOCNT` masks and read-only bits.
- Player 1 and Player 2 IDs, ready, busy, and error states.
- Normal 8 and Normal 32 at 256 KHz and 2 MHz.
- Multiplayer at 9600, 38400, 57600, and 115200 baud.
- Start-edge detection, repeated Start writes, completion data, and serial IRQ.
- Disconnect completion values: `FF`, `FFFFFFFF`, and unused `FFFF` words.
- Reset during an active transfer.

### Exit condition

`tests/test_gba_sio.c` passes under `-Wall -Wextra -Werror`, and the existing
CodeBreaker tests still pass.

Suggested commit: `Add the GBA serial core`

## Stage 2: framed link protocol

### Files

- Add `source/src/link_protocol.h`.
- Add `source/src/link_protocol.c`.
- Add `tests/test_link_protocol.c`.
- Update the root `Makefile` and `source/Makefile`.

### Work

1. Define protocol version 1.0 and encode every field manually. Do not send C
   structs directly over the network.
2. Freeze a 24-byte header with this layout:

   | Offset | Size | Field |
   | ---: | ---: | --- |
   | 0 | 4 | `GBLK` magic |
   | 4 | 1 | major version |
   | 5 | 1 | minor version |
   | 6 | 1 | message type |
   | 7 | 1 | flags |
   | 8 | 2 | payload length |
   | 10 | 2 | header length |
   | 12 | 4 | session ID |
   | 16 | 4 | transfer sequence |
   | 20 | 4 | CRC32 |

3. Set the maximum payload to 128 bytes and maximum packet to 152 bytes.
4. Store every multibyte field in network byte order.
5. Calculate CRC32 over the header with bytes 20 through 23 set to zero,
   followed by the declared payload. Reuse the zlib implementation already
   shipped by the project.
6. Define capability bits and payload codecs for `HELLO`, `SESSION_READY`,
   `SIO_STATE`, `TRANSFER_REQUEST`, `TRANSFER_PREPARE`, `TRANSFER_ACCEPT`,
   `TRANSFER_COMMIT`, `TRANSFER_ABORT`, `HEARTBEAT`, and `DISCONNECT`.
7. Include ROM title, game code, revision, exact ROM size, and ROM CRC32 in
   `HELLO`. These fields are diagnostic and never enforce a same-ROM rule.
8. Reject unknown mandatory messages, invalid sizes, incompatible major
   versions, bad CRC values, wrong session IDs, and stale transfer sequences
   before they reach the session state machine.
9. Use wrap-safe 32-bit sequence comparisons and allow repeated sequence values
   only for different messages belonging to the same active transfer.

### Tests

- Golden byte vectors for every message type.
- Big-endian encoding on a little-endian host.
- Empty and maximum-size payloads.
- Truncated header, truncated payload, oversized payload, and trailing bytes.
- Bad magic, header length, version, type, session ID, CRC, and sequence.
- Sequence wrap from `FFFFFFFF` to `00000000`.
- Incremental parsing with every possible split point in a PTP byte stream.

### Exit condition

All protocol vectors round-trip and malformed input is rejected without an
out-of-bounds read or write.

Suggested commit: `Define the link cable protocol`

## Stage 3: session state machine and fake transport

### Files

- Add `source/src/link_transport.h`.
- Add `source/src/link_session.h`.
- Add `source/src/link_session.c`.
- Add `tests/fake_link_transport.h`.
- Add `tests/fake_link_transport.c`.
- Add `tests/test_link_session.c`.
- Update both Makefiles.

### Work

1. Define a transport interface for host, scan, join, send, poll, disconnect,
   and monotonic wall-clock time.
2. Keep `link_session` single-threaded. Only transport queues cross the PSP
   thread boundary.
3. Implement states for idle, discovering, hosting, connecting, negotiating,
   connected, preparing, waiting for acceptance, waiting for commit,
   disconnecting, and failed.
4. Assign the creator zero-based hardware player ID 0 and the joiner ID 1 while
   displaying them as Player 1 and Player 2.
5. Negotiate protocol capabilities and exchange ROM metadata without rejecting
   different ROMs.
6. Send heartbeats after one second without other traffic and close the session
   after five seconds without a valid packet.
7. Implement the multiplayer barrier:
   - Player 1 latches its word and sends `TRANSFER_PREPARE`.
   - Player 2 verifies mode and readiness, latches its word, and replies with
     `TRANSFER_ACCEPT`.
   - Player 1 sends `TRANSFER_COMMIT` containing the ordered result words.
8. Implement both normal-mode clock directions. When Player 2 owns the internal
   clock, it first sends `TRANSFER_REQUEST`; Player 1 then coordinates Prepare,
   Accept, and Commit.
9. Abort a barrier after five seconds, on a malformed or stale packet, on mode
   disagreement, on queue exhaustion, or on transport loss.
10. Return typed events to the emulator: state changed, barrier entered,
    transfer committed, transfer aborted, and session closed.

### Tests

- Host and join negotiation with matching and different ROM metadata.
- Multiplayer transfer in both directions at every baud rate.
- Normal 8 and Normal 32 with either peer owning the clock.
- Variable and asymmetric wall-clock delay while both virtual clocks remain
  frozen.
- Duplicate, delayed, reordered, stale, malformed, and wrong-session packets.
- Heartbeat, barrier timeout, disconnect, reconnect, and sequence wrap.
- Queue-full behavior and clean shutdown in every state.
- Ten consecutive transfers and a simulated thirty-minute idle session.

### Exit condition

Two independent SIO cores complete transfers through the fake transport with
identical ordered results and no virtual-time advance during barriers.

Suggested commit: `Add link session synchronization`

## Stage 4: emulator and dynarec integration

### Files

- Update `source/src/cpu.h`.
- Update `source/src/mips_stub.S`.
- Update `source/src/memory.h`.
- Update `source/src/memory.c`.
- Update `source/src/main.h`.
- Update `source/src/main.c`.
- Update `source/src/sound.h`.
- Update `source/src/sound.c`.
- Update the SIO and session tests with integration fixtures.

### Work

1. Add `CPU_ALERT_LINK` as bit 5 and expand the assembly alert mask from
   `0x1e` to `0x3e`. This forces an immediate return from generated code when a
   Start write begins a synchronized transfer.
2. Route 8-, 16-, and 32-bit writes touching `SIODATA`, `SIOMULTI`, `SIOCNT`,
   `SIOMLT_SEND`, or `RCNT` through the SIO module while preserving the
   existing I/O write semantics for other modes.
3. In `update_gba`, first apply the cycles completed before the Start write,
   then promote the pending start into a session barrier. Never subtract those
   earlier cycles from the new serial transfer.
4. Add the SIO completion countdown to `CHECK_COUNT` so serial completion is a
   first-class scheduler event alongside timers, video, DMA, and delayed IRQs.
5. Pump connected-session events at every scheduler return and from menu loops.
   An inbound external-clock transfer may start at the next scheduler boundary,
   which is at most one existing video event away.
6. While a barrier is active, run a short wall-clock wait loop that pumps the
   transport and PSP callbacks but does not advance `cpu_ticks`, timers, video,
   DMA, sound, or serial time.
7. Add sound helpers that mute and discard any partial output buffer when a
   barrier begins, then restart timing cleanly after commit or abort.
8. Reset SIO state on emulator reset and ROM change. Abort and disconnect a
   live session before replacing the ROM.
9. Append a versioned SIO block to the unused tail of the existing fixed-size
   savestate buffer. Do not move existing fields. Old savestates contain zeros
   at that offset and must load as a reset SIO state.
10. Add a bounds check proving that the complete serialized state remains
    within `SAVESTATE_SIZE`.

### Tests

- Start writes cause an immediate dynarec alert.
- Cycles before Start advance once; transfer cycles begin after the barrier.
- Serial completion wins the scheduler minimum when earlier than video/timers.
- IRQ delivery follows normal delayed IRQ behavior.
- Old savestates load without shifting existing data.
- New savestates restore an idle, non-network SIO transfer correctly.
- Reset and ROM replacement leave no pending serial or session state.

### Exit condition

Host checks pass, the PSP target links, normal single-player games still boot,
and disconnected SIO behavior no longer completes a transfer immediately.

Suggested commit: `Integrate serial timing with the emulator`

## Stage 5: PSP ad-hoc transport

### Files

- Add `source/src/psp_adhoc.h`.
- Add `source/src/psp_adhoc.c`.
- Update `source/src/common.h` only for PSP-wide declarations that cannot remain
  private.
- Update `source/Makefile`.

### Work

1. Link the PSPSDK net, ad-hoc, utility, and WLAN libraries required by the
   final calls. Verify the exact link set with the current official PSPDev
   container rather than relying on transitive libraries.
2. Check the physical WLAN switch before initialization.
3. Load `PSP_NET_MODULE_COMMON` and `PSP_NET_MODULE_ADHOC`, then initialize
   `sceNet`, `sceNetAdhoc`, and `sceNetAdhocctl` in a tracked order.
4. Use a fixed product identifier and an ad-hoc group name of at most eight
   alphanumeric characters. Reserve one fixed PTP port for GBA Rush PSP.
5. Host with `sceNetAdhocctlCreate`, scan and join with
   `sceNetAdhocctlScan`/`sceNetAdhocctlGetScanInfo`/`sceNetAdhocctlJoin`, and
   wait for the control state to report connected.
6. Create a PTP listener for Player 1 and a PTP connection for Player 2. Handle
   partial sends and receives because PTP exposes a byte stream rather than
   application packets.
7. Run all blocking calls on one dedicated PSP thread. Use two bounded
   preallocated queues for commands and events; the emulator side only uses
   non-blocking queue operations.
8. Accumulate incoming bytes until a full validated protocol frame is present.
   Never trust the payload length before the protocol decoder accepts it.
9. Use finite call timeouts so cancel, shutdown, and suspend cannot leave the
   network thread stuck.
10. On shutdown, close PTP sockets, leave the ad-hoc group, remove the control
    handler, terminate the libraries, unload ADHOC, and unload COMMON in reverse
    order. Every partial-initialization path uses the same cleanup routine.
11. Map raw PSP error values to stable internal reasons; retain the raw value
    only for an optional diagnostic line.

### Tests

- Compile-time queue-size and packet-size assertions.
- Host-side queue wrap, full, empty, and shutdown tests where portable.
- PSP smoke test for WLAN disabled, create/cancel, scan/cancel, connect,
  disconnect, reconnect, suspend, and exit.
- Send protocol frames in fragments and coalesced groups to verify stream
  reconstruction.

### Exit condition

Two PSPs can create and join a session, exchange validated heartbeat frames for
thirty minutes, disconnect, and reconnect without restarting the emulator.

Suggested commit: `Add PSP ad-hoc transport`

## Stage 6: menu, lifecycle, and runtime restrictions

### Files

- Update `source/src/gui.c` and `source/src/gui.h`.
- Update `source/src/message.h`.
- Update all files under `source/src/text/`.
- Update `source/src/input.c`.
- Update state entry points in `source/src/memory.c`.
- Update shutdown and power callbacks in `source/src/main.c`.
- Update ROM metadata handling in `source/src/memory.c`, `source/src/memory.h`,
  `source/src/zip.c`, and `source/src/zip.h` as needed.

### Work

1. Add a Link cable submenu to the unused main-menu line 11 so the existing
   layout does not need to move.
2. Add Create session, Join session, Disconnect, and a read-only status line.
3. Keep discovery and connection in a cancellable modal loop that continues to
   pump session events and closes with Circle.
4. Display Wi-Fi disabled, waiting, connecting, Player 1, Player 2, active, and
   disconnected states with a concise reason.
5. Add strings for Japanese, English, Simplified Chinese, Traditional Chinese,
   and Italian without changing the message-array order between languages.
6. Force fast-forward off while connected and ignore its configured hold button
   until the session closes.
7. Reject state save and load operations at both GUI and low-level entry points
   while connected. Skip the automatic suspend savestate rather than capturing
   a half-finished barrier.
8. Disconnect before reset, ROM replacement, normal exit, sleep, or suspend.
   Keep ordinary in-game backup saves enabled.
9. Compute exact ROM CRC32 once per load. Use the decompressed buffer for ZIP
   ROMs and a chunked pass that restores the file position for paged raw ROMs.
10. Keep cheats enabled and unsynchronized as specified by the design.

### Tests

- Every menu action from first-load and running-game states.
- Cancel during scan, connect, and barrier wait.
- Fast-forward button held before, during, and after a session.
- Manual, shortcut, file-browser, and automatic savestate paths while linked.
- Reset, ROM change, suspend, resume, and exit from every connection state.
- ROM metadata for raw, ZIP, in-memory, and paged loads.

### Exit condition

The feature can be operated entirely from the PSP interface, and no lifecycle
path leaves a network thread, socket, module, fast-forward state, or partial
savestate behind.

Suggested commit: `Add link cable controls`

## Stage 7: diagnostics and compatibility hardening

### Files

- Add source under `tests/link-rom/` for redistributable diagnostic ROMs.
- Add build instructions without tracking generated `.gba` files.
- Expand host integration tests.
- Add `docs/LINK_CABLE.md`.
- Update `README.md` and `CHANGELOG.md` only when the beta is ready.

### Work

1. Build small diagnostic programs for Normal 8, Normal 32, and Multiplayer.
   Display sent data, received data, mode, player ID, status bits, IRQ count,
   and completion order on both screens.
2. Add an automated two-core stress test with random valid delays and thousands
   of sequential transfers.
3. Test malformed traffic and simulated disconnects under AddressSanitizer and
   UndefinedBehaviorSanitizer on the host.
4. Measure link-disabled performance against the 1.0.0 build with the same ROM,
   scene, CPU clock, renderer, frameskip, and measurement interval.
5. Profile only if the difference exceeds one percent. Keep any diagnostics
   behind a compile-time development flag.
6. Document supported modes, setup, limitations, error messages, savestate and
   fast-forward restrictions, and the difference between cable link and the GBA
   Wireless Adapter.

### Exit condition

Host checks, sanitizers, diagnostic ROMs, PSP build, and the one-percent
single-player performance requirement all pass.

Suggested commit: `Harden link cable compatibility`

## Stage 8: two-PSP validation and beta package

1. Back up the installed emulator directory from both PSP memory cards.
2. Install the same candidate build and keep each console's existing ROM and
   save files separate.
3. Run the physical test matrix from the design:
   - all diagnostic ROM modes in both directions;
   - Pokémon trade and battle with compatible versions;
   - one racing game and one strategy game;
   - compatible ROMs with different revisions or languages;
   - ten consecutive exchanges;
   - a thirty-minute connected session;
   - WLAN loss while idle and during a barrier;
   - suspend, resume, ROM change, and clean exit.
4. Re-run the 1.0.0 performance comparison on physical hardware.
5. Package `1.1.0-beta.1` only after the matrix has no emulator crash, stuck
   transfer, or save corruption.
6. Keep the beta local until its files and checksum are verified. Publishing a
   release remains a separate explicit action.
7. Promote to 1.1.0 only after beta feedback and a repeated physical test pass.

Suggested commit: `Prepare GBA Rush PSP 1.1.0 beta 1`

## Verification commands

Run after portable stages:

```sh
make check
```

Run sanitizer builds with the host compiler once the additional targets exist:

```sh
make test-sanitize
```

Build with the official PSPDev container when Docker is available:

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace/source \
  pspdev/pspdev:latest \
  make
```

Before every commit:

```sh
git diff --check
make check
```

Before packaging:

```sh
make package VERSION=1.1.0-beta.1
```

## Completion boundary

Code completion means all portable checks and the PSP build pass and the
candidate is ready for two-console testing. Stable release completion requires
the physical acceptance matrix; simulation alone cannot satisfy it.
