# Host tests: no board, no Pico SDK.  Run from v5/ with `make test-host` (which generates ports_gen.h first).
CXX ?= g++
CXXFLAGS ?= -std=gnu++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer
CORE = ../../firmware/lib/core
DEV = ../../firmware/lib/devices
GEN = ../../build/generated
OUT = ../../build/host-tests

all: test
$(OUT):
	mkdir -p $(OUT)
$(OUT)/cobs_test: cobs_test.cpp $(CORE)/cobs.h $(CORE)/circbuf.h | $(OUT)
	$(CXX) $(CXXFLAGS) -I$(CORE) -o $@ cobs_test.cpp
$(OUT)/iomap_test: iomap_test.cpp $(DEV)/iomap.h $(GEN)/ports_gen.h | $(OUT)
	$(CXX) $(CXXFLAGS) -I$(DEV) -I$(GEN) -o $@ iomap_test.cpp
test: $(OUT)/cobs_test $(OUT)/iomap_test
	$(OUT)/cobs_test
	$(OUT)/iomap_test
.PHONY: all test
