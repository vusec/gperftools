// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#include <cstring>             // for memset
#include "common.h"            // for kRedzone*
#include "internal_logging.h"  // for ASSERT
#include "static_vars.h"       // for Static
#include "redzone-poisoning.h"

#define SHADOW_BASE          (0x00007fff8000ULL)
#define SHADOW_SCALE         (3)
#define SHADOW_ALIGN_MASK    ((1ULL << SHADOW_SCALE) - 1)
#define SHADOW_REDZONE_MAGIC (0x11)

using tcmalloc::kLog;
using tcmalloc::Log;

typedef uint8_t shadow_t;

static inline shadow_t *ShadowPtr(void *ptr) {
  shadow_t *shadowmem = (shadow_t*)SHADOW_BASE;
  return &shadowmem[(uintptr_t)ptr >> SHADOW_SCALE];
}

static inline void SetRedzone(void *ptr, bool poison, size_t size) {
#ifndef RZ_REUSE_HEAP

# ifdef RZ_FILL
#  ifdef RZ_DEBUG
  Log(kLog, __FILE__, __LINE__, "-", poison ? "fill with" : "clear", "guard value");
#  endif
  memset(ptr, poison ? kRedzoneValue : 0, size);
# endif

# ifdef RZ_SHADOWMEM
#  ifdef RZ_DEBUG
  Log(kLog, __FILE__, __LINE__, "-", poison ? "poison" : "unpoison", "shadow memory");
#  endif
  ASSERT(((uintptr_t)ptr & SHADOW_ALIGN_MASK) == 0);
  ASSERT((size & SHADOW_ALIGN_MASK) == 0);
  const int shadowval = poison ? SHADOW_REDZONE_MAGIC : 0;
  memset(ShadowPtr(ptr), shadowval, size >> SHADOW_SCALE);
# endif

#endif // !RZ_REUSE_HEAP
}

namespace tcmalloc {

void PoisonRedzone(void *ptr, size_t size) {
  SetRedzone(ptr, true, size);
}

void UnpoisonRedzone(void *ptr, size_t size) {
  SetRedzone(ptr, false, size);
}

void UnpoisonRedzonesInSpan(Span *span) {
#if (defined(RZ_FILL) || defined(RZ_SHADOWMEM)) && !defined(RZ_REUSE_HEAP)
  ASSERT(!span->is_stack);

  // Overwrite redzones with zeroes to avoid false positives when the pages are
  // reused for spans with another size class
  const uintptr_t start = span->start << kPageShift;
  const uintptr_t end = (span->start + span->length) << kPageShift;

  if (span->sizeclass == 0) {
    // Large allocation: large redzones at start and end
    UnpoisonRedzone(start, kLargeRedzoneSize);
    UnpoisonRedzone(end - kLargeRedzoneSize, kLargeRedzoneSize);
# ifdef RZ_DEBUG
    Log(kLog, __FILE__, __LINE__, "zeroed 2 large redzones around large "
        "allocation of", span->length, "pages");
# endif
  } else {
    // Small allocation: a small redzone at the start of each object slot
    const size_t slot_size = Static::sizemap()->class_to_size(span->sizeclass);
    const uintptr_t end = (span->start + span->length) << kPageShift;
    unsigned n = 0;
    for (uintptr_t rz = start; rz <= end - kRedzoneSize; rz += slot_size) {
      UnpoisonRedzone(rz);
      n++;
    }
# ifdef RZ_DEBUG
    Log(kLog, __FILE__, __LINE__, "zeroed", n, "redzones with sizeclass", slot_size);
# endif
  }
#else // (RZ_FILL || RZ_SHADOWMEM) && !RZ_REUSE_HEAP
  // No need to overwrite with zeroes for RZ_REUSE_HEAP, the page fault handler
  // will do that before initalizing the new redzones
  (void)span;
#endif // (!RZ_FILL && !RZ_SHADOWMEM) || RZ_REUSE_HEAP
}

}  // namespace tcmalloc
