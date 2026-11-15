# Builds sections of mGBA's test suite (MIT, at the commit
# scripts/setup-dev.sh pins in ext/test-roms/mgba-suite) without devkitARM:
# arm-none-eabi-gcc and newlib, the start-up code of tests/gba-rom, and
# libgba.h, libgba.c and libgba_asm.s in place of libgba. One ROM per
# section, $(OUT)/mgba-suite-<name>.gba, opens that section by itself
# (the keys are a script, see libgba.c), runs it and leaves its passed
# and total results on screen, where tools/read_test_screen.py reads
# them; the section's failures also go to SRAM as the suite writes them.
# The Video section is not built (its pictures need grit): a stub keeps
# its place in the list.
#
#   make -C tests/mgba-suite [SUITE_DIR=...] [OUT=...]
#
# Nothing of the suite is copied into the repository. Results that time
# code the compiler or the interrupt dispatcher produced (the suite's
# expected values came from devkitARM and libgba) belong to this build:
# compare them with mGBA on the same ROM.

REPO_ROOT := $(abspath ../..)
SUITE_DIR ?= $(REPO_ROOT)/ext/test-roms/mgba-suite
OUT ?= $(REPO_ROOT)/build/test-roms
PYTHON ?= python3
ifeq ($(origin ARM_PREFIX),undefined)
ARM_PREFIX := $(if $(DEVKITARM),$(DEVKITARM)/bin/)arm-none-eabi-
endif
ARM_CC := $(ARM_PREFIX)gcc
ARM_OBJCOPY := $(ARM_PREFIX)objcopy

# index in the suite's list:ROM name
SECTIONS := 0:memory 1:io-read 2:timing 3:timers 4:timer-irq 5:shifter \
 6:carry 7:multiply-long

BUILD := $(OUT)/mgba-suite
GBA_ROM := $(REPO_ROOT)/tests/gba-rom
SHIMS := gba_types gba_interrupt gba_input gba_systemcalls gba_video \
 gba_timers gba_dma gba_sio gba_sound gba_sprites gba_compression \
 sys/iosupport
# The suite's own flags; it is not warning-free.
ARCH := -mcpu=arm7tdmi -mthumb -mthumb-interwork
CFLAGS := $(ARCH) -g -O3 -fomit-frame-pointer -ffast-math \
 -I$(SUITE_DIR)/include -I$(BUILD)/include -I$(BUILD) -I$(CURDIR)
LDFLAGS := $(ARCH) -specs=nano.specs -nostartfiles -T $(GBA_ROM)/gba.ld \
 -Wl,--no-warn-rwx-segments

SUITE_C := $(filter-out %/video.c,$(wildcard $(SUITE_DIR)/src/*.c))
SUITE_S := $(wildcard $(SUITE_DIR)/src/*.s $(SUITE_DIR)/src/tests/*.s)
OBJECTS := $(patsubst $(SUITE_DIR)/%,$(BUILD)/%.o,$(SUITE_C) $(SUITE_S)) \
 $(BUILD)/font.o $(BUILD)/crt0.o $(BUILD)/libgba_asm.o
HEADERS := $(SHIMS:%=$(BUILD)/include/%.h) $(BUILD)/font.h

rom_name = $(word 2,$(subst :, ,$(1)))
rom_index = $(word 1,$(subst :, ,$(1)))

.PHONY: all clean
.SUFFIXES:

all: $(foreach s,$(SECTIONS),$(OUT)/mgba-suite-$(call rom_name,$(s)).gba)

$(SUITE_DIR)/src/main.c:
	@echo "no mGBA test suite in $(SUITE_DIR): sh scripts/setup-dev.sh test-roms" >&2
	@exit 1

$(BUILD)/include/%.h: libgba.h
	@mkdir -p $(@D)
	echo '#include "libgba.h"' > $@

$(BUILD)/font.h: $(BUILD)/font.c
$(BUILD)/font.c: $(SUITE_DIR)/gfx/raw/font.bmp $(REPO_ROOT)/tools/read_test_screen.py
	@mkdir -p $(@D)
	$(PYTHON) $(REPO_ROOT)/tools/read_test_screen.py font-c $< $(@D)

$(BUILD)/%.c.o: $(SUITE_DIR)/%.c $(HEADERS) | $(SUITE_DIR)/src/main.c
	@mkdir -p $(@D)
	$(ARM_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.s.o: $(SUITE_DIR)/%.s | $(SUITE_DIR)/src/main.c
	@mkdir -p $(@D)
	$(ARM_CC) $(ARCH) -x assembler-with-cpp -I$(SUITE_DIR)/include -c $< -o $@

$(BUILD)/font.o: $(BUILD)/font.c
	$(ARM_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/crt0.o: $(GBA_ROM)/crt0.s
	@mkdir -p $(@D)
	$(ARM_CC) $(ARCH) -c $< -o $@

$(BUILD)/libgba_asm.o: libgba_asm.s
	@mkdir -p $(@D)
	$(ARM_CC) $(ARCH) -c $< -o $@

# $(1) index:name
define SECTION
$(BUILD)/libgba-$(call rom_name,$(1)).o: libgba.c $(HEADERS)
	$(ARM_CC) $(CFLAGS) -Wall -Wextra -Werror -DSUITE=$(call rom_index,$(1)) -c $$< -o $$@

$(OUT)/mgba-suite-$(call rom_name,$(1)).elf: $(OBJECTS) $(BUILD)/libgba-$(call rom_name,$(1)).o $(GBA_ROM)/gba.ld
	$(ARM_CC) $(LDFLAGS) $$(filter %.o,$$^) -o $$@

$(OUT)/mgba-suite-$(call rom_name,$(1)).gba: $(OUT)/mgba-suite-$(call rom_name,$(1)).elf
	$(ARM_OBJCOPY) -O binary $$< $$@
	$(PYTHON) $(REPO_ROOT)/tools/gba_header.py $$@ suite-$(call rom_name,$(1)) GRMS
endef

$(foreach s,$(SECTIONS),$(eval $(call SECTION,$(s))))

clean:
	$(RM) -r $(BUILD) $(OUT)/mgba-suite-*
