#!/bin/sh
# find-board.sh [KEY=VALUE ...] -- print the serial device whose label matches all KEY=VALUE pairs.
# Device names (/dev/ttyACM0, ...) are not stable, so identify boards by their label, e.g.:
#     scripts/find-board.sh b=tfr9 v=911h        # the TFR/911H SBC
#     scripts/find-board.sh b=centipede          # the Centipede-32z cartridge
#     scripts/find-board.sh id=uks               # one particular board
# Prints the device and exits 0 on the first match; prints nothing and exits 1 if none.
# With no args, lists every device with its label (waits for all probes).
# All devices are probed in PARALLEL, so one board that does not answer (a hung board, or a CoCo that
# is busy) cannot delay finding another.  A probe that has not answered after FIND_BOARD_WAIT seconds
# (default 8) is abandoned.  Needs build/tether (make tether).  POSIX sh.
V5=$(cd "$(dirname "$0")/.." && pwd)
TETHER=${TETHER:-$V5/build/tether}
WAIT=${FIND_BOARD_WAIT:-8}
[ -x "$TETHER" ] || { echo "find-board: $TETHER missing (run: make tether)" >&2; exit 2; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/find-board.XXXXXX") || exit 2
pids=
n=0
for d in /dev/ttyACM* /dev/cu.usbmodem*; do
  [ -e "$d" ] || continue
  n=$((n + 1))
  echo "$d" > "$tmp/$n.dev"
  "$TETHER" -n -bind="" -quick-get-label -wire="$d" > "$tmp/$n.out" 2>/dev/null < /dev/null &
  pids="$pids $!"
done
cleanup() { for p in $pids; do kill "$p" 2>/dev/null; done; rm -rf "$tmp"; }
trap cleanup EXIT

i=0
rc=1
while [ $i -lt $((WAIT * 5)) ]; do
  k=1
  while [ $k -le $n ]; do
    label=$(head -1 "$tmp/$k.out" 2>/dev/null)
    case "$label" in
      p=1,*)
        if [ $# -gt 0 ] && [ ! -f "$tmp/$k.done" ]; then
          ok=1
          for kv in "$@"; do
            case ",$label," in *",$kv,"*) ;; *) ok=0 ;; esac
          done
          : > "$tmp/$k.done"
          if [ $ok -eq 1 ]; then cat "$tmp/$k.dev"; exit 0; fi
        fi ;;
      *) ;;           # no label yet
    esac
    k=$((k + 1))
  done
  # a probe is finished when its process has exited
  still=0
  for p in $pids; do kill -0 "$p" 2>/dev/null && still=1; done
  [ $still -eq 0 ] && break
  sleep 0.2 2>/dev/null || sleep 1
  i=$((i + 1))
done
if [ $# -eq 0 ]; then
  k=1
  while [ $k -le $n ]; do
    label=$(head -1 "$tmp/$k.out" 2>/dev/null)
    case "$label" in p=1,*) echo "$(cat "$tmp/$k.dev")  $label"; rc=0 ;; esac
    k=$((k + 1))
  done
fi
exit $rc
