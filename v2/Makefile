ROM_BASIC=$$HOME/coco-shelf/toolshed/cocoroms/bas13.rom
ROM_XBASIC=$$HOME/coco-shelf/toolshed/cocoroms/extbas11.rom
ROM_N9LEVEL1=$$HOME/NEW/nitros9/level1/tfr9/bootroms/nos96809l1.rom

all: _GENERATED_FILES_ _n9drivers_

_GENERATED_FILES_:
	rm -f generated/*
	mkdir -p generated
	python3 tfr9ports.py
	sh create-dd.sh generated/level1.disk
	sh create-dd.sh generated/level2.disk

_n9drivers_:
	make -C n9drivers
	python3 binary-header-generator.py n9drivers/level1.rom > tmanager905/level1.rom.h
	python3 binary-header-generator.py n9drivers/level1.rom > tmanager905-pico2/level1.rom.h
	:
	mkdir -p build-tmanager905
	cd build-tmanager905 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  cmake ../tmanager905
	cd build-tmanager905 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  make
	:
	mkdir -p build-tmanager905-pico2
	cd build-tmanager905-pico2 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  cmake ../tmanager905-pico2
	cd build-tmanager905-pico2 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  make

re_905:
	cd build-tmanager905 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  make
