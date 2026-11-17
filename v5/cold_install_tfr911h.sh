#!/bin/sh
# cold_install_tfr911h.sh -- Cold Install Step 2 for a TFR/911H board (POSIX sh).
#
# Run AFTER Step 1 (holding FLASH + pressing RESET and copying tfr911h.uf2 onto the RP2350 USB drive).
# This script:
#   1. Restarts the board into clean Phase-1 TCL> mode (-just-restart=27, skipping any foreign /rc scripts),
#   2. Formats a clean 11 MB LittleFS partition (-just-reformat),
#   3. Writes the board's Flash Label at 0x10FFF000 (-just-set-label="p=1,b=tfr9,v=911h,mhz=250,id=...")
#   4. Verifies board discovery by label via scripts/find-board.sh.
#
# Usage:
#   ./cold_install_tfr911h.sh --id=<short-id> [--name=<board-name>] [--wire=/dev/ttyACM0]
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")

BOARD_ID=""
BOARD_NAME=""
WIRE_ARG=${WIRE:-}

usage() {
  cat >&2 << 'EOF'
Usage: ./cold_install_tfr911h.sh --id=<short-id> [--name=<board-name>] [--wire=/dev/...]

Step 1 (manual BOOTSEL, once):
  Hold FLASH on the TFR/911H, press & release RESET, release FLASH, and copy tfr911h.uf2
  onto the mounted RP2350 USB drive.  Wait ~3 seconds for the board to reboot.

Step 2 (this script):
  Formats the 11 MB LittleFS flash storage and writes the board's flash label at 0x10FFF000:
    ./cold_install_tfr911h.sh --id=t1 --name=MyTFR
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --id=*)
      BOARD_ID=${1#--id=}
      shift
      ;;
    --id)
      [ "$#" -ge 2 ] || { usage; exit 2; }
      BOARD_ID=$2
      shift 2
      ;;
    --name=*)
      BOARD_NAME=${1#--name=}
      shift
      ;;
    --name)
      [ "$#" -ge 2 ] || { usage; exit 2; }
      BOARD_NAME=$2
      shift 2
      ;;
    --wire=*|-wire=*)
      WIRE_ARG=${1#*=}
      shift
      ;;
    --wire|-wire)
      [ "$#" -ge 2 ] || { usage; exit 2; }
      WIRE_ARG=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "cold_install_tfr911h.sh: unknown option '$1'" >&2
      usage
      exit 2
      ;;
  esac
done

if [ -z "$BOARD_ID" ]; then
  echo "error: --id=<short-id> is required (for example: --id=t1)" >&2
  usage
  exit 2
fi

case "$BOARD_ID$BOARD_NAME" in
  *,*|*=*|*" "*|*"	"*)
    echo "error: --id and --name must not contain spaces, commas, or '='." >&2
    exit 2
    ;;
esac

LABEL="p=1,b=tfr9,v=911h,mhz=250,id=$BOARD_ID"
if [ -n "$BOARD_NAME" ]; then
  LABEL="$LABEL,name=$BOARD_NAME"
fi

# Validate the label string offline before touching any hardware.
TMP_UF2=$(mktemp "${TMPDIR:-/tmp}/label_check.XXXXXX")
trap 'rm -f "$TMP_UF2"' EXIT INT TERM
"$TETHER" --just-label-data="$LABEL" --just-label-filename="$TMP_UF2" >/dev/null
rm -f "$TMP_UF2"

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

if [ -z "$WIRE_ARG" ]; then
  devs=""
  count=0
  for d in /dev/ttyACM* /dev/cu.usbmodem*; do
    [ -e "$d" ] || continue
    devs="$devs $d"
    count=$((count + 1))
  done
  if [ "$count" -eq 0 ]; then
    echo "error: no USB serial Pico device (/dev/ttyACM* or /dev/cu.usbmodem*) found." >&2
    echo "Complete Step 1 first: hold FLASH + press RESET, copy tfr911h.uf2 onto the RP2350 drive, and wait 3 seconds." >&2
    exit 1
  fi
  if [ "$count" -gt 1 ]; then
    echo "error: multiple USB serial devices found:$devs" >&2
    echo "Detected board labels:" >&2
    "$DIR/scripts/find-board.sh" >&2 || true
    echo "Re-run with --wire=/dev/... to select the target TFR/911H explicitly." >&2
    exit 1
  fi
  WIRE_ARG=$(byid_path "${devs# }")
else
  if [ ! -e "$WIRE_ARG" ]; then
    echo "error: serial device '$WIRE_ARG' does not exist" >&2
    exit 1
  fi
  WIRE_ARG=$(byid_path "$WIRE_ARG")
fi

echo "=== Cold Install Target: $WIRE_ARG ==="
echo "=== 1/3 Restarting into clean TCL> (bootmode 27) ==="
"$TETHER" -n -bind="" -wire="$WIRE_ARG" -just-restart=27 </dev/null

echo "=== 2/3 Formatting 11 MB LittleFS flash storage ==="
"$TETHER" -n -bind="" -wire="$WIRE_ARG" -just-reformat </dev/null

echo "=== 3/3 Writing Flash Label at 0x10FFF000: $LABEL ==="
"$TETHER" -n -bind="" -wire="$WIRE_ARG" -just-set-label="$LABEL" </dev/null

FOUND=$("$DIR/scripts/find-board.sh" b=tfr9 v=911h "id=$BOARD_ID" || true)
if [ -z "$FOUND" ]; then
  echo "error: board wrote label, but find-board.sh did not locate b=tfr9 v=911h id=$BOARD_ID" >&2
  exit 1
fi

echo "=== Cold Install Complete! ==="
echo "  Device: $FOUND"
echo "  Label:  $("$TETHER" -n -bind="" -wire="$FOUND" -just-get-label </dev/null)"
echo "Try running: ./run_tfr911h_turbo9os.sh  or  ./run_tfr911h_jamesbrown.sh"
