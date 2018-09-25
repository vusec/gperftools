// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifdef RZ_ALLOC

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <unistd.h>             // for syscall, getpid
#include <sys/syscall.h>        // for __NR_userfaultfd
#include <sys/mman.h>           // for mmap, munmap, MADV_DONTNEED, etc
#include <sys/ioctl.h>          // for ioctl
#include <linux/userfaultfd.h>  // for UFFD*
#include <pthread.h>            // for pthread_*
#include <string.h>             // for strerror
#include <errno.h>              // for errno
#include <poll.h>               // for poll, POLLIN, etc
#include <fcntl.h>              // for O_NONBLOCK
#include "base/basictypes.h"    // for PREDICT_FALSE
#include "system-alloc.h"       // for SysAllocator, MmapSysAllocator, mmap_space
#include "internal_logging.h"   // for Log, kLog, kCrash, ASSERT
#include "span.h"               // for Span
#include "static_vars.h"        // for Static
#include "thread_cache.h"       // for ThreadCache
#include "uffd-alloc.h"         // for tcmalloc_uffd::SystemAlloc
#include "redzone-check.h"      // for tcmalloc_is_redzone, etc
//#include "noinstrument.h"       // for NOINSTRUMENT FIXME
#define NOINSTRUMENT(name) __noinstrument_##name

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
    if (PREDICT_FALSE(ret < 0))                                   \
      Log(kCrash, __FILE__, __LINE__, errmsg ":", strerror(ret)); \
  } while(0)

