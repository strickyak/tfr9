all: build install run

build:
	make -C nitros9
	mkdir -p firmware/c/build 
	cd firmware/c/build && cmake ..
	make -C firmware/c/build

install:
	cp -v firmware/c/build/tfr901main.uf2 /media/strick/RPI-RP2/

run:
	go run console.go 2>_2 | tee _1
	cat _2 | sed 's/^[!][#][ ]//' > _8 ;  gop run /sy/tfr9/listings/join/join.go < ../../../nitros9/boot.rom  --borges ~/borges/ --runlog  _8 >  _9 ; vi _9

...TODO...:
	/home/strick/coco-shelf/nitros9/level1/coco1/NOS9_6809_L1_v030300_coco1_80d.dsk
