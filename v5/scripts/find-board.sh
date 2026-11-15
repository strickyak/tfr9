#!/bin/sh
# find-board.sh [KEY=VALUE ...] -- print the serial device whose label matches all KEY=VALUE pairs.
# Device names (/dev/ttyACM0, ...) are not stable, so identify boards by their label, e.g.:
#     scripts/find-board.sh b=tfr9 v=911h        # the TFR/911H SBC
#     scripts/find-board.sh b=centipede          # the Centipede-32z cartridge
#     scripts/find-board.sh id=uks               # one particular board
# Prints the device and exits 0 on the first match; prints nothing and exits 1 if none.
# With no args, lists every device with its label.  Needs build/tether (make tether).  POSIX sh.
V5=$(cd "$(dirname "$0")/.." && pwd)
TETHER=${TETHER:-$V5/build/tether}
[ -x "$TETHER" ] || { echo "find-board: $TETHER missing (run: make tether)" >&2; exit 2; }
found=1
for d in /dev/ttyACM* /dev/cu.usbmodem*; do
  [ -e "$d" ] || continue
  label=$("$TETHER" -bind="" -quick-get-label -wire="$d" 2>/dev/null | head -1)
  [ -n "$label" ] || continue
  if [ $# -eq 0 ]; then echo "$d  $label"; found=0; continue; fi
  ok=1
  for kv in "$@"; do
    case ",$label," in *",$kv,"*) ;; *) ok=0 ;; esac
  done
  if [ $ok -eq 1 ]; then echo "$d"; exit 0; fi
done
[ $# -eq 0 ] && exit $found
exit 1
