/******************************************************************************

  ku_bridge.prx

******************************************************************************/

/*
 * Imports from ku_bridge.prx (source/prx/ku_bridge), a kernel module that
 * calls kernel-only sceImpose/sceCtrl functions on behalf of the user-mode
 * emulator. The import stubs are in ku_bridge.S.
 */

#ifndef KU_BRIDGE_PRX_H
#define KU_BRIDGE_PRX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Picks the pre-3.71 or 3.71+ sceCtrl entry points for this firmware. */
void init_ku_bridge(int devkit_version);

int kuImposeGetParam(SceImposeParam param);
int kuImposeSetParam(SceImposeParam param, int value);

int kuCtrlSetSamplingCycle(int cycle);
int kuCtrlSetSamplingMode(int mode);

int kuCtrlPeekBufferPositive(SceCtrlData *pad_data, int count);
int kuCtrlReadBufferPositive(SceCtrlData *pad_data, int count);

#ifdef __cplusplus
}
#endif

#endif /* KU_BRIDGE_PRX_H */
