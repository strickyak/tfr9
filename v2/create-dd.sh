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
cat >/tmp/tfr.startup <<~~~~
basic09
e
10 FOR I=0 to 999999
20 shell "date -t"
30 next i
q
run
~~~~

os9 copy -l -r /tmp/tfr.startup  "$D",startup
# os9 del "$D",startup

os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/date   "$D",CMDS/date
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/list   "$D",CMDS/list
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/dump   "$D",CMDS/dump
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/free   "$D",CMDS/free
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/mfree  "$D",CMDS/mfree
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/tmode  "$D",CMDS/tmode

os9 attr -r -w -e -pr -pe "$D",cmds/date
os9 attr -r -w -e -pr -pe "$D",cmds/list
os9 attr -r -w -e -pr -pe "$D",cmds/dump
os9 attr -r -w -e -pr -pe "$D",cmds/free
os9 attr -r -w -e -pr -pe "$D",cmds/mfree
os9 attr -r -w -e -pr -pe "$D",cmds/tmode
