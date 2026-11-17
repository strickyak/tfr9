#!/bin/sh
# run_tfr911h_nitros9_l1.sh - Launch NitrOS-9 Level 1 (flat65280) on TFR911H SBC (Hitachi 6309E CPU)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
if [ -n "${V5_IMG_FLAT_BOOT:-}" ]; then
  BOOT=$V5_IMG_FLAT_BOOT
elif [ -f "$DIR/images/flat65280/flat65280.img" ]; then
  BOOT="$DIR/images/flat65280/flat65280.img"
else
  SHELF=${SHELF:-$(cd "$DIR/../.." && pwd)}
  BOOT="$SHELF/nitros9/recipes/flat65280/flat65280.img"
fi

if [ -n "${V5_IMG_FLAT_DISK:-}" ]; then
  DISK=$V5_IMG_FLAT_DISK
elif [ -f "$DIR/images/flat65280/flat0.dsk" ]; then
  DISK="$DIR/images/flat65280/flat0.dsk"
else
  SHELF=${SHELF:-$(cd "$DIR/../.." && pwd)}
  DISK="$SHELF/nitros9/recipes/flat65280/flat0.dsk"
fi

exec "$DIR/scripts/run-tfr911h-demo.sh" flat65280 9031 \
  'on tfr911h run nitros9l1 for flat65280
set Config(flat_img) "/pc/flat65280.img"
set Config(drive0) "/pc/flat0.dsk"
set Config(drive1) ""
menu store Config
bye' \
  "$BOOT:flat65280.img" "$DISK:flat0.dsk" -- "$@"
