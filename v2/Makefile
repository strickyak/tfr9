ROM_BASIC=$$HOME/coco-shelf/toolshed/cocoroms/bas13.rom
ROM_XBASIC=$$HOME/coco-shelf/toolshed/cocoroms/extbas11.rom
ROM_N9LEVEL1=$$HOME/NEW/nitros9/level1/tfr9/bootroms/nos96809l1.rom

all: _GENERATED_FILES_ _n9drivers_

_GENERATED_FILES_:
	rm -f generated/*
	mkdir -p generated
	python3 tfr9ports.py
	sh create-dd.sh generated/level1.disk
	: python3 binary-header-generator.py generated/level1.disk > tmanager901/level1.disk.h
	: python3 binary-header-generator.py generated/level1.disk > tmanager903/level1.disk.h
	sh create-dd.sh generated/level2.disk
	: python3 binary-header-generator.py generated/level2.disk > tmanager901/level2.disk.h
	: python3 binary-header-generator.py generated/level2.disk > tmanager903/level2.disk.h
	python3 binary-header-generator.py $(ROM_BASIC) > coco2/cb.rom.h
	python3 binary-header-generator.py $(ROM_XBASIC) > coco2/ecb.rom.h

#	#python3 binary-header-generator.py $(ROM_BASIC) $(ROM_XBASIC) > generated/rom-basic16k.h 
#	#python3 binary-header-generator.py $(ROM_N9LEVEL1) > generated/rom-n9level1.h
#	#python3 rpc-compiler.py rpc.txt generated/rpc.h generated/rpc.go

_n9drivers_:
	make -C n9drivers
	python3 binary-header-generator.py n9drivers/level1.rom > tmanager901/level1.rom.h
	python3 binary-header-generator.py n9drivers/level1.rom > tmanager903/level1.rom.h
	python3 binary-header-generator.py n9drivers/level1.rom > tmanager905/level1.rom.h
	python3 binary-header-generator.py n9drivers/level1.rom > tmanager905-pico2/level1.rom.h
	: python3 binary-header-generator.py generated/level2.disk > tmanager901/level2.disk.h
	: python3 binary-header-generator.py generated/level2.disk > tmanager903/level2.disk.h
	:
	#mkdir -p build-wifi-scanner
	#cd build-wifi-scanner && PICOTOOL_FETCH_FROM_GIT_PATH=$$HOME/pico/picotool/  cmake ../wifi-scanner/ -D'PICO_BOARD=pico_w'
	#cd build-wifi-scanner && make
	:
	#mkdir -p build-tmanager901
	#cd build-tmanager901 && PICOTOOL_FETCH_FROM_GIT_PATH=$$HOME/pico/picotool/  cmake ../tmanager901 -D'PICO_BOARD=pico'
	#cd build-tmanager901 && make
	:
	mkdir -p build-tmanager903
	cd build-tmanager903 && PICOTOOL_FETCH_FROM_GIT_PATH=$$HOME/pico/picotool/  cmake ../tmanager903 -D'PICO_BOARD=pico_w'
	cd build-tmanager903 && make
	:
	mkdir -p build-tmanager905
	cd build-tmanager905 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  cmake ../tmanager905 -D'PICO_BOARD=pico'
	cd build-tmanager905 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  make
	:
	mkdir -p build-tmanager905-pico2
	cd build-tmanager905-pico2 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  cmake ../tmanager905-pico2 -D'PICO_BOARD=pico'
	cd build-tmanager905-pico2 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  make
	:
	mkdir -p build-coco2
	cd build-coco2 && PICOTOOL_FETCH_FROM_GIT_PATH=$$HOME/pico/picotool/  cmake ../coco2 -D'PICO_BOARD=pico_w'
	cd build-coco2 && make
