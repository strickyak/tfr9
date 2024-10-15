set -ex

case $# in
    1) D="$1" ;;
    *) echo "Usage:  $0 output-disk-filename  " >&2
       exit 2
       ;;
esac

rm -f "$D"
os9 format -l99 -e -n'TFR9-DISK' "$D"
os9 makdir "$D",CMDS

cat >/tmp/tfr.how <<~~~~
load count.bas
run
~~~~

cat >/tmp/tfr.count.in <<~~~~
e
10 FOR I=0 to 1000000
20 PRINT I;
30 NEXT I
40 END
q
list
run
~~~~

cat >/tmp/tfr.startup <<~~~~
~~~~
# t
# dir
# dump how
# tmode .1 pau=0
# basic09 < how
# echo Hello World
# mdir -e
# dir -e cmds
# dir -x
# dump startup
# tmode pau=0
# mfree
# free

os9 copy -l -r /tmp/tfr.how  "$D",how
os9 copy -l -r /tmp/tfr.count.in  "$D",count.in
os9 copy -l -r /tmp/tfr.startup  "$D",startup
os9 del "$D",startup
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/date   "$D",CMDS/date
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/list   "$D",CMDS/list
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/dump   "$D",CMDS/dump
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/free   "$D",CMDS/free
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/mfree  "$D",CMDS/mfree
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/tmode  "$D",CMDS/tmode
