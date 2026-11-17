#!/bin/sh
# tether.sh -- portable launcher for the pre-compiled or built tether binary (POSIX sh).
# Examples:
#   ./tether.sh                              # interactive TCL> shell on TFR/911H
#   ./tether.sh -bootmode=27                 # enter TCL> skipping /rc/init.tcl
#   ./tether.sh --just-get-label             # print the board's flash label
#   ./tether.sh -wire=/dev/ttyACM0 ...       # target a specific serial port
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")

is_offline=0
is_just=0
has_wire=0
has_pc=0

for arg in "$@"; do
  case "$arg" in
    -h|--help|-exit|--exit|-just-label-data|--just-label-data|-just-label-data=*|--just-label-data=*)
      is_offline=1
      ;;
    -just-*|--just-*)
      is_just=1
      ;;
    -wire|-wire=*|--wire|--wire=*)
      has_wire=1
      ;;
    -pc|-pc=*|--pc|--pc=*)
      has_pc=1
      ;;
  esac
done

if [ "$is_offline" -eq 1 ]; then
  exec "$TETHER" "$@"
fi

wire_arg=""
if [ "$has_wire" -eq 0 ]; then
  if [ -n "${WIRE:-}" ]; then
    wire_arg=$WIRE
  else
    wire_arg=$("$DIR/scripts/find-board.sh" b=tfr9 v=911h 2>/dev/null || true)
  fi
fi

if [ "$is_just" -eq 1 ]; then
  if [ -n "$wire_arg" ]; then
    exec "$TETHER" -wire="$wire_arg" "$@"
  fi
  exec "$TETHER" "$@"
fi

pc_dir=""
if [ "$has_pc" -eq 0 ]; then
  pc_dir="$DIR/build/pc"
  mkdir -p "$pc_dir"
fi

if [ -t 0 ]; then
  echo "Connecting to TFR/911H (type 'bye' at TCL> to start 6309; ^C quits tether; '^J reset ^J' fixes terminal)." >&2
fi

if [ -n "$wire_arg" ] && [ -n "$pc_dir" ]; then
  exec "$TETHER" -wire="$wire_arg" -pc "$pc_dir" "$@"
elif [ -n "$wire_arg" ]; then
  exec "$TETHER" -wire="$wire_arg" "$@"
elif [ -n "$pc_dir" ]; then
  exec "$TETHER" -pc "$pc_dir" "$@"
else
  exec "$TETHER" "$@"
fi
