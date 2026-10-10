#!/bin/bash
# run_fuzix.sh - Launch Fuzix 6809 on TFR911H SBC (Hitachi 6309E CPU) using multicomp09fuzix engine
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$DIR/build/tether.linux-amd64.exe" -pc "$DIR/misc/pc" -bootmode=70 "$@"
