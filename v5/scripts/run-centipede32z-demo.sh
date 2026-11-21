#!/bin/sh
# run-centipede32z-demo.sh -- shared launcher for v5/run-centipede32z-*.sh demo scripts on Centipede 32z.
# Usage: run-centipede32z-demo.sh <name> <tcl_cmd> [-- <extra tether args...>]
set -eu

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <name> <tcl_cmd> [-- <tether args...>]" >&2
  exit 2
fi

NAME=$1
TCL_CMD=$2
shift 2

if [ "$#" -gt 0 ] && [ "$1" = "--" ]; then
  shift
fi

DIR=$(cd "$(dirname "$0")/.." && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")

PC_DIR="$DIR/build/demo-pc/$NAME"
mkdir -p "$PC_DIR"

prev_is_wire=0
for arg in "$@"; do
  if [ "$prev_is_wire" -eq 1 ]; then
    WIRE=$arg
    prev_is_wire=0
    continue
  fi
  case "$arg" in
    -wire|--wire) prev_is_wire=1 ;;
    -wire=*|--wire=*) WIRE=${arg#*=} ;;
  esac
done

if [ -z "${WIRE:-}" ]; then
  WIRE=$("$DIR/scripts/find-board.sh" b=centipede v=32z 2>/dev/null || true)
fi

if [ -z "${WIRE:-}" ]; then
  echo "error: Centipede 32z board (b=centipede v=32z) not found; plug it in or pass -wire=..." >&2
  exit 1
fi

"$TETHER" -n -bind= -wire="$WIRE" -just-restart=27 </dev/null >/dev/null
sleep 2
"$TETHER" -n -bind= -wire="$WIRE" -pc "$PC_DIR" -just-inject="$TCL_CMD" </dev/null
sleep 2
exec "$TETHER" -centipede -wire="$WIRE" -pc "$PC_DIR" "$@"
