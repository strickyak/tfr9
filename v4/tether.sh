echo 'Error messages and logging will be in file "_log"' >&2
echo 'If tether dies and your terminal is broken, try " ^J reset ^J "' >&2
echo 'Type "bye" to leave the TCL shell and launch TurbOS.' >&2
echo '' >&2

HERE="$(dirname $0)"
set -x
mkdir -p /tmp/pc
go run "./$HERE/tether/" 2>_log -pc /tmp/pc -level 1 -borges build/listings/ "$@"
