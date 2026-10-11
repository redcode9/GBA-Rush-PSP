/* Host shim: stand-in for PSP SDK psptypes.h in host test TUs.
 * Resolved before the real SDK header via -I tests/host_shim, so a
 * test TU can include project headers that pull in <psptypes.h>
 * (e.g. psp_video.h) without PSP SDK headers. */
#ifndef HOST_PSP_TYPES_H
#define HOST_PSP_TYPES_H

#include "gba_types.h"

#endif
