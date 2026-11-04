# The core test ROM: CPU, interrupt, BIOS and bus cases, one file per area
# in source/ (cases_<area>.c, with cases_<area>_asm.s for code that must be
# hand written). A new area adds its files here and its table to the list
# in source/main.c. Builds $(OUT)/core-rom.gba with the rules of
# ../gba-rom.mk; the signature goes to SRAM.

TARGETS := core-rom
GAME_CODE := GRCR
SOURCES := source/main.c source/cases_irq.c

include ../gba-rom.mk
