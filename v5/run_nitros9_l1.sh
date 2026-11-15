#!/bin/sh
# run_nitros9_l1.sh - Launch NitrOS-9 Level 1 (flat65280) on TFR911H SBC (Hitachi 6309E CPU)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
SHELF=${SHELF:-$(cd "$DIR/../.." && pwd)}
BOOT=${V5_IMG_FLAT_BOOT:-$SHELF/nitros9/recipes/flat65280/flat65280.img}
DISK=${V5_IMG_FLAT_DISK:-$SHELF/nitros9/recipes/flat65280/flat0.dsk}

exec "$DIR/scripts/run-demo.sh" flat65280 84 \
  'on tfr9 run nitros9l1 for flat65280
set Config(flat_img) "/pc/flat65280.img"
set Config(drive0) "/pc/flat0.dsk"
set Config(drive1) ""
menu store Config
bye' \
  "$BOOT:flat65280.img" "$DISK:flat0.dsk" -- "$@"
