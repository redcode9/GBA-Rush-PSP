HOST_CC ?= cc
PYTHON ?= python3
HOST_BUILD_DIR ?= build/host
HOST_SANITIZE_DIR ?= build/sanitize
HOST_SANITIZE_FLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
PSP_SDK_INCLUDE ?= $(shell psp-config --pspsdk-path 2>/dev/null)/include
HOST_TEST_CODEBREAKER = $(HOST_BUILD_DIR)/test_codebreaker
HOST_TEST_GBA_SIO = $(HOST_BUILD_DIR)/test_gba_sio
HOST_TEST_LINK_NET = $(HOST_BUILD_DIR)/test_link_net
HOST_TEST_LINK_PROTOCOL = $(HOST_BUILD_DIR)/test_link_protocol
HOST_TEST_LINK_STREAM = $(HOST_BUILD_DIR)/test_link_stream
HOST_TEST_LINK_BENCHMARK = $(HOST_BUILD_DIR)/test_link_benchmark
HOST_TEST_ADHOC_PEER = $(HOST_BUILD_DIR)/test_adhoc_peer
HOST_TEST_ADHOC_DATAGRAM = $(HOST_BUILD_DIR)/test_adhoc_datagram
HOST_TEST_ADHOC_RECOVERY = $(HOST_BUILD_DIR)/test_adhoc_recovery
HOST_TEST_ADHOC_RENDEZVOUS = $(HOST_BUILD_DIR)/test_adhoc_rendezvous
HOST_TEST_ADHOC_RESYNC = $(HOST_BUILD_DIR)/test_adhoc_resync
HOST_TEST_LINK_SESSION = $(HOST_BUILD_DIR)/test_link_session
HOST_TEST_LINK_SESSION_JOURNAL = $(HOST_BUILD_DIR)/test_link_session_journal
HOST_TEST_LINK_PIPELINE = $(HOST_BUILD_DIR)/test_link_pipeline
HOST_TEST_LINK_CABLE_RUNTIME = $(HOST_BUILD_DIR)/test_link_cable_runtime
LINK_CABLE_RUNTIME_SOURCES = tests/test_link_cable_runtime.c \
 tests/fake_link_transport.c source/src/link_session.c \
 source/src/link_protocol.c source/src/gba_sio.c source/src/link_stream.c \
 source/src/link_benchmark.c source/src/link_clock.c source/src/link_journal.c \
 source/src/link_recovery_coordinator.c source/src/link_transport_scheduler.c \
 source/src/link_transport_lanes.c source/src/link_spsc_ring.c
LINK_CABLE_RUNTIME_HEADERS = $(wildcard source/src/link_*.h) \
 source/src/gba_sio.h tests/fake_link_transport.h
HOST_TEST_POWER_LIFECYCLE = $(HOST_BUILD_DIR)/test_power_lifecycle
HOST_TEST_LIFECYCLE_TEARDOWN = $(HOST_BUILD_DIR)/test_lifecycle_teardown
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
HOST_TEST_LINK_TX_QUEUE = $(HOST_BUILD_DIR)/test_link_tx_queue
HOST_TEST_LINK_CLOCK = $(HOST_BUILD_DIR)/test_link_clock
HOST_TEST_LINK_JOURNAL = $(HOST_BUILD_DIR)/test_link_journal_transitions
HOST_TEST_LINK_JOURNAL_STATE = $(HOST_BUILD_DIR)/test_link_journal
HOST_TEST_LINK_RECOVERY = $(HOST_BUILD_DIR)/test_link_recovery_coordinator
HOST_TEST_LINK_SPSC = $(HOST_BUILD_DIR)/test_link_spsc_ring
HOST_TEST_LINK_LANES = $(HOST_BUILD_DIR)/test_link_transport_lanes
HOST_TEST_LINK_SCHEDULER = $(HOST_BUILD_DIR)/test_link_transport_scheduler
HOST_TEST_RUNTIME_LOG = $(HOST_BUILD_DIR)/test_runtime_log
HOST_TESTS = $(HOST_TEST_CODEBREAKER) $(HOST_TEST_GBA_SIO) \
 $(HOST_TEST_LINK_NET) \
 $(HOST_TEST_ADHOC_PEER) $(HOST_TEST_ADHOC_DATAGRAM) \
 $(HOST_TEST_ADHOC_RECOVERY) \
 $(HOST_TEST_ADHOC_RENDEZVOUS) $(HOST_TEST_ADHOC_RESYNC) \
 $(HOST_TEST_LINK_PROTOCOL) $(HOST_TEST_LINK_STREAM) \
 $(HOST_TEST_LINK_BENCHMARK) $(HOST_TEST_LINK_SESSION) \
 $(HOST_TEST_LINK_SESSION_JOURNAL) \
 $(HOST_TEST_LINK_PIPELINE) $(HOST_TEST_LINK_CABLE_RUNTIME) \
 $(HOST_TEST_POWER_LIFECYCLE) $(HOST_TEST_LIFECYCLE_TEARDOWN) \
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
 $(HOST_TEST_LINK_TX_QUEUE) $(HOST_TEST_LINK_CLOCK) $(HOST_TEST_LINK_JOURNAL) \
 $(HOST_TEST_LINK_JOURNAL_STATE) $(HOST_TEST_LINK_SPSC) \
 $(HOST_TEST_LINK_RECOVERY) \
 $(HOST_TEST_LINK_LANES) $(HOST_TEST_LINK_SCHEDULER) $(HOST_TEST_RUNTIME_LOG)
