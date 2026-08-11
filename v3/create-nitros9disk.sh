#!/bin/sh
set -ex

# For /bin/sh without a time builtin (e.g. busybox/dash on a 32-bit R Pi):
# the eval hides the function-definition syntax from parsers (like bash's)
# that special-case `time` as a reserved word at parse time.
command -v time >/dev/null 2>&1 || eval 'time() { "$@" ; }'

S="$1"; shift
D="$1"; shift

# Newer nitros9 checkouts only build libNAME.a under recipes/*/floppy/.lib/,
# not under nitros9/lib/ where rmake.tcl's generated Makefiles look for them.
N9ROOT="$(cd "$S/../.." && pwd)"
mkdir -p "$N9ROOT/lib"
case "$S" in
  */level1/coco1 )
    cp -vf "$N9ROOT/recipes/coco/floppy/.lib/libalib.a" "$N9ROOT/lib/libalib.a"
    cp -vf "$N9ROOT/recipes/coco/floppy/.lib/libcoco.a" "$N9ROOT/lib/libcoco.a"
    cp -vf "$N9ROOT/recipes/coco/floppy/.lib/libnet.a" "$N9ROOT/lib/libnet.a"
  ;;
  */level2/coco3 )
    cp -vf "$N9ROOT/recipes/coco3/floppy/.lib/libalib.a" "$N9ROOT/lib/libalib.a"
    cp -vf "$N9ROOT/recipes/coco3/floppy/.lib/libcoco3.a" "$N9ROOT/lib/libcoco.a"
    cp -vf "$N9ROOT/recipes/coco3/floppy/.lib/libnet.a" "$N9ROOT/lib/libnet.a"
  ;;
esac

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
cat >/tmp/tfr.startup2 <<'END'
t
basic09
e
100 for j=1 to 5
210 print "^";"{";
220 FOR z=1 to 99
240 print "*";z;
250 next z
290 print "^";"}"
810 FOR z=0 to 300
890 next z
900 next j
q
run
END
# END STANDARD TIMING TWO

if test 1 = "$T9V3_NOSTARTUP"
then
    os9 del "$D",startup || echo Ignore Prevous Error >&2
else
    os9 copy -l -r /tmp/tfr.startup2  "$D",startup
fi
