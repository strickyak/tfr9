all: BUILD-DIR build/tfr911h_v4.uf2 TETHERS LISTING-FILES

flash: build/tfr911h_v4.uf2
	cp -fv $< /media/$$USER/RP*

run:
	sh tether.sh

clean:
	rm -rf build tfr911h/build _log

##########################################################

ifndef TOP
       TOP = $(shell cd ../.. && pwd)
endif

TETHER_GO_FILES = $(shell find tether/ -type f -name \*.go)

BUILD-DIR:
	mkdir -p build tfr911h/build

build/tfr911h_v4.uf2: BUILD-DIR
	cd tfr911h/build && PATH="$(TOP)/bin:$$PATH" PICOTOOL_DIR="$(TOP)/picotool-install" PICO_SDK_PATH="$(TOP)/pico-sdk/" cmake ..
	cd tfr911h/build && PATH="$(TOP)/bin:$$PATH" PICOTOOL_DIR="$(TOP)/picotool-install" PICO_SDK_PATH="$(TOP)/pico-sdk/" make -j4
	cp -fv tfr911h/build/tfr911h_v4.uf2 build/tfr911h_v4.uf2
	ls -l $$(pwd)/build/tfr911h_v4.uf2

verbosely-build/tfr911h_v4.uf2: BUILD-DIR
	cd tfr911h/build && PATH="$(TOP)/bin:$$PATH" PICOTOOL_DIR="$(TOP)/picotool-install" PICO_SDK_PATH="$(TOP)/pico-sdk/" cmake ..
	cd tfr911h/build && PATH="$(TOP)/bin:$$PATH" PICOTOOL_DIR="$(TOP)/picotool-install" PICO_SDK_PATH="$(TOP)/pico-sdk/" cmake --build . --verbose
	cp -fv tfr911h/build/tfr911h_v4.uf2 build/tfr911h_v4.uf2
	ls -l $$(pwd)/build/tfr911h_v4.uf2

LISTING-FILES: BUILD-DIR
	cd build && unzip -u ../misc/listings.zip

TETHERS:  BUILD-DIR  \
	build/tether.linux-amd64.exe \
	build/tether.linux-386.exe  \
	build/tether.linux-arm-7.exe \
	build/tether.linux-arm64.exe \
	build/tether.win-amd64.exe  \
	build/tether.win-386.exe    \
	build/tether.mac-arm64.exe  \
	build/tether.mac-amd64.exe

build/tether.linux-amd64.exe: $(TETHER_GO_FILES)
	GOOS=linux   GOARCH=amd64       go build -o $@ ./tether/
build/tether.linux-386.exe: $(TETHER_GO_FILES)
	GOOS=linux   GOARCH=386         go build -o $@ ./tether/
build/tether.linux-arm-7.exe: $(TETHER_GO_FILES)
	GOOS=linux   GOARCH=arm GOARM=7 go build -o $@ ./tether/
build/tether.linux-arm64.exe: $(TETHER_GO_FILES)
	GOOS=linux   GOARCH=arm64       go build -o $@ ./tether/
build/tether.win-amd64.exe: $(TETHER_GO_FILES)
	GOOS=windows GOARCH=amd64       go build -o $@ ./tether/
build/tether.win-386.exe: $(TETHER_GO_FILES)
	GOOS=windows GOARCH=386         go build -o $@ ./tether/
build/tether.mac-arm64.exe: $(TETHER_GO_FILES)
	GOOS=darwin   GOARCH=arm64      go build -o $@ ./tether/
build/tether.mac-amd64.exe: $(TETHER_GO_FILES)
	GOOS=darwin   GOARCH=amd64      go build -o $@ ./tether/

install-pico-sdk-and-picotool-on-shelf:
	sh misc/install-pico-sdk-and-picotool-on-shelf.sh
