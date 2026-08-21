# Adaptive sharp video

Status: shipped in GBA Rush PSP 1.2.0.

## Goal

Fill the 480 by 272 PSP display while keeping GBA pixel art sharper than the
previous bilinear fullscreen mode. Normal-speed frame pacing and suspend safety
take priority over the filter.

The GBA framebuffer is 240 by 160. Fullscreen output is exactly 2x horizontally
and 1.7x vertically, so it cannot also preserve the original 3:2 geometry and
integer-scale both axes. Aspect-correct modes remain available.

## Renderer

`Sharp` uses two fixed-function PSP GE passes:

1. 240 by 160 to 480 by 320 with nearest filtering.
2. 480 by 320 to 480 by 272 with linear filtering.

The second pass leaves the horizontal 2x scale unchanged and resamples only the
vertical axis. Its 512 by 512 RGB5551 backing surface fits in EDRAM after the
two display buffers and existing GBA texture.

`Crisp` is the single-pass nearest fullscreen fallback. The previous bilinear
path remains a manual option. Presentation stays on libGU; raw GE queue calls
are rejected by a source check because that path was unstable on real PSP
hardware.

## Automatic mode

`16:9 Auto` starts at `Sharp`. It evaluates 120-frame windows using 95th
percentile frame duration, GE wait time, and skipped frames.

- Two sustained bad windows select `Crisp`.
- Five clean windows return to `Sharp`.
- Manual video modes do not change automatically.

Transitions occur after a frame boundary. The FPS overlay reports the active
`SHARP` or `CRISP` level.

## Presentation and power

The renderer submits its libGU list at GBA VBlank and waits immediately before
the display-buffer swap. This overlaps GE work with the remaining emulation
work without reusing an active buffer. Fullscreen paths skip the redundant
framebuffer clear.

Suspend blocks new submissions and completes pending GE work in the foreground.
Resume restores draw buffer, viewport, texture, filter, and scissor state, then
forces one synchronized frame. Timing samples from before suspension are
discarded.

## Compatibility

Existing numeric scale values are unchanged. New configurations default to
`16:9 Auto`; upgraded installations keep their saved mode until the user
selects it.

The policy has deterministic host and sanitizer coverage. The release EBOOT
was started successfully on physical PSP hardware. Testing across every PSP
model and game remains non-exhaustive.

## References

- [PSPSDK Graphics Utility](https://pspdev.github.io/pspsdk/group__GU.html)
- [PSPSDK GU speed sample](https://github.com/pspdev/pspsdk/blob/master/src/samples/gu/speed/speed.c)
- [RetroArch PSP video driver](https://github.com/libretro/RetroArch/blob/master/gfx/drivers/psp1_gfx.c)
- [Sharp bilinear reference](https://github.com/libretro/glsl-shaders/blob/master/interpolation/shaders/sharp-bilinear-simple.glsl)
