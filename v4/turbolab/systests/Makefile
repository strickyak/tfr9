.PHONY: all test test-quick test-loading test-limits test-triggers test-trace test-watch test-debug test-interactive

all: test

test:
	./run_all.sh

test-quick:
	./run_all.sh --quick

test-loading:
	./01_loading_test.sh

test-limits: test-stop

test-stop:
	./02_stop_limits_test.sh

test-triggers:
	./03_triggers_test.sh

test-trace:
	./04_trace_flags_test.sh

test-watch:
	./05_watchpoints_test.sh

test-debug:
	./06_debug_packets_test.sh

test-interactive:
	./07_interactive.exp
