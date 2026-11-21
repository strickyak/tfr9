#!/bin/sh
# upgrade_centipede32z.sh -- Upgrade firmware on an already-installed Centipede 32z board (POSIX sh).
#
# Flashes centipede32z.uf2 while preserving both the 11 MB LittleFS flash storage (4..15 MB)
# and the board's Flash Label at 0x10FFF000.
#
# Usage:
#   ./upgrade_centipede32z.sh [--id=<short-id>] [--wire=/dev/...] [--uf2=path/to/centipede32z.uf2]
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")

BOARD_ID=""
WIRE_ARG=${WIRE:-}
UF2_ARG=""

usage() {
  cat >&2 << 'EOF'
Usage: ./upgrade_centipede32z.sh [--id=<short-id>] [--wire=/dev/...] [--uf2=path/to/centipede32z.uf2]

Upgrades centipede32z.uf2 over USB (via -just-reflash) without touching your 11 MB LittleFS
files or your board's 0x10FFF000 flash label.

(If your OS does not auto-mount the RP2350 BOOTSEL USB drive, hold BOOTSEL while plugging in USB
and copy centipede32z.uf2 onto the RP2350 drive manually — LittleFS and the label are still preserved.)
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
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "upgrade_centipede32z.sh: unknown option '$1'" >&2
      usage
      exit 2
      ;;
  esac
done

if [ -n "$UF2_ARG" ]; then
  UF2=$UF2_ARG
elif [ -f "$DIR/centipede32z.uf2" ]; then
  UF2="$DIR/centipede32z.uf2"
elif [ -f "$DIR/build/centipede32z.uf2" ]; then
  UF2="$DIR/build/centipede32z.uf2"
else
  echo "error: centipede32z.uf2 not found in $DIR (pass --uf2=... or run: make centipede32z)" >&2
  exit 1
fi

if [ -z "$WIRE_ARG" ]; then
  if [ -n "$BOARD_ID" ]; then
    WIRE_ARG=$("$DIR/scripts/find-board.sh" b=centipede v=32z "id=$BOARD_ID" || true)
  else
    WIRE_ARG=$("$DIR/scripts/find-board.sh" b=centipede v=32z || true)
  fi
  if [ -z "$WIRE_ARG" ]; then
    echo "error: labeled Centipede 32z board (b=centipede v=32z${BOARD_ID:+ id=$BOARD_ID}) not found." >&2
    echo "Plug in the Centipede 32z USB port or pass --wire=/dev/..." >&2
    exit 1
  fi
fi

BEFORE_LABEL=$("$TETHER" -n -bind="" -wire="$WIRE_ARG" -just-get-label </dev/null || true)
echo "=== Upgrading Centipede 32z on $WIRE_ARG ==="
[ -n "$BEFORE_LABEL" ] && echo "  Existing label: $BEFORE_LABEL"
echo "  Firmware UF2:   $UF2"

# On headless Linux systems without a desktop automounter, mount /dev/disk/by-label/RP2350 via udisksctl if available.
MNT_PID=""
cleanup_mnt() {
  if [ -n "$MNT_PID" ]; then
    kill "$MNT_PID" 2>/dev/null || true
    wait "$MNT_PID" 2>/dev/null || true
  fi
}
trap cleanup_mnt EXIT INT TERM

if command -v udisksctl >/dev/null 2>&1 && [ -d /dev/disk/by-label ]; then
  (
    i=0
    while [ "$i" -lt 40 ]; do
      if [ -e /dev/disk/by-label/RP2350 ]; then
        if ! mount 2>/dev/null | grep -q "RP2350"; then
          udisksctl mount -b /dev/disk/by-label/RP2350 >/dev/null 2>&1 || true
        fi
      fi
      sleep 1
      i=$((i + 1))
    done
  ) &
  MNT_PID=$!
fi

"$TETHER" -n -bind="" -wire="$WIRE_ARG" -just-reflash="$UF2" </dev/null
cleanup_mnt
MNT_PID=""

AFTER_WIRE=""
tries=0
while [ -z "$AFTER_WIRE" ] && [ "$tries" -lt 10 ]; do
  if [ -n "$BOARD_ID" ]; then
    AFTER_WIRE=$("$DIR/scripts/find-board.sh" b=centipede v=32z "id=$BOARD_ID" 2>/dev/null || true)
  else
    AFTER_WIRE=$("$DIR/scripts/find-board.sh" b=centipede v=32z 2>/dev/null || true)
  fi
  [ -n "$AFTER_WIRE" ] || sleep 1
  tries=$((tries + 1))
done

if [ -n "$AFTER_WIRE" ]; then
  AFTER_LABEL=$("$TETHER" -n -bind="" -wire="$AFTER_WIRE" -just-get-label </dev/null || true)
  echo "=== Upgrade Complete! ==="
  echo "  Device: $AFTER_WIRE"
  echo "  Label:  $AFTER_LABEL"
else
  echo "=== Firmware copied; waiting for board serial port (check ./scripts/find-board.sh) ==="
fi
