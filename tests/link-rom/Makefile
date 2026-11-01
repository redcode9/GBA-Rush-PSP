# Builds the link diagnostic ROM (source/main.c) once per serial mode with
# devkitARM: link-normal8.gba, link-normal32.gba and link-multiplayer.gba.

.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error Please set DEVKITARM to the devkitARM installation directory)
endif

include $(DEVKITARM)/gba_rules

TARGETS := link-normal8 link-normal32 link-multiplayer
BUILD := build
ARCH := -mthumb -mthumb-interwork
CFLAGS := -g -O2 -Wall -Wextra -Werror -mcpu=arm7tdmi -mtune=arm7tdmi \
 $(ARCH)
LDFLAGS := -g $(ARCH)
LIBS :=
LIBPATHS :=
GAME_TITLE := LINKSIOTEST
GAME_CODE := LSIO
MAKER_CODE := 00

.PHONY: all clean

all: $(addsuffix .gba,$(TARGETS))

# Target-specific TEST_MODE selects LINK_DIAGNOSTIC_MODE in main.c.
$(BUILD)/link-normal8.o: TEST_MODE=0
$(BUILD)/link-normal32.o: TEST_MODE=1
$(BUILD)/link-multiplayer.o: TEST_MODE=2

$(BUILD)/%.o: source/main.c | $(BUILD)
	$(CC) $(CFLAGS) -DLINK_DIAGNOSTIC_MODE=$(TEST_MODE) -MMD -MP \
	 -MF $(@:.o=.d) -c $< -o $@

$(addsuffix .elf,$(TARGETS)): %.elf: $(BUILD)/%.o
	$(CC) $(LDFLAGS) -specs=gba.specs $< $(LIBPATHS) $(LIBS) -o $@

$(BUILD):
	mkdir -p $@

clean:
	$(RM) -r $(BUILD)
	$(RM) $(addsuffix .elf,$(TARGETS)) $(addsuffix .gba,$(TARGETS)) \
	 $(addsuffix .map,$(TARGETS))

-include $(wildcard $(BUILD)/*.d)
