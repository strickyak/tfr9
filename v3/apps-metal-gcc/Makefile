all : count.decb random.decb

GCC6809_OPTS = -O2 -std=gnu99 -fno-builtin -fomit-frame-pointer -fwhole-program
LWASM_OPTS = --pragma=cescapes,pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax,export
NEEDS_LIBGCC =  -L'../../../lib/gcc/m6809-unknown/4.6.4/' -l'gcc'

count.decb : count.o
	lwlink --format=decb --entry=entry --output=$@ --map=$@.map --script=$@.script $<

count.o : count.c
	gcc6809 $(GCC6809_OPTS) -I../.. -S $< -o $@.s
	lwasm $(LWASM_OPTS) --format=obj -o'$@' --list='$<.list' --map='$<.map' $@.s

random.decb : random.o
	lwlink --format=decb --entry=entry --output=$@ --map=$@.map --script=$@.script $<

random.o : random.c
	gcc6809 $(GCC6809_OPTS) -I../.. -S $< -o $@.s
	lwasm $(LWASM_OPTS) --format=obj -o'$@' --list='$<.list' --map='$<.map' $@.s

clean:
	rm -f *.o *.s *.list *.map *.decb

ci:
	ci-l *.h *.c *.script Makefile
