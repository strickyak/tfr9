INCL=--includedir="$${HOME}/NEW/nitros9/defs/" \
     --includedir="$${HOME}/NEW/nitros9/level1/tfr9/" 
PRAG=--pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  

all: term_tfr901.os9 console_tfr901.os9 init_tfr901.os9 tfrblock_tfr901.os9 dd_tfrblock.os9 boot.rom

dd_tfrblock.os9: dd_tfrblock.asm
	lwasm --6809 --format=os9 $(INCL) $(PRAG)  $< --output=$@ --list=$@.list --map=$@.map

tfrblock_tfr901.os9: tfrblock_tfr901.asm
	lwasm --6809 --format=os9 $(INCL) $(PRAG)  $< --output=$@ --list=$@.list --map=$@.map

term_tfr901.os9: term_tfr901.asm
	lwasm --6809 --format=os9 $(INCL) $(PRAG)  $< --output=$@ --list=$@.list --map=$@.map

console_tfr901.os9: console_tfr901.asm
	lwasm --6809 --format=os9 $(INCL) $(PRAG)  $< --output=$@ --list=$@.list --map=$@.map

init_tfr901.os9: init_tfr901.asm
	lwasm --6809 --format=os9 $(INCL) $(PRAG)  $< --output=$@ --list=$@.list --map=$@.map

boot.rom: _FORCE_
	sh boot-rom-maker.sh
	python3 ../binary-header-generator.py level1.rom > ../tmanager/level1.rom.h

clean:
	rm -f *.raw *.os9 *.list *.map

_FORCE_:
