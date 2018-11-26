// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifndef TCMALLOC_LARGE_FREELIST_H_
#define TCMALLOC_LARGE_FREELIST_H_
#ifdef RZ_REUSE_HEAP

#include <cstdio>   // for fprintf, stderr
#include "common.h" // for kLargeFreelistSize, Length
#include "span.h"   // for Span

#ifdef RZ_DEBUG
# define LFL_LOG(fmt, ...) fprintf(stderr, "LFL: " fmt "\n", __VA_ARGS__)
#else
# define LFL_LOG(...) do {} while (false)
#endif

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

#else // RZ_REUSE_HEAP

# define LFL_LOG(...) do {} while (false)

#endif // !RZ_REUSE_HEAP
#endif // TCMALLOC_LARGE_FREELIST_H_
