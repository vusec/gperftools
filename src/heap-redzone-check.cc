#ifdef RZ_ALLOC

#include "span.h"               // for Span
#include "static_vars.h"        // for Static
#include "internal_logging.h"   // for Log, kLog, kCrash, ASSERT
#include "heap-redzone-check.h" // for tcmalloc_get_heap_span, tcmalloc_is_heap_redzone

using tcmalloc::Span;
using tcmalloc::Static;

static inline const Span *get_span(void *ptr) {
  const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  const PageID p = addr >> kPageShift;
  return Static::pageheap()->GetDescriptor(p);
}

__attribute__((always_inline))
static bool points_to_redzone(void *ptr, const Span *span) {
#ifdef RZ_DEBUG_VERBOSE
  ldbg("check redzone at", ptr);
#endif

  ASSERT(span);
  const uintptr_t span_start = span->start << kPageShift;
  const size_t span_offset = reinterpret_cast<uintptr_t>(ptr) - span_start;

  // Large objects have the redzone at the end of the last page.
  if (PREDICT_FALSE(span->sizeclass == 0)) {
    const size_t span_size = span->length << kPageShift;
    return span_offset < kLargeRedzoneSize || span_offset >= span_size - kLargeRedzoneSize;
  }

  // Small objects have a redzone at the start of each allocation unit.
  const size_t objsize = Static::sizemap()->class_to_size(span->sizeclass);
  const size_t object_offset = span_offset % objsize;
  return object_offset < kRedzoneSize;
}

extern "C" {
  const void *tcmalloc_get_heap_span(void *ptr) {
    return reinterpret_cast<const void*>(get_span(ptr));
  }

  // Slow path check for heap.
  bool tcmalloc_is_heap_redzone(void *ptr, const void *span) {
    return points_to_redzone(ptr, reinterpret_cast<const Span*>(span));
  }
}

#endif // RZ_ALLOC
