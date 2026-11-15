#!/bin/sh
# check-no-lambda-io.sh -- I/O handlers must be NAMED functions marked IN_RAM, never lambdas.
# A lambda cannot be IN_RAM, so it would run from flash and stall the 6309's bus cycles (spec 5.1).
# Finds   IOReaders[...] = [...](   and   IOWriters[...] = [...](   in firmware/.
# (There are no exceptions any more: turbo9sim.h's lambda installer was replaced by Turbo9SimAcia.)
V5=$(cd "$(dirname "$0")/.." && pwd)
ALLOW=""
status=0
for f in $(cd "$V5" && find firmware -name '*.h' -o -name '*.cpp' -o -name '*.c' | sort); do
  case " $ALLOW " in *" $f "*) continue ;; esac
  hits=$(grep -n -E 'IO(Readers|Writers)\[[^]]*\][[:space:]]*=[[:space:]]*\[' "$V5/$f")
  if [ -n "$hits" ]; then echo "LAMBDA I/O HANDLER in $f:"; echo "$hits" | sed 's/^/    /'; status=1; fi
done
[ $status -eq 0 ] && echo "check-no-lambda-io: OK"
exit $status
