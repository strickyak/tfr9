#!/bin/sh -e
#
#  Wrap the .s file produced by GCC with an ORG and LDS and call to _main.
#  Get stuck if _main returns.
#
#    Usage:
#      sh wrap-gcc-asm.sh 'title' < gcc-output.s > lwasm-input.tmp.asm
#
cat <<_EOF_
  TTL $1
  NAM $1

  ORG ORIGIN

entry:
  LDS #ORIGIN   ; Stack grows backward before the ORIGIN.
  LDD #0        ; Clear registers, just for cleaner debugging.
  TFR D,X
  TFR D,Y
  TFR D,U
  PSHS D,X,Y,U  ; Start stack with a block of zeros.
  TFR B,DP      ; Direct page is 0.
  JSR _main
stuck:
  BRA stuck
  
;; Wrapped by wrap-gcc-asm.sh ;;

_EOF_

sed \
	-e '/^[ \t]*[.]globl[ \t]/d' \
	-e '/^[ \t]*[.]module[ \t]/d' \
	-e '/^[ \t]*[.]area[ \t]/d' \
    ##

cat <<_EOF_

;; Wrapped by wrap-gcc-asm.sh ;;

  END entry
_EOF_
