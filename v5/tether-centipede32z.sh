#!/bin/sh
# tether-centipede32z.sh -- portable launcher targeting a Centipede 32z board (POSIX sh).
# Examples:
#   ./tether-centipede32z.sh                        # interactive TCL> shell on Centipede 32z
#   ./tether-centipede32z.sh --id=z10 -bootmode=27  # enter TCL> skipping /rc/init.tcl on board z10
#   ./tether-centipede32z.sh --just-get-label       # print the Centipede 32z board's flash label
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")

BOARD_ID=""
is_offline=0
is_just=0
has_wire=0
has_pc=0
has_centipede=0

n=$#
i=0
while [ "$i" -lt "$n" ]; do
  arg=$1
  shift
  i=$((i + 1))
  case "$arg" in
    --id=*)
      BOARD_ID=${arg#--id=}
      continue
      ;;
    --id)
      if [ "$i" -ge "$n" ]; then
        echo "tether-centipede32z.sh: --id requires an argument" >&2
        exit 2
      fi
      BOARD_ID=$1
      shift
      i=$((i + 1))
      continue
      ;;
    -h|--help|-exit|--exit|-just-list-session|--just-list-session|-just-list-sessions|--just-list-sessions|-just-label-data|--just-label-data|-just-label-data=*|--just-label-data=*)
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
    -centipede|-centipede=*|--centipede|--centipede=*)
      has_centipede=1
      ;;
  esac
  set -- "$@" "$arg"
done

if [ "$is_offline" -eq 1 ]; then
  exec "$TETHER" "$@"
fi

wire_arg=""
if [ "$has_wire" -eq 0 ]; then
  if [ -n "${WIRE:-}" ]; then
    wire_arg=$WIRE
  elif [ -n "$BOARD_ID" ]; then
    wire_arg=$("$DIR/scripts/find-board.sh" b=centipede v=32z "id=$BOARD_ID" 2>/dev/null || true)
  else
    wire_arg=$("$DIR/scripts/find-board.sh" b=centipede v=32z 2>/dev/null || true)
  fi
  if [ -z "$wire_arg" ]; then
    echo "error: Centipede 32z board (b=centipede v=32z${BOARD_ID:+ id=$BOARD_ID}) not found; plug it in or pass -wire=..." >&2
    exit 1
  fi
fi

if [ "$has_centipede" -eq 0 ]; then
  set -- -centipede=true "$@"
fi
if [ -n "$wire_arg" ]; then
  set -- -wire="$wire_arg" "$@"
fi

if [ "$is_just" -eq 1 ]; then
  exec "$TETHER" "$@"
fi

if [ "$has_pc" -eq 0 ]; then
  pc_dir="$DIR/build/pc"
  mkdir -p "$pc_dir"
  set -- -pc "$pc_dir" "$@"
fi

if [ -t 0 ]; then
  echo "Connecting to Centipede 32z${wire_arg:+ on $wire_arg} (type 'bye' at TCL> to start CoCo 6809; ^C quits tether; '^J reset ^J' fixes terminal)." >&2
fi

exec "$TETHER" "$@"
