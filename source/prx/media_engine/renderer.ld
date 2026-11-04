/* The Media Engine program (renderer.c) at a fixed place in the Media
   Engine's eDRAM, which it sees cached from 0x80000000: 64 KB in, clear of
   the exception area, with its data and zeroed data after it and the stack
   at the top of the 2 MB every PSP has. The binary made from it holds
   .text, .rodata and .data; boot.S copies it and me_start clears .bss. */

ENTRY(me_start)

SECTIONS
{
  . = 0x80010000;
  .text : { KEEP(*(.text.start)) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.* .sdata .sdata.*) }
  . = ALIGN(64);
  me_program_end = .;
  .bss (NOLOAD) :
  {
    me_bss_start = .;
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(4);
    me_bss_end = .;
  }
  /DISCARD/ : { *(.MIPS.abiflags .reginfo .pdr .comment .gnu.attributes) }
}
