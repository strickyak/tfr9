#!/bin/sh
set -eux

ROM=level1.rom

# If NITROS9DIR is not set in environment,
# assume it is under the coco-shelf under the HOME dir.
NITROS9DIR=${NITROS9DIR:-$HOME/NEW/nitros9}

# Cat everything but the kernel.
(
  # Some coco1 commands.
  ( cd "$NITROS9DIR/level1/coco1/cmds" &&
    cat shell_21 procs mdir echo dir tmode basic09
  )
  # Local modules.
  cat term_tfr901.os9 console_tfr901.os9 init_tfr901.os9
  cat tfrblock_tfr901.os9 dd_tfrblock.os9
  # Modules from coco1.
  ( cd "$NITROS9DIR/level1/coco1/modules" &&
    cat clock_60hz clock2_messemu
    cat sysgo_dd
    cat rbf.mn scf.mn ioman krnp2
  )
) > $ROM

ls -l $ROM
wc -c $ROM

# Now cat the KRN module from the /tfr9/ port.
# All these $ROM bytes so far will end at $FFE0.
KRN="$NITROS9DIR/level1/tfr9/modules/krn"
cat "$KRN" >> $ROM

# Append 24 bytes at $FEE0, ending at $FEF8.
cat level1prelude.raw >> $ROM
n=$(wc -c < $ROM)

# write Start of Preloaded Modules at $FEF8
python3 -c "import struct;import sys; x=struct.pack('>H', 0xFEE0-$n); sys.stdout.buffer.write(x)" >> $ROM
# write End of Preloaded Modules at $FEFA
python3 -c "import struct;import sys; x=struct.pack('>H', 0xFEE0); sys.stdout.buffer.write(x)" >> $ROM
k=$(wc -c < "$KRN")
# write Beginning of Kernel Module at $FEFC
python3 -c "import struct;import sys; x=struct.pack('>H', 0xFEE0-$k); sys.stdout.buffer.write(x)" >> $ROM
# Write magic number $6789 at $FEFE.
python3 -c "import struct;import sys; x=struct.pack('>H', 0x6789); sys.stdout.buffer.write(x)" >> $ROM

ls -l $ROM
wc -c $ROM

gop run /sy/doing_os9/gomar/borges/borges.go  -outdir $HOME/borges/  /sy/tfr9/nitros9/  $HOME/NEW/nitros9/  ./.
