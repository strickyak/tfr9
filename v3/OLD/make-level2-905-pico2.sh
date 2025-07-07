#!/bin/sh
set -ex
cd $(dirname $0)

Enable_SLOW=0
Enable_FLASH=0
Enable_RUN=0
while true
do
    case "$1" in
        slow ) Enable_SLOW=1 ; shift ;;
        flash ) Enable_FLASH=1 ; shift ;;
        run ) Enable_RUN=1 ; shift ;;
        * ) break ;;
    esac
done

BUILD_DIR=build-level2-905-pico2

# Which TFR circuit board number
TFR_BOARD=905

# RPCHIP is 2040 for Pi Pico 1, or 2350 for Pi Pico 2
RP_CHIP=2350

# Level is 100 for NitrOS9 Level 1, or 200 for NitroOS9 Level 2
# Level is 90 for Turbo9
OS_LEVEL=200

# Raspberry Pi Pico SDK
COCO_SHELF="${COCO_SHELF:-$(cd ../.. && pwd)}"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$COCO_SHELF/pico-sdk}"
export PICOTOOL_FETCH_FROM_GIT_PATH="${PICOTOOL_FETCH_FROM_GIT_PATH:-$COCO_SHELF/picotool}"
export PATH="$COCO_SHELF/bin:$PATH"

#####################################
#### Try not to edit below here. ####

S="${COCO_SHELF}/nitros9/level2/coco3"
D="generated/level2.dsk"
sh create-nitros9disk.sh "$S" "$D" "$S/modules/sysgo_dd" "$S/cmds/shell"
make -C launchers
python3 binary-header-generator.py launchers/launch-2500-to-2602.raw "$S/bootfiles/kernel_tfr9" > generated/level2.rom.h

make \
    BUILD_DIR="${BUILD_DIR}" \
    TFR_BOARD="${TFR_BOARD}" \
    RP_CHIP="${RP_CHIP}" \
    OS_LEVEL="${OS_LEVEL}" \
    TRACKING="${Enable_SLOW}" \
    "$@"

if expr 1 = $Enable_FLASH
then
    cp -vf $BUILD_DIR/tmanager.uf2 /media/strick/RP*/.
fi

if expr 1 = $Enable_RUN
then
    ./tconsole-level2.linux-amd64.exe -disks "$D" 2>_log  -borges $HOME/borges
fi
