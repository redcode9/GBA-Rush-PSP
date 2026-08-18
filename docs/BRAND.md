# GBA Rush PSP brand guide

## Positioning

GBA Rush PSP is a performance-focused Game Boy Advance emulator for
PlayStation Portable. It is an independent fork of
[andymcca/TempGBA4PSP-mod](https://github.com/andymcca/TempGBA4PSP-mod), built
on the work of the gpSP, gpSP Kai, TempGBA, and TempGBA4PSP-mod communities.

The public description is:

> A performance-focused GBA emulator for PSP, forked from TempGBA4PSP-mod.

The project must never imply that it is the original TempGBA4PSP-mod project
or that upstream authors endorse this fork.

## Naming

Use the following names consistently:

| Context | Name |
|---|---|
| Project and XMB title | `GBA Rush PSP` |
| GitHub repository | `GBA-Rush-PSP` |
| Install directory | `GBARushPSP` |
| Release archive | `GBA-Rush-PSP-<version>.zip` |
| Single-game launcher | `GBA Rush PSP Single-Game Launcher` |

Technical identifiers inherited from upstream may retain their historical
names when changing them would create compatibility risk or obscure source
lineage.

## Logo system

The identity uses a compact turbo badge. Two forward chevrons form an abstract
letter `R`, paired with a strong `GBA RUSH` wordmark and a smaller `PSP` label.
It should feel like a purposeful late-1990s arcade mark while remaining clean
and legible on the PSP XMB.

The palette is limited to:

- deep navy for the base;
- electric cyan for the primary mark;
- magenta for one controlled accent;
- warm yellow for small emphasis;
- off-white for essential text.

The design must avoid generic controller silhouettes, copied console shapes,
mascots, characters, ornamental circuitry, uncontrolled glow, busy neon grids,
fake scanline noise, and decorative elements without a clear function.

## PSP assets

The brand ships in two raster sizes required by the PSP:

- `ICON0.png`: 144 x 80 pixels, with the badge and short wordmark readable at
  native size;
- `PIC1.png`: 480 x 272 pixels, extending the same composition with the full
  project name and fork description.

The two images must use the same geometry and palette. `PIC1.png` is an
extension of the badge rather than a separate illustration. Source artwork is
kept alongside the exported PNG files. Exports must have no unnecessary
metadata and must be inspected at their native PSP dimensions before release.

## Documentation and attribution

The README introduces GBA Rush PSP as a fork before describing new features.
It links directly to the upstream repository and separates inherited lineage
from changes made in this fork.

`docs/LINEAGE.md` records this development path:

1. gpSP by Exophase;
2. gpSP Kai by Takka and its contributors;
3. TempGBA by Nebuleon, Normmatt, BassAceGold, and contributors;
4. TempGBA4PSP-mod by Phoe-nix and later maintainers;
5. the continued TempGBA4PSP-mod work by andymcca, JxPv2, and contributors;
6. GBA Rush PSP as an independent fork of andymcca's repository.

The project thanks upstream authors prominently and preserves inherited
copyright notices and source headers. Attribution identifies upstream work; it
does not imply endorsement.

## Release migration

Version `1.0.0` is reissued under the GBA Rush PSP identity. The release title,
archive, installation directory, EBOOT title, documentation, launcher defaults,
repository name, and final release commit all change together. The semantic
version remains `1.0.0`; a branding correction alone does not create a
synthetic patch release.

The launcher defaults to `GBARushPSP` while retaining support for an explicit
path to an existing `TempGBA4PSP` installation.

## Acceptance checks

Before publishing the rebranded release:

- build the emulator and launcher with the PSP toolchain;
- run the CodeBreaker regression and source-tree checks;
- inspect both artwork files at native size;
- verify EBOOT title, icon, and background sections;
- confirm package contents and SHA-256 checksum;
- scan tracked files, commit metadata, and release assets for private material
  and unintended metadata;
- confirm that the README, lineage document, notices, launcher, repository,
  tag, and release all use the approved names and links.
