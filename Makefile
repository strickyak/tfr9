all:
	make -C v4/ all
	echo "RESULTS ARE IN v4/build" >&2

clean:
	make -C v4/ clean

# See v4/Makefile for targets "flash" and "run".
