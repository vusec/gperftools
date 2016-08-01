#include <stdio.h>
#include <stddef.h>
#include <iostream>
#include <pthread.h>
#include <gperftools/typed_tcmalloc.h>
#include "base/logging.h"       // for logging utilities
#include "thread_cache.h"       // for ThreadCache
#include "static_vars.h"        // for pageheap lookup
#include "span.h"               // for span struct
#include "common.h"             // for PageID

using namespace std;
using namespace tcmalloc;

#define TRANSFER_LIMIT  (3 << 20) // 3MB
#define ALLOCATION_SIZE (1 << 12) // 4KB
#define DEFAULT_TYPE    123

#define spanToSize(span) Static::sizemap()->ByteSizeForClass(span->sizeclass)
#define ptrToSpan(ptr) Static::pageheap()->GetDescriptor( \
                         reinterpret_cast<uintptr_t>(ptr) >> kPageShift)

struct Data {
  void* ptr;
  TypeTag type;
  bool last;
};

pthread_t tid;
pthread_mutex_t lock;
pthread_cond_t consumed, produced;
Data data;

void *producer(void* args) {
  size_t total = 0;

  while (total < TRANSFER_LIMIT) {
    pthread_mutex_lock(&lock);
    while (data.ptr)
      pthread_cond_wait(&consumed, &lock);

    data.ptr = tc_typed_malloc(ALLOCATION_SIZE, DEFAULT_TYPE);
    CHECK_NE(data.ptr, 0);
    total += ALLOCATION_SIZE;

    data.last = total >= TRANSFER_LIMIT;
    data.type = DEFAULT_TYPE;

    pthread_cond_signal(&produced);
    pthread_mutex_unlock(&lock);
  }

  pthread_exit(0);
}

void *consumer(void* args) {
  bool done = false;
  void *ptr = NULL;
  TypeTag type = 0;

  do {
    pthread_mutex_lock(&lock);
    while (!data.ptr)
      pthread_cond_wait(&produced, &lock);

    ptr = data.ptr;
    type = data.type;
    done = data.last;
    data.ptr = NULL;

    pthread_cond_signal(&consumed);
    pthread_mutex_unlock(&lock);

    CHECK_EQ(ptrToSpan(ptr)->type, type);

    free(ptr);
  } while (!done);

  pthread_exit(0);
}

void test_producer_consumer() {
  data.ptr = NULL;

  CHECK_EQ(pthread_mutex_init(&lock, NULL), 0);
  CHECK_EQ(pthread_cond_init(&produced, NULL), 0);
  CHECK_EQ(pthread_cond_init(&consumed, NULL), 0);
  CHECK_EQ(pthread_create(&tid, NULL, &producer, NULL), 0);

  consumer(NULL);
  pthread_join(tid, NULL);

  pthread_cond_destroy(&produced);
  pthread_cond_destroy(&consumed);
  pthread_mutex_destroy(&lock);
}

void malloc_free_pair_checks(const size_t size, const TypeTag type)
{
  ThreadCache *heap = ThreadCache::GetCache();
  size_t
    cl = Static::sizemap()->SizeClass(size),
    cl_size = Static::sizemap()->class_to_size(cl),
    size_before = heap->Size();

  void* ptr = tc_typed_malloc(size, type);
  Span *span = ptrToSpan(ptr);

  // Some checks
  CHECK_EQ(span->type, type);
  CHECK_GE(spanToSize(span), size);

  free(ptr);
}

void test_malloc_free_pairs() {
  short skip;

  for (TypeTag type = 64; type <= 1024; type += 64) {
    for (int size = 0; size <= kMaxSize; size++) {
      if      (UNLIKELY(size == 0))    skip = 0;
      else if (UNLIKELY(size <= 1024)) skip = 7;
      else                             skip = 127;

      malloc_free_pair_checks(size, type);

      if (LIKELY(size > 0)) {
        size += skip;
        malloc_free_pair_checks(size, type);
      }
    }
  }
}

int main()
{
  LOG(INFO, "Running malloc-free pairs test.");
  test_malloc_free_pairs();
  LOG(INFO, "Running producer-consumer test.");
  test_producer_consumer();

  printf("PASS\n");
  return 0;
}
