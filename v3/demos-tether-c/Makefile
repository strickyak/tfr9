all: getput.gcc getputstr.gcc

CFLAGS= -std=gnu99 -O1 -fwhole-program -fomit-frame-pointer -I. -I..

LWASM_PRAGMA= --pragma=cescapes,dollarnotlocal,noforwardrefmax

ASM = lwasm $(LWASM_PRAGMA) --format=srec --define=ORIGIN=0x0800

getputstr.gcc: getputstr.c
	gcc6809 $(CFLAGS) -S $< -o $@.tmp.s
	sh wrap-gcc-asm.sh < $@.tmp.s > $@.tmp.asm
	$(ASM) $@.tmp.asm -o$@.srec

getput.gcc: getput.c
	gcc6809 $(CFLAGS) -S $< -o $@.tmp.s
	sh wrap-gcc-asm.sh < $@.tmp.s > $@.tmp.asm
	$(ASM) $@.tmp.asm -o$@.srec

clean:
	rm -f *.srec *.tmp.asm *.s *.srec.list *.srec.map _*

ci:
	ci-l *.c *.h Makefile *.sh

fmt:
	clang-format -i --style=Google *.c *.h
