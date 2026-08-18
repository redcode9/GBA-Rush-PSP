# Project lineage and acknowledgements

GBA Rush PSP is an independent continuation of a long-running open-source
emulator lineage. It did not begin as a new emulator core.

## Direct upstream

This project is forked directly from:

> [andymcca/TempGBA4PSP-mod](https://github.com/andymcca/TempGBA4PSP-mod)

That repository continues the earlier
[phoe-nix/TempGBA4PSP-mod](https://github.com/phoe-nix/TempGBA4PSP-mod) work
and contains the PSP usability, interface, compatibility, renderer,
performance, and single-game-launcher foundation inherited by GBA Rush PSP.

GBA Rush PSP is maintained separately. References to upstream projects are
attribution and thanks, not a claim of endorsement.

## Development path

### gpSP

The emulator core descends from gpSP, created by Exophase. Later gpSP work by
notaz and other contributors informed several branches of the project.

### gpSP Kai

Takka and contributors adapted and improved gpSP for PSP through the gpSP Kai
line. Their work established much of the practical PSP foundation inherited by
later projects.

### TempGBA

Nebuleon, Normmatt, BassAceGold, and the TempGBA community continued the core
with additional compatibility, interface, and platform work. Historical
copyright and asset notices are preserved in `source/copyright` and relevant
source headers.

### TempGBA4PSP-mod

Phoe-nix created TempGBA4PSP-mod from the earlier TempGBA PSP lineage. The
project was later continued by andymcca with contributions from JxPv2 and
other developers. That maintained repository is the direct source from which
GBA Rush PSP was forked.

### GBA Rush PSP

GBA Rush PSP gives the fork a distinct identity and adds:

- plaintext and encrypted CodeBreaker interpretation;
- timed master-code hooks in the dynamic recompiler;
- safer manual and transactional cheat-list loading;
- active-entry and sorted-hook indexes for lower runtime overhead;
- configurable hold-to-fast-forward with controlled display and audio pacing;
- host regression tests, source checks, and repeatable release packaging.

The inherited emulator core, renderers, interface, translations, compatibility
work, single-game launcher, and supporting libraries remain the product of the
upstream lineage and their respective contributors.

## Thank you

Special thanks go to Exophase, notaz, Takka, Nebuleon, Normmatt,
BassAceGold, Phoe-nix, andymcca, JxPv2, the libretro/gpsp contributors, and
everyone who tested, documented, translated, maintained, or improved these
projects over the years.

Please visit the
[direct upstream repository](https://github.com/andymcca/TempGBA4PSP-mod) for
its complete history, contributors, documentation, and original project work.
