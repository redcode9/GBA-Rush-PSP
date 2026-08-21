# GBA Rush PSP 1.1.0 release review

Completed on 21 August 2026 against the full change from 1.0.0.

## Reviewed areas

- GBA serial registers, transfer timing, completion state, interrupts, reset,
  disconnect, and savestate handling.
- Packet framing, payload bounds, checksums, parsing, protocol negotiation,
  session identifiers, and sequence rollover.
- Host and join handshakes, transfer barriers, heartbeats, timeouts, malformed
  traffic, peer loss, and reconnect behavior.
- PSP ad-hoc initialization, command and event queues, socket ownership,
  shutdown deadlines, and cleanup order.
- CPU, memory, GUI, input, audio, ROM, reset, suspend, resume, save, and exit
  integration.
- Translation-table ordering, diagnostic ROMs, build rules, documentation,
  package contents, history metadata, and tracked-file hygiene.

## Closed findings

### Ad-hoc queue publication

The single-producer, single-consumer command and event queues published their
write indices after the payload, but the consuming side did not explicitly
order its index observation before reading that payload. Full Allegrex memory
barriers now protect both acquisition and release sides. The overflow fallback
event follows the same rule, and source checks guard the required barriers.

Transport events larger than the protocol buffer are now rejected as protocol
errors instead of being emitted with an empty payload.

### Serialized player counts

Serial completion data and restored snapshots now enforce the GBA hardware
limit of four players. Invalid snapshot data clears transient serial state, and
the host regression suite covers both completion clamping and snapshot
rejection.

### Suspend and resume ownership

Power callbacks now publish sequenced lifecycle events and leave saves, ROM
handles, link teardown, and ordinary audio recovery to the foreground. Audio
and network worker waits have finite recovery deadlines. The detailed review
is in [Runtime lifecycle review](runtime-lifecycle.md).

## Verification results

- Host tests passed with `-Wall -Wextra -Werror` for CodeBreaker, GBA SIO, link
  protocol, link session, and power lifecycle modules.
- The complete AddressSanitizer and UndefinedBehaviorSanitizer suite passed.
- Clang static analysis completed without diagnostics on every portable module
  in the host suite.
- Message ordering, lifecycle invariants, tracked-file policy, staged and
  unstaged diff checks passed.
- A clean PSP build produced the emulator EBOOT and exception handler with the
  pinned PSPDEV toolchain. The linked executable is 32-bit little-endian MIPS
  Allegrex and contains the required ad-hoc, WLAN, power, audio, and thread
  paths.
- Normal 8-bit, Normal 32-bit, and Multiplayer diagnostic GBA ROMs built with
  warnings treated as errors.
- The release ZIP extracted to the exact five-file manifest and every packaged
  file matched its build input byte for byte.
- Release-branch commit authorship, messages, tracked paths, and publication
  files contain only intentional project metadata.

## Artifact

```text
0da68296c159f0bddf2d128af9f1bf3d052e59e6bce394c70817beed40474d3a  GBA-Rush-PSP-1.1.0.zip
```

## Accepted limitation

Broad compatibility and performance testing across two physical PSP systems
and the commercial game catalog has not yet been completed. The release does
not claim Wireless Adapter, UART, General Purpose, JoyBus, GameCube link,
multiboot, more than two systems, or internet relay support.
