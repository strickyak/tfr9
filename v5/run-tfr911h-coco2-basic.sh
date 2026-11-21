#!/bin/sh
# run-tfr911h-coco2-basic.sh - Launch 64KB CoCo 2 Disk Extended Color BASIC 1.1 on TFR911H (browser VDG screen + keyboard on :8080)
set -eu
DIR=$(cd "$(dirname "$0")" && pwd)

# Detect custom -bind=... flag for the banner hint (default in tether auto-steps :8080..:8099).
bind_addr=""
bind_set=0
prev_is_bind=0
for arg in "$@"; do
  if [ "$prev_is_bind" -eq 1 ]; then
    bind_addr=$arg
    bind_set=1
    prev_is_bind=0
    continue
  fi
  case "$arg" in
    -bind|--bind) prev_is_bind=1 ;;
    -bind=*|--bind=*) bind_addr=${arg#*=}; bind_set=1 ;;
  esac
done

if [ "$bind_set" -eq 0 ]; then
  echo "[coco2_basic: open the http://localhost:808x/ URL printed by tether in your browser for CoCo 2 screen & keyboard]" >&2
else
  case "$bind_addr" in
    "") ;;
    :*) echo "[coco2_basic: open http://localhost${bind_addr} in your browser for CoCo 2 screen & keyboard]" >&2 ;;
    *)  echo "[coco2_basic: open http://${bind_addr} in your browser for CoCo 2 screen & keyboard]" >&2 ;;
  esac
fi

if [ -n "${V5_IMG_COCO2_DISK:-}" ]; then
  DISK=$V5_IMG_COCO2_DISK
elif [ -f "$DIR/images/coco2_basic/f0.dsk" ]; then
  DISK="$DIR/images/coco2_basic/f0.dsk"
else
  DISK="$DIR/build/coco2_basic.dsk"
  if [ -f "$DIR/scripts/make-coco2-basic-dsk.sh" ]; then
    if [ ! -f "$DISK" ] || [ -n "$(find "$DIR/os/coco2" -name '*.bas' -newer "$DISK" 2>/dev/null)" ]; then
      sh "$DIR/scripts/make-coco2-basic-dsk.sh" "$DISK"
    fi
  fi
fi

exec "$DIR/scripts/run-tfr911h-demo.sh" coco2_basic 9060 \
  'on tfr911h run rsbasic for coco2 with e1 with info with rtc
bye' \
  "$DISK:f0" -- "$@"
