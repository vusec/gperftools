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

void tcmalloc_set_emergency_malloc(bool enable);

#if defined(RZ_REUSE) && defined(RZ_FILL)
void *tcmalloc_fill_heap_redzones(uintptr_t pfpage,
    unsigned long page_size, const void *span);
#endif

#ifdef __cplusplus
}
#endif

#endif // TCMALLOC_HEAP_REDZONE_CHECK_H_
