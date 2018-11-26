// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Sanjay Ghemawat <opensource@google.com>

#include <config.h>
#include "span.h"

#include <string.h>                     // for NULL, memset

#include "internal_logging.h"  // for ASSERT
#include "page_heap_allocator.h"  // for PageHeapAllocator
#include "static_vars.h"       // for Static

namespace tcmalloc {

#ifdef SPAN_HISTORY
void Event(Span* span, char op, int v = 0) {
  span->history[span->nexthistory] = op;
  span->value[span->nexthistory] = v;
  span->nexthistory++;
  if (span->nexthistory == sizeof(span->history)) span->nexthistory = 0;
}
#endif

Span* NewSpan(PageID p, Length len) {
  Span* result = Static::span_allocator()->New();
  memset(result, 0, sizeof(*result));
  result->start = p;
  result->length = len;
#ifdef SPAN_HISTORY
  result->nexthistory = 0;
#endif
#ifdef RZ_ALLOC
  result->is_stack = false;
#endif
  return result;
}

void DeleteSpan(Span* span) {
#ifndef NDEBUG
  // In debug mode, trash the contents of deleted Spans
  memset(span, 0x3f, sizeof(*span));
#endif
  Static::span_allocator()->Delete(span);
}

void DLL_Init(Span* list) {
  list->next = list;
  list->prev = list;
}

void DLL_Remove(Span* span) {
  span->prev->next = span->next;
  span->next->prev = span->prev;
  span->prev = NULL;
  span->next = NULL;
}

int DLL_Length(const Span* list) {
  int result = 0;
  for (Span* s = list->next; s != list; s = s->next) {
    result++;
  }
  return result;
}

void DLL_Prepend(Span* list, Span* span) {
  ASSERT(span->next == NULL);
  ASSERT(span->prev == NULL);
  span->next = list->next;
  span->prev = list;
  list->next->prev = span;
  list->next = span;
}

void ZeroRedzonesInSpan(Span *span) {
#if defined(RZ_FILL) && !defined(RZ_REUSE_HEAP)
  ASSERT(!span->is_stack);

  // Overwrite redzones with zeroes to avoid false positives when the pages are
  // reused for spans with another size class
  const uintptr_t start = span->start << kPageShift;
  const uintptr_t end = (span->start + span->length) << kPageShift;

  if (span->sizeclass == 0) {
    // Large allocation: large redzones at start and end
    memset(reinterpret_cast<void*>(start), 0, kLargeRedzoneSize);
    memset(reinterpret_cast<void*>(end - kLargeRedzoneSize), 0, kLargeRedzoneSize);
# ifdef RZ_DEBUG
    Log(kLog, __FILE__, __LINE__, "zeroed 2 large redzones around large "
        "allocation of", span->length, "pages");
# endif
  } else {
    // Small allocation: a small redzone at the start of each object slot
    const size_t objsize = Static::sizemap()->class_to_size(span->sizeclass);
    const uintptr_t end = (span->start + span->length) << kPageShift;
    unsigned n = 0;
    for (uintptr_t p = start; p <= end - kRedzoneSize; p += objsize) {
      memset(reinterpret_cast<void*>(p), 0, kRedzoneSize);
      n++;
    }
# ifdef RZ_DEBUG
    Log(kLog, __FILE__, __LINE__, "zeroed", n, "redzones with sizeclass", objsize);
# endif
  }
#else
  // No need to overwrite with zeroes for RZ_REUSE_HEAP, the page fault handler
  // will do that before initalizing the new redzones
  (void)span;
#endif
}

}  // namespace tcmalloc
