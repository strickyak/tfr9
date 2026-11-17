#!/bin/sh
# run_tfr911h_gep9big.sh - Launch Hatvan OS (gep9big / EMBIGGEN 2-level MMU) on TFR911H SBC
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
if [ -n "${V5_IMG_HATVAN_DIR:-}" ]; then
  HB=$V5_IMG_HATVAN_DIR
elif [ -d "$DIR/images/hatvan" ]; then
  HB="$DIR/images/hatvan"
else
  HB="$HOME/github.com/strickyak/hatvan-os/build"
fi

exec "$DIR/scripts/run-tfr911h-demo.sh" gep9big 9041 \
  'on tfr911h run hatvan for gep9big
set Config(flat_img) "/pc/kernel_6809.decb"
set Config(task1) "/pc/rbf_6809.decb"
set Config(task2) "/pc/procfs_6809.decb"
set Config(drive0) "/pc/hatvan0.dsk"
set Config(drive1) ""
set Config(drive2) ""
set Config(drive3) ""
menu store Config
bye' \
  "$HB/kernel_6809.decb:kernel_6809.decb" \
  "$HB/rbf_6809.decb:rbf_6809.decb" \
  "$HB/procfs_6809.decb:procfs_6809.decb" \
  "$HB/disk0.dsk:hatvan0.dsk" \
  -- "$@"
