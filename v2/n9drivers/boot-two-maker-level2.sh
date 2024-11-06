#!/bin/sh
set -eux

. ./tfr9ports.gen.sh  ;# source definitions

LEVEL=level2
MACH=coco3
BOOTFILE=_level2.os9boot

# If NITROS9DIR is not set in environment,
# assume it is under the coco-shelf under the HOME dir.
NITROS9DIR=${NITROS9DIR:-$HOME/NEW/nitros9}

# Cat everything but the kernel.
(
  # Some commands.
  ( cd "$NITROS9DIR/$LEVEL/$MACH/cmds" &&
    cat shell_21 echo
  )
  # Local modules.
  if [ $USE_ACIA -ne 0 ]
  then
    cat term_sc6850.os9 sc6850.os9 init_tfr901.os9
  else
    cat term_tfr901.os9 console_tfr901.os9 init_tfr901.os9
  fi

  cat $NITROS9DIR/level1/coco1/modules/emudsk.dr
  cat $NITROS9DIR/level1/coco1/modules/ddh0_emudsk.dd

  : : : cat tfrblock_tfr901.os9 dd_tfrblock.os9
  # Modules.
  ( cd "$NITROS9DIR/$LEVEL/$MACH/modules" &&
    cat clock_60hz clock2_messemu
    cat sysgo_dd
    cat rbf.mn scf.mn ioman krnp2
  )
) > $BOOTFILE

ls -l $BOOTFILE
wc -c $BOOTFILE

##################### lwasm --6809 --format=os9 --includedir="${HOME}/NEW/nitros9/defs/" --includedir="${HOME}/NEW/nitros9/level1/modules/"  --includedir="${HOME}/NEW/nitros9/level1/tfr9/"  --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  "$NITROS9DIR/level1/coco1/modules/boot_emu.asm"  -"oboot_emu.os9" --list="boot_emu.list"

cat \
  "$NITROS9DIR/level2/coco3/modules/rel_40" \
  "/home/strick/coco-shelf/nitros9/level2/coco3/modules/boot_emu" \
  "$NITROS9DIR/level2/coco3/modules/krn" \
  > _level2.track35
  #################### "boot_emu.os9" \

python3 ../binary-header-generator.py _level2.track35 > ../tmanager903/level2.rom.h

go run /sy/doing_os9/gomar/borges/borges.go  -outdir $HOME/borges/  /sy/tfr9/nitros9/  $HOME/NEW/nitros9/  ./.
