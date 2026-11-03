# Builds media_engine.prx, the kernel module that runs the Exact renderer on
# the Media Engine (main.c). The Media Engine's own program goes inside it:
# renderer.c with gba_ppu.c and gba_ppu_log.c, built freestanding for the
# Media Engine's eDRAM by the rules at the end into renderer.bin, which
# program.S includes. source/Makefile builds the module and copies it next
# to the EBOOT; make check builds renderer.bin when the PSP SDK is there.
TARGET = media_engine
OBJS = main.o boot.o program.o

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

INCDIR =
CFLAGS = -Os -G0 -Wall -Wextra -march=allegrex -mfp32 -mgp32 -mlong32 \
 -mabi=eabi
ASFLAGS = $(CFLAGS)
LDFLAGS = -nostartfiles
LIBS =
EXTRA_CLEAN = renderer.elf renderer.bin

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

SRC = ../../src
PROGRAM_SOURCES = renderer.c $(SRC)/gba_ppu.c $(SRC)/gba_ppu_log.c
PROGRAM_CFLAGS = -O2 -G0 -march=allegrex -mfp32 -mgp32 -mlong32 -mabi=eabi \
 -mno-abicalls -fno-pic -ffreestanding -fno-tree-loop-distribute-patterns \
 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -I$(SRC)
# All of the program's code is drawing code: it must fit the Media Engine's
# 16 KB instruction cache with room to spare.
PROGRAM_TEXT_LIMIT = 12288

renderer.elf: $(PROGRAM_SOURCES) renderer.ld $(SRC)/gba_ppu.h \
 $(SRC)/gba_ppu_log.h
	$(CC) $(PROGRAM_CFLAGS) -nostdlib -Wl,--gc-sections -T renderer.ld \
	 $(PROGRAM_SOURCES) -lgcc -o $@

renderer.bin: renderer.elf
	@text=$$(psp-size -A renderer.elf | awk '$$1 == ".text" { print $$2 }'); \
	 echo "Media Engine program: $$text bytes of code"; \
	 if [ "$$text" -gt $(PROGRAM_TEXT_LIMIT) ]; then \
	   echo "More than $(PROGRAM_TEXT_LIMIT) bytes of Media Engine code" >&2; \
	   exit 1; \
	 fi
	psp-objcopy -O binary renderer.elf $@

program.o: renderer.bin
