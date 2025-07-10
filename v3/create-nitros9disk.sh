#!/bin/sh
set -ex

S="$1"; shift
D="$1"; shift

time tclsh rmake.tcl

case "$D" in

  */level1.dsk )
    B=build/tfr9/level1
    time make -j4 -C $B
    cp -vf $B/tfr9-level1.dsk "$D"
  ;;

  */level2.dsk )
    B=build/tfr9/level2
    time make -j4 -C $B
    cp -vf $B/tfr9-level2.dsk "$D"
  ;;

  * )
    __________ERROR____  "$D"
    exit 13
  ;;

esac

# BEGIN STANDARD TIMING TWO
cat >/tmp/tfr.startup2 <<'EOR'
t
tmode .2 pau=0
basic09
e
100 for j=1 to 5
210 print "^{";
220 FOR z=1 to 99
240 print "*";z;
250 next z
290 print "^}"
810 FOR z=0 to 300
890 next z
900 next j
q
run
EOR
# END STANDARD TIMING TWO

if test 1 = "$T9V3_NOSTARTUP"
then
    os9 del "$D",startup || echo Ignore Prevous Error >&2
else
    os9 copy -l -r /tmp/tfr.startup2  "$D",startup
fi
