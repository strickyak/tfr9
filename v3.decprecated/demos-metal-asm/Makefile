all: hello.srec blink.srec display.srec count-g12-g19.srec inputs-g12-g19.srec ditto-g12-g19-even-in-odd-out.srec 

hello.srec: hello.asm
	lwasm --format=srec --output=$@  --list=$@.list  --map=$@.map  $<

blink.srec: blink.asm
	lwasm --format=srec --output=$@  --list=$@.list  --map=$@.map  $<

display.srec: display.asm
	lwasm --format=srec --output=$@  --list=$@.list  --map=$@.map  $<

count-g12-g19.srec: count-g12-g19.asm
	lwasm --format=srec --output=$@  --list=$@.list  --map=$@.map  $<

inputs-g12-g19.srec: inputs-g12-g19.asm
	lwasm --format=srec --output=$@  --list=$@.list  --map=$@.map  $<

ditto-g12-g19-even-in-odd-out.srec: ditto-g12-g19-even-in-odd-out.asm
	lwasm --format=srec --output=$@  --list=$@.list  --map=$@.map  $<

clean:
	rm -f *.list *.map *.srec *.decb a.out

ci:
	ci-l Makefile *.asm
