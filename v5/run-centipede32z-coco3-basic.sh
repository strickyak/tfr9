#!/bin/sh
# run-centipede32z-coco3-basic.sh - Launch 128KB CoCo 3 Super Extended Color BASIC 2.1 on Centipede 32z ("Become CoCo 3")
# Mounts Disk 0 (/pc/f0) with OPIL (LOADER.BAS), HELLO.BAS, PMODE1/2/4.BAS, and CoCo 3 hi-res HSCREEN2.BAS & HSCREEN4.BAS.
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)

if [ -n "${V5_IMG_COCO3_BASIC_DISK:-}" ]; then
  DISK=$V5_IMG_COCO3_BASIC_DISK
elif [ -f "$DIR/images/coco3_basic/f0.dsk" ]; then
  DISK="$DIR/images/coco3_basic/f0.dsk"
else
  DISK="$DIR/build/coco3_basic.dsk"
  if [ -f "$DIR/scripts/make-coco3-basic-dsk.sh" ]; then
    if [ ! -f "$DISK" ] || [ -n "$(find "$DIR/os/coco2" "$DIR/os/coco3" "$DIR/scripts/make-coco3-basic-dsk.sh" -type f -newer "$DISK" 2>/dev/null)" ]; then
      sh "$DIR/scripts/make-coco3-basic-dsk.sh" "$DISK"
    fi
  fi
fi

PC_DIR="$DIR/build/demo-pc/coco3_basic"
mkdir -p "$PC_DIR"
DST_F0="$PC_DIR/f0"
if [ ! -f "$DST_F0" ] || [ -n "$(find "$DISK" -newer "$DST_F0" 2>/dev/null)" ]; then
  cp "$DISK" "$DST_F0"
fi

exec "$DIR/scripts/run-centipede32z-demo.sh" coco3_basic \
  'on coco2 run rsbasic for coco3 with e1 with info with rtc; set Config(floppy_pc) 1; set Config(floppy_fd) 0; bye' \
  -- -coco3 "$@"
