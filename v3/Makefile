all: make_$(BUILD_DIR) cross-compile-tconsoles

$(BUILD_DIR):
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)

make_$(BUILD_DIR): $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake \
        -D'OS_LEVEL'='$(OS_LEVEL)' \
        -D'TRACK_RAM'='$(TRACK_RAM)' \
        -D'TRACE_CYCLES'='$(TRACE_CYCLES)' \
        -D'TRACE_SWI2'='$(TRACE_SWI2)' \
        ../tmanager905/pico2
	make -C $(BUILD_DIR)


GO_BUILD = go build --tags=level2,coco3 -ldflags '-extldflags "-static"'

cross-compile-tconsoles:
	GOOS=linux GOARCH=amd64       $(GO_BUILD) -o tconsole.linux-amd64.exe $$PWD/tconsole
	GOOS=linux GOARCH=386         $(GO_BUILD) -o tconsole.linux-386.exe $$PWD/tconsole
	GOOS=linux GOARCH=arm GOARM=7 $(GO_BUILD) -o tconsole.linux-arm-7.exe $$PWD/tconsole
	GOOS=linux GOARCH=arm64       $(GO_BUILD) -o tconsole.linux-arm64.exe $$PWD/tconsole
	GOOS=windows GOARCH=amd64     $(GO_BUILD) -o tconsole.win-amd64.exe $$PWD/tconsole
	GOOS=windows GOARCH=386       $(GO_BUILD) -o tconsole.win-386.exe $$PWD/tconsole
	GOOS=darwin GOARCH=arm64      $(GO_BUILD) -o tconsole.mac-arm64.exe $$PWD/tconsole
	GOOS=darwin GOARCH=amd64      $(GO_BUILD) -o tconsole.mac-amd64.exe $$PWD/tconsole



##
##_GENERATED_FILES_:
##	rm -f generated/*
##	mkdir -p generated
##	python3 tfr9ports.py
##	sh create-dd.sh generated/level1.disk
##	sh create-dd.sh generated/level2.disk
##
##_n9drivers_:
##	make -C n9drivers
##	python3 binary-header-generator.py n9drivers/level1.rom > tmanager905/level1.rom.h
##	python3 binary-header-generator.py n9drivers/level1.rom > tmanager905-pico2/level1.rom.h
##	:
##	mkdir -p build-tmanager905
##	cd build-tmanager905 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  cmake ../tmanager905
##	cd build-tmanager905 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  make
##	:
##	mkdir -p build-tmanager905-pico2
##	cd build-tmanager905-pico2 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  cmake ../tmanager905-pico2
##	cd build-tmanager905-pico2 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  make
##
##re_905:
##	cd build-tmanager905 && PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=$$HOME/modoc/coco-shelf/pico-sdk  make
