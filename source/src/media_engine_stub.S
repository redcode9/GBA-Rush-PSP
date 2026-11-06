/*
 * Import stub for the library media_engine.prx exports to the emulator
 * (source/prx/media_engine/exports.exp), in the form psp-build-exports -s
 * emits. 0x00020005 = 2 functions with 5-word stub entries; each NID is
 * the hash of the function's name. The kernel links it when video_me.c loads
 * the module; until then, and where it never loads, it is not called.
 */

	.set noreorder

#include "pspstub.s"

	STUB_START "GBARushMediaEngine",0x40090000,0x00020005
	STUB_FUNC  0xFE1F88D4,media_engine_wake
	STUB_FUNC  0x0A7D03A2,media_engine_publish
	STUB_END
