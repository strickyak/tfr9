#!/bin/sh
# diagnose_fujinet.sh [--centipede | --wire=DEV] [SUBCOMMAND ...]
#
# Restart a TFR/911H (default) or Centipede 32z into the TCL> shell (boot mode 27)
# and run the firmware's `fujinet` diagnostic command (default: `fujinet diag`).
#
# Examples:
#   sh scripts/diagnose_fujinet.sh
#   sh scripts/diagnose_fujinet.sh pins
#   sh scripts/diagnose_fujinet.sh scan
#   sh scripts/diagnose_fujinet.sh status
#   sh scripts/diagnose_fujinet.sh sniff 5000
#   sh scripts/diagnose_fujinet.sh --centipede diag
set -e

V5=$(cd "$(dirname "$0")/.." && pwd)
TETHER=${TETHER:-$V5/build/tether}
WIRE=${TFR_DEV:-}
BOARD_QUERY="b=tfr9 v=911h"

while [ $# -gt 0 ]; do
  case "$1" in
    --centipede)
      BOARD_QUERY="b=centipede"
      shift
      ;;
    --tfr911h)
      BOARD_QUERY="b=tfr9 v=911h"
      shift
      ;;
    --wire=*)
      WIRE=${1#--wire=}
      shift
      ;;
    --help|-h)
      sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

if [ ! -x "$TETHER" ]; then
  echo "diagnose_fujinet: $TETHER not found (run: make tether)" >&2
  exit 2
fi

if [ -z "$WIRE" ]; then
  # shellcheck disable=SC2086
  WIRE=$(sh "$V5/scripts/find-board.sh" $BOARD_QUERY) || {
    echo "diagnose_fujinet: no board matching '$BOARD_QUERY' found" >&2
    exit 1
  }
fi

if [ $# -eq 0 ]; then
  CMD="fujinet diag"
else
  CMD="fujinet $*"
fi

echo "=== Board: $WIRE ===" >&2
"$TETHER" -n -bind="" -wire="$WIRE" -quick-restart=27 >/dev/null 2>&1 || true
sleep 2
"$TETHER" -n -bind="" -wire="$WIRE" -quick-inject="$CMD"
