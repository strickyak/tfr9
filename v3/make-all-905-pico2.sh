#!/bin/sh
set -ex
cd $(dirname $0)

Enable_NOSTARTUP=0
Enable_JUST=0
Enable_SLOW=0
Enable_FLASH=0
Enable_RUN=0
while true
do
    case "$1" in
        -s* ) Enable_NOSTARTUP=1 ; shift ;;
        just ) Enable_JUST=1 ; shift ;;
        slow ) Enable_SLOW=1 ; shift ;;
        flash ) Enable_FLASH=1 ; shift ;;
        run ) Enable_RUN=1 ; shift ;;
        * ) break ;;
    esac
done
export Enable_NOSTARTUP
export Enable_JUST
export Enable_SLOW
export Enable_FLASH
export Enable_RUN

BUILD_DIR=build-all-905-pico2

# Which TFR circuit board number
TFR_BOARD=905

# RPCHIP is 2040 for Pi Pico 1, or 2350 for Pi Pico 2
RP_CHIP=2350

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

if expr 0 = $Enable_JUST
then
    sh create-nitros9disk.sh "$S1" "$D1" "$S1/cmds/shell_21"
    sh create-nitros9disk.sh "$S2" "$D2" "$S2/modules/sysgo_dd" "$S2/cmds/shell"

    make -C launchers

    # python3 binary-header-generator.py launchers/launch-2500-to-2602.raw "$S1/bootfiles/kernel_tfr9" > generated/level1.rom.h
    python3 binary-header-generator.py launchers/launch-2500-to-2602.raw n9recipe/tfr9-level1.t35 > generated/level1.rom.h

    python3 binary-header-generator.py launchers/launch-2500-to-2602.raw "$S2/bootfiles/kernel_tfr9" > generated/level2.rom.h

    mkdir -p /tmp/borges
    go run borges-saver/borges-saver.go -outdir /tmp/borges/ n9recipe/

    make \
        BUILD_DIR="${BUILD_DIR}" \
        TFR_BOARD="${TFR_BOARD}" \
        RP_CHIP="${RP_CHIP}" \
        TRACKING="${Enable_SLOW}" \
        "$@"
fi
if expr 1 = $Enable_FLASH
then
    cp -vf $BUILD_DIR/tmanager.uf2 /media/strick/RP*/.
fi

if expr 1 = $Enable_RUN
then
    ./tconsole-level1.linux-amd64.exe -disks "$D2,$D1,$D2" 2>_log   -borges /tmp/borges
fi
