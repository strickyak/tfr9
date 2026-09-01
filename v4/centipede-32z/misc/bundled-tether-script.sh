echo 'Error messages and logging will be in file "_log"' >&2
echo 'If tether dies and your terminal is broken, try " ^J reset ^J "' >&2
echo 'Type "bye" to leave the TCL shell and launch your Coco.' >&2
echo '' >&2

HERE="$(dirname $0)"
set -x
mkdir -p /tmp/pc

TETHER=none
for x in tether.*.exe
do
    if ./$x --help 2>/dev/null
    then
        TETHER=./$x
    fi
done

case $TETHER in
    none )
        echo "No tether program works on your machine." >&2
        exit 13 ;;
esac

case $1 in
    -quick* | --quick* )

$TETHER 2>_log "$@"

        ;;
        *)

#-- this configures for coco2 with disk11 basic
$TETHER 2>_log -pc /tmp/pc --no_modules \
    --abslists diskbasic.0x8000.list  \
      "$@" coco2.0x8000.rom disk11.0xC000.rom

        ;;
esac

# Usage of tether:
#   -abslists string
#     	.list filenames from lwasm with correct absolute addresses
#   -baud uint
#     	serial device baud rate (default 115200)
#   -bind string
#     	WebServer binds to this address (default ":8080")
#   -bootmode uint
#     	Restart firmware in this mode and then run tether console as usual
#   -borges string
#     	dir with source module listings (default "/home/strick/borges/")
#   -centipede
#     	Centipede should set this flag (default true)
#   -cobs_checksums
#     	Enable COBS packet checksums (default true)
#   -curly_dec
#     	Show nonprintable 7-bit output codes with curly decimal numbers
#   -disks string
#     	Comma-separated filepaths to disk files, in order of drive number
#   -dos_hack
#     	fix DOS command sector numbers
#   -level int
#     	NitrOS9 level, or 0
#   -linked
#     	allow other sections
#   -linklists string
#     	.list filenames from lwasm
#   -linkmap string
#     	.map file from linker
#   -logmax uint
#     	maximum bytes to log to stderr (default 1073741824)
#   -n	disable keyboard input
#   -no_modules
#     	there are no os9 modules, so don't scan for them
#   -omit_stderr
#     	send stderr to nowhere
#   -pc string
#     	root directory for virtual /pc filesystem (default "/tmp/pc")
#   -quick-inject string
#     	Quick mode: connect and inject a Tcl command to the Pico's REPL
#   -quick-label-data p=1,
#     	Quick mode: create a uf2 file with this data for the label. Must begin with p=1,
#   -quick-label-filename string
#     	Where to write the uf2 file for assigning a label (default "_metadata.uf2")
#   -quick-ping int
#     	Quick mode: connect, send a PicoRPC ping with this uint32 payload, print result, and exit (default -1)
#   -quick-reflash string
#     	Quick mode: reboot Pico into BOOTSEL and copy this UF2 file to it
#   -quick-reformat
#     	Quick mode: connect and reformat the Pico's LittleFS flash filesystem
#   -quick-restart uint
#     	Quick mode: connect and restart the Pico firmware with this boot_mode
#   -quick-upload string
#     	Quick mode: upload the Pico's flash from this zip file
#   -ram_verbose
#     	enable verbose debugging output of ram being written (if the pico is telling us)
#   -tether_log_usb
#     	Log USB COBS and RPC traffic
#   -trace_on_module string
#     	start tracing when loading this module
#   -usb_verbose
#     	enable verbose debugging output of bytes over the USB
#   -vdg_text
#     	whether to show VDG text
#   -wire string
#     	serial device connected by USB to Pi Pico (default "/dev/ttyACM0")
# 
