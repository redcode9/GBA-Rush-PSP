HOST_CC ?= cc
HOST_BUILD_DIR ?= build/host
HOST_SANITIZE_DIR ?= build/sanitize
HOST_SANITIZE_FLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
HOST_TEST_CODEBREAKER = $(HOST_BUILD_DIR)/test_codebreaker
HOST_TEST_GBA_SIO = $(HOST_BUILD_DIR)/test_gba_sio
HOST_TEST_LINK_PROTOCOL = $(HOST_BUILD_DIR)/test_link_protocol
HOST_TEST_LINK_SESSION = $(HOST_BUILD_DIR)/test_link_session
HOST_TEST_POWER_LIFECYCLE = $(HOST_BUILD_DIR)/test_power_lifecycle
HOST_TEST_VIDEO_ADAPTIVE = $(HOST_BUILD_DIR)/test_video_adaptive
HOST_TESTS = $(HOST_TEST_CODEBREAKER) $(HOST_TEST_GBA_SIO) \
 $(HOST_TEST_LINK_PROTOCOL) $(HOST_TEST_LINK_SESSION) \
 $(HOST_TEST_POWER_LIFECYCLE) $(HOST_TEST_VIDEO_ADAPTIVE)
HOST_SANITIZE_TESTS = $(HOST_SANITIZE_DIR)/test_codebreaker \
 $(HOST_SANITIZE_DIR)/test_gba_sio $(HOST_SANITIZE_DIR)/test_link_protocol \
 $(HOST_SANITIZE_DIR)/test_link_session \
 $(HOST_SANITIZE_DIR)/test_power_lifecycle \
 $(HOST_SANITIZE_DIR)/test_video_adaptive

.PHONY: all check test test-sanitize tree-check psp-build launcher-stub \
 package artifact-smoke clean

all: check

check: test tree-check

test: $(HOST_TESTS)
	$(HOST_TEST_CODEBREAKER)
	$(HOST_TEST_GBA_SIO)
	$(HOST_TEST_LINK_PROTOCOL)
	$(HOST_TEST_LINK_SESSION)
	$(HOST_TEST_POWER_LIFECYCLE)
	$(HOST_TEST_VIDEO_ADAPTIVE)

test-sanitize: $(HOST_SANITIZE_TESTS)
	$(HOST_SANITIZE_DIR)/test_codebreaker
	$(HOST_SANITIZE_DIR)/test_gba_sio
	$(HOST_SANITIZE_DIR)/test_link_protocol
	$(HOST_SANITIZE_DIR)/test_link_session
	$(HOST_SANITIZE_DIR)/test_power_lifecycle
	$(HOST_SANITIZE_DIR)/test_video_adaptive

$(HOST_TEST_CODEBREAKER): tests/test_codebreaker.c source/src/codebreaker.c source/src/codebreaker.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_codebreaker.c source/src/codebreaker.c -o $(HOST_TEST_CODEBREAKER)

$(HOST_TEST_GBA_SIO): tests/test_gba_sio.c source/src/gba_sio.c source/src/gba_sio.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_gba_sio.c source/src/gba_sio.c -o $(HOST_TEST_GBA_SIO)

$(HOST_TEST_LINK_PROTOCOL): tests/test_link_protocol.c \
 source/src/link_protocol.c source/src/link_protocol.h source/src/zlib/zlib.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 tests/test_link_protocol.c source/src/link_protocol.c -lz \
	 -o $(HOST_TEST_LINK_PROTOCOL)

$(HOST_TEST_LINK_SESSION): tests/test_link_session.c \
 tests/fake_link_transport.c tests/fake_link_transport.h \
 source/src/link_session.c source/src/link_session.h source/src/link_transport.h \
 source/src/link_protocol.c source/src/link_protocol.h \
 source/src/gba_sio.c source/src/gba_sio.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 -Itests tests/test_link_session.c tests/fake_link_transport.c \
	 source/src/link_session.c source/src/link_protocol.c source/src/gba_sio.c \
	 -lz -o $(HOST_TEST_LINK_SESSION)

$(HOST_TEST_POWER_LIFECYCLE): tests/test_power_lifecycle.c \
 source/src/power_lifecycle.c source/src/power_lifecycle.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_power_lifecycle.c source/src/power_lifecycle.c -o $@

$(HOST_TEST_VIDEO_ADAPTIVE): tests/test_video_adaptive.c \
 source/src/video_adaptive.c source/src/video_adaptive.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_video_adaptive.c source/src/video_adaptive.c -o $@

$(HOST_SANITIZE_DIR)/test_codebreaker: tests/test_codebreaker.c \
 source/src/codebreaker.c source/src/codebreaker.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_codebreaker.c source/src/codebreaker.c -o $@

$(HOST_SANITIZE_DIR)/test_gba_sio: tests/test_gba_sio.c \
 source/src/gba_sio.c source/src/gba_sio.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_gba_sio.c source/src/gba_sio.c -o $@

$(HOST_SANITIZE_DIR)/test_link_protocol: tests/test_link_protocol.c \
 source/src/link_protocol.c source/src/link_protocol.h source/src/zlib/zlib.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src tests/test_link_protocol.c \
	 source/src/link_protocol.c -lz -o $@

$(HOST_SANITIZE_DIR)/test_link_session: tests/test_link_session.c \
 tests/fake_link_transport.c tests/fake_link_transport.h \
 source/src/link_session.c source/src/link_session.h source/src/link_transport.h \
 source/src/link_protocol.c source/src/link_protocol.h \
 source/src/gba_sio.c source/src/gba_sio.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src -Itests tests/test_link_session.c \
	 tests/fake_link_transport.c source/src/link_session.c \
	 source/src/link_protocol.c source/src/gba_sio.c -lz -o $@

$(HOST_SANITIZE_DIR)/test_power_lifecycle: tests/test_power_lifecycle.c \
 source/src/power_lifecycle.c source/src/power_lifecycle.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_power_lifecycle.c source/src/power_lifecycle.c -o $@

$(HOST_SANITIZE_DIR)/test_video_adaptive: tests/test_video_adaptive.c \
 source/src/video_adaptive.c source/src/video_adaptive.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_video_adaptive.c source/src/video_adaptive.c -o $@

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
	 (echo "Set VERSION, for example VERSION=1.1.0" >&2; exit 1)
	sh scripts/package-release.sh "$(VERSION)"
	sh scripts/check-package.sh "$(VERSION)"

artifact-smoke:
	@test -n "$(VERSION)" || \
	 (echo "Set VERSION, for example VERSION=1.1.0" >&2; exit 1)
	sh scripts/check-package.sh "$(VERSION)"

clean:
	$(MAKE) -C source clean
	$(MAKE) -C source/single-game_launcher clean
	$(RM) source/single-game_launcher/tool/assets/EBOOT.PBP
	$(RM) $(HOST_TESTS)
	$(RM) $(HOST_SANITIZE_TESTS)
	-rmdir $(HOST_BUILD_DIR) $(HOST_SANITIZE_DIR) build 2>/dev/null
