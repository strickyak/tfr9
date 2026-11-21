#!/bin/sh
# tether.sh -- universal portable launcher connecting to a TFR/911H or Centipede 32z board (POSIX sh).
# Examples:
#   ./tether.sh                              # interactive TCL> shell on whichever single board is plugged in
#   ./tether.sh --id=z10 -bootmode=27        # enter TCL> skipping /rc/init.tcl on board z10
#   ./tether.sh --just-get-label             # print the board's flash label
#   ./tether.sh -wire=/dev/ttyACM0 ...       # target a specific serial port
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
        echo "tether.sh: --id requires an argument" >&2
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

byid_path() {
  for link in /dev/serial/by-id/*; do
    [ -L "$link" ] || continue
    if [ "$(basename "$(readlink "$link")")" = "$(basename "$1")" ]; then
      printf '%s\n' "$link"
      return
    fi
  done
  printf '%s\n' "$1"
}

wire_arg=""
board_desc="board"
centipede_mode=""

if [ "$has_wire" -eq 0 ]; then
  if [ -n "${WIRE:-}" ]; then
    wire_arg=$WIRE
  elif [ -n "$BOARD_ID" ]; then
    wire_arg=$("$DIR/scripts/find-board.sh" "id=$BOARD_ID" 2>/dev/null || true)
    if [ -z "$wire_arg" ]; then
      echo "error: board with id=$BOARD_ID not found; check ./scripts/find-board.sh or pass -wire=..." >&2
      exit 1
    fi
  else
    board_list=$("$DIR/scripts/find-board.sh" 2>/dev/null || true)
    lcount=0
    first_dev=""
    first_label=""
    if [ -n "$board_list" ]; then
      OLD_IFS=$IFS
      IFS='
'
      for line in $board_list; do
        [ -n "$line" ] || continue
        lcount=$((lcount + 1))
        if [ "$lcount" -eq 1 ]; then
          first_dev=${line%%  *}
          first_label=${line#*  }
        fi
      done
      IFS=$OLD_IFS
    fi

    if [ "$lcount" -eq 1 ]; then
      wire_arg=$(byid_path "$first_dev")
      case ",$first_label," in
        *,b=tfr9,*)
          board_desc="TFR/911H"
          centipede_mode="false"
          ;;
        *,b=centipede,*)
          board_desc="Centipede 32z"
          centipede_mode="true"
          ;;
      esac
    elif [ "$lcount" -gt 1 ]; then
      echo "error: multiple labeled boards are connected:" >&2
      printf '%s\n' "$board_list" | sed 's/^/  /' >&2
      echo "Use ./tether-tfr911h.sh, ./tether-centipede32z.sh, ./tether.sh --id=<id>, or -wire=/dev/..." >&2
      exit 1
    else
      # Fallback for a single unlabeled board during cold bring-up.
      raw_devs=""
      rcount=0
      for d in /dev/ttyACM* /dev/cu.usbmodem*; do
        [ -e "$d" ] || continue
        raw_devs="$raw_devs $d"
        rcount=$((rcount + 1))
      done
      if [ "$rcount" -eq 1 ]; then
        wire_arg=$(byid_path "${raw_devs# }")
      elif [ "$rcount" -gt 1 ]; then
        echo "error: multiple unlabeled USB serial devices found:$raw_devs" >&2
        echo "Pass -wire=/dev/... explicitly." >&2
        exit 1
      else
        echo "error: no TFR/911H or Centipede 32z USB serial device found." >&2
        exit 1
      fi
    fi
  fi
fi

if [ -n "$wire_arg" ] && [ -z "$centipede_mode" ] && [ "$has_centipede" -eq 0 ] && [ "$is_just" -eq 0 ]; then
  probed_label=$("$TETHER" -n -bind="" -wire="$wire_arg" -just-get-label </dev/null 2>/dev/null || true)
  case ",$probed_label," in
    *,b=tfr9,*)
      board_desc="TFR/911H"
      centipede_mode="false"
      ;;
    *,b=centipede,*)
      board_desc="Centipede 32z"
      centipede_mode="true"
      ;;
  esac
fi

if [ "$has_centipede" -eq 0 ] && [ -n "$centipede_mode" ]; then
  set -- -centipede="$centipede_mode" "$@"
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
  echo "Connecting to $board_desc${wire_arg:+ on $wire_arg} (type 'bye' at TCL> to start CPU; ^C quits tether; '^J reset ^J' fixes terminal)." >&2
fi

exec "$TETHER" "$@"
