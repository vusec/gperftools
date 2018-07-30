// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>
//
// System allocation wrapper that facilitates lazy redzone filling.

#ifndef TCMALLOC_UFFD_ALLOC_H_
#define TCMALLOC_UFFD_ALLOC_H_

#ifdef RZ_REUSE

#include <config.h>

namespace tcmalloc_uffd {
  extern PERFTOOLS_DLL_DECL
  void *SystemAlloc(size_t bytes, size_t *actual_bytes, size_t alignment = 0);

  extern PERFTOOLS_DLL_DECL
  bool SystemRelease(void *start, size_t length);
}

#endif /* RZ_REUSE */

#endif /* TCMALLOC_UFFD_ALLOC_H_ */