static char *mmapx(size_t size) {
  char *page = (char*)mmap(NULL, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  if (PREDICT_FALSE(page == MAP_FAILED))
    lperror("mmap of zeropage failed");
  return page;
}

#ifdef RZ_FILL

static void *fill_heap_redzones(uintptr_t pfpage, const Span *span) {
  // TODO: create pool of pages per size class with pre-filled redzones
  static char *buf = mmapx(kSysPageSize);
  memset(buf, 0, kSysPageSize);

  // For large allocations, put the redzones at the start of the first page and
  // the end of the last page.
  // XXX: surround large allocations with guard pages instead?
  if (span->sizeclass == 0) {
    const uintptr_t span_start = span->start << kPageShift;
    const uintptr_t span_end = (span->start + span->length) << kPageShift;
    if (pfpage == span_start) {
      // lower bound redzone
      memset(buf, kRedzoneValue, kRedzoneSize);
    }
    else if (pfpage == span_end - kSysPageSize) {
      // upper bound redzone
      memset(buf + kSysPageSize - kRedzoneSize, kRedzoneValue, kRedzoneSize);
    }
    return buf;
  }

  // For small/medium allocations, fill all redzones of objects that are either
  // fully or partially in this page. Each slot starts with a redzone.
  const size_t objsize = Static::sizemap()->ByteSizeForClass(span->sizeclass);
  const uintptr_t span_start = span->start << kPageShift;
  const ptrdiff_t span_offset = pfpage - span_start;
  const size_t obj_before_pfpage = span_offset % objsize;
  const ssize_t lead_rz = kRedzoneSize - obj_before_pfpage;
  const char *buf_end = buf + kSysPageSize;

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

#define sizedstack_fill_redzones NOINSTRUMENT(sizedstack_fill_redzones)
__attribute__((weak))
extern "C" void *sizedstack_fill_redzones(void *start);

#endif // RZ_FILL

static int uffd = -1;

static void *uffd_poller_thread(void*) {
  // note: we use kPageSize (default 8K) as a unit of copying here instead of
  // the system page size. Although this is typically 2 system pages, it does
  // not matter for the filling logic.

  ldbg("uffd: start polling");

  for (;;) {
    // Wait for message. We don't use a timeout, the thread just ends when the
    // main program ends.
    struct pollfd pollfd = { .fd = uffd, .events = POLLIN };
    int nready = poll(&pollfd, 1, -1);
    if (PREDICT_FALSE(nready < 0))
      lperror("poll");
    ASSERT(nready == 1);
    ASSERT(pollfd.revents & POLLIN);

    // Read message; We only expect page faults.
    struct uffd_msg msg;
    int nread = read(uffd, &msg, sizeof (msg));
    if (PREDICT_FALSE(nread < 0))
      lperror("read on uffd");
    ASSERT(nread == sizeof (msg));
    if (PREDICT_FALSE(msg.event != UFFD_EVENT_PAGEFAULT))
      llog(kCrash, "received non-pagefault uffd event");

    // Look up size class through span.
    const PageID p = msg.arg.pagefault.address >> kPageShift;
    ldbg("uffd: page fault", (void*)msg.arg.pagefault.address, p);
    uintptr_t pfpage = msg.arg.pagefault.address & ~(kSysPageSize - 1);
    void *rzpage = NULL;

#ifdef RZ_FILL
    if (const Span *span = Static::pageheap()->GetDescriptor(p)) {
      // Span found, this is a heap address.
      ASSERT(span->location == Span::IN_USE);
      ldbg("uffd:   span at", (void*)(span->start << kPageShift), "with length", span->length);
      rzpage = fill_heap_redzones(pfpage, span);
    } else if ((rzpage = sizedstack_fill_redzones((void*)pfpage))) {
      // This is an unsafe stack address.
      ldbg("uffd:   filled by sizedstack");
    }
#endif

    if (rzpage) {
      // Copy pre-filled redzone page to target page.
      struct uffdio_copy copy = {
        .dst = pfpage,
        .src = reinterpret_cast<uintptr_t>(rzpage),
        .len = kSysPageSize,
        .mode = 0
      };
      if (PREDICT_FALSE(ioctl(uffd, UFFDIO_COPY, &copy) < 0))
        lperror("could not copy pre-filled uffd page");
      ASSERT(copy.copy == (long)kPageSize);
    } else {
      // Zero the target page if no redzone page can be constructed.
      struct uffdio_zeropage zero = {
        .range = { .start = pfpage, .len = kPageSize },
        .mode = 0
      };
      if (PREDICT_FALSE(ioctl(uffd, UFFDIO_ZEROPAGE, &zero)))
        lperror("could not zero uffd page");
      ASSERT(zero.zeropage == (long)kPageSize);
    }

    ldbg("uffd: initialized", (void*)pfpage, "-", (void*)(pfpage + kPageSize));
  }

  return NULL;
}

static void reset_uffd() {
  if (uffd != -1) {
    ldbg("uffd: closing inherited file descriptor to force reinitialization after fork");
    close(uffd);
    uffd = -1;
  }
}

// Dummy variable whose address is passed to the poller thread so that the
// sizedstack stack interceptor can identify the thread during interception.
extern "C" { int tcmalloc_uffd_thread_arg; }

void initialize() {
  ASSERT(uffd == -1);

  ldbg("uffd: initialize in process", getpid());

  // Register userfaultfd file descriptor to poll from.
  if ((uffd = syscall(__NR_userfaultfd, O_NONBLOCK)) < 0)
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
  check_pthread(pthread_create(&tid, NULL, &uffd_poller_thread,
                               &tcmalloc_uffd_thread_arg),
                "could not create uffd poller thread");
  // The uffd file descriptor is not inherited properly after fork() and the
  // poller thread is not started in the child, so we force reinitialization
  // in the child by resetting the file descriptor.
  check_pthread(pthread_atfork(NULL, NULL, reset_uffd),
                "could not set fork handler");
  cache.ResetUseEmergencyMalloc();

  ldbg("uffd: done initializing");
}

// Register a memory range to the userfaultfd handler. Called internally and by
// sizedstack runtime.
extern "C" void tcmalloc_uffd_register_pages(void *start, size_t len) {
  if (PREDICT_FALSE(uffd == -1))
    initialize();

  if ((uintptr_t)start % kSysPageSize != 0)
    llog(kCrash, "uffd: registered range must be aligned to", kSysPageSize, "bytes");

  if (len % kSysPageSize != 0)
    llog(kCrash, "uffd: registered range must be a multiple of", kSysPageSize, "bytes");

  struct uffdio_register reg = {
    .range = { .start = reinterpret_cast<__u64>(start), .len = len },
    .mode = UFFDIO_REGISTER_MODE_MISSING
  };
  if (ioctl(uffd, UFFDIO_REGISTER, &reg) < 0)
    lperror("uffd: could not register pages");

  ldbg("uffd: registered", start, "-", (void*)((char*)start + len));

  if (!(reg.ioctls & (1 << _UFFDIO_COPY)))
    llog(kCrash, "UFFDIO_COPY operation not supported on registered range");
}

// This replaces TCMalloc_SystemAlloc in PageHeap::GrowHeap.
void *SystemAlloc(size_t size, size_t *actual_size, size_t alignment) {
  // Just wrap TCMalloc_SystemAlloc and register the allocated memory range for
  // page faults.
  void *ptr = TCMalloc_SystemAlloc(size, actual_size, alignment);
  ASSERT(ptr != NULL);
  tcmalloc_uffd_register_pages(ptr, *actual_size);
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

  ldbg("uffd: unregistered", start, "-", (char*)start + length);

  return TCMalloc_SystemRelease(start, length);
}

} // end namespace tcmalloc_uffd
#endif // RZ_REUSE

#ifndef DISABLE_SLOWPATH
static bool points_to_redzone(void *ptr) {
  ldbg("check redzone at", ptr);

  // Find span.
  const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  const PageID p = addr >> kPageShift;
  const tcmalloc::Span *span = tcmalloc::Static::pageheap()->GetDescriptor(p);

  // Ignore non-heap pointers.
  if (PREDICT_FALSE(!span)) {
    ldbg("not a heap pointer: cannot find span for", ptr, "on page", p);
    return false;
  }

  const uintptr_t span_start = span->start << kPageShift;
  const size_t span_offset = addr - span_start;

  // Large objects have the redzone at the end of the last page.
  if (PREDICT_FALSE(span->sizeclass == 0)) {
    const size_t span_size = span->length << kPageShift;
    return span_offset < kRedzoneSize || span_offset >= span_size - kRedzoneSize;
  }

  // Small objects have a redzone at the start of each allocation unit.
  const size_t objsize = Static::sizemap()->ByteSizeForClass(span->sizeclass);
  const size_t object_offset = span_offset % objsize;
  return object_offset < kRedzoneSize;
}
#endif // !DISABLE_SLOWPATH

extern "C" {
  // Expose emergency malloc to sizedstack runtime library.
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

  // Slow path check for heap.
  bool tcmalloc_is_redzone(void *ptr) {
#ifdef DISABLE_SLOWPATH
    return false;
#else
    return points_to_redzone(ptr);
#endif
  }

  bool tcmalloc_is_redzone_multi(void *ptr, uint64_t naccess) {
    llog(kCrash, "multibyte checks not yet supported");
    return false;
  }
}

#endif // RZ_ALLOC
