#!/bin/sh
# make-coco2-basic-dsk.sh -- build 35-track RS-DOS starter disk containing
# Erico's OPIL demo (LOADER.BAS, P., P.00, P.01) alongside HELLO.BAS, PMODE1.BAS, PMODE2.BAS, PMODE4.BAS.
# Usage: sh scripts/make-coco2-basic-dsk.sh [out.dsk]
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
. "$V5/scripts/env.sh"

OUT=${1:-"$V5_BUILD/coco2_basic.dsk"}
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"

SRC_DIR="$V5/os/coco2"
OPIL_ZIP="$SRC_DIR/opil-2026-04-22.zip"
OPIL_MEMBER=${OPIL_DSK_NAME:-OPIL_EN_2026-04-22.dsk}

if command -v decb >/dev/null 2>&1 && command -v unzip >/dev/null 2>&1 && [ -f "$OPIL_ZIP" ]; then
  unzip -p "$OPIL_ZIP" "$OPIL_MEMBER" > "$OUT"
  decb copy -0 -b -t -l -r "$SRC_DIR/hello.bas"  "$OUT,HELLO.BAS"
  decb copy -0 -b -t -l -r "$SRC_DIR/pmode1.bas" "$OUT,PMODE1.BAS"
  decb copy -0 -b -t -l -r "$SRC_DIR/pmode2.bas" "$OUT,PMODE2.BAS"
  decb copy -0 -b -t -l -r "$SRC_DIR/pmode4.bas" "$OUT,PMODE4.BAS"
else
  # Portable fallback when Toolshed `decb` or `unzip` is not installed:
  # extract OPIL base disk via Python's stdlib `zipfile` and add tokenized/ASCII .BAS files in free granules.
  python3 - "$SRC_DIR" "$OPIL_ZIP" "$OPIL_MEMBER" "$OUT" << 'PYEOF'
import pathlib, sys, zipfile

src_dir = pathlib.Path(sys.argv[1])
opil_zip = pathlib.Path(sys.argv[2])
opil_member = sys.argv[3]
out_path = pathlib.Path(sys.argv[4])

if opil_zip.is_file():
    with zipfile.ZipFile(opil_zip) as zf:
        disk = bytearray(zf.read(opil_member))
else:
    disk = bytearray(b"\xff" * (35 * 18 * 256))

fat_off = 17 * 18 * 256 + 256
dir_off = 17 * 18 * 256 + 2 * 256

def gran_offset(g: int) -> int:
    track = g // 2
    if track >= 17:
        track += 1
    return (track * 18 + (g % 2) * 9) * 256

def alloc_free_gran() -> int:
    for g in range(68):
        if disk[fat_off + g] == 0xFF:
            return g
    raise RuntimeError("RS-DOS disk full (no free granules)")

def find_dir_slot(name8: bytes, ext3: bytes) -> int:
    first_free = -1
    for i in range(72):
        off = dir_off + i * 32
        b0 = disk[off]
        if b0 in (0x00, 0xFF):
            if first_free < 0:
                first_free = i
            if b0 == 0xFF:
                break
        elif disk[off : off + 8] == name8 and disk[off + 8 : off + 11] == ext3:
            return i
    if first_free < 0:
        raise RuntimeError("RS-DOS directory full")
    return first_free

for stem, fname in [("hello.bas", "HELLO"), ("pmode1.bas", "PMODE1"), ("pmode2.bas", "PMODE2"), ("pmode4.bas", "PMODE4")]:
    raw = (src_dir / stem).read_text(encoding="ascii").replace("\r\n", "\n").replace("\n", "\r").encode("ascii")
    if not raw.endswith(b"\r"):
        raw += b"\r"
    if len(raw) > 9 * 256:
        raise RuntimeError(f"{stem} exceeds single granule ({len(raw)} bytes)")
    g = alloc_free_gran()
    n_sec = max(1, (len(raw) + 255) // 256)
    last_bytes = len(raw) - (n_sec - 1) * 256
    goff = gran_offset(g)
    disk[goff : goff + 9 * 256] = b"\xff" * (9 * 256)
    disk[goff : goff + len(raw)] = raw
    disk[fat_off + g] = 0xC0 | n_sec
    name8 = fname.encode("ascii").ljust(8, b" ")
    ext3 = b"BAS"
    slot = find_dir_slot(name8, ext3)
    entry = name8 + ext3 + bytes([0x00, 0xFF, g, (last_bytes >> 8) & 0xFF, last_bytes & 0xFF]) + b"\x00" * 16
    disk[dir_off + slot * 32 : dir_off + (slot + 1) * 32] = entry

out_path.write_bytes(disk)
PYEOF
fi

echo "built $OUT"
