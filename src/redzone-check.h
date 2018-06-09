// Author: Taddeus Kroes

#ifndef REDZONE_CHECK_H_
#define REDZONE_CHECK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Slow path for single-byte redzone checks, implemented in system-alloc.c.
void __check_redzone(void* ptr) __attribute__((noinline));

// Slow path for multi-byte redzone checks, implemented in system-alloc.c.
void __check_redzone_multi(void* ptr, uint64_t size) __attribute__((noinline));

#ifdef __cplusplus
}
#endif

#endif /* REDZONE_CHECK_H_ */
