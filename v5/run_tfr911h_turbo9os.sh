#!/bin/sh
# run_tfr911h_turbo9os.sh - Launch built-in Turbo9OS on TFR911H SBC (Hitachi 6309E CPU)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)

exec "$DIR/scripts/run-tfr911h-demo.sh" turbo9os 9010 \
  'on tfr911h run turbo9os for turbo9sim
bye' \
  -- "$@"
