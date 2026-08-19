# Link cable

The link cable feature connects two PSP systems over local ad-hoc Wi-Fi and
emulates a two-player GBA cable. It is under development for 1.1.0-beta.1 and
does not change the 1.0.0 release.

## Requirements

- Two PSP systems running the same GBA Rush PSP candidate build.
- The physical WLAN switch enabled on both systems.
- A legally obtained GBA BIOS and ROM on each PSP.
- Games that use a two-player GBA cable mode supported below.

The ROM files do not have to be identical. The emulator exchanges the title,
game code, revision, size, and CRC32 for diagnostics but does not reject a
different ROM. The games themselves still decide whether two versions,
regions, or languages can communicate.

## Connect

1. Load the intended game on each PSP.
2. Open **Main menu > Link cable** on Player 1 and choose **Create session**.
3. Open the same menu on Player 2 and choose **Join session**.
4. Wait until both screens report **Connected as Player 1** or
   **Connected as Player 2**.
5. Return to each game and enter its normal link, trade, battle, or multiplayer
   screen.

Player 1 creates the ad-hoc group and acts as the cable parent. Player 2 scans
for that group and joins it. Leaving the menu does not close an established
session. Use **Disconnect** when link play is finished.

## Supported serial modes

- Normal 8-bit, with external or internal clock.
- Normal 32-bit, with external or internal clock.
- Multiplayer 16-bit at 9600, 38400, 57600, and 115200 baud.
- Serial completion timing, status bits, and serial interrupts for two players.

The implementation synchronizes each transfer before either emulated GBA
continues. Network delay therefore pauses GBA time during a transfer instead of
letting the two games drift apart.

The following are not supported in this beta:

- three- or four-player sessions;
- the GBA Wireless Adapter and its RFU protocol;
- UART, General Purpose, and JoyBus communication;
- GameCube link, GBA multiboot, and Game Boy or Game Boy Color link modes;
- internet relay play.

A game advertised for the GBA Wireless Adapter is not automatically compatible
with cable emulation. Some games support both accessories through separate menu
choices; select their cable option.

## Runtime behavior

- Savestate load and save are disabled while a link session is open. Use each
  game's ordinary in-game save system instead.
- Fast-forward is forced off until the session closes.
- Cheats remain available but are local to each PSP and are not synchronized.
- Reset, ROM replacement, suspend, sleep, and emulator exit close the session.
- A one-second heartbeat keeps an idle connection alive. Five seconds without
  valid traffic closes it cleanly.

Save normally inside the game before starting a link session, especially before
a trade. Do not remove a Memory Stick while either emulator is writing a save.

## Status and recovery

| Message | Meaning | Action |
|---|---|---|
| WLAN switch is off | The PSP radio is disabled. | Enable WLAN, then create or join again. |
| Timeout | The peer stopped answering. | Return both games to an idle link screen and reconnect. |
| Network error | Ad-hoc setup or transport failed. | Disconnect both PSPs, verify WLAN, and retry nearby. |
| Protocol error | A damaged, stale, or invalid packet was received. | Install the same candidate build on both PSPs and reconnect. |
| Incompatible protocol | The two emulator builds cannot negotiate the required modes. | Use the same build on both systems. |
| Serial mode mismatch | The games requested different cable modes or settings. | Return to the same compatible multiplayer screen on both games. |
| Network queue full | Traffic could not be queued safely. | Disconnect, close other network activity, and retry. |
| Peer closed the connection | The other PSP disconnected or left link play. | Reconnect from the Link cable menu. |

On a failed transfer, the serial hardware receives disconnected values and an
error indication before the session closes. This prevents the game from waiting
forever, but its own error screen may take a moment to appear.

## Diagnostics

Redistributable source for Normal 8, Normal 32, and Multiplayer test ROMs is in
[`tests/link-rom`](../tests/link-rom/README.md). These programs expose sent and
received values, raw status, player ID, interrupt count, and completion order
without requiring a commercial game.
