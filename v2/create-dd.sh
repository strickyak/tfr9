set -ex

case $# in
    1) D="$1" ;;
    *) echo "Usage:  $0 output-disk-filename  " >&2
       exit 2
       ;;
esac

. n9drivers/tfr9ports.gen.sh

rm -f "$D"
# os9 format -l200 -e -n'TFR9-DISK' "$D"
os9 format -l9999 -e -n'TFR9-DISK' "$D"
os9 makdir "$D",CMDS

case $N9_LEVEL in
    # 2) os9 copy -r n9drivers/_level2.os9boot   "$D",/OS9Boot ;;
    2) os9 gen -b=n9drivers/_level2.os9boot   "$D" ;;
esac

cat >/tmp/tfr.startup <<~~~~
dir
mdir -e
free
mfree
basic09
e
10 FOR I=0 to 1000
20 FOR J=0 to 100
30 PRINT I*100 + J;
40 NEXT J
50 PRINT
100 SHELL "DATE -T"
999 NEXT I
q
list
run
q
q
bye
bye
~~~~

# BEGIN STANDARD TIMING ONE
cat >/tmp/tfr.startup1 <<~~~~
basic09
e
10 for j=1 to 20
20 x = 0
50 print "{";
110 FOR I=0 to 300
120 x = x + i
130 next i
140 print "}"
210 FOR z=0 to 300
230 next z
900 next j
q
run
~~~~
# END STANDARD TIMING ONE

# BEGIN STANDARD TIMING TWO
cat >/tmp/tfr.startup2 <<~~~~
basic09
e
100 for j=1 to 5
210 print "{";
220 FOR z=1 to 99
240 print "*";z;
250 next z
290 print "}"
810 FOR z=0 to 300
890 next z
900 next j
q
run
~~~~
# END STANDARD TIMING TWO


# cat >/tmp/tfr.startup <<~~~~
# echo One Two Three
# ~~~~

os9 copy -l -r /tmp/tfr.startup2  "$D",startup
# os9 del "$D",startup

if false
then
    os9 copy -r $HOME/NEW/nitros9/level2/coco3/cmds/basic09   "$D",CMDS/basic09
    os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/date   "$D",CMDS/date
    os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/list   "$D",CMDS/list
    os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/dump   "$D",CMDS/dump
    os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/free   "$D",CMDS/free
    os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/mfree  "$D",CMDS/mfree

    os9 attr -r -w -e -pr -pe "$D",cmds/basic09
    os9 attr -r -w -e -pr -pe "$D",cmds/date
    os9 attr -r -w -e -pr -pe "$D",cmds/list
    os9 attr -r -w -e -pr -pe "$D",cmds/dump
    os9 attr -r -w -e -pr -pe "$D",cmds/free
    os9 attr -r -w -e -pr -pe "$D",cmds/mfree
else
    #ls -1 $HOME/NEW/nitros9/level1/tfr9/cmds/ | grep -v [.]list | grep -v [.]map | while read f
    #do
    #  os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/$f  "$D",CMDS/$f
    #  os9 attr -r -w -e -pr -pe "$D",cmds/$f
    #done
    ls -1 $HOME/coco-shelf/nitros9/level2/coco3/cmds/ | egrep -v '[.](list|map)$' | while read f
    do
      os9 copy -r $HOME/coco-shelf/nitros9/level2/coco3/cmds/$f  "$D",CMDS/$f
      os9 attr -r -w -e -pr -pe "$D",cmds/$f
    done

    os9 copy -r /home/strick/glap/coco-shelf-oct10/mirror/frobio/built/v0extra/CMDS/ncl  "$D",CMDS/ncl
    os9 attr -r -w -e -pr -pe "$D",cmds/ncl
fi

# Mon Nov 25 11:53:40 PM EST 2024
# startup1: {}[0.305099 : 20 :  0.305659]
# startup1: {}[0.305825 : 20 :  0.305791]
# startup1: {}[0.306337 : 20 :  0.305667]
# startup2: 98.*99.}[0.706842 : 30 :  0.707287]
# startup2: 98.*99.}[0.707397 : 30 :  0.707322]
# startup2: 98.*99.}[0.707461 : 30 :  0.707289]

# Wed Nov 27 11:24:17 PM EST 2024
# 98.*99.}[0.695263 : 30 :  0.695987]
# 98.*99.}[0.695661 : 30 :  0.695961]
# 98.*99.}[0.695299 : 60 :  0.695804]

# Thu Nov 28 12:43:52 AM EST 2024
# 0.678086
# 0.678024
# 0.678025

#################### Experiment:
#rm -f "$D"
#N=NOS9_6809_L2_v030300_coco3_cocosdc.dsk
#N=NOS9_6809_L2_v030300_coco3_emudsk.dsk
#N=NOS9_6809_L2_v030300_coco3_cocosdc.dsk
#cp -vf ~/coco-shelf/nitros9/level2/coco3/$N "$D"
