// Author: Taddeus Kroes

#ifndef REDZONE_CHECK_H_
#define REDZONE_CHECK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Slow path for redzone checks, implemented in system-alloc.c.
void __check_redzone(void* ptr, uint64_t size) __attribute__ ((noinline));

#ifdef __cplusplus
}
#endif

#endif /* REDZONE_CHECK_H_ */
