# Hardware results

Every run on real consoles of a test from the
[hardware test catalog](hardware-tests.md), one row each. A row is tied to the
sha256 of the `EBOOT.PBP` it ran, not of the ELF: of the binaries, the release
zip ships only `EBOOT.PBP` and `exception.prx`, and the ELF of a published
build cannot be checked. Rows recorded before builds had a manifest say
`unknown` there, and a row with an `unknown` digest never validates a
release.

The columns:

- Date: the day of the run, as `YYYY-MM-DD`.
- Version: the version of the build, such as `1.4.3`, or `1.4.3 dev` for a
  development build.
- EBOOT sha256: the first 12 hex digits, in lower case, of the sha256 of
  `EBOOT.PBP`, or `unknown`. The full digest is in the release manifest.
- Model: `PSP-1000`, `PSP-2000`, `PSP-3000`, `PSP-N1000` (PSP Go) or
  `PSP-E1000` (PSP Street). A link session is one row that names both
  consoles, such as `PSP-2000 + PSP-1000`.
- Firmware/CFW: the system software and custom firmware, or `unknown`.
- Storage: the Memory Stick or internal storage the emulator ran from, or
  `unknown`.
- Test id: an id from the catalog.
- Result: `pass`, `fail` or `partial`, for a run that covered only part of
  the test. Only `pass` counts for a release.
- Minutes: the minutes the run took, or `unknown`.
- Evidence: the decoded log or report the row comes from.

`make check` fails when a row is malformed or names a test id missing from
the catalog; `python3 scripts/hardware-session.py --check` checks the same
on its own. A cell cannot contain `|`.

## Results

| Date | Version | EBOOT sha256 | Model | Firmware/CFW | Storage | Test id | Result | Minutes | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| 2026-10-31 | 1.4.0 | unknown | PSP-2000 + PSP-1000 | unknown | unknown | link-frlg-both-roles | partial | unknown | Notes: first paired run |
| 2026-11-02 | 1.4.3 dev | unknown | PSP-2000 + PSP-1000 | unknown | unknown | link-mksc-race | partial | unknown | Notes: Mario Kart races, 40 fps |
| 2026-11-02 | 1.4.3 dev | unknown | PSP-2000 + PSP-1000 | unknown | unknown | link-mksc-race | partial | unknown | Notes: Mario Kart races, 45.5 fps |
| 2026-11-02 | 1.4.3 dev | unknown | PSP-1000 | unknown | unknown | menu-every-page | fail | unknown | Notes: Performance page crash |
| 2026-11-02 | 1.4.3 dev | unknown | PSP-2000 | unknown | unknown | menu-every-page | fail | unknown | Notes: Performance page crash |

## Notes

### First paired run

On 2026-10-31, with the 1.4.0 EBOOT: FireRed with FireRed, a PSP-2000 hosting
a PSP-1000. Both games linked in Pokemon mode and stayed in the Cable Club for
two and a half minutes until the player left, with every frame delivered in
both directions. The consoles stalled for about 40 seconds in total, and about
30 frames, half a second, stayed queued in each direction once the room was
busy. No trade or battle was run, and only one role, so the row is partial.

### Mario Kart races

Two Mario Kart: Super Circuit Multi-Pak races between a PSP-2000 and a
PSP-1000 during the 1.4.3 work: 40 fps on the build before 1.4.3's lockstep
changes and 45.5 fps on the first build with them. They set the PSP
conditions that `make check-rom-link` simulates (see the
[link cable notes](link-cable.md)). Each race ran in one role, and which PSP
hosted was not recorded.

### Performance page crash

During the 1.4.3 work, opening the Performance page crashed both PSPs with a
blue "Reserved instruction" screen. The page's fast-forward callbacks were
nested functions, which the PSP reached through a trampoline on the stack.
1.4.3 moved them to file scope, and the PSP build now fails on any stack
trampoline. 1.4.0 to 1.4.2 have the same callbacks.