HOST_SANITIZE_TESTS = $(HOST_SANITIZE_DIR)/test_codebreaker \
 $(HOST_SANITIZE_DIR)/test_gba_sio $(HOST_SANITIZE_DIR)/test_link_net $(HOST_SANITIZE_DIR)/test_link_protocol \
 $(HOST_SANITIZE_DIR)/test_link_stream \
 $(HOST_SANITIZE_DIR)/test_link_benchmark \
 $(HOST_SANITIZE_DIR)/test_adhoc_peer \
 $(HOST_SANITIZE_DIR)/test_adhoc_datagram \
 $(HOST_SANITIZE_DIR)/test_adhoc_recovery \
 $(HOST_SANITIZE_DIR)/test_adhoc_rendezvous \
 $(HOST_SANITIZE_DIR)/test_adhoc_resync \
 $(HOST_SANITIZE_DIR)/test_link_session \
 $(HOST_SANITIZE_DIR)/test_link_session_journal \
 $(HOST_SANITIZE_DIR)/test_link_pipeline \
 $(HOST_SANITIZE_DIR)/test_link_cable_runtime \
 $(HOST_SANITIZE_DIR)/test_power_lifecycle \
 $(HOST_SANITIZE_DIR)/test_lifecycle_teardown \
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
 $(HOST_SANITIZE_DIR)/test_link_tx_queue \
 $(HOST_SANITIZE_DIR)/test_link_clock \
 $(HOST_SANITIZE_DIR)/test_link_journal_transitions \
 $(HOST_SANITIZE_DIR)/test_link_journal \
 $(HOST_SANITIZE_DIR)/test_link_recovery_coordinator \
 $(HOST_SANITIZE_DIR)/test_link_spsc_ring \
 $(HOST_SANITIZE_DIR)/test_link_transport_lanes \
 $(HOST_SANITIZE_DIR)/test_link_transport_scheduler \
 $(HOST_SANITIZE_DIR)/test_runtime_log

.PHONY: all check test test-benchmark-tool test-sanitize check-link-model \
 check-cbmc \
 tree-check psp-build launcher-stub package artifact-smoke clean

all: check

check: test tree-check

test: $(HOST_TESTS) test-benchmark-tool
	$(HOST_TEST_CODEBREAKER)
	$(HOST_TEST_GBA_SIO)
	$(HOST_TEST_LINK_NET)
	$(HOST_TEST_ADHOC_PEER)
	$(HOST_TEST_ADHOC_DATAGRAM)
	$(HOST_TEST_ADHOC_RECOVERY)
	$(HOST_TEST_ADHOC_RENDEZVOUS)
	$(HOST_TEST_ADHOC_RESYNC)
	$(HOST_TEST_LINK_PROTOCOL)
	$(HOST_TEST_LINK_STREAM)
	$(HOST_TEST_LINK_BENCHMARK)
	$(HOST_TEST_LINK_SESSION)
	$(HOST_TEST_LINK_SESSION_JOURNAL)
	$(HOST_TEST_LINK_PIPELINE)
	$(HOST_TEST_LINK_CABLE_RUNTIME)
	$(HOST_TEST_POWER_LIFECYCLE)
	$(HOST_TEST_LIFECYCLE_TEARDOWN)
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
	$(HOST_TEST_LINK_TX_QUEUE)
	$(HOST_TEST_LINK_CLOCK)
	$(HOST_TEST_LINK_JOURNAL)
	$(HOST_TEST_LINK_JOURNAL_STATE)
	$(HOST_TEST_LINK_RECOVERY)
	$(HOST_TEST_LINK_SPSC)
	$(HOST_TEST_LINK_LANES)
	$(HOST_TEST_LINK_SCHEDULER)
	$(HOST_TEST_RUNTIME_LOG)

