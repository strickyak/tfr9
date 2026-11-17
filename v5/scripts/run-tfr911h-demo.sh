#!/bin/sh
# run-tfr911h-demo.sh -- shared launcher for v5/run_tfr911h_*.sh OS demo scripts on TFR911H.
# TFR911H demo scripts reserve bootmode numbers 9000..9100 (avoiding /rc/modeN.tcl clashes).
# Usage: run-tfr911h-demo.sh <name> <bootmode> <tcl_body> [src:dst ...] [-- <extra tether args...>]
set -eu

if [ "$#" -lt 3 ]; then
  echo "usage: $0 <name> <bootmode> <tcl_body> [src:dst ...] [-- <tether args...>]" >&2
  exit 2
fi

NAME=$1
BOOTMODE=$2
TCL_BODY=$3
shift 3

DIR=$(cd "$(dirname "$0")/.." && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")

PC_DIR="$DIR/build/demo-pc/$NAME"
mkdir -p "$PC_DIR"

# Stage regular files (tether refuses symlinks under -pc).
while [ "$#" -gt 0 ]; do
  case "$1" in
    --) shift; break ;;
  esac
  src=${1%%:*}
  dst="$PC_DIR/${1#*:}"
  if [ -z "$src" ] || [ ! -f "$src" ]; then
    echo "error: missing image file: ${src:-<unset>} (for $1)" >&2
    exit 1
  fi
  if [ ! -f "$dst" ] || [ -n "$(find "$src" -newer "$dst" 2>/dev/null)" ]; then
    cp "$src" "$dst"
  fi
  shift
done

printf '%s\n' "$TCL_BODY" > "$PC_DIR/mode${BOOTMODE}.tcl"

# Resolve TFR911H wire unless caller passed -wire=... or set WIRE.
has_wire=0
for arg in "$@"; do
  case "$arg" in
    -wire|-wire=*|--wire|--wire=*) has_wire=1 ;;
  esac
done

if [ "$has_wire" -eq 0 ]; then
  if [ -z "${WIRE:-}" ]; then
    WIRE=$("$DIR/scripts/find-board.sh" b=tfr9 v=911h 2>/dev/null || true)
  fi
  if [ -z "${WIRE:-}" ]; then
    echo "error: TFR911H board (b=tfr9 v=911h) not found; plug it in or pass -wire=..." >&2
    exit 1
  fi
  exec "$TETHER" -wire="$WIRE" -pc "$PC_DIR" -bootmode="$BOOTMODE" "$@"
else
  exec "$TETHER" -pc "$PC_DIR" -bootmode="$BOOTMODE" "$@"
fi
