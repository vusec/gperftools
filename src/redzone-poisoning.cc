// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#include <cstring>             // for memset
#include "common.h"            // for kRedzone*
#include "internal_logging.h"  // for ASSERT
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

static inline void MaybeSetRedzone(void *ptr, bool poison, size_t size) {
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

void MaybePoisonRedzone(void *ptr, size_t size) {
  MaybeSetRedzone(ptr, true, size);
}

void MaybeUnpoisonRedzone(void *ptr, size_t size) {
  MaybeSetRedzone(ptr, false, size);
}

}  // namespace tcmalloc
