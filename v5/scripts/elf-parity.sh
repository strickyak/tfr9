#!/bin/sh
# elf-parity.sh BEFORE.elf AFTER.elf -- is the AFTER firmware the same program as BEFORE?
# Used to prove that a refactor (moving code between files, renaming a file, ...) changed nothing:
#   1. cp build/tfr911h/tfr911h_v4.elf build/before.elf        (before the refactor; keep it out of git)
#   2. refactor; make tfr911h
#   3. scripts/elf-parity.sh build/before.elf build/tfr911h/tfr911h_v4.elf
# Exit 0 if the flat binaries are identical.  If they differ, it says whether the symbol tables (names,
# addresses, sizes) still match and lists the symbols that moved or changed size, to help find out why.
# Needs arm-none-eabi-nm / -objcopy (see scripts/env.sh).  POSIX sh.
[ $# -eq 2 ] || { echo "usage: $0 BEFORE.elf AFTER.elf" >&2; exit 2; }
V5=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=${ARM_PREFIX:-arm-none-eabi-}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/elf-parity.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT
for f in "$1" "$2"; do [ -f "$f" ] || { echo "elf-parity: no such file: $f" >&2; exit 2; }; done
"${PREFIX}objcopy" -O binary "$1" "$tmp/a.bin" && "${PREFIX}objcopy" -O binary "$2" "$tmp/b.bin" || exit 2
if cmp -s "$tmp/a.bin" "$tmp/b.bin"; then
  echo "elf-parity: IDENTICAL ($(wc -c < "$tmp/a.bin" | tr -d ' ') bytes, md5 $(md5sum "$tmp/a.bin" 2>/dev/null | cut -c1-32))"
  exit 0
fi
echo "elf-parity: binaries DIFFER"
"${PREFIX}nm" -S -n "$1" | sort > "$tmp/a.nm"; "${PREFIX}nm" -S -n "$2" | sort > "$tmp/b.nm"
if cmp -s "$tmp/a.nm" "$tmp/b.nm"; then echo "  (symbol tables are identical: only code/data bytes differ)"
else
  echo "  symbols only in BEFORE (or moved/resized):"; comm -23 "$tmp/a.nm" "$tmp/b.nm" | head -20 | sed 's/^/    /'
  echo "  symbols only in AFTER (or moved/resized):";  comm -13 "$tmp/a.nm" "$tmp/b.nm" | head -20 | sed 's/^/    /'
fi
exit 1
