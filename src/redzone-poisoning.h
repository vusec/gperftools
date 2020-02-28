// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifndef TCMALLOC_REDZONE_POISONING_H_
#define TCMALLOC_REDZONE_POISONING_H_

#include "common.h"  // for kRedzoneSize
#include "span.h"    // for Span

namespace tcmalloc {

void PoisonRedzone(void *ptr, size_t size = kRedzoneSize);

void UnpoisonRedzone(void *ptr, size_t size = kRedzoneSize);

static inline void PoisonRedzone(uintptr_t ptr, size_t size = kRedzoneSize) {
  PoisonRedzone(reinterpret_cast<void*>(ptr), size);
}

static inline void UnpoisonRedzone(uintptr_t ptr, size_t size = kRedzoneSize) {
  UnpoisonRedzone(reinterpret_cast<void*>(ptr), size);
}

void UnpoisonRedzonesInSpan(Span *span);

}  // namespace tcmalloc

#endif // TCMALLOC_REDZONE_POISONING_H_
