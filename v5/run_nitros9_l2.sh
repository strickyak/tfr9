#!/bin/sh
# run_nitros9_l2.sh - Launch NitrOS-9 Level 2 (deep65280) on TFR911H SBC (Hitachi 6309E CPU)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
SHELF=${SHELF:-$(cd "$DIR/../.." && pwd)}
BOOT=${V5_IMG_DEEP_BOOT:-$SHELF/nitros9/recipes/deep65280/deep65280.img}
DISK=${V5_IMG_DEEP_DISK:-$SHELF/nitros9/recipes/deep65280/deep0.dsk}

exec "$DIR/scripts/run-demo.sh" deep65280 85 \
  'on tfr911h run nitros9l2 for deep65280
set Config(flat_img) "/pc/deep65280.img"
set Config(drive0) "/pc/deep0.dsk"
set Config(drive1) ""
menu store Config
bye' \
  "$BOOT:deep65280.img" "$DISK:deep0.dsk" -- "$@"
