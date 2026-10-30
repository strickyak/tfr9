# tfr9 v5 top-level Makefile.  All output goes under build/.
# the coco-shelf (parent of tfr9/); override with  make SHELF=/other/shelf
SHELF ?= $(abspath ../..)
export SHELF

.PHONY: help doctor check-sh clean
help:
	@echo "targets:"
	@echo "  doctor   check toolchain (SHELF=$(SHELF))"
	@echo "  check-sh lint scripts for bashisms (macOS-friendly sh)"
	@echo "  clean    rm -rf build/"
	@echo "(firmware/tether/test targets arrive in Phase 1+)"

doctor:
	@sh scripts/doctor.sh

check-sh:
	@sh scripts/check-sh.sh

clean:
	rm -rf build
