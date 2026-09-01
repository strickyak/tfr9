CXX ?= g++
CXXFLAGS ?= -std=gnu++20 -O2 -Wall -Wextra -I.
LOG_FILES ?= ../misc/golden-log-1.txt ../misc/golden-log-2.txt ../misc/golden-log-3.txt

all: test_cpu_tracker

test_cpu_tracker: test_cpu_tracker.cpp cpu_tracker.h opcodes_6809.h register_state.h
	$(CXX) $(CXXFLAGS) -o $@ test_cpu_tracker.cpp

test: test_cpu_tracker
	./test_cpu_tracker $(LOG_FILES)

clean:
	rm -f test_cpu_tracker

.PHONY: all test clean
