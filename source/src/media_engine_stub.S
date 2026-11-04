/*
 * Import stub for the library media_engine.prx exports to the emulator
 * (source/prx/media_engine/exports.exp), in the form psp-build-exports -s
 * emits. 0x00010005 = 1 function with 5-word stub entries; the NID is the
 * hash of the function's name. The kernel links it when video_me.c loads
 * the module; until then, and where it never loads, it is not called.
 */

	.set noreorder

#include "pspstub.s"

	STUB_START "GBARushMediaEngine",0x40090000,0x00010005
	STUB_FUNC  0xFE1F88D4,media_engine_wake
	STUB_END
