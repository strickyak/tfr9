#!/bin/sh
# run_tfr911h_coco2_basic.sh - Launch 64KB CoCo 2 Disk Extended Color BASIC 1.1 on TFR911H (browser VDG screen + keyboard on :8080)
set -eu
DIR=$(cd "$(dirname "$0")" && pwd)
PC_DIR="$DIR/build/demo-pc/coco2_basic"
mkdir -p "$PC_DIR"

# Detect custom -bind=... flag for the banner hint (default in tether is :8080).
bind_addr=":8080"
prev_is_bind=0
for arg in "$@"; do
  if [ "$prev_is_bind" -eq 1 ]; then
    bind_addr=$arg
    prev_is_bind=0
    continue
  fi
  case "$arg" in
    -bind|--bind) prev_is_bind=1 ;;
    -bind=*|--bind=*) bind_addr=${arg#*=} ;;
  esac
done

case "$bind_addr" in
  "") ;;
  :*) echo "[coco2_basic: open http://localhost${bind_addr} in your browser for CoCo 2 screen & keyboard]" >&2 ;;
  *)  echo "[coco2_basic: open http://${bind_addr} in your browser for CoCo 2 screen & keyboard]" >&2 ;;
esac

if [ -n "${V5_IMG_COCO2_DISK:-}" ]; then
  exec "$DIR/scripts/run-tfr911h-demo.sh" coco2_basic 9060 \
    'on tfr911h run rsbasic for coco2 with e1 with info with rtc
bye' \
    "$V5_IMG_COCO2_DISK:f0" -- "$@"
fi

# Auto-create a blank formatted 35-track RS-DOS floppy in Drive 0 (630 * 256 = 161280 bytes of 0xFF) if none exists.
if [ ! -f "$PC_DIR/f0" ]; then
  echo "[coco2_basic: creating blank 35-track RS-DOS disk at $PC_DIR/f0]" >&2
  tr '\000' '\377' < /dev/zero | dd bs=256 count=630 of="$PC_DIR/f0" 2>/dev/null
fi

exec "$DIR/scripts/run-tfr911h-demo.sh" coco2_basic 9060 \
  'on tfr911h run rsbasic for coco2 with e1 with info with rtc
bye' \
  -- "$@"
