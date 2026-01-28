##
##   Attempt Fuzix.
##   Along with F3_Mixins, F3_Slow, F3_Fast.
##
##   Needs some work.  Needs $THINGS on coco-shelf.
##   Hangs after ========
##
set -ex

THINGS=" EmulatorKit FUZIX Fuzix-Bintools Fuzix-Compiler-Kit "

rm -rf /tmp/fcc/*
mkdir -p /tmp/fcc

T=/tmp/etch
rm -rf $T/*
mkdir -p $T

cd $T
for x in $THINGS
do
    git clone /gh/EtchedPixels/$x
done

(
    cd Fuzix-Bintools
    make CCROOT=/tmp/fcc
    make CCROOT=/tmp/fcc install
)

PATH="/tmp/fcc/bin:/home/strick/modoc/coco-shelf/bin:$PATH"
export PATH
env | grep PATH

(
    cd Fuzix-Compiler-Kit
    make CCROOT=/tmp/fcc install
)

(
    cd FUZIX
    p make  TARGET=coco3 | tee /tmp/m
)
