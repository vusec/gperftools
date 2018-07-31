// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifdef RZ_ALLOC

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <unistd.h>             // for syscall, sysconf
#include <sys/syscall.h>        // for __NR_userfaultfd
#include <sys/mman.h>           // for mmap, munmap, MADV_DONTNEED, etc
#include <sys/ioctl.h>          // for ioctl, UFFD*
#include <linux/userfaultfd.h>  // for 
#include <pthread.h>            // for pthread_*
#include <string.h>             // for strerror
#include <errno.h>              // for errno
#include <poll.h>               // for poll, POLLIN, etc
#include "base/basictypes.h"    // for PREDICT_FALSE
#include "system-alloc.h"       // for SysAllocator, MmapSysAllocator, mmap_space
#include "internal_logging.h"   // for Log, kLog, kCrash, ASSERT
#include "span.h"               // for Span
#include "static_vars.h"        // for Static
#include "thread_cache.h"       // for ThreadCache
#include "uffd-alloc.h"         // for tcmalloc_uffd::SystemAlloc
#include "redzone-check.h"      // for tcmalloc_is_redzone, etc

using namespace tcmalloc;

#define llog(level, ...) Log((level), __FILE__, __LINE__, __VA_ARGS__)
#define lperror(msg) llog(kCrash, msg ":", strerror(errno))

#ifdef RZ_DEBUG
# define ldbg(...) llog(kLog, __VA_ARGS__)
#else
# define ldbg(...)
#endif

