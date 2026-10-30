# Workspace Rules for tfr9/v5

## RULE ONE: Never change `../v4/`

- v5 is a **copy-then-modify** of v4.  **Never edit, build in, or add files to `../v4/`** (or to
  `~/github.com/dillera/tfr9`).  Building v4 writes into v4, so don't.
- v5 must not *depend* on `../v4/` at build or test time.  Anything v5 needs is imported (RULE THREE).
- All build intermediates, temporaries and products go in **`v5/build/`** (git-ignored).

## RULE TWO: Commit only on `work*` branches

- Commit to a branch whose name starts with `work` (currently `work`).  Do not change `main`.

## RULE THREE: Import from v4 with git, never with `cp -r`

- `../v4/` is ~750 MB on disk but ~780 tracked files.  Use `scripts/import-from-v4.sh <path>...`
  (it uses `git archive`), so vim undo files, RCS dirs, `_log`, `build/` trees do not come along.

## RULE FOUR: Files with mode 000 are not to be read or changed.

## RULE FIVE: Shell scripts are portable POSIX `sh`

- Some users are on macOS.  Write `#!/bin/sh` scripts without bashisms: no `[[ ]]`, arrays, `$BASH_SOURCE`,
  `${var//x/y}`, `local`, `source`, `echo -e`, `function f`, `<( )`, and avoid GNU-only options
  (`sort -V`, `readlink -f`, `sed -i`, `grep -P`, `stat -c`, `date -d`, `xargs -d`).
- Check with `make check-sh` (runs `sh -n`, `dash -n`, a bashism grep, and `shellcheck -s sh` if installed).
- Makefiles stay GNU-make-3.81-compatible (what macOS ships).

## Where the design lives

`doc/v5-notes-and-recommendations-two-and-a-half.md` is the accepted spec (sections 13, 15, 16 hold the
decisions and the revision list).  Read it before large changes.  Key terms: *engine*, *port profile*
(`TFR4`, `COCO`), *enhancement*, `.pak`.
