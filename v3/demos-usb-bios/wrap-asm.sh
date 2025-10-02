#!/bin/sh -e
#
#  Wrap the .s file produced by GCC with an ORG and LDS and call to _main.
#  Get stuck if _main returns.
#
cat <<\_EOF_
  ORG $1000

entry:
  LDS #$1000
  LDU #0
  JSR _main
stuck:
  BRA stuck
  
;; Wrapped by wrap-asm.sh ;;

_EOF_

sed \
	-e '/^[ \t]*[.]globl[ \t]/d' \
	-e '/^[ \t]*[.]module[ \t]/d' \
	-e '/^[ \t]*[.]area[ \t]/d' \

cat <<\_EOF_

  END entry
_EOF_
