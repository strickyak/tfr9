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

# ---- tcl_stream_test: the real Tcl 6.7c sources compiled for the PC ----
TCLDIR = ../../third_party/tcl6.7c
TCLSRCS = panic regexp tclAssem tclBasic tclCkalloc tclCmdAH tclCmdIL tclCmdMZ tclEnv tclExpr tclGet tclGlob tclHash tclHistory tclParse tclProc tclUtil tclVar
TCLOBJS = $(addprefix $(OUT)/tcl/,$(addsuffix .o,$(TCLSRCS)))
$(OUT)/tcl:
	mkdir -p $(OUT)/tcl
$(OUT)/tcl/%.o: $(TCLDIR)/%.c | $(OUT)/tcl
	$(CC) -std=gnu99 -O1 -w -DTCL_GENERIC_ONLY=1 -I$(TCLDIR) -c $< -o $@
$(OUT)/tcl_stream_test: tcl_stream_test.cpp ../../firmware/lib/tcl/tcl_stream.h $(TCLOBJS) | $(OUT)
	$(CXX) -std=gnu++17 -O1 -g -Wall -Wextra -I../../firmware/lib/tcl -I../../third_party -I$(TCLDIR) -fsanitize=address,undefined -o $@ tcl_stream_test.cpp $(TCLOBJS)
test: $(OUT)/tcl_stream_test
	$(OUT)/tcl_stream_test
