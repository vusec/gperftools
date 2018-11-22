// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifdef RZ_ALLOC

#include <sys/mman.h>           // for mmap, etc
#include <string.h>             // for strerror
#include <errno.h>              // for errno
#include "config.h"
#include "base/basictypes.h"    // for PREDICT_FALSE
#include "internal_logging.h"   // for Log, kLog, kCrash, ASSERT
#include "span.h"               // for Span
#include "static_vars.h"        // for Static
#include "thread_cache.h"       // for ThreadCache
#include "heap-redzone-check.h" // for tcmalloc_get_heap_span, tcmalloc_is_heap_redzone

#include "span.h"               // for Span
#include "static_vars.h"        // for Static
#include "internal_logging.h"   // for Log, kLog, kCrash, ASSERT
#include "heap-redzone-check.h" // for tcmalloc_get_heap_span, tcmalloc_is_heap_redzone

// Uncomment to log slow path redzone checks on the heap.
//#define RZ_DEBUG_VERBOSE

using tcmalloc::kLog;
using tcmalloc::kCrash;
using tcmalloc::Span;
using tcmalloc::Static;
using tcmalloc::ThreadCache;

#define llog(level, ...) Log((level), __FILE__, __LINE__, __VA_ARGS__)
#define lperror(msg) llog(kCrash, msg ":", strerror(errno))

#ifdef RZ_DEBUG
# define ldbg(...) llog(kLog, __VA_ARGS__)
#else
# define ldbg(...)
#endif

static inline const Span *get_span(void *ptr) {
  const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  const PageID p = addr >> kPageShift;
  return Static::pageheap()->GetDescriptor(p);
}

__attribute__((always_inline))
static bool points_to_redzone(void *ptr, const Span *span) {
#ifdef RZ_DEBUG_VERBOSE
  ldbg("check redzone at", ptr);
#endif

  ASSERT(span);
  const uintptr_t span_start = span->start << kPageShift;
  const size_t span_offset = reinterpret_cast<uintptr_t>(ptr) - span_start;

  // Large objects have the redzone at the end of the last page.
  if (PREDICT_FALSE(span->sizeclass == 0)) {
    const size_t span_size = span->length << kPageShift;
    return span_offset < kLargeRedzoneSize || span_offset >= span_size - kLargeRedzoneSize;
  }

  // Small objects have a redzone at the start of each allocation unit.
  const size_t objsize = Static::sizemap()->class_to_size(span->sizeclass);
  const size_t object_offset = span_offset % objsize;
  return object_offset < kRedzoneSize;
}

extern "C" {
  const void *tcmalloc_get_heap_span(void *ptr) {
    return reinterpret_cast<const void*>(get_span(ptr));
  }

  // Slow path check for heap.
  bool tcmalloc_is_heap_redzone(void *ptr, const void *span) {
    return points_to_redzone(ptr, reinterpret_cast<const Span*>(span));
  }

  // Expose emergency malloc to runtime library.
  void tcmalloc_set_emergency_malloc(bool enable) {
    ThreadCache &cache = *ThreadCache::GetCacheWhichMustBePresent();
    if (enable) {
      ldbg("uffd: enabling emergency malloc");
      cache.SetUseEmergencyMalloc();
    } else {
      ldbg("uffd: disabling emergency malloc");
      cache.ResetUseEmergencyMalloc();
    }
  }
}

#if defined(RZ_REUSE) && defined(RZ_FILL)

static char *mmapx(size_t size) {
  char *page = (char*)mmap(NULL, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  if (PREDICT_FALSE(page == MAP_FAILED))
    lperror("mmap of zeropage failed");
  return page;
}

static void *fill_heap_redzones(uintptr_t pfpage, unsigned long sysPageSize, const Span *span) {
  ASSERT(span->location == Span::IN_USE);
  ldbg("uffd:   span at", (void*)(span->start << kPageShift), "with length", span->length);

  // TODO: create pool of pages per size class with pre-filled redzones
  ASSERT(sysPageSize > 0);
  static char *buf = mmapx(sysPageSize);
  memset(buf, 0, sysPageSize);

  // For large allocations, put the redzones at the start of the first page and
  // the end of the last page.
  // XXX: surround large allocations with guard pages instead?
  if (span->sizeclass == 0) {
    ASSERT(kLargeRedzoneSize <= sysPageSize);
    const uintptr_t span_start = span->start << kPageShift;
    const uintptr_t span_end = (span->start + span->length) << kPageShift;
    if (pfpage == span_start) {
      // lower bound redzone
      memset(buf, kRedzoneValue, kLargeRedzoneSize);
    }
    else if (pfpage == span_end - sysPageSize) {
      // upper bound redzone
      memset(buf + sysPageSize - kLargeRedzoneSize, kRedzoneValue, kLargeRedzoneSize);
    }
    return buf;
  }

  // For small/medium allocations, fill all redzones of objects that are either
  // fully or partially in this page. Each slot starts with a redzone.
  const size_t objsize = Static::sizemap()->class_to_size(span->sizeclass);
  const uintptr_t span_start = span->start << kPageShift;
  const ptrdiff_t span_offset = pfpage - span_start;
  const size_t obj_before_pfpage = span_offset % objsize;
  const ssize_t lead_rz = kRedzoneSize - obj_before_pfpage;
  const char *buf_end = buf + sysPageSize;

  ldbg("uffd: initialize redzones for page at", (void*)pfpage, "with object size", objsize);

  if (lead_rz > 0) {
    ldbg("uffd:   fill", lead_rz, "redzone bytes at offset 0");
    memset(buf, kRedzoneValue, lead_rz);
  }

  char *next_rz = buf - obj_before_pfpage + objsize;
  while (next_rz <= buf_end - kRedzoneSize) {
    ldbg("uffd:   fill", kRedzoneSize, "redzone bytes at offset", next_rz - buf);
    memset(next_rz, kRedzoneValue, kRedzoneSize);
    next_rz += objsize;
  }

  const ptrdiff_t tail_rz = buf_end - next_rz;
  if (tail_rz > 0) {
    ldbg("uffd:   fill", tail_rz, "redzone bytes at offset", next_rz - buf);
    memset(next_rz, kRedzoneValue, tail_rz);
  }

  return buf;
}

extern "C" void *tcmalloc_fill_heap_redzones(uintptr_t pfpage,
    unsigned long page_size, const void *span) {
  return fill_heap_redzones(pfpage, page_size, reinterpret_cast<const Span*>(span));
}

#endif // RZ_REUSE and RZ_FILL

#endif // RZ_ALLOC
