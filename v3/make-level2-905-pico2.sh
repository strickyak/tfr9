BUILD_DIR=build-level2-905-pico2

# Which TFR circuit board number
TFR_BOARD=905

# RPCHIP is 2040 for Pi Pico 1, or 2350 for Pi Pico 2
RP_CHIP=2350

# Level is 100 for NitrOS9 Level 1, or 200 for NitroOS9 Level 2
OS_LEVEL=200

# These features are 0 to disable, 1 to enable.
TRACK_RAM=0
TRACE_CYCLES=0
TRACE_SWI2=0

# Device Port starting addresses
ACIA_PORT=FF06
EMUDSK_PORT=FF80

# Raspberry Pi Pico SDK
COCO_SHELF="${COCO_SHELF:-$(cd ../.. && pwd)}"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$COCO_SHELF/pico-sdk}"
PICOTOOL_FETCH_FROM_GIT_PATH="${PICOTOOL_FETCH_FROM_GIT_PATH:-$COCO_SHELF/picotool}"

#####################################
#### Try not to edit below here. ####

make \
    BUILD_DIR="${BUILD_DIR}" \
    TFR_BOARD="${TFR_BOARD}" \
    RP_CHIP="${RP_CHIP}" \
    OS_LEVEL="${OS_LEVEL}" \
    TRACK_RAM="${TRACK_RAM}" \
    TRACE_CYCLES="${TRACE_CYCLES}" \
    TRACE_SWI2="${TRACE_SWI2}" \
    ACIA_PORT="${ACIA_PORT}" \
    EMUDSK_PORT="${EMUDSK_PORT}" \
    "$@"
