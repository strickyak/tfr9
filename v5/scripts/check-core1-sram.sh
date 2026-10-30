#!/bin/sh
# check-core1-sram.sh [-s] ELF
#   Spec S6: code that runs on core 1 (the bus loop and every I/O handler) must live in SRAM and must not
#   call into flash (an XIP cache miss, or a flash erase on core 0, stalls the bus).  This lists every
#   function in SRAM (the .data section holds all IN_RAM code) that calls a flash-resident function,
#   resolving the linker's call veneers.  Indirect calls (the IOReaders/IOWriters tables) cannot be seen
#   here, but the handlers they point at are themselves IN_RAM functions and are checked as callers.
#   -s  strict: exit 1 if anything is found not listed in scripts/core1-allowlist.txt
# POSIX sh + awk (no gawk extensions).
here=$(cd "$(dirname "$0")" && pwd)
strict=0
if [ "$1" = "-s" ]; then strict=1; shift; fi
elf=${1:-}
[ -n "$elf" ] && [ -f "$elf" ] || { echo "usage: $0 [-s] file.elf" >&2; exit 2; }
allow="$here/core1-allowlist.txt"
[ -f "$allow" ] || allow=/dev/null
{
  arm-none-eabi-nm "$elf"
  echo "=== DISASSEMBLY ==="
  arm-none-eabi-objdump -d --no-show-raw-insn -j .data "$elf"
} | awk '
  /^=== DISASSEMBLY ===/ { dis = 1; next }
  !dis { if (NF == 3) addr[$3] = $1; next }
  /^[0-9a-f]+ <.*>:$/ { cur = $2; gsub(/[<>:]/, "", cur); next }
  /\tbl\t/ || /\tb\.w\t/ || /\tbl\.w\t/ {
    if (match($0, /<[^>]*>/)) {
      t = substr($0, RSTART + 1, RLENGTH - 2); sub(/\+0x[0-9a-f]+$/, "", t)
      if (t == cur) next
      edge[cur SUBSEP t] = 1
    }
  }
  END {
    for (e in edge) {
      split(e, p, SUBSEP); caller = p[1]; callee = p[2]; real = callee
      if (callee ~ /^__.*_veneer$/) real = substr(callee, 3, length(callee) - 9)   # "__" real "_veneer"
      a = addr[real]
      if (a >= "10000000" && a < "20000000") print caller " -> " real
    }
  }' | arm-none-eabi-c++filt | sort -u | awk -v allow="$allow" '
  BEGIN { while ((getline line < allow) > 0) { if (line !~ /^#/ && line != "") ok[line] = 1 } }
  { caller = $0; sub(/ -> .*/, "", caller)
    if (($0 in ok) || ((caller ":*") in ok)) print "ALLOWED    " $0; else print "FLASH-CALL " $0 }' > "${TMPDIR:-/tmp}/core1sram.$$"
cat "${TMPDIR:-/tmp}/core1sram.$$" | grep -v '^ALLOWED'
total=$(grep -c '^FLASH-CALL' "${TMPDIR:-/tmp}/core1sram.$$")
allowed=$(grep -c '^ALLOWED' "${TMPDIR:-/tmp}/core1sram.$$")
rm -f "${TMPDIR:-/tmp}/core1sram.$$"
echo "check-core1-sram: $total flash call(s) from SRAM functions, $allowed allowed (scripts/core1-allowlist.txt)"
if [ "$strict" -eq 1 ] && [ "$total" -gt 0 ]; then exit 1; fi
exit 0
