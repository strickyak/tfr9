all: hello.srec

hello.srec: hello.asm
	lwasm --format=srec --output=hello.srec  --list=hello.list hello.asm

clean:
	rm -f *.list *.map *.srec *.decb a.out

ci:
	ci-l Makefile *.asm
