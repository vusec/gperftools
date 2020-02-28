// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifndef TCMALLOC_REDZONE_POISONING_H_
#define TCMALLOC_REDZONE_POISONING_H_

#include "common.h"            // for kRedzone*

namespace tcmalloc {

void MaybePoisonRedzone(void *ptr, size_t size = kRedzoneSize);

void MaybeUnpoisonRedzone(void *ptr, size_t size = kRedzoneSize);

}  // namespace tcmalloc

#endif // TCMALLOC_REDZONE_POISONING_H_
