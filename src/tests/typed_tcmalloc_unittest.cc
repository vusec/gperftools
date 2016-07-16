#include <stdio.h>
#include <stddef.h>
#include <gperftools/typed_tcmalloc.h>
#include "base/logging.h"       // for logging utilities
#include "static_vars.h"        // for pageheap lookup
#include "span.h"               // for span struct
#include "common.h"             // for PageID

using namespace tcmalloc;

PageID malloc_free_pair_checks(const size_t size, const TypeTag type, PageID prev)
{
  void* ptr = tc_typed_malloc(size, type);
  const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  Span *span = Static::pageheap()->GetDescriptor(p);
  size_t size_cl = Static::sizemap()->ByteSizeForClass(span->sizeclass);

  CHECK_EQ(span->type, type);

  if (prev != span->start) {
    LOG(INFO, "new span: %p", span->start);
  }
  // CHECK_EQ((uintptr_t)span->objects, (uintptr_t)ptr + size_cl);

  free(ptr);
  // CHECK_EQ(span->objects, ptr);

  return span->start;
}

int main()
{
  short skip;
  const TypeTag type = 123;
  PageID pg = 0;

  for (int i = 0; i <= kMaxSize; i++) {
    if (UNLIKELY(i == 0)) {
      skip = 0;
    } else if (UNLIKELY(i <= 1024)) {
      skip = 7;
    } else {
      skip = 127;
    }
    LOG(INFO, "Testing range: %d-%d", i, i+skip);

    pg = malloc_free_pair_checks(i, type, pg);

    if (LIKELY(i > 0)) {
      i += skip;
      pg = malloc_free_pair_checks(i, type, pg);
    }
  }

  // LOG(INFO, "%d %d", kMaxSize, 13313);
  // malloc_free_pair_checks(13313, 123);

  printf("PASS\n");
  return 0;
}
