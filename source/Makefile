# -x assembler-with-cpp
# gpSP makefile
# Gilead Kutnick - Exophase


# Global definitions

# PSP build of the emulator (make psp-build from the repository root): the
# PSPSDK build.mak rules turn TARGET into EBOOT.PBP, and "all" also builds
# the exception handler and Media Engine PRXs that ship next to it.

.DEFAULT_GOAL := all

PSPSDK = ${shell psp-config --pspsdk-path}
PREFIX = ${shell psp-config --psp-prefix}

# A relocatable PRX EBOOT, encrypted so it also starts on official firmware.
BUILD_PRX = 1
ENCRYPT = 1

PSP_FW_VERSION = 371
PSP_LARGE_MEMORY = 1

PSP_EBOOT_ICON = res/ICON0.png
PSP_EBOOT_PIC1 = res/PIC1.png
PSP_EBOOT_TITLE = GBA Rush PSP

EXTRA_TARGETS = EBOOT.PBP

# Headers that every main object is rebuilt on when they change.
COMMON_FILE = \
 common.h adhoc_peer.h codebreaker.h dynarec_worklist.h fast_forward_audio.h fast_forward_policy.h gba_sio.h gba_ppu.h gba_ppu_log.h video_me.h link_audio_wait.h link_cable.h link_net.h power_lifecycle.h runtime_log.h runtime_log_psp.h runtime_log_store.h sound_math.h video_adaptive.h video_mosaic.h perf_metrics.h perf_monitor.h perf_report.h menu_model.h menu_layout.h menu_catalog.h config_model.h config_store.h save_format.h save_store.h psp_adhoc.h \
 cpu.h memory.h video.h psp_video.h input.h sound.h main.h message.h mh_print.h gui.h zip.h png.h \
 ku_bridge.h exception.h ch_print.h \
 arm_core.h arm_ir.h arm_mips.h arm_gba.h

MESSAGE_FILE = \
 src/text/message_jpn.h src/text/message_eng.h src/text/message_chs.h \
 src/text/message_cht.h src/text/message_ita.h

MAINOBJS = \
 cpu.o memory.o gba_ppu.o gba_ppu_log.o video_me.o video_dispatch_state.o video_dispatch.o video_cc_wrap.o video_old_wrap.o psp_video.o input.o sound.o main.o message.o mh_print.o gui.o zip.o png.o \
 mips_stub.o video_blend_mips.o \
 arm_decode.o arm_interp.o arm_mips.o arm_gba.o \
 ku_bridge.o exception.o cheats.o codebreaker.o ch_print.o \
 adhoc_peer.o boxart.o dynarec_worklist.o fast_forward_audio.o fast_forward_policy.o gba_sio.o link_audio_wait.o link_cable.o link_net.o power_lifecycle.o runtime_log.o runtime_log_psp.o runtime_log_store.o sound_math.o video_adaptive.o perf_metrics.o perf_monitor.o perf_report.o menu_model.o menu_layout.o menu_catalog.o config_model.o config_store.o save_format.o save_store.o psp_adhoc.o

# make DIAGNOSTIC_INPUT_TRACE=1 adds the diagnostic input recorder.
ifneq ($(DIAGNOSTIC_INPUT_TRACE),)
COMMON_FILE += input_trace.h
MAINOBJS += input_trace.o
endif

# make DIAGNOSTIC_CORE_PROFILE=1 samples where the new core spends its time
# during a benchmark capture and writes it beside the report (arm_gba.c).
ifneq ($(DIAGNOSTIC_CORE_PROFILE),)
COMMON_FILE += core_profile.h
MAINOBJS += core_profile.o
endif

# The bundled zlib in src/zlib (VPATH finds its sources).
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

# Taking the address of a nested function that uses its parent's variables
# makes GCC write a trampoline to the stack; two such menu callbacks crashed
# the PSP with a reserved instruction. Fail the build instead.
CFLAGS += -Werror=trampolines

# The new core's translation cache, halved from the default so the core
# fits the Classic core's 2 MB writable code cache it borrows (arm_gba.c);
# Mario Kart and FireRed flush it no more often.
CFLAGS += -DARM_CACHE_OPS=0x20000u

ifneq ($(DIAGNOSTIC_INPUT_TRACE),)
CFLAGS += -DENABLE_DIAGNOSTIC_INPUT_TRACE
endif
ifneq ($(DIAGNOSTIC_CORE_PROFILE),)
CFLAGS += -DENABLE_DIAGNOSTIC_CORE_PROFILE
endif

CXXFLAGS =

ASFLAGS = ${CFLAGS}

# Stricter C-only warnings, so the C++ and assembly objects are left out;
# zlib's old-style definitions are tolerated.
C_ONLY_OBJS = $(filter-out video_cc_wrap.o mips_stub.o video_blend_mips.o ku_bridge.o,$(OBJS))
$(C_ONLY_OBJS): CFLAGS += -Wbad-function-cast -Wmissing-prototypes
$(OBJZ) $(OBJG): CFLAGS += -Wno-old-style-definition
$(MAINOBJS): $(COMMON_FILE)
# The translations are written in UTF-8 and stored as GBK, the encoding the
# menu font code reads.
message.o: $(MESSAGE_FILE)
message.o: CFLAGS += -finput-charset=UTF-8 -fexec-charset=GBK
# The two renderers are built from wrappers that rename their symbols and
# #include the real source, so each wrapper depends on that file.
video_cc_wrap.o: src/video_cc.cc
video_old_wrap.o: src/video.c
# memory.c embeds the built-in BIOS from this header.
memory.o: src/open_bios.h

INCDIR = SDK/include src src/zlib src/libpower
LIBDIR = SDK/lib

LIBS = src/libpower/libpsppower.a -lpspgu -lpsprtc -lpspaudio -lpspkubridge \
 -lpsputility -lpspnet -lpspnet_adhoc -lpspnet_adhocctl -lpspwlan

include ${PSPSDK}/lib/build.mak

LDFLAGS += -flto

all: $(EXTRA_TARGETS)
	@$(MAKE) -C prx/exceptionHandler/prx
	@cp prx/exceptionHandler/prx/exception.prx .
	@$(MAKE) -C prx/media_engine
	@cp prx/media_engine/media_engine.prx .
