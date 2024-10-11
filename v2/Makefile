ROM_BASIC=$$HOME/coco-shelf/toolshed/cocoroms/bas13.rom
ROM_XBASIC=$$HOME/coco-shelf/toolshed/cocoroms/extbas11.rom
ROM_N9LEVEL1=$$HOME/NEW/nitros9/level1/tfr9/bootroms/nos96809l1.rom

all: _GENERATED_FILES_

_GENERATED_FILES_:
	rm -f generated/*
	mkdir -p generated
	python3 binary-header-generator.py $(ROM_BASIC) $(ROM_XBASIC) > generated/rom-basic16k.h 
	python3 binary-header-generator.py $(ROM_N9LEVEL1) > generated/rom-n9level1.h
	py rpc-compiler.py rpc.txt generated/rpc.h generated/rpc.go