test-benchmark-tool:
	$(PYTHON) -m unittest tests/test_compare_benchmarks.py \
	 tests/test_decode_runtime_log.py tests/test_analyze_link_benchmark.py \
	 tests/test_startup.py tests/test_browser_text.py

test-sanitize: $(HOST_SANITIZE_TESTS)
	$(HOST_SANITIZE_DIR)/test_codebreaker
	$(HOST_SANITIZE_DIR)/test_gba_sio
	$(HOST_SANITIZE_DIR)/test_link_net
	$(HOST_SANITIZE_DIR)/test_adhoc_peer
	$(HOST_SANITIZE_DIR)/test_adhoc_datagram
	$(HOST_SANITIZE_DIR)/test_adhoc_recovery
	$(HOST_SANITIZE_DIR)/test_adhoc_rendezvous
	$(HOST_SANITIZE_DIR)/test_adhoc_resync
	$(HOST_SANITIZE_DIR)/test_link_protocol
	$(HOST_SANITIZE_DIR)/test_link_stream
	$(HOST_SANITIZE_DIR)/test_link_benchmark
	$(HOST_SANITIZE_DIR)/test_link_session
	$(HOST_SANITIZE_DIR)/test_link_session_journal
	$(HOST_SANITIZE_DIR)/test_link_pipeline
	$(HOST_SANITIZE_DIR)/test_link_cable_runtime
	$(HOST_SANITIZE_DIR)/test_power_lifecycle
	$(HOST_SANITIZE_DIR)/test_lifecycle_teardown
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
	$(HOST_SANITIZE_DIR)/test_link_tx_queue
	$(HOST_SANITIZE_DIR)/test_link_clock
	$(HOST_SANITIZE_DIR)/test_link_journal_transitions
	$(HOST_SANITIZE_DIR)/test_link_journal
	$(HOST_SANITIZE_DIR)/test_link_recovery_coordinator
	$(HOST_SANITIZE_DIR)/test_link_spsc_ring
	$(HOST_SANITIZE_DIR)/test_link_transport_lanes
	$(HOST_SANITIZE_DIR)/test_link_transport_scheduler
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

$(HOST_TEST_ADHOC_PEER): tests/test_adhoc_peer.c \
 source/src/adhoc_peer.c source/src/adhoc_peer.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_adhoc_peer.c source/src/adhoc_peer.c -o $@

$(HOST_TEST_ADHOC_DATAGRAM): tests/test_adhoc_datagram.c \
 source/src/adhoc_datagram.c source/src/adhoc_datagram.h source/src/zlib/zlib.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 tests/test_adhoc_datagram.c source/src/adhoc_datagram.c -lz -o $@

$(HOST_TEST_ADHOC_RECOVERY): tests/test_adhoc_recovery.c \
 source/src/adhoc_recovery.c source/src/adhoc_recovery.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_adhoc_recovery.c source/src/adhoc_recovery.c -o $@

$(HOST_TEST_ADHOC_RENDEZVOUS): tests/test_adhoc_rendezvous.c \
 source/src/adhoc_rendezvous.c source/src/adhoc_rendezvous.h \
 source/src/zlib/zlib.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 tests/test_adhoc_rendezvous.c source/src/adhoc_rendezvous.c -lz -o $@

$(HOST_TEST_ADHOC_RESYNC): tests/test_adhoc_resync.c \
 source/src/adhoc_resync.c source/src/adhoc_resync.h \
 source/src/adhoc_rendezvous.h source/src/adhoc_datagram.c \
 source/src/adhoc_datagram.h source/src/zlib/zlib.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 tests/test_adhoc_resync.c source/src/adhoc_resync.c \
	 source/src/adhoc_datagram.c -lz -o $@

