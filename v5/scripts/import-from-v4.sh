#!/bin/sh
# import-from-v4.sh [-n] [-d DESTDIR] [-s N] PATH...      (POSIX sh; bsdtar and GNU tar both work)
#   Copy files out of v4's *git tree* (HEAD of the repo that contains ../v4) into v5.
#   PATH is relative to v4/, e.g. firmware/one/littlefs.
#   -d DIR  destination directory, relative to v5/ (default: .)
#   -s N    strip N leading components of PATH, so  -s 3 firmware/one/littlefs  lands the
#           files directly in DIR instead of DIR/firmware/one/littlefs  (default 0)
#   -n      dry run: list what would be imported
# Uses `git archive`, so only tracked files come along (no .un~, RCS, build/, _log).
# It never writes to ../v4.
#   example (from v5/):  scripts/import-from-v4.sh -d third_party/littlefs -s 3 firmware/one/littlefs
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
repo=$(git -C "$here/.." rev-parse --show-toplevel)
dry=0; dest=.; strip=0
while getopts nd:s: o; do
  case $o in
    n) dry=1 ;;
    d) dest=$OPTARG ;;
    s) strip=$OPTARG ;;
    *) echo "import-from-v4.sh: bad option" >&2; exit 2 ;;
  esac
done
shift $((OPTIND-1))
if [ $# -lt 1 ]; then sed -n '2,13p' "$0" >&2; exit 2; fi
for p in "$@"; do
  src="v4/$p"
  if [ "$dry" -eq 1 ]; then
    echo "== $src"; git -C "$repo" ls-tree -r --name-only HEAD -- "$src" | sed 's/^/   /'
    continue
  fi
  mkdir -p "$here/$dest"
  # strip count: "v4/" is one component, plus the caller's $strip
  git -C "$repo" archive HEAD -- "$src" | tar -x -C "$here/$dest" --strip-components="$((1 + strip))"
  echo "imported $src -> $dest"
done
