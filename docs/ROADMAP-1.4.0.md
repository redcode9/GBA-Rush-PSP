# Remaining work for 1.4.0

The release target is a working two-player GBA cable over PSP ad-hoc Wi-Fi on
PSP-1000 and PSP-2000, with either console acting as host.

The 1.4.0 build replaces the earlier link stack, which never completed a transfer
on hardware, with the design described in the [link cable notes](link-cable.md):
a full-speed frame relay for Pokemon Ruby, Sapphire, Emerald, FireRed and
LeafGreen, word-by-word lockstep for every other game, and a reliable channel
that repeats unacknowledged messages in every datagram. The host test suite
simulates complete FireRed-style link sessions between two consoles over lossy
networks and passes, and an end-to-end test trades between two real FireRed
games through the same cable code on typical, noisy and uneven-speed links.

## Hardware tests

Use one PSP-1000 and one PSP-2000 with the same EBOOT. Copy both
`logs/` folders after each run and decode them with
`tools/decode_runtime_log.py`.

- [ ] FireRed with FireRed: enter the Cable Club trade room, trade one Pokemon
  each way, save in-game, reload and check both Pokemon. Repeat with the other
  PSP as host.
- [ ] FireRed with LeafGreen: the same trade, both roles.
- [ ] A link battle in the Cable Club Colosseum.
- [ ] Ten trades in one session without leaving the room, then leave the room
  normally on both consoles.
- [ ] Disconnect from the menu, switch WLAN off, suspend one PSP and exit one
  PSP during a session: the other PSP must report the end within ten seconds
  and keep running.
- [ ] One non-Pokemon game in lockstep mode, such as a Mario Kart: Super
  Circuit Multi-Pak race, recording the frame rate shown with the FPS
  overlay.

## Runtime and build fixes

- [x] Startup-menu, SMOL overlay, renderer dependency, text encoding, ROM
  paging and performance-report fixes from the October 11 upstream review.
- [ ] Check fonts, startup dialogs, SMOL overlays, paging recovery and probe
  overhead on both PSP models.

## Release

- [ ] Record the hardware results above and update the compatibility notes.
- [x] Promote the reviewed source through `develop` into `master` and tag the
  stable 1.4.0 release.

## Later work

The upstream Media Engine renderer is a separate performance project. It needs
PSP memory, cache-coherence, raster, input-latency and suspend/resume testing
before adoption.

Additional players, the Wireless Adapter, internet relay, UART, JoyBus and
multiboot follow the two-player cable. Other games with frame-based link
protocols could get their own relay, as Pokemon has, once a game shows that
lockstep is too slow for it.
