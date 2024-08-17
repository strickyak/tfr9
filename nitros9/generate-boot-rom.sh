#!/bin/sh
set -eux

# If NITROS9DIR is not set in environment,
# assume it is under the coco-shelf under the HOME dir.
NITROS9DIR=${NITROS9DIR:-$HOME/NEW/nitros9}

# Cat everything but the kernel.
(
  # Some coco1 commands.
  ( cd "$NITROS9DIR/level1/coco1/cmds" &&
    cat shell_21 procs mdir echo dir
  )
  # Local modules.
  cat term_tfr901.os9 console_tfr901.os9 init_tfr901.os9
  cat tfrblock_tfr901.os9 dd_tfrblock.os9
  # Modules from coco1.
  ( cd "$NITROS9DIR/level1/coco1/modules" &&
    cat clock2_messemu
    cat sysgo_dd
    cat rbf.mn scf.mn ioman krnp2
  )
) > boot.rom

ls -l boot.rom
wc -c boot.rom

# Now cat the KRN module from the /tfr9/ port.
KRN="$NITROS9DIR/level1/tfr9/modules/krn"
cat "$KRN" >> boot.rom

ls -l boot.rom
wc -c boot.rom

gop run /sy/doing_os9/gomar/borges/borges.go  -outdir $HOME/borges/  /sy/tfr9/nitros9/  $HOME/NEW/nitros9/
