all: run-coco2

run: run-coco2

run-coco2: ../generated/diskbasic.0x8000.rom ../generated/floppy0
	: HINT :
	: HINT :
	: HINT :   After the Basic Rom is loaded, type 'c9'
	: HINT :
	: HINT :   You will be running Disk Basic on a coco2 with 40-track floppy disk 0 mounted.
	: HINT :   Try typing 'dir'
	: HINT :
	: HINT :
	../generated/tconsole.linux-amd64.exe --vdg_text --disks f0:../generated/floppy0 -no_modules -abslists ../generated/diskbasic.0x8000.rom.list  ../generated/diskbasic.0x8000.rom   2>/tmp/log

../generated/floppy0:
	mkdir -p ../generated
	decb dskini -4 $@
	decb copy -0 -r COUNT.BAS $@,COUNT.BAS
	decb dir $@


../generated/diskbasic.0x8000.rom:
	mkdir -p ../generated
	( cd ../../../toolshed/cocoroms ; cat extbas11.rom bas13.rom disk11.rom ) > $@
	( cd ../../../toolshed/cocoroms ; extbas11.rom.list bas13.rom.list disk11.rom.list ) > $@.list
