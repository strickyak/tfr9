#!/bin/sh
# run_centipede32z_coco2_nitros9_l1.sh - Launch 64KB CoCo 2 Disk Extended Color BASIC 1.1
# with l1_coco.dsk mounted as floppy drive 0 (/pc/f0) on Centipede 32z, so `DOS` boots NitrOS-9 Level 1.
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
SHELF=${SHELF:-$(cd "$DIR/../.." && pwd)}

if [ -n "${V5_IMG_COCO2_L1_FLOPPY:-}" ]; then
  SRC_DSK=$V5_IMG_COCO2_L1_FLOPPY
else
  SRC_DSK="$SHELF/nitros9/recipes/coco/floppy/l1_coco.dsk"
fi

if [ ! -f "$SRC_DSK" ]; then
  echo "error: missing NitrOS-9 Level 1 floppy image: $SRC_DSK" >&2
  exit 1
fi

PC_DIR="$DIR/build/demo-pc/coco2_nitros9_l1"
mkdir -p "$PC_DIR"
DST_F0="$PC_DIR/f0"
if [ ! -f "$DST_F0" ] || [ -n "$(find "$SRC_DSK" -newer "$DST_F0" 2>/dev/null)" ]; then
  cp "$SRC_DSK" "$DST_F0"
fi

exec "$DIR/scripts/run-centipede32z-demo.sh" coco2_nitros9_l1 \
  'on coco2 run rsbasic for coco2 with e1 with info with rtc; set Config(ram_64k) 1; set Config(rom_disk11) 1; set Config(floppy_pc) 1; set Config(floppy_fd) 0; bye' \
  -- "$@"
