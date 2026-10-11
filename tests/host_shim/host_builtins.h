// Host test shim: portable stand-in for the Allegrex __builtin_allegrex_rotr builtin,
// used only when compiling memory.c on the host for tests.
#ifndef __builtin_allegrex_rotr
#include <stdint.h>
static inline uint32_t host_allegrex_rotr(uint32_t value, unsigned int shift)
{
  shift &= 31;
  return (value >> shift) | (value << ((32 - shift) & 31));
}
#define __builtin_allegrex_rotr(value, shift) \
  host_allegrex_rotr(value, shift)
#endif
