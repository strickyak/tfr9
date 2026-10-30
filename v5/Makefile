# tfr9 v5 top-level Makefile.  All output goes under build/.
# the coco-shelf (parent of tfr9/); override with  make SHELF=/other/shelf
SHELF ?= $(abspath ../..)
export SHELF

# parallel jobs: getconf works on Linux and macOS
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

.PHONY: help doctor check-sh check-core1 clean tfr911h test-host tether tethers gen-ports
help:
	@echo "targets:"
	@echo "  tfr911h  build the TFR/911H firmware -> build/tfr911h.uf2"
	@echo "  tether   build the Go tether for this machine -> build/tether"
	@echo "  tethers  cross-build the 8 tether binaries -> build/tether.*.exe"
	@echo "  doctor   check toolchain (SHELF=$(SHELF))"
	@echo "  gen-ports generate build/generated/{ports_gen.h,io-map.md} from firmware/engines/ports.tsv"
	@echo "  test-host run the no-board host tests (C++ cobs test + go test)"
	@echo "  check-core1 report flash calls made from SRAM (core-1) code in build/tfr911h"
	@echo "  check-sh lint scripts for bashisms (macOS-friendly sh)"
	@echo "  clean    rm -rf build/"
	@echo "(firmware/tether/test targets arrive in Phase 1+)"

doctor:
	@sh scripts/doctor.sh

test-host:
	@sh scripts/gen-ports.sh
	@$(MAKE) -C tests/host test
	@V5=$(CURDIR); . scripts/env.sh; cd host && GOWORK=off go test ./...

gen-ports:
	@sh scripts/gen-ports.sh

tether:
	@sh scripts/build-tethers.sh native

tethers:
	@sh scripts/build-tethers.sh all

check-core1:
	@sh scripts/check-core1-sram.sh build/tfr911h/tfr911h_v4.elf

check-sh:
	@sh scripts/check-sh.sh

# Configure + build with the v5 environment (scripts/env.sh); everything lands in build/.
tfr911h:
	@V5=$(CURDIR); . scripts/env.sh; mkdir -p build/logs build/tfr911h; \
	echo "cmake: build/logs/tfr911h-cmake.log, make: build/logs/tfr911h-make.log"; \
	cmake -S firmware/products/tfr911h -B build/tfr911h -Dpicotool_DIR="$$picotool_DIR" >build/logs/tfr911h-cmake.log 2>&1 \
	  || { tail -20 build/logs/tfr911h-cmake.log; exit 1; }; \
	cmake --build build/tfr911h -j $(JOBS) >build/logs/tfr911h-make.log 2>&1 \
	  || { grep -n -E 'error|Error' build/logs/tfr911h-make.log | head -30; exit 1; }; \
	cp build/tfr911h/tfr911h_v4.uf2 build/tfr911h.uf2; \
	arm-none-eabi-size build/tfr911h/tfr911h_v4.elf; ls -l build/tfr911h.uf2

clean:
	rm -rf build
