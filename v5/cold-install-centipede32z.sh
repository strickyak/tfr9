#!/bin/sh
# cold-install-centipede32z.sh -- Cold Install for a Centipede 32z board (POSIX sh).
#
# Can be run either:
#   - While the new board is in RP2350 BOOTSEL mode (copies centipede32z.uf2 automatically if found), OR
#   - After Step 1 (holding BOOTSEL while plugging in USB and copying centipede32z.uf2 onto the RP2350 USB drive).
#
# This script:
#   1. Copies centipede32z.uf2 if an RP2350 BOOTSEL drive is present and no serial port is up yet,
#   2. Restarts the board into clean Phase-1 TCL> mode (-just-restart=27, skipping any foreign /rc scripts),
#   3. Formats a clean 11 MB LittleFS partition (-just-reformat),
#   4. Writes the board's Flash Label at 0x10FFF000 (-just-set-label="p=1,b=centipede,v=32z,mhz=250,id=...")
#   5. Verifies board discovery by label via scripts/find-board.sh.
#
# Usage:
#   ./cold-install-centipede32z.sh --id=<short-id> [--name=<board-name>] [--wire=/dev/ttyACM0] [--uf2=path] [--force]
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")

BOARD_ID=""
BOARD_NAME=""
WIRE_ARG=${WIRE:-}
UF2_ARG=""
FORCE=0

usage() {
  cat >&2 << 'EOF'
Usage: ./cold-install-centipede32z.sh --id=<short-id> [--name=<board-name>] [--wire=/dev/...] [--uf2=path] [--force]

Step 1 (BOOTSEL, once on a blank board):
  Hold BOOTSEL while plugging the Centipede 32z into USB (or hold BOOTSEL + press RESET),
  and copy centipede32z.uf2 onto the mounted RP2350 USB drive (or let this script copy it
  if the RP2350 drive is already present).  Wait ~3 seconds for the board to reboot.

Step 2 (this script — works over USB with or without a CoCo attached):
  Formats the 11 MB LittleFS flash storage and writes the board's flash label at 0x10FFF000:
    ./cold-install-centipede32z.sh --id=z11 --name=MyCentipede

Options:
  --id=<short-id>      Required short board identifier (e.g. z11)
  --name=<board-name>  Optional human-readable board name
  --wire=/dev/...      Explicit serial device (required if multiple boards are plugged in)
  --uf2=path           Path to centipede32z.uf2 (if board is still in BOOTSEL mode)
  --force              Reformat LittleFS and overwrite label even if board already has a label
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
    --uf2=*)
      UF2_ARG=${1#--uf2=}
      shift
      ;;
    --uf2)
      [ "$#" -ge 2 ] || { usage; exit 2; }
      UF2_ARG=$2
      shift 2
      ;;
    --force|-f)
      FORCE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "cold-install-centipede32z.sh: unknown option '$1'" >&2
      usage
      exit 2
      ;;
  esac
done

if [ -z "$BOARD_ID" ]; then
  echo "error: --id=<short-id> is required (for example: --id=z11)" >&2
  usage
  exit 2
fi

case "$BOARD_ID$BOARD_NAME" in
  *,*|*=*|*" "*|*"	"*)
    echo "error: --id and --name must not contain spaces, commas, or '='." >&2
    exit 2
    ;;
esac

LABEL="p=1,b=centipede,v=32z,mhz=250,id=$BOARD_ID"
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