$(HOST_TEST_LINK_PROTOCOL): tests/test_link_protocol.c \
 source/src/link_protocol.c source/src/link_protocol.h source/src/zlib/zlib.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 tests/test_link_protocol.c source/src/link_protocol.c -lz \
	 -o $(HOST_TEST_LINK_PROTOCOL)

$(HOST_TEST_LINK_STREAM): tests/test_link_stream.c \
 source/src/link_stream.c source/src/link_stream.h source/src/link_protocol.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_link_stream.c source/src/link_stream.c -o $@

$(HOST_TEST_LINK_BENCHMARK): tests/test_link_benchmark.c \
 source/src/link_benchmark.c source/src/link_benchmark.h \
 source/src/link_stream.c source/src/link_stream.h source/src/link_protocol.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_link_benchmark.c source/src/link_benchmark.c \
	 source/src/link_stream.c -o $@

$(HOST_TEST_LINK_SESSION): tests/test_link_session.c \
 tests/fake_link_transport.c tests/fake_link_transport.h \
 source/src/link_session.c source/src/link_session.h source/src/link_transport.h \
 source/src/link_protocol.c source/src/link_protocol.h \
 source/src/link_benchmark.c source/src/link_benchmark.h \
 source/src/link_stream.c source/src/link_stream.h \
 source/src/link_journal.c source/src/link_journal.h \
 source/src/link_recovery_coordinator.c source/src/link_recovery_coordinator.h \
 source/src/gba_sio.c source/src/gba_sio.h \
 source/src/link_clock.c source/src/link_clock.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 -Itests tests/test_link_session.c tests/fake_link_transport.c \
	 source/src/link_session.c source/src/link_protocol.c source/src/gba_sio.c \
	 source/src/link_stream.c source/src/link_benchmark.c \
	 source/src/link_clock.c source/src/link_journal.c \
	 source/src/link_recovery_coordinator.c \
	 -lz -o $(HOST_TEST_LINK_SESSION)

$(HOST_TEST_LINK_SESSION_JOURNAL): tests/test_link_session_journal.c \
 tests/fake_link_transport.c tests/fake_link_transport.h \
 source/src/link_session.c source/src/link_session.h source/src/link_transport.h \
 source/src/link_protocol.c source/src/link_protocol.h \
 source/src/link_benchmark.c source/src/link_benchmark.h \
 source/src/link_stream.c source/src/link_stream.h \
 source/src/link_journal.c source/src/link_journal.h \
 source/src/link_recovery_coordinator.c source/src/link_recovery_coordinator.h \
 source/src/gba_sio.c source/src/gba_sio.h \
 source/src/link_clock.c source/src/link_clock.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 -Itests tests/test_link_session_journal.c tests/fake_link_transport.c \
	 source/src/link_session.c source/src/link_protocol.c source/src/gba_sio.c \
	 source/src/link_stream.c source/src/link_benchmark.c \
	 source/src/link_clock.c source/src/link_journal.c \
	 source/src/link_recovery_coordinator.c -lz -o $@

$(HOST_TEST_LINK_PIPELINE): tests/test_link_pipeline.c \
 source/src/adhoc_datagram.c source/src/adhoc_datagram.h \
 source/src/link_tx_queue.c source/src/link_tx_queue.h \
 source/src/link_session.c source/src/link_session.h source/src/link_transport.h \
 source/src/link_protocol.c source/src/link_protocol.h \
 source/src/link_benchmark.c source/src/link_benchmark.h \
 source/src/link_stream.c source/src/link_stream.h \
 source/src/link_journal.c source/src/link_journal.h \
 source/src/link_recovery_coordinator.c source/src/link_recovery_coordinator.h \
 source/src/gba_sio.c source/src/gba_sio.h \
 source/src/link_clock.c source/src/link_clock.h source/src/zlib/zlib.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 tests/test_link_pipeline.c source/src/adhoc_datagram.c \
	 source/src/link_tx_queue.c source/src/link_session.c \
	 source/src/link_protocol.c source/src/gba_sio.c source/src/link_clock.c \
	 source/src/link_stream.c source/src/link_benchmark.c \
	 source/src/link_journal.c source/src/link_recovery_coordinator.c \
	 -lz -o $@

$(HOST_TEST_LINK_CABLE_RUNTIME): $(LINK_CABLE_RUNTIME_SOURCES) $(LINK_CABLE_RUNTIME_HEADERS)
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 -Itests $(LINK_CABLE_RUNTIME_SOURCES) -lz -o $@

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

