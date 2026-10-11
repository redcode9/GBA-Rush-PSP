# -x assembler-with-cpp
# gpSP makefile
# Gilead Kutnick - Exophase


# Global definitions

.DEFAULT_GOAL := all

PSPSDK = ${shell psp-config --pspsdk-path}
PREFIX = ${shell psp-config --psp-prefix}

BUILD_PRX = 1
ENCRYPT = 1

PSP_FW_VERSION = 371
PSP_LARGE_MEMORY = 1

PSP_EBOOT_ICON = res/ICON0.png
PSP_EBOOT_PIC1 = res/PIC1.png
PSP_EBOOT_TITLE = GBA Rush PSP

EXTRA_TARGETS = EBOOT.PBP

COMMON_FILE = \
 common.h adhoc_datagram.h adhoc_peer.h adhoc_recovery.h adhoc_rendezvous.h adhoc_resync.h codebreaker.h dynarec_worklist.h fast_forward_audio.h fast_forward_policy.h gba_sio.h lifecycle_teardown.h link_audio_wait.h link_benchmark.h link_clock.h link_journal.h link_journal_transitions.h link_protocol.h link_recovery_coordinator.h link_session.h link_spsc_ring.h link_stream.h link_transport.h link_transport_lanes.h link_transport_scheduler.h power_lifecycle.h runtime_log.h runtime_log_psp.h runtime_log_store.h sound_math.h video_adaptive.h video_mosaic.h perf_metrics.h perf_monitor.h perf_report.h menu_model.h menu_layout.h menu_catalog.h config_model.h config_store.h psp_adhoc.h \
 cpu.h memory.h video.h psp_video.h input.h sound.h main.h message.h mh_print.h gui.h zip.h png.h \
 ku_bridge.h exception.h ch_print.h

MESSAGE_FILE = \
 src/text/message_jpn.h src/text/message_eng.h src/text/message_chs.h \
 src/text/message_cht.h src/text/message_ita.h

MAINOBJS = \
 cpu.o memory.o video_dispatch_state.o video_dispatch.o video_cc_wrap.o video_old_wrap.o psp_video.o input.o sound.o main.o message.o mh_print.o gui.o zip.o png.o \
 mips_stub.o video_blend_mips.o \
 ku_bridge.o exception.o cheats.o codebreaker.o ch_print.o \
 adhoc_datagram.o adhoc_peer.o adhoc_recovery.o adhoc_rendezvous.o adhoc_resync.o boxart.o dynarec_worklist.o fast_forward_audio.o fast_forward_policy.o gba_sio.o lifecycle_teardown.o link_audio_wait.o link_benchmark.o link_clock.o link_journal.o link_protocol.o link_recovery_coordinator.o link_session.o link_spsc_ring.o link_stream.o link_transport_lanes.o link_transport_scheduler.o power_lifecycle.o runtime_log.o runtime_log_psp.o runtime_log_store.o sound_math.o video_adaptive.o perf_metrics.o perf_monitor.o perf_report.o menu_model.o menu_layout.o menu_catalog.o config_model.o config_store.o psp_adhoc.o

ifneq ($(DIAGNOSTIC_INPUT_TRACE),)
COMMON_FILE += input_trace.h
MAINOBJS += input_trace.o
endif

OBJZ = adler32.o crc32.o deflate.o infback.o inffast.o inflate.o inftrees.o trees.o zutil.o
OBJG = compress.o uncompr.o gzclose.o gzlib.o gzread.o gzwrite.o

OBJS = $(MAINOBJS) $(OBJZ) $(OBJG)

TARGET = TempGBA

VPATH = src src/zlib

CFLAGS = \
 -O2 -G0 \
 -march=allegrex -mfp32 -mgp32 -mlong32 -mabi=eabi \
 -fomit-frame-pointer -fstrict-aliasing \
 -falign-functions=32 -falign-loops -falign-labels -falign-jumps \
 -Wall -Wundef -Wpointer-arith -Wwrite-strings -Wsign-compare \
 -DZLIB_CONST -flto

ifneq ($(DIAGNOSTIC_INPUT_TRACE),)
CFLAGS += -DENABLE_DIAGNOSTIC_INPUT_TRACE
endif

CXXFLAGS =

ASFLAGS = ${CFLAGS}

C_ONLY_OBJS = $(filter-out video_cc_wrap.o mips_stub.o video_blend_mips.o ku_bridge.o,$(OBJS))
$(C_ONLY_OBJS): CFLAGS += -Wbad-function-cast -Wmissing-prototypes
$(OBJZ) $(OBJG): CFLAGS += -Wno-old-style-definition
$(MAINOBJS): $(COMMON_FILE)
message.o: $(MESSAGE_FILE)
message.o: CFLAGS += -finput-charset=UTF-8 -fexec-charset=GBK
video_cc_wrap.o: src/video_cc.cc
video_old_wrap.o: src/video.c

INCDIR = SDK/include src src/zlib src/libpower
LIBDIR = SDK/lib

LIBS = src/libpower/libpsppower.a -lpspgu -lpsprtc -lpspaudio -lpspkubridge \
 -lpsputility -lpspnet -lpspnet_adhoc -lpspnet_adhocctl -lpspwlan

include ${PSPSDK}/lib/build.mak

LDFLAGS += -flto

all: $(EXTRA_TARGETS)
	@$(MAKE) -C prx/exceptionHandler/prx
	@cp prx/exceptionHandler/prx/exception.prx .
