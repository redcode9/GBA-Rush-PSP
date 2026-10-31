/* Kernel imports used by ku_bridge.prx: each sceCtrl_driver call under its
   3.71+ NID (the *371 names) and its older NID; init_ku_bridge() picks the
   set for the running firmware. */
  .set noreorder

# include "pspimport.s"

  IMPORT_START "sceCtrl_driver", 0x00010000

  IMPORT_FUNC  "sceCtrl_driver", 0x6D74BF08, sceCtrlSetSamplingCycle371
  IMPORT_FUNC  "sceCtrl_driver", 0x6A2774F3, sceCtrlSetSamplingCycle

  IMPORT_FUNC  "sceCtrl_driver", 0x28E71A16, sceCtrlSetSamplingMode371
  IMPORT_FUNC  "sceCtrl_driver", 0x1F4011E6, sceCtrlSetSamplingMode

  IMPORT_FUNC  "sceCtrl_driver", 0x454455AC, sceCtrlReadBufferPositive371
  IMPORT_FUNC  "sceCtrl_driver", 0x1F803938, sceCtrlReadBufferPositive

  IMPORT_FUNC  "sceCtrl_driver", 0xC4AAD55F, sceCtrlPeekBufferPositive371
  /* Misspelled and unused: main.c's pre-3.71 sceCtrlPeekBufferPositive
     comes from the SDK's sceCtrl_driver stub library instead. */
  IMPORT_FUNC  "sceCtrl_driver", 0x3A622550, sceCtrlReekBufferPositive

