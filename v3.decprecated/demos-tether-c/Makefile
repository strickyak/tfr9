all: getput.gcc getputstr.gcc spc476-ans-forth

CFLAGS= -std=gnu99 -Os -fwhole-program -fomit-frame-pointer -I. -I..

LWASM_PRAGMA= --pragma=cescapes,dollarnotlocal,noforwardrefmax

ASM = lwasm $(LWASM_PRAGMA) --format=srec

getputstr.gcc: getputstr.c
	gcc6809 $(CFLAGS) -S $< -o $@.tmp.s
	sh wrap-gcc-asm.sh < $@.tmp.s > $@.tmp.asm
	$(ASM) $@.tmp.asm -o$@.srec --define=ORIGIN=0x0800

getput.gcc: getput.c
	gcc6809 $(CFLAGS) -S $< -o $@.tmp.s
	sh wrap-gcc-asm.sh < $@.tmp.s > $@.tmp.asm
	$(ASM) $@.tmp.asm -o$@.srec --define=ORIGIN=0x0800

spc476-ans-forth: spc476-ans-forth.c
	gcc6809 $(CFLAGS) -S $< -o $@.tmp.s -D'FORTH_ORIGIN'=0x1000
	sh wrap-gcc-asm.sh < $@.tmp.s > $@.tmp.1
	A=$$(awk '$$2 == "forth__ds_top" { print "$$" $$5 }'  _forth.lwasm.decb.map) ;\
	  sed -e "s/{{forth__ds_top}}/$$A/" < $@.tmp.1 > $@.tmp.2
	A=$$(awk '$$2 == "forth__rs_top" { print "$$" $$5 }'  _forth.lwasm.decb.map) ;\
	  sed -e "s/{{forth__rs_top}}/$$A/" < $@.tmp.2 > $@.tmp.3
	A=$$(awk '$$2 == "forth_core_quit.xt" { print "$$" $$5 }'  _forth.lwasm.decb.map) ;\
	  sed -e "s/{{forth_core_quit.xt}}/$$A/" < $@.tmp.3 > $@.tmp.4
	A=$$(awk '$$2 == "forth_core_execute.asm" { print "$$" $$5 }'  _forth.lwasm.decb.map) ;\
	  sed -e "s/{{forth_core_execute.asm}}/$$A/" < $@.tmp.4 > $@.tmp.asm
	$(ASM) $@.tmp.asm -o$@.srec --define=ORIGIN=0xF400 -list=$@.srec.list -map=$@.srec.map

wrap-forth: wrap-forth.c
	gcc6809 $(CFLAGS) -S $< -o $@.tmp.s $$(awk -f symbols.awk < /gh/spc476/ANS-Forth/forth.bin.list)
	sh wrap-gcc-asm.sh < $@.tmp.s > $@.tmp.asm
	$(ASM) $@.tmp.asm -o$@.srec --define=ORIGIN=0xF400 -list=$@.srec.list -map=$@.srec.map $$(awk -f symbols.awk < /gh/spc476/ANS-Forth/forth.bin.list)


clean:
	rm -f *.srec *.tmp.asm *.s *.srec.list *.srec.map _*

ci:
	ci-l *.c *.h Makefile *.sh

fmt:
	clang-format -i --style=Google *.c *.h
