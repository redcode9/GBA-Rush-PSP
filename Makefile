HOST_CC ?= cc
HOST_BUILD_DIR ?= build/host
HOST_TEST_CODEBREAKER = $(HOST_BUILD_DIR)/test_codebreaker
HOST_TEST_GBA_SIO = $(HOST_BUILD_DIR)/test_gba_sio
HOST_TESTS = $(HOST_TEST_CODEBREAKER) $(HOST_TEST_GBA_SIO)

.PHONY: all check test tree-check psp-build launcher-stub package clean

all: check

check: test tree-check

test: $(HOST_TESTS)
	$(HOST_TEST_CODEBREAKER)
	$(HOST_TEST_GBA_SIO)

$(HOST_TEST_CODEBREAKER): tests/test_codebreaker.c source/src/codebreaker.c source/src/codebreaker.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_codebreaker.c source/src/codebreaker.c -o $(HOST_TEST_CODEBREAKER)

$(HOST_TEST_GBA_SIO): tests/test_gba_sio.c source/src/gba_sio.c source/src/gba_sio.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_gba_sio.c source/src/gba_sio.c -o $(HOST_TEST_GBA_SIO)

tree-check:
	sh scripts/check-tree.sh

psp-build:
	$(MAKE) -C source

launcher-stub:
	$(MAKE) -C source/single-game_launcher
	cp source/single-game_launcher/EBOOT.PBP \
	 source/single-game_launcher/tool/assets/EBOOT.PBP

package: psp-build check
	@test -n "$(VERSION)" || \
	 (echo "Set VERSION, for example VERSION=20260817-rc1" >&2; exit 1)
	sh scripts/package-release.sh "$(VERSION)"

clean:
	$(MAKE) -C source clean
	$(MAKE) -C source/single-game_launcher clean
	$(RM) source/single-game_launcher/tool/assets/EBOOT.PBP
	$(RM) $(HOST_TESTS)
	-rmdir $(HOST_BUILD_DIR) build 2>/dev/null
