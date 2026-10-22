.PHONY: all test test-quick test-rpc test-tcl test-turbo9os test-flat test-deep test-fuzix

all: test

test:
	./run_all.sh

test-quick:
	./run_all.sh --quick

test-rpc:
	./01_quick_rpc_test.sh

test-tcl:
	./02_tcl_vfs_test.sh

test-turbo9os:
	./03_engine_turbo9os_test.exp

test-flat:
	./04_engine_flat65280_test.exp

test-deep:
	./05_engine_deep65280_test.exp

test-fuzix:
	./06_engine_fuzix_test.exp
