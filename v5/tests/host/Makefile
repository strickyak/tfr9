# Host tests: no board, no Pico SDK.  Run from v5/ with `make test-host`.
CXX ?= g++
CXXFLAGS ?= -std=gnu++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer
CORE = ../../firmware/lib/core
OUT = ../../build/host-tests

all: test
$(OUT):
	mkdir -p $(OUT)
$(OUT)/cobs_test: cobs_test.cpp $(CORE)/cobs.h $(CORE)/circbuf.h | $(OUT)
	$(CXX) $(CXXFLAGS) -I$(CORE) -o $@ cobs_test.cpp
test: $(OUT)/cobs_test
	$(OUT)/cobs_test
.PHONY: all test
