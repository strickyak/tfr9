#pragma once
// heap.h -- two ways to ask for memory, and a way to ask "is the heap nearly gone?"  (spec S49)
//
//   MustAlloc(n)  -- never returns NULL: out of memory is a panic (the board reboots with a dump).
//                    Use it where failure cannot be handled.  This is what Tcl's ckalloc() does:
//                    Tcl 6.7 assumes ckalloc never fails, so it cannot be changed to return NULL.
//   TryAlloc(n)   -- returns NULL when the request would not leave HEAP_RESERVE bytes free, or when malloc
//                    fails.  Use it where the caller CHECKS for NULL and reports an error, e.g. "read this
//                    file into a buffer": a big request fails politely and the board lives.
//   HeapIsLow()   -- true when free heap < HEAP_RESERVE.  Tcl_LowMemoryHook (third_party/tcl6.7c) is set
//                    to this, so a runaway script gets "out of memory" at its next command instead of a
//                    reboot.  The reserve is what lets the error message, and one more small command, run.
//
// C++ note: `new`, std::string and std::vector are NOT covered: with exceptions disabled a failed `new`
// panics (core/oom_abort.h prints the message), which is the intended behavior for `new`.  Code that can hold
// O(file size) data must use TryAlloc and check, or (better) stream.
#include <cstddef>
#include <cstdlib>

#ifndef HEAP_RESERVE
#define HEAP_RESERVE (16u * 1024u)
#endif

#if __has_include("pico.h")        // on the board: the Pico SDK declares panic(); abort.h defines it (dump + reboot)
#include "pico.h"
#define HEAP_ON_DEVICE 1
#else                             // host tests: no SDK
extern "C" void panic(const char* format, ...);
#endif

#ifdef HEAP_ON_DEVICE
#include <malloc.h>
#include <unistd.h>
extern "C" char __StackLimit;
// Free bytes: unused space between the heap's end and the stack limit, plus freed chunks inside the heap.
inline size_t HeapFreeBytes() {
  struct mallinfo mi = mallinfo();
  size_t unused = (size_t)(&__StackLimit - (char*)sbrk(0));
  return unused + (size_t)mi.fordblks;
}
#else
// Host build: no heap limit to speak of (tests substitute their own hook).
inline size_t HeapFreeBytes() { return (size_t)-1 / 2; }
#endif

inline bool HeapIsLow() { return HeapFreeBytes() < HEAP_RESERVE; }

// TryAlloc: malloc that also refuses to dip into the HEAP_RESERVE.  It is the ONLY way to ask "is there room":
// ask for the memory and CHECK FOR NULL.  Never decide from HeapFreeBytes() that an allocation will succeed:
// the heap fragments (free memory is not one block), and other allocations happen in between.  A size
// check can only refuse early; it can never promise success.  Callers must handle NULL (malloc returns NULL
// on this platform: PICO_MALLOC_PANIC=0, spec S51).
inline void* TryAlloc(size_t n) {
  size_t free_now = HeapFreeBytes();
  if (n > free_now || free_now - n < HEAP_RESERVE) return nullptr;
  return malloc(n);
}

inline void* MustAlloc(size_t n) {
  void* p = malloc(n);
  if (!p && n != 0) panic("MustAlloc(%u): out of memory", (unsigned)n);
  return p;
}