$(HOST_TEST_LIFECYCLE_TEARDOWN): tests/test_lifecycle_teardown.c \
 source/src/lifecycle_teardown.c source/src/lifecycle_teardown.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_lifecycle_teardown.c source/src/lifecycle_teardown.c -o $@

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

$(HOST_TEST_LINK_TX_QUEUE): tests/test_link_tx_queue.c \
 source/src/link_tx_queue.c source/src/link_tx_queue.h source/src/link_protocol.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_link_tx_queue.c source/src/link_tx_queue.c -o $@

$(HOST_TEST_LINK_CLOCK): tests/test_link_clock.c source/src/link_clock.c \
 source/src/link_clock.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_link_clock.c source/src/link_clock.c -o $@

$(HOST_TEST_LINK_JOURNAL): tests/test_link_journal_transitions.c \
 source/src/link_journal_transitions.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_link_journal_transitions.c -o $@

$(HOST_TEST_LINK_JOURNAL_STATE): tests/test_link_journal.c \
 source/src/link_journal.c source/src/link_journal.h \
 source/src/link_journal_transitions.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_link_journal.c source/src/link_journal.c -o $@

$(HOST_TEST_LINK_RECOVERY): tests/test_link_recovery_coordinator.c \
 source/src/link_recovery_coordinator.c source/src/link_recovery_coordinator.h \
 source/src/link_journal.c source/src/link_journal.h \
 source/src/link_journal_transitions.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -Isource/src \
	 tests/test_link_recovery_coordinator.c \
	 source/src/link_recovery_coordinator.c source/src/link_journal.c -o $@

$(HOST_TEST_LINK_SPSC): tests/test_link_spsc_ring.c \
 source/src/link_spsc_ring.c source/src/link_spsc_ring.h \
 source/src/link_protocol.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -pthread -Isource/src \
	 tests/test_link_spsc_ring.c source/src/link_spsc_ring.c -o $@

$(HOST_TEST_LINK_LANES): tests/test_link_transport_lanes.c \
 source/src/link_transport_lanes.c source/src/link_transport_lanes.h \
 source/src/link_spsc_ring.h source/src/link_protocol.h source/src/zlib/zlib.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 tests/test_link_transport_lanes.c source/src/link_transport_lanes.c \
	 -lz -o $@

$(HOST_TEST_LINK_SCHEDULER): tests/test_link_transport_scheduler.c \
 source/src/link_transport_scheduler.c source/src/link_transport_scheduler.h \
 source/src/link_transport_lanes.c source/src/link_transport_lanes.h \
 source/src/link_spsc_ring.c source/src/link_spsc_ring.h \
 source/src/link_protocol.h source/src/zlib/zlib.h
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DZLIB_CONST -Isource/src \
	 -pthread \
	 tests/test_link_transport_scheduler.c \
	 source/src/link_transport_scheduler.c source/src/link_transport_lanes.c \
	 source/src/link_spsc_ring.c -lz -o $@

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

$(HOST_SANITIZE_DIR)/test_adhoc_datagram: tests/test_adhoc_datagram.c \
 source/src/adhoc_datagram.c source/src/adhoc_datagram.h source/src/zlib/zlib.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src tests/test_adhoc_datagram.c \
	 source/src/adhoc_datagram.c -lz -o $@

$(HOST_SANITIZE_DIR)/test_adhoc_recovery: tests/test_adhoc_recovery.c \
 source/src/adhoc_recovery.c source/src/adhoc_recovery.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_adhoc_recovery.c \
	 source/src/adhoc_recovery.c -o $@

$(HOST_SANITIZE_DIR)/test_adhoc_rendezvous: \
 tests/test_adhoc_rendezvous.c source/src/adhoc_rendezvous.c \
 source/src/adhoc_rendezvous.h \
 source/src/zlib/zlib.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src tests/test_adhoc_rendezvous.c \
	 source/src/adhoc_rendezvous.c -lz -o $@

$(HOST_SANITIZE_DIR)/test_adhoc_resync: tests/test_adhoc_resync.c \
 source/src/adhoc_resync.c source/src/adhoc_resync.h \
 source/src/adhoc_rendezvous.h source/src/adhoc_datagram.c \
 source/src/adhoc_datagram.h source/src/zlib/zlib.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src tests/test_adhoc_resync.c \
	 source/src/adhoc_resync.c source/src/adhoc_datagram.c -lz -o $@

