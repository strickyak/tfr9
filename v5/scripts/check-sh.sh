#!/bin/sh
# check-sh.sh -- lint the shell scripts in v5 for bashisms (macOS /bin/sh is not bash on every setup,
# and users run dash too).  POSIX sh.  Exit non-zero if anything is flagged.
V5=$(cd "$(dirname "$0")/.." && pwd)
bad=0
files=$(find "$V5" -name build -prune -o -name third_party -prune -o -name '*.sh' -print)
for f in $files; do
  sh -n "$f" || { echo "SYNTAX: $f"; bad=1; }
  if command -v dash >/dev/null 2>&1; then dash -n "$f" || { echo "DASH SYNTAX: $f"; bad=1; }; fi
  # Patterns that are bash/GNU-only.  (This script's own pattern list is excluded from the scan.)
  hits=$(grep -n -E '\[\[ |BASH_SOURCE|sort -V|readlink -f|echo -e|^[[:space:]]*function [A-Za-z_0-9]+[[:space:]]*(\{|$)|<\(|^[[:space:]]*\(\(|=\(|\$\{[A-Za-z_]+(//|\^\^|,,|:[0-9])|sed -i|grep -P|stat -c|date -d|xargs -d|mapfile|declare |local |^[[:space:]]*source ' "$f" | grep -v 'check-sh.sh' | grep -v '^[0-9]*:[[:space:]]*#')
  if [ -n "$hits" ]; then echo "POSSIBLE BASHISM in $f:"; echo "$hits" | sed 's/^/    /'; bad=1; fi
done
if command -v shellcheck >/dev/null 2>&1; then
  # shellcheck -s sh flags bashisms precisely when available
  for f in $files; do shellcheck -s sh "$f" || bad=1; done
else
  echo "(shellcheck not installed; grep-based check only)"
fi
[ "$bad" -eq 0 ] && echo "check-sh: OK"
[ "$bad" -eq 0 ]
