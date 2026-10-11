# Remaining work for 1.4.0

The release target is a working two-player GBA cable over PSP ad-hoc Wi-Fi,
covering Normal 8-bit, Normal 32-bit and Multiplayer 16-bit modes on PSP-1000
and PSP-2000, with either console acting as host.

The current build is the `1.4.0-rc.4` candidate, prepared on `release/1.4.0`
for integration through `develop` into `master`. The last recorded FireRed test
on September 16 failed before any serial transfer and spent about 20 seconds
and 44.6 seconds in recovery on the two consoles. No complete trade or paired
ten-minute benchmark has passed. The cause of the pre-transfer failure and
the timing of arbitrary guest programs remain unresolved.

## Link cable

- [ ] Reproduce the connection failure with known source, toolchain and EBOOT
  hashes, keeping the first failing pair of logs.
- [x] Preserve full failure details and decode packed counters and exclusive
  transport-health states in snapshot v4. Keep older snapshots readable.
- [ ] Test the actual network-worker queues and scheduling, including one-way
  packet loss, lost presence updates and pending recovery requests.
- [ ] Fix connection liveness and recovery without blocking input, menus, audio
  or power handling during a serial wait.
- [ ] Establish a shared serial timeline for the first word, delayed words,
  mode changes and guest interrupt handlers. Preserve scheduled results across
  recovery and reject packets from old sessions.
- [ ] Compare guest-executed register and IRQ traces for all three modes with
  documented reference behavior. Cover cycle wrap, asymmetric guest speeds,
  loss, duplicates, reordering, pressure and disconnects.

## Runtime and build fixes

The upstream review of October 11, 2026 identified these useful changes to adapt:

- [x] Adapt the [startup-menu fix](https://github.com/andymcca/TempGBA4PSP-mod/commit/068db08b794a1372672f6e4f87654023aeb54835),
  retaining any state the menu restored. Show the auto-savestate prompt on
  direct launches too, and restore HOME-dialog handling after the file browser.
- [x] Port invalidation of aligned IWRAM stack stores from
  the [SMOL fix](https://github.com/andymcca/TempGBA4PSP-mod/commit/bd150150a3f7b0495261ae86be3d24deb6acb97b)
  while preserving stores below SP. Verify SMOL overlays and audio on PSP.
- [x] Add the missing [included-renderer dependencies](https://github.com/andymcca/TempGBA4PSP-mod/commit/1b7c5a9b382f647cde41cab0f1de4990839a2cda).
  Common-header dependencies already exist.
- [x] Preserve character boundaries in browser clipping and scrolling. Compile
  UTF-8 translations as GBK and keep Shift-JIS filenames and kana.
- [x] Preserve stopped reports' ROM identity and settings; test DMA accounting
  and frame-time probes at clock wrap.
- [x] Test and fix stale ROM-page pointers, failed seeks, short reads and valid
  partial final pages without changing eviction policy or adding prefetch.
- [ ] Check fonts, startup dialogs, SMOL overlays, paging recovery and probe
  overhead on both PSP models.

## Release tests

- [ ] Run ten-minute no-ROM benchmarks with host/join roles reversed. Both need
  at least 600 completed words/s at an offered 1,000 words/s, nine-word burst
  p99 at most 20 ms, no full recovery, persistent queue growth, stream timeout,
  stale-generation event or audio underrun, and no main-thread pause over 50 ms.
  Check both terminal logs with `tools/analyze_link_benchmark.py`.
- [ ] Complete 10,000 transfers per diagnostic mode and a 60-minute idle hot
  session without recovery churn, covering both roles.
- [ ] Complete at least 20 FireRed trades across repeated sessions and both
  roles. Verify room exit, in-game saves and both received Pokémon after reload.
- [ ] Test user disconnect, WLAN loss, menus, suspend/resume, peer exit and local
  exit. Recovery or shutdown must finish without a crash, freeze or duplicate IRQ.
- [ ] Run host, sanitizer, CBMC and model checks on the final source; make a clean
  PSP build and verify the release ZIP. Record hardware results and package
  hashes, merge the completed link branch, update compatibility notes, then
  tag 1.4.0.

## Later work

The upstream Media Engine renderer is a separate performance project. It needs
PSP memory, cache-coherence, raster, input-latency and suspend/resume testing
before adoption; it adds one frame of display latency and retains limitations
for VRAM changes during a frame.

Additional players, Wireless Adapter, internet relay, UART, JoyBus, multiboot,
GPIO and a larger game matrix follow the two-player cable. Further DMA, JIT,
renderer and paging optimizations depend on measured benefit and correctness.
