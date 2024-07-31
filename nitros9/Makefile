all: term_tfr901.os9 console_tfr901.os9 init_tfr901.os9

term_tfr901.os9: term_tfr901.asm
	lwasm --6809 --format=os9 --includedir="$${HOME}/coco-shelf/nitros9/defs/" --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  $< --output=$@ --list=$@.list --map=$@.map

console_tfr901.os9: console_tfr901.asm
	lwasm --6809 --format=os9 --includedir="$${HOME}/coco-shelf/nitros9/defs/" --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  $< --output=$@ --list=$@.list --map=$@.map

init_tfr901.os9: init_tfr901.asm
	lwasm --6809 --format=os9 --includedir="$${HOME}/coco-shelf/nitros9/defs/" --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax  $< --output=$@ --list=$@.list --map=$@.map

boot.rom: _FORCE_
	( \
  ( cd "$${HOME}/coco-shelf/nitros9/level1/coco1/cmds" ; \
  cat shell_21 procs mdir \
  ) ; \
  cat term_tfr901.os9 console_tfr901.os9 init_tfr901.os9 ; \
  ( cd "$${HOME}/coco-shelf/nitros9/level1/coco1/modules" ; \
  cat ddh0_emudsk.dd emudsk.dr ; \
  cat clock2_messemu ; \
  cat sysgo_dd ; \
  cat rbf.mn scf.mn ioman krnp2 ; \
  ) ; \
  ) > $@
	ls -l $@
	wc -c $@
	python3 -c "print( `wc -c < $@` + 17 + 3*(2**14) )"
	python3 -c "print(hex( `wc -c < $@` + 17 + 3*(2**14) ))"
	( \
  ( cd "$${HOME}/coco-shelf/nitros9/level1/coco1/modules" ; \
  cat krn ; \
  ) ; \
  ) >> $@
	ls -l $@
	wc -c $@


clean:
	rm -f *.os9 *.list *.map

_FORCE_:
