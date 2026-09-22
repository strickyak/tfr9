all: BUILD-DIR build/turbolab_v4.uf2 TETHERS TURBOS

ifndef TOP
TOP := $(abspath ../../..)
endif

BUILD-DIR:
	mkdir -p build build/firmware

build/turbolab_v4.uf2: BUILD-DIR firmware/main.cpp firmware/hamster.pio firmware/CMakeLists.txt firmware/cobs.h firmware/pcb.h firmware/cross-core.h firmware/circbuf.h
	cd build/firmware && PATH="$(TOP)/bin:$$PATH" PICOTOOL_DIR="$(TOP)/picotool-install" PICO_SDK_PATH="$(TOP)/pico-sdk/" cmake ../../firmware
	cd build/firmware && PATH="$(TOP)/bin:$$PATH" PICOTOOL_DIR="$(TOP)/picotool-install" PICO_SDK_PATH="$(TOP)/pico-sdk/" make -j4
	cp -fv build/firmware/turbolab_v4.uf2 build/turbolab_v4.uf2
	ls -lh build/turbolab_v4.uf2

#build/tether: $(shell find tether/ -type f -name \*.go)
#	cd tether && GOWORK=off go build -o ../build/tether .
#	ls -lh build/tether
TETHER_GO_FILES = $(shell find tether/ -type f -name \*.go)
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
	CGO_ENABLED=0 GOOS=linux   GOARCH=amd64       go build -a -ldflags '-extldflags "-static"' -o $@ ./tether/
build/tether.linux-386.exe: $(TETHER_GO_FILES)
	CGO_ENABLED=0 GOOS=linux   GOARCH=386         go build -a -ldflags '-extldflags "-static"' -o $@ ./tether/
build/tether.linux-arm-7.exe: $(TETHER_GO_FILES)
	CGO_ENABLED=0 GOOS=linux   GOARCH=arm GOARM=7 go build -a -ldflags '-extldflags "-static"' -o $@ ./tether/
build/tether.linux-arm64.exe: $(TETHER_GO_FILES)
	CGO_ENABLED=0 GOOS=linux   GOARCH=arm64       go build -a -ldflags '-extldflags "-static"' -o $@ ./tether/
build/tether.win-amd64.exe: $(TETHER_GO_FILES)
	CGO_ENABLED=0 GOOS=windows GOARCH=amd64       go build -a -ldflags '-extldflags "-static"' -o $@ ./tether/
build/tether.win-386.exe: $(TETHER_GO_FILES)
	CGO_ENABLED=0 GOOS=windows GOARCH=386         go build -a -ldflags '-extldflags "-static"' -o $@ ./tether/
build/tether.mac-arm64.exe: $(TETHER_GO_FILES)
	CGO_ENABLED=0 GOOS=darwin   GOARCH=arm64      go build -a -ldflags '-extldflags "-static"' -o $@ ./tether/
build/tether.mac-amd64.exe: $(TETHER_GO_FILES)
	CGO_ENABLED=0 GOOS=darwin   GOARCH=amd64      go build -a -ldflags '-extldflags "-static"' -o $@ ./tether/

TURBOS: BUILD-DIR
	cd build && unzip -u ../data/turbos-2026-09-21.zip

flash: build/turbolab_v4.uf2
	cp -fv $< /media/$$USER/RP*

clean:
	rm -rf build/*

.PHONY: all clean flash BUILD-DIR TETHERS TURBOS
