all: tfr9cmd tfr9log

tfr9cmd: tfr9cmd.asm
	lwasm --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax --format=os9 $< -o'$@' -I'../../../nitros9/level1/coco1/' -I'../../../nitros9/defs'

tfr9log: tfr9log.asm
	lwasm --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax --format=os9 $< -o'$@' -I'../../../nitros9/level1/coco1/' -I'../../../nitros9/defs'

install: all
	os9 copy -r tfr9cmd ../generated/level1.dsk,CMDS/tfr9cmd
	os9 attr -q -r -w -e -pr -pe ../generated/level1.dsk,CMDS/tfr9cmd
	os9 copy -r tfr9cmd ../generated/level2.dsk,CMDS/tfr9cmd
	os9 attr -q -r -w -e -pr -pe ../generated/level2.dsk,CMDS/tfr9cmd
	os9 copy -r tfr9log ../generated/level1.dsk,CMDS/tfr9log
	os9 attr -q -r -w -e -pr -pe ../generated/level1.dsk,CMDS/tfr9log
	os9 copy -r tfr9log ../generated/level2.dsk,CMDS/tfr9log
	os9 attr -q -r -w -e -pr -pe ../generated/level2.dsk,CMDS/tfr9log

clean:
	rm -f tfr9cmd tfr9log *.map *.list
