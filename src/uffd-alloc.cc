// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifdef UFFD_SYS_ALLOC

#define _GNU_SOURCE
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

namespace tcmalloc_uffd {

using tcmalloc::kLog;
using tcmalloc::kCrash;
using tcmalloc::ThreadCache;

#define llog(level, ...) Log((level), __FILE__, __LINE__, __VA_ARGS__)

#define check_pthread(call, errmsg)                             \
  do {                                                          \
    int ret = (call);                                           \
    if (ret < 0)                                                \
      Log(kCrash, __FILE__, __LINE__, (errmsg), strerror(ret)); \
  } while(0)

static int uffd = -1;

static void *uffd_poller_thread(void*) {
  struct pollfd pollfd = { .fd = uffd, .events = POLLIN };
  struct uffd_msg msg;
  int nready, nread;

  llog(kLog, "uffd: start polling");

  // note: we use kPageSize (default 8K) as a unit of copying here instead of
  // the system page size. Although this is typically 2 system pages, it does
  // not matter for the filling logic.

  // Allocate zeroed page to copy later.
  // TODO: create pool of pages per size class with pre-filled redzones
  char *zeropage = (char*)mmap(NULL, kPageSize, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (zeropage == MAP_FAILED)
    llog(kCrash, "mmap of zeropage failed:", strerror(errno));

  while (1) {
    // Wait for message. We don't use a timeout, the thread just ends when the
    // main program ends.
    if ((nready = poll(&pollfd, 1, -1)) < 0)
      llog(kCrash, "poll:", strerror(errno));
    ASSERT(nready == 1);
    ASSERT(pollfd.revents & POLLIN);

    // Read message; We only expect page faults.
    if ((nread = read(uffd, &msg, sizeof (msg))) < 0)
      llog(kCrash, "read on uffd:", strerror(errno));
    ASSERT(nread == sizeof (msg));
    if (msg.event != UFFD_EVENT_PAGEFAULT)
      llog(kCrash, "received non-pagefault uffd event");

    // Look up corresponding span.
    const PageID p = msg.arg.pagefault.address >> kPageShift;
    tcmalloc::Span *span = tcmalloc::Static::pageheap()->GetDescriptor(p);
    uintptr_t pfpage = p << kPageShift;
    //uintptr_t pfpage = msg.arg.pagefault.address & ~(kPageSize - 1);

    // Print page fault address, span ID and object size.
    llog(kLog, "uffd: page fault", (void*)msg.arg.pagefault.address, p, span->sizeclass);

    // TODO: Fill redzones

    // Copy pre-filled redzone page to target page.
    struct uffdio_copy copy = {
      .dst = pfpage,
      .src = reinterpret_cast<uintptr_t>(zeropage),
      .len = kPageSize,
      .mode = 0
    };
    if (ioctl(uffd, UFFDIO_COPY, &copy) < 0)
      llog(kCrash, "could not copy pre-filled uffd page:", strerror(-copy.copy));
    ASSERT((size_t)copy.copy == kPageSize);
  }

  return NULL;
}

void initialize() {
  ASSERT(uffd == -1);

  llog(kLog, "uffd: initialize");

  // Register userfaultfd file descriptor to poll from.
  if ((uffd = syscall(__NR_userfaultfd, 0)) < 0)
    llog(kCrash, "userfaultfd call failed");

  // Check ioctl features.
  struct uffdio_api api = { .api = UFFD_API, .features = 0 };
  if (ioctl(uffd, UFFDIO_API, &api) < 0)
    llog(kCrash, "couldn't set userfaultfd api:", strerror(errno));
  if (!(api.ioctls & (1 << _UFFDIO_REGISTER)))
    llog(kCrash, "userfaultfd REGISTER operation not supported");

  // Poll file descriptor from helper thread. pthread_create allocates a stack
  // for the poller thread, which should use emergencyMalloc to avoid deadlock.
  ThreadCache &cache = *ThreadCache::GetCacheWhichMustBePresent();
  cache.SetUseEmergencyMalloc();
  pthread_t tid;
  check_pthread(pthread_create(&tid, NULL, &uffd_poller_thread, NULL),
                "could not create uffd poller thread");
  cache.ResetUseEmergencyMalloc();

  llog(kLog, "uffd: done initializing");
}

// This replaces TCMalloc_SystemAlloc in PageHeap::GrowHeap.
void *SystemAlloc(size_t size, size_t *actual_size, size_t alignment) {
  if (PREDICT_FALSE(uffd == -1))
    initialize();

  // Just wrap TCMalloc_SystemAlloc and register the allocated memory range for
  // page faults.
  void *ptr = ::TCMalloc_SystemAlloc(size, actual_size, alignment);

  struct uffdio_register argp = {
    .range = {
      .start = reinterpret_cast<__u64>(ptr),
      .len = *actual_size
    },
    .mode = UFFDIO_REGISTER_MODE_MISSING
  };
  if (ioctl(uffd, UFFDIO_REGISTER, &argp) < 0)
    llog(kCrash, "could not register page for userfaultfd:", strerror(errno));

  llog(kLog, "uffd: registered", *actual_size, "bytes at", ptr);

  return ptr;
}

} // end namespace tcmalloc_uffd

#endif // UFFD_SYS_ALLOC

// placeholder for pass
extern "C" void __check_redzone(void* ptr) {}
