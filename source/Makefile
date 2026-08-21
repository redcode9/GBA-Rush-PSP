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
 common.h codebreaker.h gba_sio.h link_protocol.h link_session.h link_transport.h power_lifecycle.h video_adaptive.h perf_metrics.h perf_monitor.h perf_report.h menu_model.h menu_catalog.h config_model.h config_store.h psp_adhoc.h \
 cpu.h memory.h video.h psp_video.h input.h sound.h main.h message.h mh_print.h gui.h zip.h png.h \
 ku_bridge.h exception.h ch_print.h

MESSAGE_FILE = \
 src/text/message_jpn.h src/text/message_eng.h src/text/message_chs.h \
 src/text/message_cht.h src/text/message_ita.h

MAINOBJS = \
 cpu.o memory.o video_dispatch_state.o video_dispatch.o video_cc_wrap.o video_old_wrap.o psp_video.o input.o sound.o main.o message.o mh_print.o gui.o zip.o png.o \
 mips_stub.o video_blend_mips.o \
 ku_bridge.o exception.o cheats.o codebreaker.o ch_print.o \
 boxart.o gba_sio.o link_protocol.o link_session.o power_lifecycle.o video_adaptive.o perf_metrics.o perf_monitor.o perf_report.o menu_model.o menu_catalog.o config_model.o config_store.o psp_adhoc.o

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

INCDIR = SDK/include src src/zlib src/libpower
LIBDIR = SDK/lib

LIBS = src/libpower/libpsppower.a -lpspgu -lpsprtc -lpspaudio -lpspkubridge \
 -lpsputility -lpspnet -lpspnet_adhoc -lpspnet_adhocctl -lpspwlan

include ${PSPSDK}/lib/build.mak

LDFLAGS += -flto

all: $(EXTRA_TARGETS)
	@$(MAKE) -C prx/exceptionHandler/prx
	@cp prx/exceptionHandler/prx/exception.prx .
