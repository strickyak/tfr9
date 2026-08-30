all : count.decb random.decb count.srec random.srec
run: run-count

GCC6809_OPTS = -O2 -std=gnu99 -fno-builtin -fomit-frame-pointer -fwhole-program
LWASM_OPTS = --pragma=cescapes,pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax,export
NEEDS_LIBGCC =  -L'../../../lib/gcc/m6809-unknown/4.6.4/' -l'gcc'

count.decb : count.o
	lwlink --format=decb --entry=entry --output=$@ --map=$@.map --script=load1000.lwlink $<
count.srec : count.o
	lwlink --format=srec --entry=entry --output=$@ --map=$@.map --script=load1000.lwlink $<

count.o : count.c
	gcc6809 $(GCC6809_OPTS) -I../.. -S $< -o $@.s
	lwasm $(LWASM_OPTS) --format=obj -o'$@' --list='$<.list' --map='$<.map' $@.s

random.decb : random.o
	lwlink --format=decb --entry=entry --output=$@ --map=$@.map --script=load1000.lwlink $<
random.srec : random.o
	lwlink --format=srec --entry=entry --output=$@ --map=$@.map --script=load1000.lwlink $<

random.o : random.c
	gcc6809 $(GCC6809_OPTS) -I../.. -S $< -o $@.s
	lwasm $(LWASM_OPTS) --format=obj -o'$@' --list='$<.list' --map='$<.map' $@.s

run-count: count.srec
	: HINT :
	: HINT :
	: HINT :   After the S Records are loaded, type 'c3'
	: HINT :   [ or 'ce8' to debug ]
	: HINT :
	: HINT :
	../generated/tconsole.linux-amd64.exe 2>/tmp/log -no_modules  -linked -linklists count.o.list -linkmap count.srec.map count.srec

run-random: random.srec
	: HINT :
	: HINT :
	: HINT :   After the S Records are loaded, type 'c3'
	: HINT :   [ or 'ce8' to debug ]
	: HINT :
	: HINT :
	../generated/tconsole.linux-amd64.exe 2>/tmp/log -no_modules  -linked -linklists random.o.list -linkmap random.srec.map random.srec

clean:
	rm -f *.o *.s *.list *.map *.decb

ci:
	ci-l *.h *.c *.lwlink Makefile
