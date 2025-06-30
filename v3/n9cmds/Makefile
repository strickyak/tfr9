all: hypercmd

hypercmd: hypercmd.asm
	lwasm --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax --format=os9 $< -o'$@' -I'../../../nitros9/level1/coco1/' -I'../../../nitros9/defs'

install: all
	os9 copy -r hypercmd ../generated/level1.dsk,CMDS/hypercmd
	os9 attr -q -r -w -e -pr -pe ../generated/level1.dsk,CMDS/hypercmd
	os9 copy -r hypercmd ../generated/level2.dsk,CMDS/hypercmd
	os9 attr -q -r -w -e -pr -pe ../generated/level2.dsk,CMDS/hypercmd
