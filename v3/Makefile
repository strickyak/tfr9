# See help:

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
	DEST:=/Volumes
	TCONSOLE:=generated/tconsole.mac-arm64.exe
	TCONSOLE_WIRE:=/dev/cu.usbmodem8333401
else
	DEST:=/media/$$USER
	TCONSOLE:=generated/tconsole.linux-amd64.exe
	TCONSOLE_WIRE:=/dev/ttyACM0
endif

PWD:=$(shell /bin/pwd)
S:=$(shell ( cd ../.. ; /bin/pwd ))
export COCO_SHELF:=$S
export PICOTOOL_FETCH_FROM_GIT_PATH:=$(shell echo $${PICOTOOL_FETCH_FROM_GIT_PATH:-$S/picotool})
BUILD_DIR=build-all-905-pico2
DISKS=generated/disk0,generated/level1.dsk,generated/level2.dsk,generated/disk3,generated/disk4,generated/disk5,generated/disk6,generated/disk7
WHICH_PICO=pico2

all: _primary_  _apps-metal-asm_ _borges_  _tconsoles_
	:
	:
	: tfr9/v3 OKAY.
	:
	: FIRMWARE IS IN $(PWD)/generated/tmanager.pico1_or_pico2.uf2
	:
	: RESULTS ARE IN $(PWD)/generated/
	:
	: HINT: chdir to $(PWD) and "make help"
	:

flash:
	cp -vf generated/tmanager.pico1_or_pico2.uf2 $(DEST)/RP*/.

run:
	generated/tconsole.linux-amd64.exe  -disks '$(DISKS)'  -borges /tmp/borges  -load '$(LOAD)' 2>_log

help:
	: Just to build files:
	:     make all
	: To build files and then flash the pico:
	:     make all flash
	: To do all that and then run the computer:
	:     make all flash run
	: Then type 0, 1, or 2 for TurbOS, NitrOS-9 Level1 or Level2.

_primary_:
	mkdir -p generated
	sh make-all-905-both_1_2.sh
	cat build-all-905-pico2/tmanager.uf2 build-all-905-pico1/tmanager.uf2 > generated/tmanager.pico1_or_pico2.uf2

_secondary_:  _make_$(BUILD_DIR)

$(BUILD_DIR):
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)

_make_$(BUILD_DIR): $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake \
        -D'OS_LEVEL'='$(OS_LEVEL)' \
        -D'TRACKING'='$(TRACKING)' \
        ../tmanager905/$(WHICH_PICO)
	make -C n9cmds all install
	make -C $(BUILD_DIR)

_apps-metal-asm_:
	PATH="$S/bin:$$PATH" make -C apps-metal-asm all

_borges_:
	mkdir -p /tmp/borges
	go run borges-saver/borges-saver.go  -outdir /tmp/borges/  ../../turbos/

GO_BUILD_1 = go build --tags=level1,coco1 -ldflags '-extldflags "-static"'
GO_BUILD_2 = go build --tags=level2,coco3 -ldflags '-extldflags "-static"'

TCONSOLE_SRC:=$(shell echo tconsole/*.go)

_tconsoles_ : \
    generated/tconsole.linux-amd64.exe \
    generated/tconsole.linux-386.exe \
    generated/tconsole.linux-arm-7.exe \
    generated/tconsole.linux-arm64.exe \
    generated/tconsole.win-amd64.exe \
    generated/tconsole.win-386.exe \
    generated/tconsole.mac-arm64.exe \
    generated/tconsole.mac-amd64.exe \
    ##

generated/tconsole.linux-amd64.exe : $(TCONSOLE_SRC)
	GOOS=linux GOARCH=amd64       $(GO_BUILD_1) -o $@ $$PWD/tconsole
generated/tconsole.linux-386.exe : $(TCONSOLE_SRC)
	GOOS=linux GOARCH=386         $(GO_BUILD_1) -o $@ $$PWD/tconsole
generated/tconsole.linux-arm-7.exe : $(TCONSOLE_SRC)
	GOOS=linux GOARCH=arm GOARM=7 $(GO_BUILD_1) -o $@ $$PWD/tconsole
generated/tconsole.linux-arm64.exe : $(TCONSOLE_SRC)
	GOOS=linux GOARCH=arm64       $(GO_BUILD_1) -o $@ $$PWD/tconsole
generated/tconsole.win-amd64.exe : $(TCONSOLE_SRC)
	GOOS=windows GOARCH=amd64     $(GO_BUILD_1) -o $@ $$PWD/tconsole
generated/tconsole.win-386.exe : $(TCONSOLE_SRC)
	GOOS=windows GOARCH=386       $(GO_BUILD_1) -o $@ $$PWD/tconsole
generated/tconsole.mac-arm64.exe : $(TCONSOLE_SRC)
	GOOS=darwin GOARCH=arm64      $(GO_BUILD_1) -o $@ $$PWD/tconsole
generated/tconsole.mac-amd64.exe : $(TCONSOLE_SRC)
	GOOS=darwin GOARCH=amd64      $(GO_BUILD_1) -o $@ $$PWD/tconsole

clean:
	rm -rf build
	rm -rf build-all-905-pico1
	rm -rf build-all-905-pico2
	mkdir -p generated
	mkdir -p /tmp/tfr9.v3.generated.backup/
	rsync -av generated/ /tmp/tfr9.v3.generated.backup/
	rm -rf generated
	make -C apps-metal-asm clean

create-runtime-bundle:
	set -ex ; \
  test -s doc/README.runtime-bundle-`date +%Y-%m-%d`.md && \
  B=tfr905-runtime-bundle-`date +%Y-%m-%d-%H-%M` && \
  mkdir $$B && \
  cp -rv doc/README.runtime-bundle-`date +%Y-%m-%d`.md create-nitros9disk.sh Makefile generated $$B/ && \
  zip $$B.zip ` find $$B -type f ! -name '*,v' -name '[A-Za-z]*' | LANG=C sort`
