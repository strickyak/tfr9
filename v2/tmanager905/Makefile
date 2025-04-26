all:
	py generate-bootrom-header.py < $$HOME/coco-shelf/toolshed/cocoroms/bas13.rom > bas13.rom.h 
	py generate-bootrom-header.py < $$HOME/NEW/nitros9/level1/tfr9/bootroms/nos96809l1.rom > boot.rom.h 
	python3 rpc-compiler.py usb.rpc
