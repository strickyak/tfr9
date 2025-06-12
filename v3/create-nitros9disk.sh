#!/bin/sh

S="$1"; shift
D="$1"; shift
# Remaining args are added to OS9Boot:
T=/tmp/tmp.$$.os9boot
cat "$@" "$S/bootfiles/bootfile_tfr9" > $T
trap "rm -f $T" 0 1 2 3

rm -f "$D"
os9 format -l9999 -e -n'TFR9-DISK' "$D"
os9 makdir "$D",CMDS
os9 gen -b="$T" "$D"
os9 copy -l -r "$S/startup"  "$D",startup

ls -1 "$S"/cmds/ | egrep -v '[.](list|map)$' | while read f
do
      os9 copy -r "$S/cmds/$f"  "$D",CMDS/$f
      os9 attr -q -r -w -e -pr -pe "$D",cmds/$f
done

# BEGIN STANDARD TIMING TWO
cat >/tmp/tfr.startup2 <<~~~~
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
~~~~
# END STANDARD TIMING TWO
os9 copy -l -r /tmp/tfr.startup2  "$D",startup

