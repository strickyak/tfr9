#!/bin/sh
set -ex
cd $(dirname $0)


BUILD_DIR=build-all-905-pico2

# Which TFR circuit board number
TFR_BOARD=905

# RPCHIP is 2040 for Pi Pico 1, or 2350 for Pi Pico 2
RP_CHIP=2350
WHICH_PICO=pico2

# Raspberry Pi Pico SDK
COCO_SHELF="${COCO_SHELF:-$(cd ../.. && pwd)}"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$COCO_SHELF/pico-sdk}"
export PICOTOOL_FETCH_FROM_GIT_PATH="${PICOTOOL_FETCH_FROM_GIT_PATH:-$COCO_SHELF/picotool}"
export PATH="$COCO_SHELF/bin:$PATH"

#####################################
#### Try not to edit below here. ####

S1="${COCO_SHELF}/nitros9/level1/coco1"
D1="generated/level1.dsk"

S2="${COCO_SHELF}/nitros9/level2/coco3"
D2="generated/level2.dsk"

if expr 0 = $T9V3_JUST
then
    time sh create-nitros9disk.sh "$S1" "$D1" "$S1/cmds/shell_21"
    time sh create-nitros9disk.sh "$S2" "$D2" "$S2/modules/sysgo_dd" "$S2/cmds/shell"

    make -j4 -C launchers

    # python3 binary-header-generator.py launchers/launch-2500-to-2602.raw "$S1/bootfiles/kernel_tfr9" > generated/level1.rom.h
    B=build/tfr9/level1
    python3 binary-header-generator.py launchers/launch-2500-to-2602.raw $B/tfr9-level1.t35 > generated/level1.rom.h
    B=build/tfr9/level2
    python3 binary-header-generator.py launchers/launch-2500-to-2602.raw $B/tfr9-level2.t35 > generated/level2.rom.h

    #### python3 binary-header-generator.py launchers/launch-2500-to-2602.raw "$S2/bootfiles/kernel_tfr9" > generated/level2.rom.h

    mkdir -p /tmp/borges
    go run borges-saver/borges-saver.go -outdir /tmp/borges/ n9recipe/ build/

    time make -j4 _secondary_ \
        BUILD_DIR="${BUILD_DIR}" \
        TFR_BOARD="${TFR_BOARD}" \
        RP_CHIP="${RP_CHIP}" \
        WHICH_PICO="${WHICH_PICO}" \
        "$@"
fi

if expr 1 = $T9V3_FLASH
then
    cp -vf $BUILD_DIR/tmanager.uf2 /media/strick/RP*/.
fi

if expr 1 = $T9V3_RUN
then
    for i in 0 3 4 5 6 7
    do
        test -s generated/disk$i || os9 format -l99999 -n"disk$i" generated/disk$i
    done
    DISKS=$( echo generated/disk0 generated/level1.dsk generated/level2.dsk generated/disk3 generated/disk4 generated/disk5 generated/disk6 generated/disk7 | tr " " "," )

    ./tconsole-level1.linux-amd64.exe  -disks "$DISKS"  -borges /tmp/borges  2>_log
fi
