set -ex

rm -f /tmp/tfr.disk
os9 format -l99 -e -n'TFR9-DISK' /tmp/tfr.disk
os9 makdir /tmp/tfr.disk,CMDS

cat >/tmp/tfr.startup <<~~~~
t
echo Hello World
dir
~~~~

os9 copy -l -r /tmp/tfr.startup  /tmp/tfr.disk,startup
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/date  /tmp/tfr.disk,CMDS/date
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/list  /tmp/tfr.disk,CMDS/list
os9 copy -r $HOME/NEW/nitros9/level1/tfr9/cmds/dump  /tmp/tfr.disk,CMDS/dump
