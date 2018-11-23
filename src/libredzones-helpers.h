// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifndef LIBREDZONES_HELPERS_H_
#define LIBREDZONES_HELPERS_H_

#ifdef RZ_REUSE
// register_uffd_pages is defined in libredzones runtime which is linked
// statically, so use a weak symbol to allow deferred symbol resolution.
__attribute__((weak))
extern "C" void register_uffd_pages(void *start, size_t len);
#endif

#endif // LIBREDZONES_HELPERS_H_
