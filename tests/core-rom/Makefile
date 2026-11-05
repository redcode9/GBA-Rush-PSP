# The core test ROM: CPU, interrupt, BIOS and bus cases, one file per area
# in source/ (cases_<area>.c, with cases_<area>_asm.s for code that must be
# hand written). A new area adds its files here and its table to the list
# in source/main.c. Builds $(OUT)/core-rom.gba with the rules of
# ../gba-rom.mk; the signature goes to SRAM.

TARGETS := core-rom
GAME_CODE := GRCR
SOURCES := source/main.c source/cases_irq.c source/cases_stack.c \
 source/cases_stack_asm.s source/cases_pc.c source/cases_pc_asm.s \
 source/cases_cpu.c source/cases_cpu_asm.s
SOURCES += source/cases_timer.c source/cases_timer_asm.s
SOURCES += source/cases_branch.c source/cases_branch_asm.s
SOURCES += source/cases_prefetch.c source/cases_prefetch_asm.s
SOURCES += source/cases_vram.c source/cases_vram_asm.s
SOURCES += source/cases_stm.c source/cases_stm_asm.s
SOURCES += source/cases_bios.c source/cases_bios_asm.s
# The expected results of cpu_shift_reg, written at build time.
SHIFT_TABLE = $(OUT)/gen/shift_table.h
FLAGS_core-rom = -I$(OUT)/gen

include ../gba-rom.mk

$(call gba_rom_object,core-rom,source/cases_cpu.c): $(SHIFT_TABLE)
$(SHIFT_TABLE): gen_shift_table.py
	@mkdir -p $(@D)
	$(PYTHON) gen_shift_table.py > $@.tmp && mv $@.tmp $@
