BUILD_DIR=build-turbos9sim-905-pico2

# Which TFR circuit board number
TFR_BOARD=905

# RPCHIP is 2040 for Pi Pico 1, or 2350 for Pi Pico 2
RP_CHIP=2350

# Level is 100 for NitrOS9 Level 1, or 200 for NitroOS9 Level 2
# Level is 90 for Turbos9
OS_LEVEL=90

# These features are 0 to disable, 1 to enable.
TRACK_RAM=1
TRACE_CYCLES=1
TRACE_SWI2=1

# Raspberry Pi Pico SDK
COCO_SHELF="${COCO_SHELF:-$(cd ../.. && pwd)}"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$COCO_SHELF/pico-sdk}"
export PICOTOOL_FETCH_FROM_GIT_PATH="${PICOTOOL_FETCH_FROM_GIT_PATH:-$COCO_SHELF/picotool}"

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
