all: term_tfr901.os9 console_tfr901.os9 init_tfr901.os9 tfrblock_tfr901.os9 dd_tfrblock.os9 boot.rom

dd_tfrblock.os9: dd_tfrblock.asm
	lwasm --6809 --format=os9 --includedir="$${HOME}/coco-shelf/nitros9/defs/" --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  $< --output=$@ --list=$@.list --map=$@.map

tfrblock_tfr901.os9: tfrblock_tfr901.asm
	lwasm --6809 --format=os9 --includedir="$${HOME}/coco-shelf/nitros9/defs/" --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  $< --output=$@ --list=$@.list --map=$@.map

term_tfr901.os9: term_tfr901.asm
	lwasm --6809 --format=os9 --includedir="$${HOME}/coco-shelf/nitros9/defs/" --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  $< --output=$@ --list=$@.list --map=$@.map

console_tfr901.os9: console_tfr901.asm
	lwasm --6809 --format=os9 --includedir="$${HOME}/coco-shelf/nitros9/defs/" --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  $< --output=$@ --list=$@.list --map=$@.map

init_tfr901.os9: init_tfr901.asm
	lwasm --6809 --format=os9 --includedir="$${HOME}/coco-shelf/nitros9/defs/" --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  $< --output=$@ --list=$@.list --map=$@.map

boot.rom: _FORCE_
	sh generate-boot-rom.sh
	(cd ../firmware/c && python3 generate-bootrom-header.py < ../../nitros9/boot.rom > boot.rom.h)

clean:
	rm -f *.os9 *.list *.map

_FORCE_:
