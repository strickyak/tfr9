all: BUILD-DIR build/turbolab_v4.uf2 build/tether

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

build/tether: $(shell find tether/ -type f -name \*.go)
	cd tether && GOWORK=off go build -o ../build/tether .
	ls -lh build/tether

flash: build/turbolab_v4.uf2
	cp -fv $< /media/$$USER/RP*

clean:
	rm -rf build

.PHONY: all clean flash BUILD-DIR
