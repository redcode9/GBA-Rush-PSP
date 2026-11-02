/* Crash screen. exception.prx, a kernel module beside the EBOOT, installs
   the default exception handler and calls ExceptionHandler with the
   faulting registers, which shows them and can dump them to a log. */

#include "common.h"

/* Filled by exception.prx before it calls ExceptionHandler. */
PspDebugRegBlock exception_regs;

extern SceModuleInfo module_info;
extern int _ftext;

/* Names of the MIPS exception codes (Cause bits 2-6). */
static const char *codeTxt[32] =
{
    "Interrupt", "TLB modification", "TLB load/inst fetch", "TLB store",
    "Address load/inst fetch", "Address store", "Bus error (instr)",
    "Bus error (data)", "Syscall", "Breakpoint", "Reserved instruction",
    "Coprocessor unusable", "Arithmetic overflow", "Unknown 14",
    "Unknown 15", "Unknown 16", "Unknown 17", "Unknown 18", "Unknown 19",
    "Unknown 20", "Unknown 21", "Unknown 22", "Unknown 23", "Unknown 24",
    "Unknown 25", "Unknown 26", "Unknown 27", "Unknown 28", "Unknown 29",
    "Unknown 31"
};

static const unsigned char regName[32][5] =
{
    "zr", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

void ExceptionHandler(PspDebugRegBlock *regs)
{
    int i;
    SceCtrlData pad;

    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0x00FF0000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenClear();

    pspDebugScreenPrintf("Your PSP has just crashed!\n\n");

    pspDebugScreenPrintf("Exception details:\n\n");
    pspDebugScreenPrintf("Exception - %s\n", codeTxt[(regs->cause >> 2) & 31]);
    // EPC also as an offset into .text, for lookup in the linker map
    pspDebugScreenPrintf("EPC       - %08X / %s.text + %08X\n", (int)regs->epc, module_info.modname, (unsigned int)(regs->epc-(int)&_ftext));
    pspDebugScreenPrintf("Cause     - %08X\n", (int)regs->cause);
    pspDebugScreenPrintf("Status    - %08X\n", (int)regs->status);
    pspDebugScreenPrintf("BadVAddr  - %08X\n\n", (int)regs->badvaddr);
    for (i=0; i<32; i+=4) pspDebugScreenPrintf("%s:%08X %s:%08X %s:%08X %s:%08X\n", regName[i], (int)regs->r[i], regName[i+1], (int)regs->r[i+1], regName[i+2], (int)regs->r[i+2], regName[i+3], (int)regs->r[i+3]);

    sceKernelDelayThread(1000000);
    pspDebugScreenPrintf("\n\nPress X to dump information on file tempgba_crash.log and quit");
    pspDebugScreenPrintf("\nPress O to quit");

    for (;;) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CROSS) {
            char filename[MAX_PATH];
            snprintf(filename, sizeof(filename), "%stempgba_crash.log", main_path);
            FILE *log = fopen(filename, "w");
            if (log != NULL) {
                char crash_line[512];
                sprintf(crash_line, "Exception details:\n\n");
                fwrite(crash_line, 1, strlen(crash_line), log);
                sprintf(crash_line, "Exception - %s\n", codeTxt[(regs->cause >> 2) & 31]);
                fwrite(crash_line, 1, strlen(crash_line), log);
                sprintf(crash_line, "EPC       - %08X / %s.text + %08X\n", (int)regs->epc, module_info.modname, (unsigned int)(regs->epc-(int)&_ftext));
                fwrite(crash_line, 1, strlen(crash_line), log);
                sprintf(crash_line, "Cause     - %08X\n", (int)regs->cause);
                fwrite(crash_line, 1, strlen(crash_line), log);
                sprintf(crash_line, "Status    - %08X\n", (int)regs->status);
                fwrite(crash_line, 1, strlen(crash_line), log);
                sprintf(crash_line, "BadVAddr  - %08X\n\n", (int)regs->badvaddr);
                fwrite(crash_line, 1, strlen(crash_line), log);
                for (i=0; i<32; i+=4) {
                    sprintf(crash_line, "%s:%08X %s:%08X %s:%08X %s:%08X\n", regName[i], (int)regs->r[i], regName[i+1], (int)regs->r[i+1], regName[i+2], (int)regs->r[i+2], regName[i+3], (int)regs->r[i+3]);
                    fwrite(crash_line, 1, strlen(crash_line), log);
                }
                fclose(log);
            }
            break;
        } else if (pad.Buttons & PSP_CTRL_CIRCLE) {
            break;
        }
        sceKernelDelayThread(100000);
    }
    scePowerUnlock(0);
    sceKernelExitGame();
}

/* Loads exception.prx into kernel memory and passes it the handler and the
   register block. If loading fails, crashes get no crash screen. */
void initExceptionHandler(void)
{
   SceKernelLMOption option = { 0 };
   int args[2], start_status, modid;
   char prx_path[MAX_PATH];

   option.size = sizeof(option);
   option.mpidtext = PSP_MEMORY_PARTITION_KERNEL;
   option.mpiddata = PSP_MEMORY_PARTITION_KERNEL;
   option.position = 0;
   option.access = 1;

   snprintf(prx_path, sizeof(prx_path), "%sexception.prx", main_path);

   if ((modid = kuKernelLoadModule(prx_path, 0, &option)) >= 0)
   {
      args[0] = (int)ExceptionHandler;
      args[1] = (int)&exception_regs;
      sceKernelStartModule(modid, 8, args, &start_status, NULL);
   }
}