$(HOST_SANITIZE_DIR)/test_link_protocol: tests/test_link_protocol.c \
 source/src/link_protocol.c source/src/link_protocol.h source/src/zlib/zlib.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src tests/test_link_protocol.c \
	 source/src/link_protocol.c -lz -o $@

$(HOST_SANITIZE_DIR)/test_link_stream: tests/test_link_stream.c \
 source/src/link_stream.c source/src/link_stream.h source/src/link_protocol.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_link_stream.c source/src/link_stream.c -o $@

$(HOST_SANITIZE_DIR)/test_link_benchmark: tests/test_link_benchmark.c \
 source/src/link_benchmark.c source/src/link_benchmark.h \
 source/src/link_stream.c source/src/link_stream.h source/src/link_protocol.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_link_benchmark.c source/src/link_benchmark.c \
	 source/src/link_stream.c -o $@

$(HOST_SANITIZE_DIR)/test_link_session: tests/test_link_session.c \
 tests/fake_link_transport.c tests/fake_link_transport.h \
 source/src/link_session.c source/src/link_session.h source/src/link_transport.h \
 source/src/link_protocol.c source/src/link_protocol.h \
 source/src/link_benchmark.c source/src/link_benchmark.h \
 source/src/link_stream.c source/src/link_stream.h \
 source/src/link_journal.c source/src/link_journal.h \
 source/src/link_recovery_coordinator.c source/src/link_recovery_coordinator.h \
 source/src/gba_sio.c source/src/gba_sio.h \
 source/src/link_clock.c source/src/link_clock.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src -Itests tests/test_link_session.c \
	 tests/fake_link_transport.c source/src/link_session.c \
	 source/src/link_protocol.c source/src/gba_sio.c source/src/link_clock.c \
	 source/src/link_stream.c source/src/link_benchmark.c \
	 source/src/link_journal.c source/src/link_recovery_coordinator.c \
	 -lz -o $@

$(HOST_SANITIZE_DIR)/test_link_session_journal: \
 tests/test_link_session_journal.c tests/fake_link_transport.c \
 tests/fake_link_transport.h source/src/link_session.c \
 source/src/link_session.h source/src/link_transport.h \
 source/src/link_protocol.c source/src/link_protocol.h \
 source/src/link_benchmark.c source/src/link_benchmark.h \
 source/src/link_stream.c source/src/link_stream.h \
 source/src/link_journal.c source/src/link_journal.h \
 source/src/link_recovery_coordinator.c source/src/link_recovery_coordinator.h \
 source/src/gba_sio.c source/src/gba_sio.h \
 source/src/link_clock.c source/src/link_clock.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src -Itests tests/test_link_session_journal.c \
	 tests/fake_link_transport.c source/src/link_session.c \
	 source/src/link_protocol.c source/src/gba_sio.c source/src/link_clock.c \
	 source/src/link_stream.c source/src/link_benchmark.c \
	 source/src/link_journal.c source/src/link_recovery_coordinator.c \
	 -lz -o $@

$(HOST_SANITIZE_DIR)/test_link_pipeline: tests/test_link_pipeline.c \
 source/src/adhoc_datagram.c source/src/adhoc_datagram.h \
 source/src/link_tx_queue.c source/src/link_tx_queue.h \
 source/src/link_session.c source/src/link_session.h source/src/link_transport.h \
 source/src/link_protocol.c source/src/link_protocol.h \
 source/src/link_benchmark.c source/src/link_benchmark.h \
 source/src/link_stream.c source/src/link_stream.h \
 source/src/link_journal.c source/src/link_journal.h \
 source/src/link_recovery_coordinator.c source/src/link_recovery_coordinator.h \
 source/src/gba_sio.c source/src/gba_sio.h \
 source/src/link_clock.c source/src/link_clock.h source/src/zlib/zlib.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src tests/test_link_pipeline.c \
	 source/src/adhoc_datagram.c source/src/link_tx_queue.c \
	 source/src/link_session.c source/src/link_protocol.c \
	 source/src/gba_sio.c source/src/link_clock.c source/src/link_journal.c \
	 source/src/link_stream.c source/src/link_benchmark.c \
	 source/src/link_recovery_coordinator.c -lz -o $@

