all: FIRMWARE MISC_BUILD misc/basefs.zip

TOP = $(shell cd ../../.. && pwd)

PICOENV = PATH="$(TOP)/bin/:$$PATH" PICOTOOL_DIR=$(TOP) PICO_SDK_PATH=$(TOP)/pico-sdk/

FIRMWARE:
	mkdir -p firmware/build
	( cd firmware/build && $(PICOENV) cmake .. )
	( cd firmware/build && $(PICOENV) make )
	mkdir -p build
	cp -fv firmware/build/centipede.uf2 build

CENTISCOPE-FIRMWARE:
	mkdir -p centiscope-firmware/build
	( cd centiscope-firmware/build && $(PICOENV) cmake .. )
	( cd centiscope-firmware/build && $(PICOENV) make )
	mkdir -p build
	cp -fv centiscope-firmware/build/centiscope.uf2 build

flash: FIRMWARE
	cp -fv firmware/build/centipede.uf2 /media/$$USER/RP*/.

run:
	sh "tether.sh" 2>&1 | tee _out

MISC_BUILD:
	mkdir -p build
	cd build && unzip -u ../misc/coco2.zip
	cd build && unzip -u ../misc/opil-2026-04-22.zip
	rm -f misc/basefs.zip
	cd basefs && zip -r ../build/basefs.zip $$(find [a-z]* -type f -name [a-z]\* | LC_ALL=C sort)
	make -C ../../v4/ TETHERS
	cp -fv ../../v4/build/tether.*.exe build/
	cp misc/bundled-tether-script.sh build/tether.sh
	chmod +x build/tether.sh

misc/basefs.zip: $(shell find basefs -type f -name [a-z]\*)
	rm -f misc/basefs.zip
	cd basefs && zip -r ../misc/basefs.zip $$(find [a-z]* -type f -name [a-z]\* | LC_ALL=C sort)

clean:
	rm -rf firmware/build
	rm -rf build
