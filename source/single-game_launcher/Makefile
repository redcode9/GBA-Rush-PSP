# Builds the single-game launcher stub EBOOT.PBP. `make launcher-stub` at the
# repository root runs this and copies the result to tool/assets/, where the
# desktop builder picks it up as the template for every game package.
PSPSDK = ${shell psp-config --pspsdk-path}

BUILD_PRX = 1

PSP_FW_VERSION = 371
PSP_LARGE_MEMORY = 1

PSP_EBOOT_TITLE = GBA Rush PSP Single-Game Launcher

EXTRA_TARGETS = EBOOT.PBP

OBJS = main.o

TARGET = GBA_Rush_PSP_Single-Game_Launcher

# SystemCtrl user stubs (sctrlHENGetVersion, CFW launch path in main.c)
LIBS = -lpspsystemctrl_user

include ${PSPSDK}/lib/build.mak
