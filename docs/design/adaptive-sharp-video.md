# Adaptive sharp video output

Status: approved for implementation on 21 August 2026.

## Objective

Add an automatic video path that fills the PSP display while keeping GBA pixel
art substantially sharper than the current fullscreen bilinear mode. The
renderer must protect normal-speed audio and frame pacing, work on supported
PSP models, and preserve the suspend and resume guarantees shipped in 1.1.0.

This work targets the next feature release. It does not change emulation
accuracy or create detail beyond the GBA framebuffer.

## Display constraints

The GBA framebuffer is 240 by 160 pixels with a 3:2 aspect ratio. The PSP
display is 480 by 272 pixels. Fullscreen output therefore uses an exact 2x
horizontal scale and a non-integer 1.7x vertical scale.

No renderer can simultaneously fill 480 by 272, retain the original 3:2
geometry, avoid cropping, and keep every source pixel at an integer scale. The
automatic mode follows the approved priority order:

1. fill the PSP display;
2. do not crop game content;
3. preserve hard pixel interiors and stable scrolling;
4. maintain full-speed emulation and clean audio.

Fullscreen output consequently retains the existing horizontal stretch. The
aspect-correct modes remain available as manual alternatives.

## Rendering approach

The preferred path is a two-pass fixed-function GE pipeline:

1. expand the 240 by 160 source to a 480 by 320 intermediate texture with
   nearest-neighbour filtering;
2. draw the 480 by 320 texture to the 480 by 272 display buffer with linear
   filtering and correctly aligned texture coordinates.

The first pass gives every source pixel a stable 2 by 2 footprint. The second
pass leaves the exact horizontal 2x scale unchanged and resamples only the
vertical excess. Linear interpolation is therefore limited to pixel
boundaries instead of softening the complete source image in both axes.

This implements the integer-prescale principle used by sharp-bilinear filters
without requiring programmable shaders, which the PSP GE does not expose.
The intermediate surface uses GU_PSM_5551 because the GBA source is 15-bit;
using a 32-bit surface would increase bandwidth without adding source colour
precision.

The intermediate texture is reserved in the PSP's 2 MiB EDRAM after the two
display buffers and the existing 256 by 256 GBA texture. A 512 by 512 backing
allocation provides the power-of-two dimensions required by the GE and leaves
space for the active 480 by 320 image without sampling beyond the allocation.

## Runtime quality policy

The new default is `Automatic - Sharp fullscreen`. Three internal quality
levels are retained:

- `Sharp`: two-pass integer prescale and vertical reduction;
- `Crisp`: single-pass fullscreen nearest-neighbour output;
- `Smooth`: the existing single-pass bilinear output, available as a manual
  compatibility choice but not selected automatically to recover speed.

Automatic mode starts at `Sharp`. It observes actual runtime behaviour rather
than relying on a hard-coded game list. Its inputs are:

- PSP model, available user memory, and configured clock;
- frame completion time and rolling high-percentile frame time;
- automatic frameskip events;
- time spent waiting for the GE before buffer exchange;
- audio underrun or recovery events where the sound subsystem exposes them.

Sustained missed budgets lower the renderer from `Sharp` to `Crisp`. A short
spike does not change quality. Returning to `Sharp` requires a substantially
longer clean interval so the mode cannot oscillate during gameplay.

The selected level and the measurements that justified it may be cached per
ROM using stable cartridge metadata. Cached data is advisory: every launch
keeps the runtime guard active so a different PSP model, clock, link session,
or cheat workload can revise the decision.

Manual `Sharp`, `Crisp`, `Smooth`, and aspect-correct modes bypass automatic
quality changes. Automatic operation must never alter emulation renderer,
cheat, link, input, or sound settings.

## Present scheduling

The current path submits the GE draw at the beginning of GBA VBlank and waits
for it immediately. The new path finishes the libGU display list without an
immediate blocking sync. Emulation continues through the remaining GBA VBlank
work, then synchronizes the GE immediately before swapping display buffers.

