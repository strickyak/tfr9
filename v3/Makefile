S:=$(shell ( cd ../.. ; /bin/pwd ))
export COCO_SHELF:=$S
export PICOTOOL_FETCH_FROM_GIT_PATH:=$(shell echo $${PICOTOOL_FETCH_FROM_GIT_PATH:-$S/picotool})
BUILD_DIR=build-all-905-pico2
DISKS=generated/disk0,generated/level1.dsk,generated/level2.dsk,generated/disk3,generated/disk4,generated/disk5,generated/disk6,generated/disk7

all: _primary_

flash:
	cp -vf $(BUILD_DIR)/tmanager.uf2 /media/$$USER/RP*/.

run:
	./tconsole.linux-amd64.exe  -disks '$(DISKS)'  -borges /tmp/borges  2>_log

_primary_:
	sh make-all-905-pico2.sh

_secondary_: _make_$(BUILD_DIR) _tconsoles_

$(BUILD_DIR):
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)

_make_$(BUILD_DIR): $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake \
        -D'OS_LEVEL'='$(OS_LEVEL)' \
        -D'TRACKING'='$(TRACKING)' \
        ../tmanager905/pico2
	make -C n9cmds all install
	make -C $(BUILD_DIR)


GO_BUILD_1 = go build --tags=level1,coco1 -ldflags '-extldflags "-static"'
GO_BUILD_2 = go build --tags=level2,coco3 -ldflags '-extldflags "-static"'

TCONSOLE_SRC:=$(shell echo tconsole/*.go)

_tconsoles_ : \
    tconsole.linux-amd64.exe \
    tconsole.linux-386.exe \
    tconsole.linux-arm-7.exe \
    tconsole.linux-arm64.exe \
    tconsole.win-amd64.exe \
    tconsole.win-386.exe \
    tconsole.mac-arm64.exe \
    tconsole.mac-amd64.exe \
    ##

tconsole.linux-amd64.exe : $(TCONSOLE_SRC)
	GOOS=linux GOARCH=amd64       $(GO_BUILD_1) -o $@ $$PWD/tconsole
tconsole.linux-386.exe : $(TCONSOLE_SRC)
	GOOS=linux GOARCH=386         $(GO_BUILD_1) -o $@ $$PWD/tconsole
tconsole.linux-arm-7.exe : $(TCONSOLE_SRC)
	GOOS=linux GOARCH=arm GOARM=7 $(GO_BUILD_1) -o $@ $$PWD/tconsole
tconsole.linux-arm64.exe : $(TCONSOLE_SRC)
	GOOS=linux GOARCH=arm64       $(GO_BUILD_1) -o $@ $$PWD/tconsole
tconsole.win-amd64.exe : $(TCONSOLE_SRC)
	GOOS=windows GOARCH=amd64     $(GO_BUILD_1) -o $@ $$PWD/tconsole
tconsole.win-386.exe : $(TCONSOLE_SRC)
	GOOS=windows GOARCH=386       $(GO_BUILD_1) -o $@ $$PWD/tconsole
tconsole.mac-arm64.exe : $(TCONSOLE_SRC)
	GOOS=darwin GOARCH=arm64      $(GO_BUILD_1) -o $@ $$PWD/tconsole
tconsole.mac-amd64.exe : $(TCONSOLE_SRC)
	GOOS=darwin GOARCH=amd64      $(GO_BUILD_1) -o $@ $$PWD/tconsole
