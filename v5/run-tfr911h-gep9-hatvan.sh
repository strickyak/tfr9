#!/bin/sh
# run-tfr911h-gep9-hatvan.sh - Launch Hatvan OS (gep9) on TFR911H SBC (Hitachi 6309E CPU)
#
# Terminal input modes:
#   (default)           Single-character cbreak mode (^C -> Hatvan, ^] -> quit tether)
#   -c, -cooked         Cooked line mode (^C -> Hatvan, ^] -> quit tether)
#   -r, -readline       Readline mode with bash-style history (^C -> Hatvan, ^] -> quit tether)
#   -n, -nostdin        Disable stdin reading (^C kills tether via SIGINT)
set -e

case "${1:-}" in
  -h|--help)
    cat <<'EOF'
Usage: sh run-tfr911h-gep9-hatvan.sh [mode] [extra tether flags...]

Terminal modes:
  (default)           Single-char cbreak mode (^C -> Hatvan, ^] -> quit tether)
  -c, -cooked         Cooked line input mode (^C -> Hatvan, ^] -> quit tether)
  -r, -readline       Readline mode in tether with history (^C -> Hatvan, ^] -> quit tether)
  -n, -nostdin        Never read stdin (^C kills tether via SIGINT)
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

is_nostdin=0
for arg in "$@"; do
  case "$arg" in
    -n|-nostdin|--nostdin) is_nostdin=1 ;;
  esac
done

if [ -t 0 ]; then
  if [ "$is_nostdin" -eq 1 ]; then
    echo "Hatvan OS (gep9, -nostdin): stdin disabled; quit tether with ^C ." >&2
  else
    echo "Hatvan OS (gep9): ^C goes to Hatvan; quit tether with ^] ." >&2
  fi
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
  -- -pass_ctrl_c "$@"
