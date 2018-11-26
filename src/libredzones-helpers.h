// ---
// Author: Taddeus Kroes <t.kroes@vu.nl>

#ifndef LIBREDZONES_HELPERS_H_
#define LIBREDZONES_HELPERS_H_

extern "C" {

#ifdef RZ_REUSE

// (un)register_uffd_pages are defined in libredzones runtime which is linked
// statically, so use weak symbols to allow deferred symbol resolution.

__attribute__((weak))
void register_uffd_pages(void *start, size_t len);

__attribute__((weak))
void unregister_uffd_pages(void *start, size_t len);

#endif // RZ_REUSE

} // extern "C"

#endif // LIBREDZONES_HELPERS_H_
