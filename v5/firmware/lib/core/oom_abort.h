#pragma once
// oom_abort.h -- make "C++ ran out of memory" and abort() visible.  Include from exactly ONE .cpp per product.
//
// Without exceptions, a failing `new` (std::string, std::vector, std::make_shared, ...) ends in libc abort(),
// which is a silent infinite loop: the board just stops answering, with no message.  Here both go to
// centipede_abort(), which prints "*** ABORT: <msg> ***" on the tether console before it hangs (it hangs by
// design; see abort.h).  Spec S49: Tcl commands are protected by Tcl_LowMemoryHook and TryAlloc; this makes
// the remaining C++ allocations diagnosable.
#include <cstddef>
#include <cstdlib>
#include <new>
#include "abort.h"

extern "C" void abort(void) { centipede_abort("abort() called"); }

static inline void* oom_checked_malloc(std::size_t n) {
  void* p = std::malloc(n ? n : 1);
  if (!p) centipede_abort("new: out of memory");
  return p;
}
void* operator new(std::size_t n) { return oom_checked_malloc(n); }
void* operator new[](std::size_t n) { return oom_checked_malloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
