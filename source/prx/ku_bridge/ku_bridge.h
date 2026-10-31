/******************************************************************************

  ku_bridge.prx

  User-side prototypes for the ku_bridge.prx exports. A copy lives in
  source/src/ku_bridge.h next to the generated import stub.

******************************************************************************/

#ifndef KU_BRIDGE_PRX_H
#define KU_BRIDGE_PRX_H

#ifdef __cplusplus
extern "C" {
#endif

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
