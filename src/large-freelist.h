// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifndef TCMALLOC_LARGE_FREELIST_H_
#define TCMALLOC_LARGE_FREELIST_H_
#ifdef RZ_REUSE

#include "common.h" // for kLargeFreelistSize, Length
#include "span.h"   // for Span

namespace tcmalloc {

class LargeFreeList {
  static const size_t _size = kLargeFreeListSize;
  static Span *_spans[_size];

  static Span *SplitSpan(Span *span, Length n);

public:
  static bool AddSpanToFreelist(Span *span);

  static Span *FindOrSplitSpan(Length n);
};

}  // namespace tcmalloc

#endif // RZ_REUSE
#endif // TCMALLOC_LARGE_FREELIST_H_
