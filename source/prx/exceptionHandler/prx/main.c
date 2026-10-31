// exception.prx: kernel module that installs the emulator's crash handler.
// initExceptionHandler() (source/src/exception.c) loads it into kernel memory
// and starts it with two pointers: its C handler and a PspDebugRegBlock. On
// any CPU exception _pspDebugExceptionHandler (exception_asm.S) fills that
// block and jumps into the handler. Built by source/Makefile and shipped
// next to the emulator's EBOOT.PBP.

#include <pspkernel.h>
#include <psppower.h>
#include <pspdebug.h>

// kernel | no stop | single load | single start: never unloaded
PSP_MODULE_INFO("exception", 0x1007, 1, 1);

// Read by exception_asm.S
PspDebugErrorHandler curr_handler;
PspDebugRegBlock *exception_regs;

void _pspDebugExceptionHandler(void);
int sceKernelRegisterDefaultExceptionHandler(void *func);
int sceKernelRegisterDefaultExceptionHandler371(void *func);

// sceKernelDevkitVersion() of firmware 3.71, which uses a different NID for
// sceKernelRegisterDefaultExceptionHandler
#define DEVKIT_VERSION_371 0x03070110

// argp holds two 32-bit pointers: the C handler and the register block
int module_start(SceSize args, void *argp)
{
   if(args != 8) return -1;
   curr_handler = (PspDebugErrorHandler)((int *)argp)[0];
   exception_regs = (PspDebugRegBlock *)((int *)argp)[1];
   if(!curr_handler || !exception_regs) return -1;

   if(sceKernelDevkitVersion() < DEVKIT_VERSION_371)
      return sceKernelRegisterDefaultExceptionHandler((void *)_pspDebugExceptionHandler);
   else
      return sceKernelRegisterDefaultExceptionHandler371((void *)_pspDebugExceptionHandler);
}