$(HOST_SANITIZE_DIR)/test_link_cable_runtime: $(LINK_CABLE_RUNTIME_SOURCES) $(LINK_CABLE_RUNTIME_HEADERS)
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src -Itests $(LINK_CABLE_RUNTIME_SOURCES) -lz -o $@

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

$(HOST_SANITIZE_DIR)/test_lifecycle_teardown: \
 tests/test_lifecycle_teardown.c source/src/lifecycle_teardown.c \
 source/src/lifecycle_teardown.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_lifecycle_teardown.c \
	 source/src/lifecycle_teardown.c -o $@

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

$(HOST_SANITIZE_DIR)/test_link_tx_queue: tests/test_link_tx_queue.c \
 source/src/link_tx_queue.c source/src/link_tx_queue.h source/src/link_protocol.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_link_tx_queue.c source/src/link_tx_queue.c -o $@

$(HOST_SANITIZE_DIR)/test_link_clock: tests/test_link_clock.c \
 source/src/link_clock.c source/src/link_clock.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_link_clock.c source/src/link_clock.c -o $@

$(HOST_SANITIZE_DIR)/test_link_journal_transitions: \
 tests/test_link_journal_transitions.c source/src/link_journal_transitions.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_link_journal_transitions.c -o $@

$(HOST_SANITIZE_DIR)/test_link_journal: tests/test_link_journal.c \
 source/src/link_journal.c source/src/link_journal.h \
 source/src/link_journal_transitions.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_link_journal.c source/src/link_journal.c -o $@

$(HOST_SANITIZE_DIR)/test_link_recovery_coordinator: \
 tests/test_link_recovery_coordinator.c source/src/link_recovery_coordinator.c \
 source/src/link_recovery_coordinator.h source/src/link_journal.c \
 source/src/link_journal.h source/src/link_journal_transitions.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -Isource/src tests/test_link_recovery_coordinator.c \
	 source/src/link_recovery_coordinator.c source/src/link_journal.c -o $@

$(HOST_SANITIZE_DIR)/test_link_spsc_ring: tests/test_link_spsc_ring.c \
 source/src/link_spsc_ring.c source/src/link_spsc_ring.h \
 source/src/link_protocol.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -pthread -Isource/src tests/test_link_spsc_ring.c \
	 source/src/link_spsc_ring.c -o $@

$(HOST_SANITIZE_DIR)/test_link_transport_lanes: \
 tests/test_link_transport_lanes.c source/src/link_transport_lanes.c \
 source/src/link_transport_lanes.h source/src/link_spsc_ring.h \
 source/src/link_protocol.h source/src/zlib/zlib.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -DZLIB_CONST -Isource/src tests/test_link_transport_lanes.c \
	 source/src/link_transport_lanes.c -lz -o $@

$(HOST_SANITIZE_DIR)/test_link_transport_scheduler: \
 tests/test_link_transport_scheduler.c source/src/link_transport_scheduler.c \
 source/src/link_transport_scheduler.h source/src/link_transport_lanes.c \
 source/src/link_transport_lanes.h source/src/link_spsc_ring.c \
 source/src/link_spsc_ring.h source/src/link_protocol.h source/src/zlib/zlib.h
	mkdir -p $(HOST_SANITIZE_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror $(HOST_SANITIZE_FLAGS) \
	 -pthread -DZLIB_CONST -Isource/src tests/test_link_transport_scheduler.c \
	 source/src/link_transport_scheduler.c source/src/link_transport_lanes.c \
	 source/src/link_spsc_ring.c -lz -o $@

check-link-model:
	sh scripts/check-link-model.sh

check-cbmc:
	sh scripts/check-cbmc.sh

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
	 (echo "Set VERSION, for example VERSION=1.4.0" >&2; exit 1)
	sh scripts/package-release.sh "$(VERSION)"
	sh scripts/check-package.sh "$(VERSION)"

artifact-smoke:
	@test -n "$(VERSION)" || \
	 (echo "Set VERSION, for example VERSION=1.4.0" >&2; exit 1)
	sh scripts/check-package.sh "$(VERSION)"

clean:
	$(MAKE) -C source clean
	$(MAKE) -C source/single-game_launcher clean
	$(RM) source/single-game_launcher/tool/assets/EBOOT.PBP
	$(RM) $(HOST_TESTS)
	$(RM) $(HOST_SANITIZE_TESTS)
	-rmdir $(HOST_BUILD_DIR) $(HOST_SANITIZE_DIR) build 2>/dev/null
