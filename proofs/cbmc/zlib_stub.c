#include "zlib/zlib.h"

uLong ZEXPORT crc32(uLong crc, const Bytef *buffer, uInt length)
{
  uint32_t value = (uint32_t)crc ^ ((uint32_t)length * 0x9E3779B9u);

  /* A bounded deterministic abstraction is sufficient for the protocol
   * proofs: equal byte ranges must produce equal results, while different
   * endpoints or lengths remain distinguishable without a payload loop. */
  if(buffer != Z_NULL && length != 0u)
  {
    value ^= buffer[0];
    value ^= (uint32_t)buffer[length - 1u] << 8;
  }
  return (uLong)value;
}