This moves the wait to the resource-use boundary and permits CPU and GE work
to overlap without allowing either side to reuse an active texture or draw
buffer. The implementation remains on libGU. A previous raw
`sceGeListEnQueue` experiment in repository history was unstable on physical
hardware and is not a basis for this work.

Fullscreen draws do not clear the complete display buffer because every
visible pixel is replaced. Modes with borders clear both display buffers when
the viewport or mode changes, not once per gameplay frame.

## Texture upload and cache policy

Cache and memory-path changes are introduced independently from the visual
filter so hardware failures can be isolated.

The baseline keeps the current GBA framebuffer in EDRAM and replaces the
global data-cache writeback with an aligned writeback of the active texture
range only. The GE texture cache is flushed before sampling data that the CPU
or an earlier render pass changed.

A second candidate renders the GBA framebuffer into aligned cached user RAM,
then transfers it into EDRAM with `sceGuCopyImage`. This matches the established
PSP software-core pattern and may reduce the cost of CPU scanline writes. It is
enabled only if a hardware benchmark shows a repeatable gain and memory
allocation succeeds. Failure always falls back to the direct EDRAM source.

Texture slicing is benchmarked with representative strip widths instead of
assuming the current width of 64 is optimal. The selected layout must not
produce seams with linear filtering. Swizzling is not adopted unless its
per-frame conversion cost is lower than the measured sampling benefit.

## Power lifecycle

An asynchronous present introduces explicit ownership that must be reconciled
with power events:

- suspend records that no new frame may be submitted;
- the foreground power service completes or cancels pending presentation work
  before game memory can be reused;
- resume restores draw-buffer, texture, filter, viewport, and display-list
  state before presentation is re-enabled;
- the first post-resume frame performs a conservative synchronized present;
- the adaptive controller discards pre-suspend timing samples.

No GE wait, allocation, file write, or configuration update is performed from
the PSP power callback. Those operations remain owned by the foreground power
service established in 1.1.0.

## Diagnostics

Release builds keep diagnostics disabled by default. A developer overlay or
bounded log can expose:

- active automatic quality level;
- average and high-percentile frame duration;
- GE wait time at buffer exchange;
- frames rendered and skipped;
- quality transitions and their reason.

Diagnostics must not write continuously to the Memory Stick during normal
gameplay. Counters use fixed storage and reset on ROM change, clock change,
manual video-mode change, suspend, and resume.

## Verification

Portable tests cover the adaptive state machine, hysteresis, reset events,
cached-profile validation, and allocation fallback. Source checks preserve the
libGU-only presentation rule and prohibit blocking GE work from the power
callback.

The PSP build must be checked with synthetic image patterns and commercial
games that exercise tiled, affine, and bitmap GBA modes. Hardware validation
includes:

1. one-pixel checkerboards, text, diagonals, and slow scrolling;
2. at least ten minutes of Pokemon Red gameplay at normal speed;
3. fast-forward entry and exit while automatic video is active;
4. cheats enabled and disabled;
5. an active two-PSP link session where supported by the game;
6. ten consecutive suspend and resume cycles during gameplay;
7. menu entry, savestate capture, ROM replacement, and normal exit;
8. comparison of `Sharp`, `Crisp`, and current `Smooth` output on the same
   scenes;
9. frame-time and audio results for each supported PSP model available for
   testing.

The sharp path is accepted only if it visibly reduces fullscreen blur without
introducing seams, missing rows, unstable pixel widths, audio crackle, extra
frameskip, suspend failures, or game-load crashes.

## References

- [PSPSDK Graphics Utility documentation](https://pspdev.github.io/pspsdk/group__GU.html)
- [PSPSDK GU memory-speed sample](https://github.com/pspdev/pspsdk/blob/master/src/samples/gu/speed/speed.c)
- [RetroArch PSP video driver](https://github.com/libretro/RetroArch/blob/master/gfx/drivers/psp1_gfx.c)
- [Libretro sharp-bilinear reference](https://github.com/libretro/glsl-shaders/blob/master/interpolation/shaders/sharp-bilinear-simple.glsl)
