#!/bin/sh

D=`dirname $0`
TURBOS_ROM=${TURBOS_ROM:-$D/build/turbos/turbos_dev.img.rom}

TETHER=None
for t in $D/build/tether.*.exe
do
    if $t --exit 2>/dev/null
    then
        TETHER=$t
    fi
done

case $TETHER in
    None )
        echo "ERROR: valid tether.*.exe command not found, in $D/build" >&2
        exit 13
        ;;
esac

echo "-------------------------------------------------------------------------"
echo "ERRORS AND LOGGING WILL BE IN FILE `pwd`/_log"
echo "Type these two characters to send Keyboard Interrupt to Turbos: ^C"
echo "Hit Control-C (or your PC interrupt sequence) to exit the Tether program."
echo "-------------------------------------------------------------------------"
set -x
$TETHER -listings $D/build/listings/ "$@" $TURBOS_ROM 2>_log
