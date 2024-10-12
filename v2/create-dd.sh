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
t
echo Hello World
mdir -e
mdir
dir -e cmds
dir -e
dir
~~~~

os9 copy -l -r /tmp/tfr.startup  "$D",startup
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/date  "$D",CMDS/date
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/list  "$D",CMDS/list
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/dump  "$D",CMDS/dump
