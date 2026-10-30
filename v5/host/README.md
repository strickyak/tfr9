# host/

The Go `tether` — one module (`github.com/strickyak/tfr9/v5/host`), one binary.

    make tether      # build/tether for this machine
    make tethers     # the 8 static cross builds -> build/tether.<os>-<arch>.exe
    make test-host   # includes `go test ./...`

Layout: `cmd/tether/` (package main), `internal/{cobs,lib,bootmenu}`.

## Provenance
Merged from v4's two diverging trees (`tether/` for the TFR/911H and `centipede-32z/tether/`): both began as
the same copy (v4 commit `fb63e5c`), so they were 3-way merged against that base.  Files only one side changed
were taken as is; the two files both sides changed (`pico_rpc.go`, `tconsole.go`) were resolved by hand
(`pico_rpc.go` = the Centipede superset + the TFR tree's `/dev/ttyACM*` re-glob).  Andrew Diller's tether
patches (`disk.go` EmulateDiskWrite fix, UTF-8 pass-through, `-pass_ctrl_c`, `-boot`, `T_WALLTIME`) were applied on top.

## The `/pc` filesystem (spec 11.12, 11.13)
`pcfs.go` is the only way the Pico reaches the PC's disk.  Symlinks are invisible, only regular files and
directories are served, the root (`-pc`, default `/tmp/pc`) is verified on every request and is never created by
tether.  Tests: `cmd/tether/pcfs_test.go`.
