STDERR=${STDERR:-_log}

echo 'Error messages and logging will be in file "${STDERR}"' >&2
echo 'If tether dies and your terminal is broken, try " ^J reset ^J "' >&2
echo 'Type "bye" to leave the TCL shell and launch your Coco.' >&2
echo '' >&2

HERE="$(dirname $0)"
set -x
mkdir -p /tmp/pc

#-- this configured for TurbOS9
# go run "./$HERE/../..//v4/tether/" 2>${STDERR} -pc /tmp/pc -level 1 -borges build/listings/ "$@"

ABSLISTS=build/diskbasic.0x8000.list
ROMS="build/coco2.0x8000.rom build/disk11.0xC000.rom"
EXTRA_FLAGS=""
case $1 in
    +COCO3 )
            shift
            ABSLISTS=/home/strick/modoc/coco-shelf/toolshed/cocoroms/coco3.rom.list,/home/strick/modoc/coco-shelf/toolshed/cocoroms/disk11.rom.list
            ROMS="build/coco3.0x8000.rom build/disk11.0xC000.rom"
            EXTRA_FLAGS="--coco3"
        ;;
esac

case $1 in
    -quick* | --quick* )

#-- this configures for coco2 with disk11 basic
go run "./$HERE/tether/" 2>${STDERR} "$@" 

    ;;
    * )

go run "./$HERE/tether/" 2>${STDERR} -pc /tmp/pc --no_modules \
    --abslists $ABSLISTS  $EXTRA_FLAGS \
      "$@"  $ROMS

    ;;
esac