#ifdef RZ_REUSE
namespace tcmalloc_uffd {

#define check_pthread(call, errmsg)                               \
  do {                                                            \
    int ret = (call);                                             \
    if (ret < 0)                                                  \
      Log(kCrash, __FILE__, __LINE__, errmsg ":", strerror(ret)); \
  } while(0)

static int uffd = -1;

static void *uffd_poller_thread(void*) {
  // note: we use kPageSize (default 8K) as a unit of copying here instead of
  // the system page size. Although this is typically 2 system pages, it does
  // not matter for the filling logic.

  ldbg("uffd: start polling");

  // Allocate zeroed page to copy later.
  // TODO: create pool of pages per size class with pre-filled redzones
  char *zeropage = (char*)mmap(NULL, kPageSize, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (zeropage == MAP_FAILED)
    lperror("mmap of zeropage failed");

  while (1) {
    // Wait for message. We don't use a timeout, the thread just ends when the
    // main program ends.
    struct pollfd pollfd = { .fd = uffd, .events = POLLIN };
    int nready = poll(&pollfd, 1, -1);
    if (nready < 0)
      lperror("poll");
    ASSERT(nready == 1);
    ASSERT(pollfd.revents & POLLIN);

    // Read message; We only expect page faults.
    struct uffd_msg msg;
    int nread = read(uffd, &msg, sizeof (msg));
    if (nread < 0)
      lperror("read on uffd");
    ASSERT(nread == sizeof (msg));
    if (msg.event != UFFD_EVENT_PAGEFAULT)
      llog(kCrash, "received non-pagefault uffd event");

    // Look up size class through span.
    const PageID p = msg.arg.pagefault.address >> kPageShift;
    const Span *span = Static::pageheap()->GetDescriptor(p);
    // TODO: For large allocations, only the first and last page in the span
    // are mapped originally, so I added mappings for the pages in between. The
    // alternative is slow lookups by walking back one page at a time (see
    // below), we should benchmark which is faster.
    //for (PageID prev = p - 1; span == NULL && prev >= 0; prev--) {
    //  span = Static::pageheap()->GetDescriptor(prev);
    //}
    ASSERT(span);
    ASSERT(span->location == Span::IN_USE);
    ldbg("uffd: page fault", (void*)msg.arg.pagefault.address, p);
    ldbg("uffd:   span:", span, span->sizeclass, span->length);

    // TODO: Fill redzones

    // Copy pre-filled redzone page to target page. TODO: remove this
    uintptr_t pfpage = msg.arg.pagefault.address & ~(kPageSize - 1);
    struct uffdio_copy copy = {
      .dst = pfpage,
      .src = reinterpret_cast<uintptr_t>(zeropage),
      .len = kPageSize,
      .mode = 0
    };
    if (ioctl(uffd, UFFDIO_COPY, &copy) < 0)
      lperror("could not copy pre-filled uffd page");
    ASSERT((size_t)copy.copy == kPageSize);

    ldbg("uffd: initialized", (void*)pfpage, "-", (void*)(pfpage + kPageSize));
  }

  return NULL;
}

void initialize() {
  ASSERT(uffd == -1);

  ldbg("uffd: initialize");

  // Register userfaultfd file descriptor to poll from.
  if ((uffd = syscall(__NR_userfaultfd, 0)) < 0)
    llog(kCrash, "userfaultfd call failed");

  // Check ioctl features.
  struct uffdio_api api = { .api = UFFD_API, .features = 0 };
  if (ioctl(uffd, UFFDIO_API, &api) < 0)
    lperror("couldn't set userfaultfd api");
  if (!(api.ioctls & (1 << _UFFDIO_REGISTER)))
    llog(kCrash, "userfaultfd REGISTER operation not supported");

  // Poll file descriptor from helper thread. pthread_create allocates a stack
  // for the poller thread, which should use emergencyMalloc to avoid deadlock.
  // XXX would be nice to register pthread_join with atexit
  ThreadCache &cache = *ThreadCache::GetCacheWhichMustBePresent();
  cache.SetUseEmergencyMalloc();
  pthread_t tid;
  check_pthread(pthread_create(&tid, NULL, &uffd_poller_thread, NULL),
                "could not create uffd poller thread");
  cache.ResetUseEmergencyMalloc();

  ldbg("uffd: done initializing");
}

// This replaces TCMalloc_SystemAlloc in PageHeap::GrowHeap.
void *SystemAlloc(size_t size, size_t *actual_size, size_t alignment) {
  if (PREDICT_FALSE(uffd == -1))
    initialize();

  // Just wrap TCMalloc_SystemAlloc and register the allocated memory range for
  // page faults.
  void *ptr = TCMalloc_SystemAlloc(size, actual_size, alignment);

  struct uffdio_register reg = {
    .range = {
      .start = reinterpret_cast<__u64>(ptr),
      .len = *actual_size
    },
    .mode = UFFDIO_REGISTER_MODE_MISSING
  };
  if (ioctl(uffd, UFFDIO_REGISTER, &reg) < 0)
    lperror("uffd: could not register pages");

  ldbg("uffd: registered", ptr, "-", (void*)((char*)ptr + *actual_size));

  if (!(reg.ioctls & (1 << _UFFDIO_COPY)))
    llog(kCrash, "UFFDIO_COPY operation not supported on registered range");

  return ptr;
}

bool SystemRelease(void *start, size_t length) {
  // Unregister allocated pages that never saw a page fault.
  // FIXME: should we do this before calling TCMalloc_SystemRelease?
  struct uffdio_range range = {
    .start = reinterpret_cast<__u64>(start),
    .len = length
  };
  if (ioctl(uffd, UFFDIO_UNREGISTER, &range) < 0)
    lperror("uffd: could not unregister pages");

  ldbg("uffd: unregistered", start, "-", (void*)((char*)start + length));

  return TCMalloc_SystemRelease(start, length);
}

} // end namespace tcmalloc_uffd
#endif // RZ_REUSE

static bool points_to_redzone(void *ptr) {
  ldbg("check redzone at", ptr);

  const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  const PageID p = addr >> kPageShift;
  const tcmalloc::Span *span = tcmalloc::Static::pageheap()->GetDescriptor(p);

  // Ignore if we cannot access span.
  ASSERT(span);
  if (PREDICT_FALSE(!span)) {
    ldbg("cannot find span for", ptr, "on page", p);
    return false;
  }

  const uintptr_t base = span->start << kPageShift;
  ASSERT(base <= addr);

  // Large objects have the redzone at the end of the last page.
  if (PREDICT_FALSE(span->sizeclass == 0)) {
    const uintptr_t span_end = base + span->length * kPageSize;
    return addr + kRedzoneSize >= span_end;
  }

  // Small objects have a redzone at the end of each allocation unit.
  const size_t size = Static::sizemap()->ByteSizeForClass(span->sizeclass);
  const size_t span_offset = addr - base;
  const size_t object_offset = span_offset % size;
  return object_offset + kRedzoneSize >= size;
}

extern "C" bool tcmalloc_is_redzone(void *ptr) {
  return points_to_redzone(ptr);
}

extern "C" bool tcmalloc_is_redzone_multi(void *ptr, uint64_t naccess) {
  llog(kCrash, "multibyte checks not yet supported");
  return false;
}

#endif // RZ_ALLOC
