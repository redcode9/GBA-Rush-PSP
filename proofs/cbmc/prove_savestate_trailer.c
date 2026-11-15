/*
 * Bounded proof for the savestate trailer decoder.
 *
 * The bytes after a savestate's 512 KiB payload come from the Memory
 * Stick, so any of them must be safe to decode. CBMC checks every tail of
 * up to PROOF_TAIL bytes against any file size, payload CRC and running
 * ROM: no read outside the tail_size bytes given, no overflow, one of the
 * six results, LEGACY exactly when the file is 512 KiB, and a usable
 * trailer only from a tail of exactly 48 bytes.
 *
 * zlib's crc32 is not linked in: the stand-in below checks that every byte
 * it is asked for is readable and returns any 32-bit value, so every CRC
 * outcome (and with it the V2 result) is explored.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "savestate_trailer.h"
#include "zlib/zlib.h"

#define PROOF_TAIL 64u

/* Left undefined on purpose: CBMC lets each call return any value. */
uint32_t nondet_u32(void);
size_t nondet_size(void);
uLong nondet_ulong(void);

uLong ZEXPORT crc32(uLong crc, const Bytef *buf, uInt len)
{
  uLong value = nondet_ulong();

  (void)crc;
  if(buf != Z_NULL)
    assert(__CPROVER_r_ok(buf, len));
  __CPROVER_assume(value <= 0xFFFFFFFFul);
  return value;
}

int main(void)
{
  uint8_t storage[PROOF_TAIL]; /* left unset: CBMC tries every value */
  SavestateRomId current;      /* likewise */
  SavestateTrailer out;
  SavestateTrailerResult result;
  size_t tail_size = nondet_size();
  uint32_t file_size = nondet_u32();
  const uint8_t *tail;

  __CPROVER_assume(tail_size <= PROOF_TAIL);
  /* The tail ends where the array ends, so reading past tail_size leaves
     the object and fails the bounds check. */
  tail = storage + (PROOF_TAIL - tail_size);

  result = savestate_trailer_decode(file_size, tail, tail_size, nondet_u32(),
   &current, &out);

  assert(result == SAVESTATE_TRAILER_LEGACY ||
   result == SAVESTATE_TRAILER_V2 || result == SAVESTATE_TRAILER_CORRUPT ||
   result == SAVESTATE_TRAILER_NEWER ||
   result == SAVESTATE_TRAILER_OTHER_ROM ||
   result == SAVESTATE_TRAILER_ROM_SIZE_DIFFERS);
  assert((result == SAVESTATE_TRAILER_LEGACY) ==
   (file_size == SAVESTATE_TRAILER_PAYLOAD_SIZE));
  assert((result != SAVESTATE_TRAILER_V2 &&
   result != SAVESTATE_TRAILER_ROM_SIZE_DIFFERS) ||
   tail_size == SAVESTATE_TRAILER_SIZE);
  return 0;
}
