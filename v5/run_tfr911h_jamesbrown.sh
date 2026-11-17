#!/bin/sh
# run_tfr911h_jamesbrown.sh - Launch NitrOS-9 Level 2 (jamesbrown / Andrew Diller edition) on TFR911H SBC
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
DISK=${V5_IMG_JAMESBROWN_DISK:-$HOME/github.com/dillera/tfr9/v4/nitros9-l2/build/tfr911-level2.dsk}

if [ -t 0 ]; then
  echo "JamesBrown NitrOS-9 Level 2: ^C goes to OS-9; quit tether with ^] ." >&2
fi

exec "$DIR/scripts/run-tfr911h-demo.sh" jamesbrown 9033 \
  'on tfr911h run nitros9l2 for jamesbrown
set Config(flat_img) ""
set Config(drive0) "/pc/tfr911-level2.dsk"
set Config(drive1) ""
menu store Config
bye' \
  "$DISK:tfr911-level2.dsk" -- -pass_ctrl_c -level=2 "$@"
