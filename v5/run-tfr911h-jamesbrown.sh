#!/bin/sh
# run-tfr911h-jamesbrown.sh - Launch NitrOS-9 Level 2 (jamesbrown / Andrew Diller edition) on TFR911H SBC
set -e
DIR=$(cd "$(dirname "$0")" && pwd)

if [ -n "${V5_IMG_JAMESBROWN_DISK:-}" ]; then
  DISK=$V5_IMG_JAMESBROWN_DISK
elif [ -f "$DIR/images/jamesbrown/tfr911-jamesbrown.dsk" ]; then
  DISK="$DIR/images/jamesbrown/tfr911-jamesbrown.dsk"
else
  DISK="$DIR/build/tfr911-jamesbrown.dsk"
  if [ ! -f "$DISK" ] && [ -f "$DIR/scripts/make-jamesbrown-dsk.sh" ]; then
    sh "$DIR/scripts/make-jamesbrown-dsk.sh" "$DISK"
  fi
fi

if [ -t 0 ]; then
  echo "JamesBrown NitrOS-9 Level 2: ^C goes to OS-9; quit tether with ^] ." >&2
fi

exec "$DIR/scripts/run-tfr911h-demo.sh" jamesbrown 9033 \
  'on tfr911h run nitros9l2 for jamesbrown
set Config(flat_img) ""
set Config(drive0) "/pc/tfr911-jamesbrown.dsk"
set Config(drive1) ""
menu store Config
bye' \
  "$DISK:tfr911-jamesbrown.dsk" -- -pass_ctrl_c -level=2 "$@"
