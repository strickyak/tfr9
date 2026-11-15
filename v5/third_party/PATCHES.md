# third_party: provenance and local patches

All three libraries were imported **verbatim** from v4 (`v4/firmware/one/...`, commit `f963d3f`) with
`scripts/import-from-v4.sh`.  v4 had two byte-identical copies (`firmware/one` and `centipede-32z/three`);
v5 has one.

| Library | In v5 | Notes |
|---|---|---|
| `littlefs/` | `lfs.c lfs.h lfs_util.c lfs_util.h` | `lfs-centipede.[ch]` (the Pico flash glue, **not** upstream) lives in `firmware/lib/fs/` |
| `miniz/` | `miniz.c miniz.h` + docs | `examples/` dropped |
| `tcl6.7c/` | Tcl 6.7c with `TCL_GENERIC_ONLY=1` and `TCL_MEM_DEBUG=1` (set by the product CMake) | `unused/` dropped; `compat/`, `library/`, `cli/`, `tests/` kept for reference.  `tclUnix*.c`, `tclTest.c` are **not compiled** |

## Local changes against upstream

**Not yet audited.**  v4 never recorded which files differ from upstream (LittleFS, miniz, Tcl 6.7c
as originally released).  To do: diff each against its upstream release and list every difference here
(this matters for the licensing questions in the spec, section 4 S22).

## v5 patches (known)

* **`tcl6.7c/tclBasic.c`, `tcl.h`: `Tcl_LowMemoryHook`** (spec S49).  A function pointer tested before each command
  runs; nonzero makes `Tcl_Eval` return `TCL_ERROR "out of memory: heap nearly exhausted"` instead of letting
  `ckalloc` panic.  The firmware sets it (in `register_tcl_commands`) to `HeapIsLow()` from `firmware/lib/core/heap.h`.
  Test: `tests/host/tcl_lowmem_test.cpp` (host) and `tests/systests/04_heap_test.sh` (board).

* **`miniz/miniz.h`: `MZ_ZIP_MAX_IO_BUF_SIZE` is overridable** with `-DMZ_ZIP_IO_BUF_SIZE=N` (spec S38).  The
  firmware builds with 4096 instead of 64 KB because only ~68 KB of heap is free.

## Rules

* Do not edit files here casually.  A change needs a line in this file saying what and why.
* Product-specific glue goes in `firmware/lib/...`, not here.
