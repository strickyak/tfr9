# Usage:
#   Add ...../coco-shelf/bin to your PATH.
#   Set the SRC variable to a clone of github.com/spc476/ANS-Forth
#
# Example:
#   PATH="$HOME/coco-shelf/bin:$PATH" make SRC=/tmp/github.com/spc476/ANS-Forth

all: wrap-forth-a09 wrap-forth-lwasm

SRC=/gh/spc476/ANS-Forth
P=../demos-tether-c
CFLAGS= -std=gnu99 -Os -fwhole-program -fomit-frame-pointer -I. -I.. -I$P

LWASM_PRAGMA= --pragma=cescapes,dollarnotlocal,noforwardrefmax

ASM = lwasm $(LWASM_PRAGMA) --format=srec

wrap-forth-a09: wrap-forth.c grab-symbols-from-a09.awk $(SRC)/forth.asm $(SRC)/forth.bin
	make -C $(SRC) forth.bin
	cp -vf $(SRC)/forth.bin forth-a09.0.rom
	go run a09list2lw/a09list2lw.go < $(SRC)/forth.bin.list  > forth-a09.list
	awk -f grab-symbols-from-a09.awk < $(SRC)/forth.bin.list > forth-a09.defs
	set -x; gcc6809 $(CFLAGS) -S $< -o $@.tmp.s $$(cat forth-a09.defs)
	sh $P/wrap-gcc-asm.sh < $@.tmp.s > $@.tmp.asm
	set -x; $(ASM) $@.tmp.asm -o$@.srec --define=ORIGIN=0xF400 -list=$@.srec.list -map=$@.srec.map $$(cat forth-a09.defs)

wrap-forth-lwasm: wrap-forth.c grab-symbols-from-a09.awk $(SRC)/forth.asm $(SRC)/forth.bin
	go run a09asm2lw/a09asm2lw.go < $(SRC)/forth.asm  > forth-lwasm.asm
	$(ASM) forth-lwasm.asm --decb -o'forth-lwasm.decb' --define=ORIGIN=0x0000 -list=forth-lwasm.decb.list -map=forth-lwasm.decb.map
	awk -f grab-symbols-from-lwasm.awk < forth-lwasm.decb.map > forth-lwasm.defs
	set -x; gcc6809 $(CFLAGS) -S $< -o $@.tmp.s $$(cat forth-lwasm.defs)
	sh $P/wrap-gcc-asm.sh < $@.tmp.s > $@.tmp.asm
	set -x; $(ASM) $@.tmp.asm -o'$@.srec' --define=ORIGIN=0xF400 -list=$@.srec.list -map=$@.srec.map $$(cat forth-lwasm.defs)

run-wrap-forth-a09: wrap-forth-a09
	: HINT :
	: HINT :
	: HINT :   After the Forth Rom is loaded, type 'c3'
	: HINT :   [ or 'ce8' to debug ]
	: HINT :
	: HINT :
	../generated/tconsole.linux-amd64.exe  --disks /dev/null -no_modules -abslists  forth-a09.list,wrap-forth-a09.srec.list   forth-a09.0.rom wrap-forth-a09.srec 2>/tmp/log

run-wrap-forth-lwasm: wrap-forth-lwasm
	: HINT :
	: HINT :
	: HINT :   After the Forth Rom is loaded, type 'c3'
	: HINT :   [ or 'ce8' to debug ]
	: HINT :
	: HINT :
	../generated/tconsole.linux-amd64.exe  --disks /dev/null -no_modules -abslists  forth-lwasm.decb.list,wrap-forth-lwasm.srec.list   forth-lwasm.decb wrap-forth-lwasm.srec 2>/tmp/log

run: run-wrap-forth-a09

clean:
	rm -f *.decb *.srec *.tmp.asm *.s *.list *.map forth-a09.* forth-lwasm.*  ./_* *.defs

ci:
	ci-l *.c *.h Makefile *.sh

fmt:
	clang-format -i --style=Google *.c *.h
