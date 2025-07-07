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

BUILD_DIR=build-turbo9sim-905-pico2

# Which TFR circuit board number
TFR_BOARD=905

# RPCHIP is 2040 for Pi Pico 1, or 2350 for Pi Pico 2
RP_CHIP=2350

# Level is 100 for NitrOS9 Level 1, or 200 for NitroOS9 Level 2
# Level is 90 for Turbo9
OS_LEVEL=90

# Raspberry Pi Pico SDK
COCO_SHELF="${COCO_SHELF:-$(cd ../.. && pwd)}"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$COCO_SHELF/pico-sdk}"
export PICOTOOL_FETCH_FROM_GIT_PATH="${PICOTOOL_FETCH_FROM_GIT_PATH:-$COCO_SHELF/picotool}"
export PATH="$COCO_SHELF/bin:$PATH"

#####################################
#### Try not to edit below here. ####

make \
    BUILD_DIR="${BUILD_DIR}" \
    TFR_BOARD="${TFR_BOARD}" \
    RP_CHIP="${RP_CHIP}" \
    OS_LEVEL="${OS_LEVEL}" \
    TRACKING="${Enable_SLOW}" \
    "$@"

if expr 1 = $Enable_FLASH
then
    cp -vf build-turbo9sim-905-pico2/tmanager.uf2 /media/strick/RP*/.
fi

if expr 1 = $Enable_RUN
then
    ./tconsole-level1.linux-amd64.exe -disks "/dev/null" 2>_log   -borges $HOME/borges
fi
