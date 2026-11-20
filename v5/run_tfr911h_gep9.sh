#!/bin/sh
# run_tfr911h_gep9.sh - Launch Hatvan OS (gep9) on TFR911H SBC (Hitachi 6309E CPU)
#
# Terminal input modes:
#   (default)           Single-character cbreak mode (target echoes characters)
#   -c, -cooked         Cooked line mode (Linux tty cooks lines; exits after EOF)
#   -r, -readline       Readline mode (tether cooks lines with bash-style history)
#   -n, -nostdin        Disable stdin reading (passes -n to tether)
set -e

case "${1:-}" in
  -h|--help)
    cat <<'EOF'
Usage: sh run_tfr911h_gep9.sh [mode] [extra tether flags...]

Terminal modes:
  (default)           Single-char cbreak mode (Hatvan OS echoes characters)
  -c, -cooked         Cooked line input mode (host tty cooks lines; exits on EOF)
  -r, -readline       Readline mode in tether (line editing + Up/Down history)
  -n, -nostdin        Never read stdin (passes -n to tether)
EOF
    exit 0
    ;;
esac

DIR=$(cd "$(dirname "$0")" && pwd)
if [ -n "${V5_IMG_HATVAN_DIR:-}" ]; then
  HB=$V5_IMG_HATVAN_DIR
elif [ -d "$DIR/images/hatvan" ]; then
  HB="$DIR/images/hatvan"
else
  HB="$HOME/github.com/strickyak/hatvan-os/build"
fi

exec "$DIR/scripts/run-tfr911h-demo.sh" gep9 9040 \
  'on tfr911h run hatvan for gep9
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
