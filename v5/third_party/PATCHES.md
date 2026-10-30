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

## Rules

* Do not edit files here casually.  A change needs a line in this file saying what and why.
* Product-specific glue goes in `firmware/lib/...`, not here.
