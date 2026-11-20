#!/bin/sh
# run_tfr911h_deep65280_nitros9_l2.sh - Launch NitrOS-9 Level 2 (deep65280) on TFR911H SBC (Hitachi 6309E CPU)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
if [ -n "${V5_IMG_DEEP_BOOT:-}" ]; then
  BOOT=$V5_IMG_DEEP_BOOT
elif [ -f "$DIR/images/deep65280/deep65280.img" ]; then
  BOOT="$DIR/images/deep65280/deep65280.img"
else
  SHELF=${SHELF:-$(cd "$DIR/../.." && pwd)}
  BOOT="$SHELF/nitros9/recipes/deep65280/deep65280.img"
fi

if [ -n "${V5_IMG_DEEP_DISK:-}" ]; then
  DISK=$V5_IMG_DEEP_DISK
elif [ -f "$DIR/images/deep65280/deep0.dsk" ]; then
  DISK="$DIR/images/deep65280/deep0.dsk"
else
  SHELF=${SHELF:-$(cd "$DIR/../.." && pwd)}
  DISK="$SHELF/nitros9/recipes/deep65280/deep0.dsk"
fi

exec "$DIR/scripts/run-tfr911h-demo.sh" deep65280 9032 \
  'on tfr911h run nitros9l2 for deep65280
set Config(flat_img) "/pc/deep65280.img"
set Config(drive0) "/pc/deep0.dsk"
set Config(drive1) ""
menu store Config
bye' \
  "$BOOT:deep65280.img" "$DISK:deep0.dsk" -- "$@"
