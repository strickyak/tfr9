#!/bin/bash
# run_gep9.sh - Launch Hatvan OS on TFR911H SBC (Hitachi 6309E CPU) using GEP9 engine
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$DIR/build/tether.linux-amd64.exe" -pc "$DIR/misc/pc" -bootmode=80 "$@"
