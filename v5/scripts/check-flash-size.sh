#!/bin/sh
# check-flash-size.sh ELF
#   Verify that flash-resident load sections in ELF fit strictly below the 4 MB LittleFS
#   partition offset (LFS_FLASH_OFFSET = 0x00400000, XIP address 0x10400000; see lfs-centipede.h).
#   Portable POSIX sh + awk (no gawk extensions).
set -eu

elf=${1:-}
if [ -z "$elf" ] || [ ! -f "$elf" ]; then
  echo "usage: $0 file.elf" >&2
  exit 2
fi

if ! command -v arm-none-eabi-objdump >/dev/null 2>&1; then
  V5=$(cd "$(dirname "$0")/.." && pwd)
  # shellcheck source=scripts/env.sh
  . "$V5/scripts/env.sh"
fi

arm-none-eabi-objdump -h "$elf" | awk '
  function hex(s,    i, c, v, n) {
    sub(/^0[xX]/, "", s)
    v = 0
    n = length(s)
    for (i = 1; i <= n; i++) {
      c = substr(s, i, 1)
      if (c >= "0" && c <= "9")      v = v * 16 + (c - "0")
      else if (c >= "a" && c <= "f") v = v * 16 + 10 + index("abcdef", c) - 1
      else if (c >= "A" && c <= "F") v = v * 16 + 10 + index("ABCDEF", c) - 1
    }
    return v
  }
  /^[[:space:]]*[0-9]+[[:space:]]+\./ {
    sz = hex($3)
    lma = hex($5)
    have = 1
    next
  }
  have && /LOAD/ {
    # RP2350 XIP flash lives at 0x10000000..0x1FFFFFFF; LittleFS starts at +4 MB (0x10400000).
    xip_base = 268435456
    xip_top  = 536870912
    if (sz > 0 && lma >= xip_base && lma < xip_top) {
      end = lma + sz
      if (end > max_end) max_end = end
    }
    have = 0
    next
  }
  { have = 0 }
  END {
    xip_base = 268435456
    limit = 4 * 1024 * 1024
    if (max_end <= xip_base) {
      printf "check-flash-size: FAIL: no XIP flash load sections found\n" > "/dev/stderr"
      exit 1
    }
    used = max_end - xip_base
    pct = (100.0 * used) / limit
    printf "check-flash-size: %d / %d bytes (%.1f%% of 4 MB flash budget before LittleFS)\n", used, limit, pct
    if (used >= limit) {
      printf "check-flash-size: FAIL: firmware image (%d bytes) overlaps LittleFS at 0x10400000\n", used > "/dev/stderr"
      exit 1
    }
  }
'
