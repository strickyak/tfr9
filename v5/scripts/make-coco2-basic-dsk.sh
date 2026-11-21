#!/bin/sh
# make-coco2-basic-dsk.sh -- build 35-track RS-DOS starter disk (HELLO.BAS, PMODE1.BAS, PMODE2.BAS, PMODE4.BAS).
# Usage: sh scripts/make-coco2-basic-dsk.sh [out.dsk]
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
. "$V5/scripts/env.sh"

OUT=${1:-"$V5_BUILD/coco2_basic.dsk"}
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"

SRC_DIR="$V5/os/coco2"

if command -v decb >/dev/null 2>&1; then
  decb dskini "$OUT"
  decb copy -0 -b -t -l -r "$SRC_DIR/hello.bas"  "$OUT,HELLO.BAS"
  decb copy -0 -b -t -l -r "$SRC_DIR/pmode1.bas" "$OUT,PMODE1.BAS"
  decb copy -0 -b -t -l -r "$SRC_DIR/pmode2.bas" "$OUT,PMODE2.BAS"
  decb copy -0 -b -t -l -r "$SRC_DIR/pmode4.bas" "$OUT,PMODE4.BAS"
else
  # Portable fallback when Toolshed `decb` is not installed: write ASCII RS-DOS .BAS files directly.
  python3 - "$SRC_DIR" "$OUT" << 'PYEOF'
import pathlib, struct, sys

src_dir = pathlib.Path(sys.argv[1])
out_path = pathlib.Path(sys.argv[2])
disk = bytearray(b"\xff" * (35 * 18 * 256))
fat_off = 17 * 18 * 256 + 256
dir_off = 17 * 18 * 256 + 2 * 256

def gran_offset(g: int) -> int:
    track = g // 2
    if track >= 17:
        track += 1
    return (track * 18 + (g % 2) * 9) * 256

next_gran = 0
for idx, (stem, fname) in enumerate([("hello.bas", "HELLO"), ("pmode1.bas", "PMODE1"), ("pmode2.bas", "PMODE2"), ("pmode4.bas", "PMODE4")]):
    raw = (src_dir / stem).read_text(encoding="ascii").replace("\r\n", "\n").replace("\n", "\r").encode("ascii")
    if not raw.endswith(b"\r"):
        raw += b"\r"
    g = next_gran
    next_gran += 1
    n_sec = max(1, (len(raw) + 255) // 256)
    last_bytes = len(raw) - (n_sec - 1) * 256
    goff = gran_offset(g)
    disk[goff : goff + len(raw)] = raw
    disk[fat_off + g] = 0xC0 | n_sec
    entry = struct.pack(">8s3sBBBBH", fname.encode("ascii").ljust(8, b" "), b"BAS", 0x00, 0xFF, g, (last_bytes >> 8) & 0xFF, (last_bytes & 0xFF) << 8)
    # Fix exact 32-byte RS-DOS directory entry: name[8], ext[3], type, ascii, first_gran, last_sec_bytes(BE16), pad[16]
    entry = fname.encode("ascii").ljust(8, b" ") + b"BAS" + bytes([0x00, 0xFF, g, (last_bytes >> 8) & 0xFF, last_bytes & 0xFF]) + b"\x00" * 16
    disk[dir_off + idx * 32 : dir_off + (idx + 1) * 32] = entry

out_path.write_bytes(disk)
PYEOF
fi

echo "built $OUT"
