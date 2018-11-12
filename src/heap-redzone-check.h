// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifndef TCMALLOC_HEAP_REDZONE_CHECK_H_
#define TCMALLOC_HEAP_REDZONE_CHECK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Helper to check if a pointer points to the heap, returns corresponding span.
 __attribute__((always_inline))
const void *tcmalloc_get_heap_span(void *ptr);

// Slow path for single-byte redzone checks, implemented in system-alloc.c. the
// second argument must be non-NULL and returned by tcmalloc_get_heap_span.
 __attribute__((noinline))
bool tcmalloc_is_heap_redzone(void *ptr, const void *span);

#ifdef __cplusplus
}
#endif

#endif // TCMALLOC_HEAP_REDZONE_CHECK_H_
