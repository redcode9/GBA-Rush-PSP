/* The Media Engine program, renderer.bin, built by the Makefile from
   renderer.c, gba_ppu.c and gba_ppu_log.c. */

	.section .rodata
	.align	6
	.globl	media_engine_program
	.globl	media_engine_program_end
media_engine_program:
	.incbin	"renderer.bin"
media_engine_program_end:
