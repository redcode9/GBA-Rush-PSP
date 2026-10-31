# Host build of the tests and checks; the PSP build lives in source/Makefile.
#   make / make check     every host test plus the source tree checks
#   make test-sanitize    the C tests again under ASan and UBSan
#   make psp-build        the EBOOT (needs the PSP SDK)
#   make package VERSION=x.y.z   build, check and zip a release
# Each test builds the module it covers straight from source/src, so most
# need only a host C compiler.
#
# Adding a C test (follow test_sound_math as the example):
#   1. a HOST_TEST_<NAME> variable below and an entry in HOST_TESTS;
#   2. a $(HOST_SANITIZE_DIR)/test_<name> entry in HOST_SANITIZE_TESTS;
#   3. its line in the `test` recipe and in the `test-sanitize` recipe;
#   4. a build rule for each of the two binaries near the end of the file.
HOST_CC ?= cc
PYTHON ?= python3
HOST_BUILD_DIR ?= build/host
HOST_SANITIZE_DIR ?= build/sanitize
HOST_SANITIZE_FLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
# SDK headers for the two tests that compile PSP sources (memory.c and
# psp_video.c) on the host.
PSP_SDK_INCLUDE ?= $(shell psp-config --pspsdk-path 2>/dev/null)/include
HOST_TEST_CODEBREAKER = $(HOST_BUILD_DIR)/test_codebreaker
HOST_TEST_GBA_SIO = $(HOST_BUILD_DIR)/test_gba_sio
HOST_TEST_LINK_NET = $(HOST_BUILD_DIR)/test_link_net
HOST_TEST_LINK_CABLE = $(HOST_BUILD_DIR)/test_link_cable
# The link cable test runs the real cable, channel and SIO code together.
LINK_CABLE_SOURCES = tests/test_link_cable.c source/src/link_cable.c \
 source/src/link_net.c source/src/gba_sio.c
LINK_CABLE_HEADERS = source/src/link_cable.h source/src/link_net.h \
 source/src/gba_sio.h
HOST_TEST_ADHOC_PEER = $(HOST_BUILD_DIR)/test_adhoc_peer
HOST_TEST_POWER_LIFECYCLE = $(HOST_BUILD_DIR)/test_power_lifecycle
HOST_TEST_LINK_AUDIO_WAIT = $(HOST_BUILD_DIR)/test_link_audio_wait
HOST_TEST_VIDEO_ADAPTIVE = $(HOST_BUILD_DIR)/test_video_adaptive
HOST_TEST_VIDEO_DISPATCH = $(HOST_BUILD_DIR)/test_video_dispatch_state
HOST_TEST_PERF_METRICS = $(HOST_BUILD_DIR)/test_perf_metrics
HOST_TEST_PERF_REPORT = $(HOST_BUILD_DIR)/test_perf_report
HOST_TEST_PERF_MONITOR = $(HOST_BUILD_DIR)/test_perf_monitor
HOST_TEST_PERF_DMA_PROBE = $(HOST_BUILD_DIR)/test_perf_dma_probe
HOST_TEST_PERF_VIDEO_SENTINEL = $(HOST_BUILD_DIR)/test_perf_video_sentinel
HOST_TEST_INPUT_TRACE = $(HOST_BUILD_DIR)/test_input_trace
HOST_TEST_MENU_MODEL = $(HOST_BUILD_DIR)/test_menu_model
HOST_TEST_MENU_LAYOUT = $(HOST_BUILD_DIR)/test_menu_layout
HOST_TEST_CONFIG_MODEL = $(HOST_BUILD_DIR)/test_config_model
HOST_TEST_CONFIG_STORE = $(HOST_BUILD_DIR)/test_config_store
HOST_TEST_FAST_FORWARD = $(HOST_BUILD_DIR)/test_fast_forward_policy
HOST_TEST_FAST_FORWARD_AUDIO = $(HOST_BUILD_DIR)/test_fast_forward_audio
HOST_TEST_VIDEO_MOSAIC = $(HOST_BUILD_DIR)/test_video_mosaic
HOST_TEST_DYNAREC_WORKLIST = $(HOST_BUILD_DIR)/test_dynarec_worklist
HOST_TEST_SOUND_MATH = $(HOST_BUILD_DIR)/test_sound_math
HOST_TEST_RUNTIME_LOG = $(HOST_BUILD_DIR)/test_runtime_log
HOST_TESTS = $(HOST_TEST_CODEBREAKER) $(HOST_TEST_GBA_SIO) \
 $(HOST_TEST_LINK_NET) $(HOST_TEST_LINK_CABLE) \
 $(HOST_TEST_ADHOC_PEER) \
 $(HOST_TEST_POWER_LIFECYCLE) \
 $(HOST_TEST_LINK_AUDIO_WAIT) $(HOST_TEST_VIDEO_ADAPTIVE) \
 $(HOST_TEST_VIDEO_DISPATCH) \
 $(HOST_TEST_PERF_METRICS) $(HOST_TEST_PERF_REPORT) \
 $(HOST_TEST_PERF_MONITOR) $(HOST_TEST_PERF_DMA_PROBE) \
 $(HOST_TEST_PERF_VIDEO_SENTINEL) \
 $(HOST_TEST_INPUT_TRACE) $(HOST_TEST_MENU_MODEL) \
 $(HOST_TEST_MENU_LAYOUT) \
 $(HOST_TEST_CONFIG_MODEL) $(HOST_TEST_CONFIG_STORE) $(HOST_TEST_FAST_FORWARD) \
 $(HOST_TEST_FAST_FORWARD_AUDIO) $(HOST_TEST_VIDEO_MOSAIC) \
 $(HOST_TEST_DYNAREC_WORKLIST) $(HOST_TEST_SOUND_MATH) \
 $(HOST_TEST_RUNTIME_LOG)
# The same C tests built with HOST_SANITIZE_FLAGS for make test-sanitize.
HOST_SANITIZE_TESTS = $(HOST_SANITIZE_DIR)/test_codebreaker \
 $(HOST_SANITIZE_DIR)/test_gba_sio $(HOST_SANITIZE_DIR)/test_link_net \
 $(HOST_SANITIZE_DIR)/test_link_cable \
 $(HOST_SANITIZE_DIR)/test_adhoc_peer \
 $(HOST_SANITIZE_DIR)/test_power_lifecycle \
 $(HOST_SANITIZE_DIR)/test_link_audio_wait \
 $(HOST_SANITIZE_DIR)/test_video_adaptive \
 $(HOST_SANITIZE_DIR)/test_video_dispatch_state \
 $(HOST_SANITIZE_DIR)/test_perf_metrics \
 $(HOST_SANITIZE_DIR)/test_perf_report \
 $(HOST_SANITIZE_DIR)/test_perf_monitor \
 $(HOST_SANITIZE_DIR)/test_perf_dma_probe \
 $(HOST_SANITIZE_DIR)/test_perf_video_sentinel \
 $(HOST_SANITIZE_DIR)/test_input_trace \
 $(HOST_SANITIZE_DIR)/test_menu_model \
 $(HOST_SANITIZE_DIR)/test_menu_layout \
 $(HOST_SANITIZE_DIR)/test_config_model \
 $(HOST_SANITIZE_DIR)/test_config_store \
 $(HOST_SANITIZE_DIR)/test_fast_forward_policy \
 $(HOST_SANITIZE_DIR)/test_fast_forward_audio \
 $(HOST_SANITIZE_DIR)/test_video_mosaic \
 $(HOST_SANITIZE_DIR)/test_dynarec_worklist \
 $(HOST_SANITIZE_DIR)/test_sound_math \
 $(HOST_SANITIZE_DIR)/test_runtime_log

.PHONY: all check test test-tools test-sanitize check-cbmc check-rom-link \
 tree-check psp-build launcher-stub package artifact-smoke clean

all: check

check: test tree-check

test: $(HOST_TESTS) test-tools
	$(HOST_TEST_CODEBREAKER)
	$(HOST_TEST_GBA_SIO)
	$(HOST_TEST_LINK_NET)
	$(HOST_TEST_LINK_CABLE)
	$(HOST_TEST_ADHOC_PEER)
	$(HOST_TEST_POWER_LIFECYCLE)
	$(HOST_TEST_LINK_AUDIO_WAIT)
	$(HOST_TEST_VIDEO_ADAPTIVE)
	$(HOST_TEST_VIDEO_DISPATCH)
	$(HOST_TEST_PERF_METRICS)
	$(HOST_TEST_PERF_REPORT)
	$(HOST_TEST_PERF_MONITOR)
	$(HOST_TEST_PERF_DMA_PROBE)
	$(HOST_TEST_PERF_VIDEO_SENTINEL)
	$(HOST_TEST_INPUT_TRACE)
	$(HOST_TEST_MENU_MODEL)
	$(HOST_TEST_MENU_LAYOUT)
	$(HOST_TEST_CONFIG_MODEL)
	$(HOST_TEST_CONFIG_STORE)
	$(HOST_TEST_FAST_FORWARD)
	$(HOST_TEST_FAST_FORWARD_AUDIO)
	$(HOST_TEST_VIDEO_MOSAIC)
	$(HOST_TEST_DYNAREC_WORKLIST)
	$(HOST_TEST_SOUND_MATH)
	$(HOST_TEST_RUNTIME_LOG)

# Python tests. test_startup and test_browser_text also compile functions
# cut out of main.c and gui.c with HOST_CC.
test-tools:
	$(PYTHON) -m unittest tests/test_compare_benchmarks.py \
	 tests/test_decode_runtime_log.py tests/test_startup.py \
	 tests/test_browser_text.py

# The link cable test runs fewer network profiles (--quick) under the
# sanitizers, where it is much slower.
test-sanitize: $(HOST_SANITIZE_TESTS)
	$(HOST_SANITIZE_DIR)/test_codebreaker
	$(HOST_SANITIZE_DIR)/test_gba_sio
	$(HOST_SANITIZE_DIR)/test_link_net
	$(HOST_SANITIZE_DIR)/test_link_cable --quick
	$(HOST_SANITIZE_DIR)/test_adhoc_peer
	$(HOST_SANITIZE_DIR)/test_power_lifecycle
	$(HOST_SANITIZE_DIR)/test_link_audio_wait
	$(HOST_SANITIZE_DIR)/test_video_adaptive
	$(HOST_SANITIZE_DIR)/test_video_dispatch_state
	$(HOST_SANITIZE_DIR)/test_perf_metrics
	$(HOST_SANITIZE_DIR)/test_perf_report
	$(HOST_SANITIZE_DIR)/test_perf_monitor
	$(HOST_SANITIZE_DIR)/test_perf_dma_probe
	$(HOST_SANITIZE_DIR)/test_perf_video_sentinel
	$(HOST_SANITIZE_DIR)/test_input_trace
	$(HOST_SANITIZE_DIR)/test_menu_model
	$(HOST_SANITIZE_DIR)/test_menu_layout
	$(HOST_SANITIZE_DIR)/test_config_model
	$(HOST_SANITIZE_DIR)/test_config_store
	$(HOST_SANITIZE_DIR)/test_fast_forward_policy
	$(HOST_SANITIZE_DIR)/test_fast_forward_audio
	$(HOST_SANITIZE_DIR)/test_video_mosaic
	$(HOST_SANITIZE_DIR)/test_dynarec_worklist
	$(HOST_SANITIZE_DIR)/test_sound_math
	$(HOST_SANITIZE_DIR)/test_runtime_log

$(HOST_TEST_CODEBREAKER): tests/test_codebreaker.c source/src/codebreaker.c source/src/codebreaker.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_codebreaker.c source/src/codebreaker.c -o $(HOST_TEST_CODEBREAKER)

$(HOST_TEST_GBA_SIO): tests/test_gba_sio.c source/src/gba_sio.c source/src/gba_sio.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_gba_sio.c source/src/gba_sio.c -o $(HOST_TEST_GBA_SIO)

$(HOST_TEST_LINK_NET): tests/test_link_net.c source/src/link_net.c \
 source/src/link_net.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_link_net.c source/src/link_net.c -o $@

# -O2 keeps the long simulated link sessions fast.
$(HOST_TEST_LINK_CABLE): $(LINK_CABLE_SOURCES) $(LINK_CABLE_HEADERS)
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -Isource/src \
	 $(LINK_CABLE_SOURCES) -o $@

$(HOST_TEST_ADHOC_PEER): tests/test_adhoc_peer.c \
 source/src/adhoc_peer.c source/src/adhoc_peer.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_adhoc_peer.c source/src/adhoc_peer.c -o $@

# Compiles against the bundled zlib header but links the host libz;
# ZLIB_CONST as in the PSP build.
$(HOST_TEST_RUNTIME_LOG): tests/test_runtime_log.c \
 source/src/runtime_log.c source/src/runtime_log.h \
 source/src/runtime_log_store.c source/src/runtime_log_store.h \
 source/src/zlib/zlib.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 tests/test_runtime_log.c source/src/runtime_log.c \
	 source/src/runtime_log_store.c -lz -o $@

$(HOST_TEST_POWER_LIFECYCLE): tests/test_power_lifecycle.c \
 source/src/power_lifecycle.c source/src/power_lifecycle.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_power_lifecycle.c source/src/power_lifecycle.c -o $@

$(HOST_TEST_LINK_AUDIO_WAIT): tests/test_link_audio_wait.c \
 source/src/link_audio_wait.c source/src/link_audio_wait.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_link_audio_wait.c source/src/link_audio_wait.c -o $@

$(HOST_TEST_VIDEO_ADAPTIVE): tests/test_video_adaptive.c \
 source/src/video_adaptive.c source/src/video_adaptive.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_video_adaptive.c source/src/video_adaptive.c -o $@

$(HOST_TEST_VIDEO_DISPATCH): tests/test_video_dispatch_state.c \
 source/src/video_dispatch_state.c source/src/video_dispatch_state.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_video_dispatch_state.c source/src/video_dispatch_state.c -o $@

$(HOST_TEST_PERF_METRICS): tests/test_perf_metrics.c \
 source/src/perf_metrics.c source/src/perf_metrics.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_perf_metrics.c source/src/perf_metrics.c -o $@

$(HOST_TEST_PERF_REPORT): tests/test_perf_report.c \
 source/src/perf_report.c source/src/perf_report.h source/src/perf_metrics.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_perf_report.c source/src/perf_report.c -o $@

$(HOST_TEST_PERF_MONITOR): tests/test_perf_monitor.c \
 source/src/perf_monitor.c source/src/perf_monitor.h \
 source/src/perf_metrics.c source/src/perf_metrics.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_perf_monitor.c source/src/perf_monitor.c \
	 source/src/perf_metrics.c -o $@

# Host test for the production dma_transfer() probe path.
# memory.c is compiled against the real PSP SDK headers with -w (host
# build); the test TU itself is built with -Wall -Wextra -Werror.
$(HOST_TEST_PERF_DMA_PROBE): tests/test_perf_dma_probe.c tests/host_dma_stubs.c \
 source/src/memory.c source/src/memory.h source/src/cpu.h \
 source/src/perf_monitor.c source/src/perf_monitor.h \
 source/src/perf_metrics.c source/src/perf_metrics.h \
 tests/host_shim/malloc.h tests/host_shim/host_builtins.h \
 tests/host_shim/psptypes.h
	mkdir -p $(HOST_BUILD_DIR)
	# SDK include before tests/host_shim: the real SDK psptypes.h must win
	# for memory.c (the shim psptypes.h must not shadow it).
	$(HOST_CC) -std=c99 -O1 -w -include tests/host_shim/host_builtins.h \
	 -I source/src -I $(PSP_SDK_INCLUDE) -I tests/host_shim \
	 -c source/src/memory.c -o $(HOST_BUILD_DIR)/perf_dma_memory.o
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror \
	 -I source/src \
	 tests/test_perf_dma_probe.c tests/host_dma_stubs.c \
	 source/src/perf_monitor.c source/src/perf_metrics.c \
	 $(HOST_BUILD_DIR)/perf_dma_memory.o -o $@

# Host test for the psp_video.c frame-time timestamp sentinel
# psp_video.c and the stub TU are compiled against the
# real PSP SDK headers with -w (host build); the test TU itself is
# built with -Wall -Wextra -Werror.
$(HOST_TEST_PERF_VIDEO_SENTINEL): tests/test_perf_video_sentinel.c \
 tests/host_psp_video_stubs.c \
 source/src/psp_video.c source/src/psp_video.h \
 source/src/video_adaptive.h source/src/video_adaptive.c \
 source/src/perf_monitor.c source/src/perf_monitor.h \
 source/src/perf_metrics.c source/src/perf_metrics.h \
 tests/host_shim/gba_types.h tests/host_shim/host_builtins.h \
 tests/host_shim/psptypes.h
	mkdir -p $(HOST_BUILD_DIR)
	# Stages 1-2 put the SDK include before tests/host_shim: the real
	# SDK psptypes.h (needed by pspthreadman.h, ...) wins, while the
	# shim malloc.h still resolves (the SDK ships no malloc.h and the
	# macOS SDK dropped it). Only the Werror test TU (stage 3) uses
	# the shim psptypes.h (host_shim first, no SDK path).
	$(HOST_CC) -std=c99 -O1 -w -include tests/host_shim/host_builtins.h \
	 -I source/src -I $(PSP_SDK_INCLUDE) -I tests/host_shim \
	 -c source/src/psp_video.c -o $(HOST_BUILD_DIR)/perf_video_psp_video.o
	$(HOST_CC) -std=c99 -O1 -w -include tests/host_shim/host_builtins.h \
	 -I source/src -I $(PSP_SDK_INCLUDE) -I tests/host_shim \
	 -c tests/host_psp_video_stubs.c -o $(HOST_BUILD_DIR)/perf_video_stubs.o
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror \
	 -I source/src -I tests/host_shim \
	 tests/test_perf_video_sentinel.c \
	 source/src/perf_monitor.c source/src/perf_metrics.c \
	 source/src/video_adaptive.c \
	 $(HOST_BUILD_DIR)/perf_video_psp_video.o \
	 $(HOST_BUILD_DIR)/perf_video_stubs.o -o $@

$(HOST_TEST_INPUT_TRACE): tests/test_input_trace.c \
 source/src/input_trace.c source/src/input_trace.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_input_trace.c source/src/input_trace.c -o $@

$(HOST_TEST_MENU_MODEL): tests/test_menu_model.c \
 source/src/menu_model.c source/src/menu_model.h \
 source/src/menu_catalog.c source/src/menu_catalog.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_menu_model.c source/src/menu_model.c source/src/menu_catalog.c -o $@

$(HOST_TEST_MENU_LAYOUT): tests/test_menu_layout.c \
 source/src/menu_layout.c source/src/menu_layout.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_menu_layout.c source/src/menu_layout.c -o $@

$(HOST_TEST_CONFIG_MODEL): tests/test_config_model.c \
 source/src/config_model.c source/src/config_model.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_config_model.c source/src/config_model.c -o $@

$(HOST_TEST_CONFIG_STORE): tests/test_config_store.c \
 source/src/config_store.c source/src/config_store.h \
 source/src/config_model.c source/src/config_model.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_config_store.c source/src/config_store.c \
	 source/src/config_model.c -o $@

$(HOST_TEST_FAST_FORWARD): tests/test_fast_forward_policy.c \
 source/src/fast_forward_policy.c source/src/fast_forward_policy.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_fast_forward_policy.c source/src/fast_forward_policy.c -o $@

$(HOST_TEST_FAST_FORWARD_AUDIO): tests/test_fast_forward_audio.c \
 source/src/fast_forward_audio.c source/src/fast_forward_audio.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_fast_forward_audio.c source/src/fast_forward_audio.c -o $@

# video_mosaic.h is header-only.
$(HOST_TEST_VIDEO_MOSAIC): tests/test_video_mosaic.c source/src/video_mosaic.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_video_mosaic.c -o $@

$(HOST_TEST_DYNAREC_WORKLIST): tests/test_dynarec_worklist.c \
 source/src/dynarec_worklist.c source/src/dynarec_worklist.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_dynarec_worklist.c source/src/dynarec_worklist.c -o $@

$(HOST_TEST_SOUND_MATH): tests/test_sound_math.c source/src/sound_math.c \
 source/src/sound_math.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_sound_math.c source/src/sound_math.c -o $@

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

$(HOST_SANITIZE_DIR)/test_link_cable: $(LINK_CABLE_SOURCES) \
 $(LINK_CABLE_HEADERS)
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src $(LINK_CABLE_SOURCES) -o $@

$(HOST_SANITIZE_DIR)/test_link_net: tests/test_link_net.c \
 source/src/link_net.c source/src/link_net.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_link_net.c source/src/link_net.c -o $@

$(HOST_SANITIZE_DIR)/test_adhoc_peer: tests/test_adhoc_peer.c \
 source/src/adhoc_peer.c source/src/adhoc_peer.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_adhoc_peer.c source/src/adhoc_peer.c -o $@

$(HOST_SANITIZE_DIR)/test_runtime_log: tests/test_runtime_log.c \
 source/src/runtime_log.c source/src/runtime_log.h \
 source/src/runtime_log_store.c source/src/runtime_log_store.h \
 source/src/zlib/zlib.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src tests/test_runtime_log.c \
	 source/src/runtime_log.c source/src/runtime_log_store.c -lz -o $@

$(HOST_SANITIZE_DIR)/test_power_lifecycle: tests/test_power_lifecycle.c \
 source/src/power_lifecycle.c source/src/power_lifecycle.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_power_lifecycle.c source/src/power_lifecycle.c -o $@

$(HOST_SANITIZE_DIR)/test_link_audio_wait: tests/test_link_audio_wait.c \
 source/src/link_audio_wait.c source/src/link_audio_wait.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_link_audio_wait.c source/src/link_audio_wait.c -o $@

$(HOST_SANITIZE_DIR)/test_video_adaptive: tests/test_video_adaptive.c \
 source/src/video_adaptive.c source/src/video_adaptive.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_video_adaptive.c source/src/video_adaptive.c -o $@

$(HOST_SANITIZE_DIR)/test_video_dispatch_state: \
 tests/test_video_dispatch_state.c source/src/video_dispatch_state.c \
 source/src/video_dispatch_state.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_video_dispatch_state.c \
	 source/src/video_dispatch_state.c -o $@

$(HOST_SANITIZE_DIR)/test_perf_metrics: tests/test_perf_metrics.c \
 source/src/perf_metrics.c source/src/perf_metrics.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_perf_metrics.c source/src/perf_metrics.c -o $@

$(HOST_SANITIZE_DIR)/test_perf_report: tests/test_perf_report.c \
 source/src/perf_report.c source/src/perf_report.h source/src/perf_metrics.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_perf_report.c source/src/perf_report.c -o $@

$(HOST_SANITIZE_DIR)/test_perf_dma_probe: tests/test_perf_dma_probe.c \
 tests/host_dma_stubs.c \
 source/src/memory.c source/src/memory.h source/src/cpu.h \
 source/src/perf_monitor.c source/src/perf_monitor.h \
 source/src/perf_metrics.c source/src/perf_metrics.h \
 tests/host_shim/malloc.h tests/host_shim/host_builtins.h \
 tests/host_shim/psptypes.h
	mkdir -p $(HOST_SANITIZE_DIR)
	# SDK include before tests/host_shim: the real SDK psptypes.h must win
	# for memory.c (the shim psptypes.h must not shadow it).
	$(HOST_CC) -std=c99 -w $(HOST_SANITIZE_FLAGS) \
	 -include tests/host_shim/host_builtins.h \
	 -I source/src -I $(PSP_SDK_INCLUDE) -I tests/host_shim \
	 -c source/src/memory.c -o $(HOST_SANITIZE_DIR)/perf_dma_memory.o
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -I source/src \
	 tests/test_perf_dma_probe.c tests/host_dma_stubs.c \
	 source/src/perf_monitor.c source/src/perf_metrics.c \
	 $(HOST_SANITIZE_DIR)/perf_dma_memory.o -o $@

$(HOST_SANITIZE_DIR)/test_perf_video_sentinel: tests/test_perf_video_sentinel.c \
 tests/host_psp_video_stubs.c \
 source/src/psp_video.c source/src/psp_video.h \
 source/src/video_adaptive.h source/src/video_adaptive.c \
 source/src/perf_monitor.c source/src/perf_monitor.h \
 source/src/perf_metrics.c source/src/perf_metrics.h \
 tests/host_shim/gba_types.h tests/host_shim/host_builtins.h \
 tests/host_shim/psptypes.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -w $(HOST_SANITIZE_FLAGS) \
	 -include tests/host_shim/host_builtins.h \
	 -I source/src -I $(PSP_SDK_INCLUDE) -I tests/host_shim \
	 -c source/src/psp_video.c -o $(HOST_SANITIZE_DIR)/perf_video_psp_video.o
	$(HOST_CC) -std=c99 -w $(HOST_SANITIZE_FLAGS) \
	 -include tests/host_shim/host_builtins.h \
	 -I source/src -I $(PSP_SDK_INCLUDE) -I tests/host_shim \
	 -c tests/host_psp_video_stubs.c -o $(HOST_SANITIZE_DIR)/perf_video_stubs.o
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -I source/src -I tests/host_shim \
	 tests/test_perf_video_sentinel.c \
	 source/src/perf_monitor.c source/src/perf_metrics.c \
	 source/src/video_adaptive.c \
	 $(HOST_SANITIZE_DIR)/perf_video_psp_video.o \
	 $(HOST_SANITIZE_DIR)/perf_video_stubs.o -o $@

$(HOST_SANITIZE_DIR)/test_perf_monitor: tests/test_perf_monitor.c \
 source/src/perf_monitor.c source/src/perf_monitor.h \
 source/src/perf_metrics.c source/src/perf_metrics.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_perf_monitor.c source/src/perf_monitor.c \
	 source/src/perf_metrics.c -o $@

$(HOST_SANITIZE_DIR)/test_input_trace: tests/test_input_trace.c \
 source/src/input_trace.c source/src/input_trace.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_input_trace.c source/src/input_trace.c -o $@

$(HOST_SANITIZE_DIR)/test_menu_model: tests/test_menu_model.c \
 source/src/menu_model.c source/src/menu_model.h \
 source/src/menu_catalog.c source/src/menu_catalog.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_menu_model.c source/src/menu_model.c \
	 source/src/menu_catalog.c -o $@

$(HOST_SANITIZE_DIR)/test_menu_layout: tests/test_menu_layout.c \
 source/src/menu_layout.c source/src/menu_layout.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_menu_layout.c source/src/menu_layout.c -o $@

$(HOST_SANITIZE_DIR)/test_config_model: tests/test_config_model.c \
 source/src/config_model.c source/src/config_model.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_config_model.c source/src/config_model.c -o $@

$(HOST_SANITIZE_DIR)/test_config_store: tests/test_config_store.c \
 source/src/config_store.c source/src/config_store.h \
 source/src/config_model.c source/src/config_model.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_config_store.c source/src/config_store.c \
	 source/src/config_model.c -o $@

$(HOST_SANITIZE_DIR)/test_fast_forward_policy: \
 tests/test_fast_forward_policy.c source/src/fast_forward_policy.c \
 source/src/fast_forward_policy.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_fast_forward_policy.c \
	 source/src/fast_forward_policy.c -o $@

$(HOST_SANITIZE_DIR)/test_fast_forward_audio: \
 tests/test_fast_forward_audio.c source/src/fast_forward_audio.c \
 source/src/fast_forward_audio.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_fast_forward_audio.c \
	 source/src/fast_forward_audio.c -o $@

$(HOST_SANITIZE_DIR)/test_video_mosaic: tests/test_video_mosaic.c \
 source/src/video_mosaic.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_video_mosaic.c -o $@

$(HOST_SANITIZE_DIR)/test_dynarec_worklist: tests/test_dynarec_worklist.c \
 source/src/dynarec_worklist.c source/src/dynarec_worklist.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_dynarec_worklist.c \
	 source/src/dynarec_worklist.c -o $@

$(HOST_SANITIZE_DIR)/test_sound_math: tests/test_sound_math.c \
 source/src/sound_math.c source/src/sound_math.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_sound_math.c source/src/sound_math.c -o $@

# Optional gates outside make check: the CBMC proof needs CBMC, the ROM link
# trade needs mGBA plus your own FireRed ROM, BIOS and save.
check-cbmc:
	sh scripts/check-cbmc.sh

check-rom-link:
	sh scripts/check-rom-link.sh

tree-check:
	sh scripts/check-tree.sh

psp-build:
	$(MAKE) -C source

# Rebuilds the single-game launcher EBOOT that the desktop launcher tool
# ships in its assets.
launcher-stub:
	$(MAKE) -C source/single-game_launcher
	cp source/single-game_launcher/EBOOT.PBP \
	 source/single-game_launcher/tool/assets/EBOOT.PBP

package: psp-build check
	@test -n "$(VERSION)" || \
	 (echo "Set VERSION, for example VERSION=1.4.1" >&2; exit 1)
	sh scripts/package-release.sh "$(VERSION)"
	sh scripts/check-package.sh "$(VERSION)"

# Re-checks an existing release archive without rebuilding it.
artifact-smoke:
	@test -n "$(VERSION)" || \
	 (echo "Set VERSION, for example VERSION=1.4.1" >&2; exit 1)
	sh scripts/check-package.sh "$(VERSION)"

clean:
	$(MAKE) -C source clean
	$(MAKE) -C source/single-game_launcher clean
	$(RM) source/single-game_launcher/tool/assets/EBOOT.PBP
	$(RM) $(HOST_TESTS)
	$(RM) $(HOST_SANITIZE_TESTS)
	-rmdir $(HOST_BUILD_DIR) $(HOST_SANITIZE_DIR) build 2>/dev/null
