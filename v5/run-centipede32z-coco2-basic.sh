#!/bin/sh
# run-centipede32z-coco2-basic.sh - Launch 64KB CoCo 2 Disk Extended Color BASIC 1.1 on Centipede 32z
# Mounts Disk 0 (/pc/f0) with OPIL (LOADER.BAS) + HELLO.BAS, PMODE1.BAS, PMODE2.BAS, PMODE4.BAS.
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)

if [ -n "${V5_IMG_COCO2_DISK:-}" ]; then
  DISK=$V5_IMG_COCO2_DISK
elif [ -f "$DIR/images/coco2_basic/f0.dsk" ]; then
  DISK="$DIR/images/coco2_basic/f0.dsk"
else
  DISK="$DIR/build/coco2_basic.dsk"
  if [ -f "$DIR/scripts/make-coco2-basic-dsk.sh" ]; then
    if [ ! -f "$DISK" ] || [ -n "$(find "$DIR/os/coco2" "$DIR/scripts/make-coco2-basic-dsk.sh" -type f -newer "$DISK" 2>/dev/null)" ]; then
      sh "$DIR/scripts/make-coco2-basic-dsk.sh" "$DISK"
    fi
  fi
fi

PC_DIR="$DIR/build/demo-pc/coco2_basic"
mkdir -p "$PC_DIR"
DST_F0="$PC_DIR/f0"
if [ ! -f "$DST_F0" ] || [ -n "$(find "$DISK" -newer "$DST_F0" 2>/dev/null)" ]; then
  cp "$DISK" "$DST_F0"
fi

exec "$DIR/scripts/run-centipede32z-demo.sh" coco2_basic \
  'on coco2 run rsbasic for coco2 with e1 with info with rtc; set Config(floppy_pc) 1; set Config(floppy_fd) 0; bye' \
  -- "$@"
