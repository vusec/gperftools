// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Author: Chris

#ifndef TYPED_TCMALLOC_H_
#define TYPED_TCMALLOC_H_

/* Copied from tcmalloc.h */
#ifdef __cplusplus
#define PERFTOOLS_THROW throw()
#else
# ifdef __GNUC__
#  define PERFTOOLS_THROW __attribute__((__nothrow__))
# else
#  define PERFTOOLS_THROW
# endif
#endif

#ifndef PERFTOOLS_DLL_DECL
#define PERFTOOLS_DLL_DECL_DEFINED
# ifdef _WIN32
#   define PERFTOOLS_DLL_DECL  __declspec(dllimport)
# else
#   define PERFTOOLS_DLL_DECL
# endif
#endif


#include <stddef.h>                     /* for size_t */
#include <stdint.h>                     /* for uintptr_t */

typedef uintptr_t TypeTag;

#ifdef __cplusplus
extern "C" {
#endif

/* Typed allocation function declarations. */
PERFTOOLS_DLL_DECL void* tc_typed_malloc(size_t size, TypeTag type) PERFTOOLS_THROW;
PERFTOOLS_DLL_DECL void* tc_typed_calloc(size_t n, size_t elem_size, TypeTag type) PERFTOOLS_THROW;
PERFTOOLS_DLL_DECL void* tc_typed_realloc(void* old_ptr, size_t new_size, TypeTag type) PERFTOOLS_THROW;
PERFTOOLS_DLL_DECL void* tc_typed_new(size_t size, TypeTag type);
PERFTOOLS_DLL_DECL void* tc_typed_memalign(size_t align, size_t size, TypeTag type) PERFTOOLS_THROW;
PERFTOOLS_DLL_DECL void* tc_typed_valloc(size_t size, TypeTag type) PERFTOOLS_THROW;
PERFTOOLS_DLL_DECL void* tc_typed_pvalloc(size_t size, TypeTag type) PERFTOOLS_THROW;

#ifdef __cplusplus
}
#endif

/* Again, copied from tcmalloc.h */
/* We're only un-defining those for public */
#if !defined(GPERFTOOLS_CONFIG_H_)

#undef PERFTOOLS_THROW

#ifdef PERFTOOLS_DLL_DECL_DEFINED
#undef PERFTOOLS_DLL_DECL
#undef PERFTOOLS_DLL_DECL_DEFINED
#endif

#endif /* GPERFTOOLS_CONFIG_H_ */

#endif  // TYPED_TCMALLOC_H_
