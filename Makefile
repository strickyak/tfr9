all: build install run

build:
	:
	make -C $$HOME/coco-shelf/toolshed/cocoroms
	cat $$HOME/coco-shelf/toolshed/cocoroms/extbas11.rom $$HOME/coco-shelf/toolshed/cocoroms/bas13.rom | python3 firmware/c/generate-bootrom-header.py > firmware/c/basic.rom.h
	:
	make -C nitros9
	mkdir -p firmware/c/build 
	cd firmware/c/build && cmake ..
	make -C firmware/c/build

install:
	cp -v firmware/c/build/tfr901main.uf2 /media/strick/RPI-RP2/

run:
	go run console/console.go 2>_2 | tee _1
	cat _2 | sed 's/^[!][#][ ]//' > _8 ;  gop run /sy/tfr9/listings/join/join.go < nitros9/boot.rom  --borges ~/borges/ --runlog  _8 >  _9 ; vi _9

...TODO...:
	/home/strick/coco-shelf/nitros9/level1/coco1/NOS9_6809_L1_v030300_coco1_80d.dsk