find_bootsel_mount() {
  for m in /Volumes/RP2350 /media/*/RP2350 /run/media/*/*/RP2350 /run/media/*/RP2350 /mnt/RP2350; do
    if [ -d "$m" ] && [ -f "$m/INFO_UF2.TXT" ]; then
      printf '%s\n' "$m"
      return 0
    fi
  done
  return 1
}

try_flash_bootsel_if_present() {
  if [ -e /dev/disk/by-label/RP2350 ] && command -v udisksctl >/dev/null 2>&1; then
    if ! mount 2>/dev/null | grep -q "RP2350"; then
      udisksctl mount -b /dev/disk/by-label/RP2350 >/dev/null 2>&1 || true
      sleep 1
    fi
  fi
  MNT=$(find_bootsel_mount || true)
  [ -n "$MNT" ] || return 1

  if [ -n "$UF2_ARG" ]; then
    UF2=$UF2_ARG
  elif [ -f "$DIR/centipede32z.uf2" ]; then
    UF2="$DIR/centipede32z.uf2"
  elif [ -f "$DIR/build/centipede32z.uf2" ]; then
    UF2="$DIR/build/centipede32z.uf2"
  else
    echo "error: RP2350 BOOTSEL drive found at $MNT, but centipede32z.uf2 not found (pass --uf2=... or run: make centipede32z)" >&2
    exit 1
  fi

  echo "=== Step 1: Copying $UF2 -> $MNT/centipede32z.uf2 ==="
  cp "$UF2" "$MNT/centipede32z.uf2"
  sync 2>/dev/null || true

  tries=0
  while [ "$tries" -lt 15 ]; do
    sleep 1
    for d in /dev/ttyACM* /dev/cu.usbmodem*; do
      if [ -e "$d" ]; then
        sleep 1
        return 0
      fi
    done
    tries=$((tries + 1))
  done
  return 0
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
    if try_flash_bootsel_if_present; then
      for d in /dev/ttyACM* /dev/cu.usbmodem*; do
        [ -e "$d" ] || continue
        devs="$devs $d"
        count=$((count + 1))
      done
    fi
  fi
  if [ "$count" -eq 0 ]; then
    echo "error: no USB serial Pico device (/dev/ttyACM* or /dev/cu.usbmodem*) found." >&2
    echo "Complete Step 1 first: hold BOOTSEL while plugging in USB, copy centipede32z.uf2 onto the RP2350 drive, and wait 3 seconds." >&2
    exit 1
  fi
  if [ "$count" -gt 1 ]; then
    echo "error: multiple USB serial devices found:$devs" >&2
    echo "Detected board labels:" >&2
    "$DIR/scripts/find-board.sh" >&2 || true
    echo "Re-run with --wire=/dev/... to select the target Centipede 32z explicitly." >&2
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

EXISTING_LABEL=$("$TETHER" -n -bind="" -wire="$WIRE_ARG" -just-get-label </dev/null 2>/dev/null || true)
case "$EXISTING_LABEL" in
  p=1,*)
    if [ "$FORCE" -ne 1 ]; then
      echo "error: board on $WIRE_ARG already has a flash label:" >&2
      echo "  $EXISTING_LABEL" >&2
      echo "Use ./upgrade-centipede32z.sh to upgrade firmware while preserving LittleFS & label," >&2
      echo "or re-run with --force to reformat LittleFS and overwrite the label." >&2
      exit 1
    fi
    ;;
esac

echo "=== Cold Install Target: $WIRE_ARG ==="
echo "=== 1/3 Restarting into clean TCL> (bootmode 27) ==="
"$TETHER" -n -bind="" -wire="$WIRE_ARG" -just-restart=27 </dev/null

echo "=== 2/3 Formatting 11 MB LittleFS flash storage ==="
"$TETHER" -n -bind="" -wire="$WIRE_ARG" -just-reformat </dev/null

echo "=== 3/3 Writing Flash Label at 0x10FFF000: $LABEL ==="
"$TETHER" -n -bind="" -wire="$WIRE_ARG" -just-set-label="$LABEL" </dev/null

FOUND=$("$DIR/scripts/find-board.sh" b=centipede v=32z "id=$BOARD_ID" || true)
if [ -z "$FOUND" ]; then
  echo "error: board wrote label, but find-board.sh did not locate b=centipede v=32z id=$BOARD_ID" >&2
  exit 1
fi

echo "=== Cold Install Complete! ==="
echo "  Device: $FOUND"
echo "  Label:  $("$TETHER" -n -bind="" -wire="$FOUND" -just-get-label </dev/null)"
echo "Try running: ./run-centipede32z-coco2-basic.sh  or  ./run-centipede32z-coco3-basic.sh"
