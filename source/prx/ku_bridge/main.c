/******************************************************************************

  ku_bridge.prx

  Kernel-mode helper that lets the user-mode emulator reach kernel-only
  APIs: sceImpose (system volume state) and sceCtrl_driver (pad sampling
  and reads). The emulator links the import stub source/src/ku_bridge.S,
  generated from ku_bridge.exp by this folder's Makefile.

  Not to be confused with the CFW's KUBridge (kuKernelLoadModule), which
  the emulator links through -lpspkubridge. The top-level build does not
  build, package or load this module.

******************************************************************************/

#include <pspsdk.h>
#include <pspimpose_driver.h>
#include <pspctrl.h>


PSP_MODULE_INFO("ku_bridge", PSP_MODULE_KERNEL, 1, 1);
PSP_MAIN_THREAD_ATTR(0);


/******************************************************************************
  prototypes
******************************************************************************/

/* 3.71+ NIDs of the sceCtrl_driver calls, imported in sceCtrl_driver.S */
int sceCtrlSetSamplingCycle371(int cycle);
int sceCtrlSetSamplingMode371(int mode);

int sceCtrlReadBufferPositive371(SceCtrlData *pad_data, int count);
int sceCtrlPeekBufferPositive371(SceCtrlData *pad_data, int count);


/******************************************************************************
  local variables
******************************************************************************/

/* sceCtrl_driver entry points for the running firmware; set by
   init_ku_bridge(), NULL (wrappers return -1) until then. */
static int (*__sceCtrlPeekBufferPositive)(SceCtrlData *pad_data, int count);
static int (*__sceCtrlReadBufferPositive)(SceCtrlData *pad_data, int count);

static int (*__sceCtrlSetSamplingCycle)(int cycle);
static int (*__sceCtrlSetSamplingMode)(int mode);

/******************************************************************************
  functions
******************************************************************************/

/* Each wrapper clears $k1 around the kernel call: $k1 carries the caller's
   privilege, and with it cleared the kernel accepts the call and the user
   pointers as if they came from kernel mode. */
int kuImposeGetParam(SceImposeParam param)
{
  int k1 = pspSdkSetK1(0);
  int ret = sceImposeGetParam(param);
  pspSdkSetK1(k1);
  return ret;
}

/* Like kuImposeGetParam(), for setting a value. */
int kuImposeSetParam(SceImposeParam param, int value)
{
  int k1 = pspSdkSetK1(0);
  int ret = sceImposeSetParam(param, value);
  pspSdkSetK1(k1);
  return ret;
}


/* Calls the firmware's sceCtrlSetSamplingCycle; -1 before init_ku_bridge(). */
int kuCtrlSetSamplingCycle(int cycle)
{
  if (__sceCtrlSetSamplingCycle)
  {
    int k1 = pspSdkSetK1(0);
    int ret = (*__sceCtrlSetSamplingCycle)(cycle);
    pspSdkSetK1(k1);
    return ret;
  }

  return -1;
}

/* Like kuCtrlSetSamplingCycle(), for the analog sampling mode. */
int kuCtrlSetSamplingMode(int mode)
{
  if (__sceCtrlSetSamplingMode)
  {
    int k1 = pspSdkSetK1(0);
    int ret = (*__sceCtrlSetSamplingMode)(mode);
    pspSdkSetK1(k1);
    return ret;
  }

  return -1;
}


/* Copies the latest pad samples without waiting; -1 before init_ku_bridge(). */
int kuCtrlPeekBufferPositive(SceCtrlData *pad_data, int count)
{
  if (__sceCtrlPeekBufferPositive)
  {
    int k1 = pspSdkSetK1(0);
    int ret = (*__sceCtrlPeekBufferPositive)(pad_data, count);
    pspSdkSetK1(k1);
    return ret;
  }

  return -1;
}

/* Like kuCtrlPeekBufferPositive(), but blocks until the next sample. */
int kuCtrlReadBufferPositive(SceCtrlData *pad_data, int count)
{
  if (__sceCtrlReadBufferPositive)
  {
    int k1 = pspSdkSetK1(0);
    int ret = (*__sceCtrlReadBufferPositive)(pad_data, count);
    pspSdkSetK1(k1);
    return ret;
  }

  return -1;
}


/* sceKernelDevkitVersion() of firmware 3.71, the first whose sceCtrl_driver
   NIDs differ from the older ones */
#define DEVKIT_VERSION_371 0x03070110

/* The caller passes sceKernelDevkitVersion(); picks the sceCtrl_driver NID
   set matching the running firmware. */
void init_ku_bridge(int devkit_version)
{
  if (devkit_version < DEVKIT_VERSION_371)
  {
    __sceCtrlSetSamplingCycle = sceCtrlSetSamplingCycle;
    __sceCtrlSetSamplingMode  = sceCtrlSetSamplingMode;

    __sceCtrlPeekBufferPositive = sceCtrlPeekBufferPositive;
    __sceCtrlReadBufferPositive = sceCtrlReadBufferPositive;
  }
  else
  {
    __sceCtrlSetSamplingCycle = sceCtrlSetSamplingCycle371;
    __sceCtrlSetSamplingMode  = sceCtrlSetSamplingMode371;

    __sceCtrlPeekBufferPositive = sceCtrlPeekBufferPositive371;
    __sceCtrlReadBufferPositive = sceCtrlReadBufferPositive371;
  }
}


/* Leaves every kuCtrl* wrapper returning -1 until the emulator calls
   init_ku_bridge(). */
int module_start(SceSize args, void *argp)
{
  __sceCtrlSetSamplingCycle = NULL;
  __sceCtrlSetSamplingMode  = NULL;

  __sceCtrlPeekBufferPositive = NULL;
  __sceCtrlReadBufferPositive = NULL;

  return 0;
}

/* Nothing to undo on unload. */
int module_stop(void)
{
  return 0;
}


