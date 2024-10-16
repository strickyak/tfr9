ROM_BASIC=$$HOME/coco-shelf/toolshed/cocoroms/bas13.rom
ROM_XBASIC=$$HOME/coco-shelf/toolshed/cocoroms/extbas11.rom
ROM_N9LEVEL1=$$HOME/NEW/nitros9/level1/tfr9/bootroms/nos96809l1.rom

all: _GENERATED_FILES_ _n9drivers_

_GENERATED_FILES_:
	rm -f generated/*
	mkdir -p generated
	sh create-dd.sh generated/level1.disk
	python3 binary-header-generator.py generated/level1.disk > tmanager/level1.disk.h

#	#python3 binary-header-generator.py $(ROM_BASIC) $(ROM_XBASIC) > generated/rom-basic16k.h 
#	#python3 binary-header-generator.py $(ROM_N9LEVEL1) > generated/rom-n9level1.h
#	#python3 rpc-compiler.py rpc.txt generated/rpc.h generated/rpc.go

_n9drivers_:
	make -C n9drivers
	python3 binary-header-generator.py n9drivers/level1.rom > tmanager/level1.rom.h
	mkdir -p build
	cd build && cmake ../tmanager903
	make -C build
