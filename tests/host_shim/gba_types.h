/* Host shim: GBA integer typedefs normally provided by PSP SDK psptypes.h.
 * Included by host test TUs before the project headers (cpu.h, memory.h)
 * so the test TU compiles without PSP SDK headers. */
#ifndef GBA_TYPES_H
#define GBA_TYPES_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/* PSP kernel handle type (see PSP SDK pspkernel.h). */
typedef unsigned int SceUID;

/* Same value as MAX_FILE in common.h. */
#define MAX_FILE 256

#endif
