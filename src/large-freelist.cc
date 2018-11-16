// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifdef RZ_REUSE

#include "page_heap.h"        // for DeleteAndUnmapSpan
#include "static_vars.h"      // for Static
#include "large-freelist.h"   // for LargeFreeList

namespace tcmalloc {

Span *LargeFreeList::_spans[LargeFreeList::_size] = {NULL};

static inline void SetRedzoneAtStart(Span *span, int val) {
  const uintptr_t start = span->start << kPageShift;
  memset(reinterpret_cast<void*>(start), val, kLargeRedzoneSize);
}

static inline void SetRedzoneAtEnd(Span *span, int val) {
  const uintptr_t end = (span->start + span->length) << kPageShift;
  memset(reinterpret_cast<void*>(end - kLargeRedzoneSize), val, kLargeRedzoneSize);
}

// REQUIRES: the pageheap lock to be held
bool LargeFreeList::AddSpanToFreelist(Span *span) {
  ASSERT(span->location == Span::IN_USE);
  ASSERT(span->sizeclass == 0);

  // Find the smallest span.
  size_t ismall = _size;
  Span *small = NULL;
  for (size_t i = 0; i < _size; ++i) {
    Span *span = _spans[i];
    if (span == NULL) {
      ismall = i;
      small = NULL;
      break;
    }
    if (small == NULL || span->length < small->length) {
      ismall = i;
      small = span;
    }
  }
  ASSERT(ismall < _size);

  // If a free slot was found, insert there.
  if (small == NULL) {
    _spans[ismall] = span;
    return true;
  }

  // If the buffer is full, replace the smallest span in the buffer or fail.
  if (span->length > small->length) {
    SetRedzoneAtStart(small, 0);
    SetRedzoneAtEnd(small, 0);
    DeleteAndUnmapSpan(small);
    _spans[ismall] = span;
    return true;
  }

  return false;
}

// REQUIRES: the pageheap lock to be held
Span *LargeFreeList::FindOrSplitSpan(Length n) {
  ASSERT(n > 0);

  // Find the largest span that fits.
  Span *best = NULL;
  size_t ibest;
  for (size_t i = 0; i < _size; ++i) {
    Span *span = _spans[i];
    if (span != NULL && span->length >= n) {
      if (best == NULL || span->length > best->length) {
        best = span;
        ibest = i;
      }
    }
  }

  // No fit: do nothing
  if (best == NULL)
    return NULL;

  // Perfect fit: remove from buffer
  Span *leftover = NULL;

  // Imperfect fit: split and put leftover in buffer
  if (best->length > n)
    leftover = SplitSpan(best, n);

  _spans[ibest] = leftover;
  return best;
}

// REQUIRES: the pageheap lock to be held
Span *LargeFreeList::SplitSpan(Span *span, Length n) {
  Span *leftover = Static::pageheap()->Split(span, n);

  // If the leftover span fits in a small allocation, it will not be reused for
  // a large allocation unless it is merged into a larger adjacent span.
  // Don't wait for this to happen and free it instead.
  // TODO: benchmark effect of this
  uint32 lastcl = Static::num_size_classes() - 1;
  size_t max_small_object_size = Static::sizemap()->ByteSizeForClass(lastcl);
  size_t object_size = (leftover->length << kPageShift) - 2 * kRedzoneSize;

  if (object_size <= max_small_object_size) {
    SetRedzoneAtEnd(leftover, 0);
    DeleteAndUnmapSpan(leftover);
    leftover = NULL;
  } else {
    SetRedzoneAtEnd(span, kRedzoneValue);
    SetRedzoneAtStart(leftover, kRedzoneValue);
  }

  return leftover;
}

}  // namespace tcmalloc

#endif // RZ_REUSE
