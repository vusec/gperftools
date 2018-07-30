// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifndef TCMALLOC_REDZONE_CHECK_H_
#define TCMALLOC_REDZONE_CHECK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Slow path for single-byte redzone checks, implemented in system-alloc.c.
 __attribute__((noinline))
bool tcmalloc_is_redzone(void *ptr);

// Slow path for multi-byte redzone checks, implemented in system-alloc.c.
 __attribute__((noinline))
bool tcmalloc_is_redzone_multi(void *ptr, uint64_t naccess);

#ifdef __cplusplus
}
#endif

#endif // TCMALLOC_REDZONE_CHECK_H_
