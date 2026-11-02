// Host test shim: PSP toolchain provides <malloc.h> with memalign(); host libc does not.
#include <stdlib.h>
static inline void *host_shim_memalign(size_t alignment, size_t size)
{
  size_t pad = alignment > sizeof(void *) ? alignment : sizeof(void *);
  void *raw = malloc(size + pad);
  if (!raw) return NULL;
  unsigned char *p = (unsigned char *)raw;
  uintptr_t addr = (uintptr_t)p;
  uintptr_t aligned = (addr + pad - 1) & ~(pad - 1);
  if (aligned == addr) return p; // already aligned; no header needed
  ((void **)aligned)[-1] = p; // raw pointer; tests never free these
  return (void *)aligned;
}
#define memalign(a, s) host_shim_memalign(a, s)
