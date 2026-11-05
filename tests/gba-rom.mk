# Build rules shared by the GBA test ROMs (tests/core-rom, tests/save-rom,
# tests/raster-rom).
# A ROM's Makefile sets
#   TARGETS         the ROMs to build, each to $(OUT)/<target>.gba
#   SOURCES         its own .c and .s files, compiled once per target
#   GAME_CODE       four characters for the cartridge header
#   FLAGS_<target>  extra compiler flags of one target, such as its save
#                   chip (tests/gba-rom/backup.h)
# and includes this file. Each target also gets the code in tests/gba-rom/:
# start-up (crt0.s, gba.ld), the result protocol (result.c) and the save
# chip drivers (backup.c), with ROM_ID set to the target's name.
#
# The compiler is arm-none-eabi-gcc from PATH, or from $(DEVKITARM)/bin
# when DEVKITARM is set; ARM_PREFIX overrides both. No libgba and no
# devkitARM start-up files are used. Every warning is an error.
#
# tools/gba_header.py fills in the cartridge header after the link: title
# (the target's name), game code, maker code, the fixed byte 96h and the
# complement check, and pads the ROM with FFh to a power of two of at
# least 32 KiB, so an emulator's scan for the save type ends inside the
# file. The 156-byte logo is left blank: it is Nintendo's, and the build
# takes it neither from the repository nor from your files. The official
# BIOS compares it with its own copy and will not start the cartridge, so
# boot the ROMs directly: GBA Rush with "Boot from BIOS" off (the
# default), mGBA without a BIOS or with "Skip BIOS intro". On a real GBA,
# a flash cartridge's loader or a header tool of your own has to write
# the logo. The emulator's direct boot is also what the BIOS open bus
# cases expect after boot (GBATEK's value once the BIOS has started the
# cartridge).

GBA_ROM_COMMON := $(dir $(lastword $(MAKEFILE_LIST)))gba-rom
REPO_ROOT := $(abspath $(GBA_ROM_COMMON)/../..)
OUT ?= $(REPO_ROOT)/build/test-roms
PYTHON ?= python3

ifeq ($(origin ARM_PREFIX),undefined)
ARM_PREFIX := $(if $(DEVKITARM),$(DEVKITARM)/bin/)arm-none-eabi-
endif
ARM_CC := $(ARM_PREFIX)gcc
ARM_OBJCOPY := $(ARM_PREFIX)objcopy

# min-pagesize=0: reads of fixed low addresses, such as the BIOS at 0,
# are intended.
ARM_CFLAGS := -mcpu=arm7tdmi -mthumb -mthumb-interwork -std=c99 -O2 -g \
 -Wall -Wextra -Werror -ffreestanding -fno-delete-null-pointer-checks \
 --param=min-pagesize=0 -I$(GBA_ROM_COMMON)
ARM_LDFLAGS := -mcpu=arm7tdmi -mthumb -mthumb-interwork -nostartfiles \
 -T $(GBA_ROM_COMMON)/gba.ld -Wl,--no-warn-rwx-segments

GBA_ROM_SOURCES := $(GBA_ROM_COMMON)/crt0.s $(GBA_ROM_COMMON)/result.c \
 $(GBA_ROM_COMMON)/backup.c
GBA_ROM_HEADERS := $(wildcard $(GBA_ROM_COMMON)/*.h) \
 $(wildcard $(sort $(dir $(SOURCES)))*.h)
HEADER_TOOL := $(REPO_ROOT)/tools/gba_header.py

.PHONY: all clean
.SUFFIXES:

all: $(TARGETS:%=$(OUT)/%.gba)

gba_rom_object = $(OUT)/obj/$(1)/$(notdir $(basename $(2))).o

# $(1) target, $(2) source
define GBA_ROM_SOURCE
$(call gba_rom_object,$(1),$(2)): $(2) $(GBA_ROM_HEADERS) Makefile
	@mkdir -p $$(@D)
	$(ARM_CC) $(ARM_CFLAGS) $(FLAGS_$(1)) -DROM_ID='"$(1)"' -c $$< -o $$@
endef

define GBA_ROM_TARGET
$(OUT)/$(1).elf: $(foreach s,$(SOURCES) $(GBA_ROM_SOURCES),$(call gba_rom_object,$(1),$(s))) $(GBA_ROM_COMMON)/gba.ld
	$(ARM_CC) $(ARM_LDFLAGS) -Wl,-Map,$(OUT)/$(1).map $$(filter %.o,$$^) \
	 -o $$@

$(OUT)/$(1).gba: $(OUT)/$(1).elf $(HEADER_TOOL)
	$(ARM_OBJCOPY) -O binary $$< $$@
	$(PYTHON) $(HEADER_TOOL) $$@ $(1) $(GAME_CODE)
endef

$(foreach t,$(TARGETS),$(eval $(call GBA_ROM_TARGET,$(t)))$(foreach s,$(SOURCES) $(GBA_ROM_SOURCES),$(eval $(call GBA_ROM_SOURCE,$(t),$(s)))))

clean:
	$(RM) -r $(TARGETS:%=$(OUT)/obj/%)
	$(RM) $(foreach t,$(TARGETS),$(OUT)/$(t).gba $(OUT)/$(t).elf $(OUT)/$(t).map)
