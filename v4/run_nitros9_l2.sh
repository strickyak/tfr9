#!/bin/sh
#
# run_nitros9_l2.sh - Launch NitrOS-9 Level 2 (Deep65280) on TFR911H SBC (Hitachi 6309E CPU)
#
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"

exec "$DIR/build/tether.linux-amd64.exe" -pc "$DIR/misc/pc" -bootmode=8 "$@"
