#!/bin/sh
# run_turbolab.sh - Launch TurboLab on TFR911H SBC (`on tfr911h run turbolab [with ...]`).
#
# Examples:
#   ./run_turbolab.sh
#   ./run_turbolab.sh WITH trigger = r/0xFFFE WITH trace_count = 100 -- -n
#   ./run_turbolab.sh WITH trace = i WITH stop = c/1500k -- -n
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)

WITH_ARGS=${TURBOLAB_WITH:-}
while [ "$#" -gt 0 ]; do
  case "$1" in
    --) shift; break ;;
    [Ww][Ii][Tt][Hh]|*)
      # If the first arg starts with `-` (e.g. `-n` or `-wire=...`), pass remaining args to tether.
      case "$1" in
        -*) break ;;
      esac
      if [ -n "$WITH_ARGS" ]; then
        WITH_ARGS="$WITH_ARGS $1"
      else
        WITH_ARGS="$1"
      fi
      shift
      ;;
  esac
done

if [ -n "$WITH_ARGS" ]; then
  TCL_CMD="ON tfr911h RUN turbolab $WITH_ARGS"
else
  TCL_CMD="ON tfr911h RUN turbolab"
fi

exec "$DIR/scripts/run-demo.sh" turbolab 82 \
  "$TCL_CMD
bye" \
  -- "$@"
