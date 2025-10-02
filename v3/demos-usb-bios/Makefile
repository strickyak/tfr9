all: getput getputstr

CFLAGS= -std=gnu99 -O1 -fwhole-program -fomit-frame-pointer -I. -I..

PRAGMA= --pragma=cescapes,pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax,export

getputstr: getputstr.c
	gcc6809 $(CFLAGS) -S $<
	sh wrap-asm.sh < $@.s > $@.tmp.asm
	lwasm $(PRAGMA) --format=srec $@.tmp.asm -o$@.srec

getput: getput.c
	gcc6809 $(CFLAGS) -S $<
	sh wrap-asm.sh < $@.s > $@.tmp.asm
	lwasm $(PRAGMA) --format=srec $@.tmp.asm -o$@.srec

clean:
	rm -f *.srec *.tmp.asm *.s *.srec.list *.srec.map _*

ci:
	ci-l *.c *.h Makefile *.sh

fmt:
	clang-format -i --style=Google *.c *.h
