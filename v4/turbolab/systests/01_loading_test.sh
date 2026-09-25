#!/bin/bash
# 01_loading_test.sh - Test memory loading formats and listing relocations
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

echo "Running Suite 1: Memory & Listing Loading Tests"

# 1. Test DECB Loading
log_test "Load DECB binary (pi.decb) and verify entry point"
run_tether -n --stop=c:50 "$DATA_DIR/bootable/pi.decb"
assert_contains "$TETHER_STDERR" "Loaded DECB file" "DECB file detected and loaded"
assert_contains "$TETHER_STDERR" "entry point at \$1000" "Entry point at \$1000 identified"
assert_contains "$TETHER_STDERR" "RESET vector set at \$FFFE" "Reset vector configured"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 2. Test DECB Loading with Explicit Listing
log_test "Load DECB binary with explicit listing file"
run_tether -n --trace=x --stop=c:10 "$DATA_DIR/bootable/pi.decb" "$DATA_DIR/bootable/pi.decb.list"
assert_contains "$TETHER_STDERR" "Loaded absolute listing" "Explicit listing file loaded"
assert_contains "$TETHER_STDERR" "x 1000 C6 #4;" "Executed first instruction at entry point \$1000"
assert_contains "$TETHER_STDERR" "START: ldb #N" "Disassembly matched source listing line"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 3. Test Raw 64KB Memory Image
log_test "Load raw 64KB image (turbos_core.img) and verify primordial modules"
run_tether -n --stop=c:50 "$BUILD_DIR/turbos/turbos_core.img"
assert_contains "$TETHER_STDERR" "Prepared 65536-byte memory image" "Raw 64KB image prepared"
assert_contains "$TETHER_STDERR" "Found primordial module \"kernel\"" "Module 'kernel' detected"
assert_contains "$TETHER_STDERR" "Found primordial module \"ioman\"" "Module 'ioman' detected"
assert_contains "$TETHER_STDERR" "Found primordial module \"go\"" "Module 'go' detected"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 4. Test OS-9 Module ROM and Automatic Directory Listings Resolution
log_test "Load OS-9 module ROM with --listings directory"
run_tether -n --stop=c:10 -listings "$LISTINGS_DIR" "$BUILD_DIR/turbos/turbos_dev.img.rom"
assert_contains "$TETHER_STDERR" "Found primordial module \"shell\"" "Module 'shell' detected"
assert_contains "$TETHER_STDERR" "Loaded listing \"kernel." "Kernel listing automatically resolved from directory"
assert_contains "$TETHER_STDERR" "Loaded listing \"shell." "Shell listing automatically resolved from directory"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

print_summary "Suite 1 (Loading)"
