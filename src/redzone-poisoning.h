// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifndef TCMALLOC_REDZONE_POISONING_H_
#define TCMALLOC_REDZONE_POISONING_H_

#include "common.h"            // for kRedzone*

namespace tcmalloc {

void MaybePoisonRedzone(void *ptr, size_t size = kRedzoneSize);

void MaybeUnpoisonRedzone(void *ptr, size_t size = kRedzoneSize);

static inline void MaybePoisonRedzone(uintptr_t ptr, size_t size = kRedzoneSize) {
  MaybePoisonRedzone(reinterpret_cast<void*>(ptr), size);
}

static inline void MaybeUnpoisonRedzone(uintptr_t ptr, size_t size = kRedzoneSize) {
  MaybeUnpoisonRedzone(reinterpret_cast<void*>(ptr), size);
}

}  // namespace tcmalloc

#endif // TCMALLOC_REDZONE_POISONING_H_
