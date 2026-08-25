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

## Link transport

The two-PSP data path uses PDP after discovery and keeps the GBA protocol
independent of packet loss. Datagram headers identify the session and epoch,
carry sequence and acknowledgement state, bound the payload and protect it
with a checksum. A fixed eight-packet transmit window and sixteen-packet
receive ring provide ordered delivery without runtime allocation.

Acknowledgements are cumulative with a selective bitmap. Duplicate packets
are acknowledged again, gaps are retained within the receive window and
unacknowledged packets use bounded adaptive retransmission. Link messages are
delivered to the emulator only in sequence; critical messages remain FIFO,
while replaceable SIO, clock and heartbeat state may be coalesced before
transmission.

Recovery first retransmits missing datagrams, then probes the peer and starts
a new transport epoch on the existing PDP socket. Epoch changes use a bilateral
begin, commit and completion barrier; the session layer is notified only after
the final acknowledgement. Requests from an older epoch are consumed without
restarting recovery. The ad-hoc group is rebuilt only after the control API
reports that it has been lost. PTP is not used as a fallback.

Starting a link session must leave GBA serial I/O in a known state. If a game
has already started a cable operation, abort that transient operation before
negotiation and deliver its serial interrupt; do not import an unmatched
in-flight transfer into a new session. Once connected, the game can retry it
against the synchronized peer.

No transport or transfer barrier may hold the emulation loop indefinitely.
Every wait needs a monotonic deadline and must either resume coherently or
abort the current transfer and return control to the game. Cover late attach,
one-sided attach and timeout exit in the host harness.

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
