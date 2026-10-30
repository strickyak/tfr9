# env.sh -- POSIX sh.  Source it:   V5=/path/to/tfr9/v5 . scripts/env.sh     (or from a script that already set V5)
# Sets the paths the v5 build needs.  It deliberately IGNORES an ambient PICO_SDK_PATH
# (e.g. an old SDK left in ~/pico); override with V5_PICO_SDK=/path instead.
if [ -z "${V5:-}" ]; then V5=$(cd "$(dirname "$0")/.." && pwd); fi
export V5
: "${SHELF:=$(cd "$V5/../.." && pwd)}"          # the coco-shelf (parent of tfr9/)
export SHELF
V5_AMBIENT_PICO_SDK_PATH=${PICO_SDK_PATH:-}      # remembered only so doctor can warn
export V5_AMBIENT_PICO_SDK_PATH
PICO_SDK_PATH=${V5_PICO_SDK:-$SHELF/pico-sdk}
export PICO_SDK_PATH
picotool_DIR=${V5_PICOTOOL_DIR:-$SHELF/lib/cmake/picotool}
export picotool_DIR
case ":$PATH:" in *":$SHELF/bin:"*) ;; *) PATH="$SHELF/bin:$PATH"; export PATH;; esac
V5_BUILD=$V5/build
export V5_BUILD
