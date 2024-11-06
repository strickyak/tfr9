include tfr9ports.gen.mk
L=${N9_LEVEL}

INCL=--includedir="$${HOME}/NEW/nitros9/defs/" \
     --includedir="$${HOME}/NEW/nitros9/level$L/tfr9/" 
PRAG=--pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  

all: sc6850.os9 term_sc6850.os9 term_tfr901.os9 console_tfr901.os9 init_tfr901.os9 tfrblock_tfr901.os9 dd_tfrblock.os9 level$Lprelude.raw _ROM1_ _ROM2_

sc6850.os9: sc6850.asm
	lwasm --6809 --format=os9 $(INCL) $(PRAG)  $< --output=$@ --list=$@.list --map=$@.map

term_sc6850.os9: term_sc6850.asm
	lwasm --6809 --format=os9 $(INCL) $(PRAG)  $< --output=$@ --list=$@.list --map=$@.map

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

level$Lprelude.raw: level$Lprelude.asm
	lwasm --6809 --format=raw $(INCL) $(PRAG)  $< --output=$@ --list=$@.list --map=$@.map

_ROM1_: _FORCE_
	sh boot-rom-maker.sh
	python3 ../binary-header-generator.py level1.rom > ../tmanager/level1.rom.h
	python3 ../binary-header-generator.py level1.rom > ../tmanager903/level1.rom.h

_ROM2_: _FORCE_
	sh boot-two-maker-level2.sh
	python3 ../binary-header-generator.py _level2.track35 > ../tmanager/level2.rom.h
	python3 ../binary-header-generator.py _level2.track35 > ../tmanager903/level2.rom.h

clean:
	rm -f *.raw *.os9 *.list *.map

_FORCE_:
