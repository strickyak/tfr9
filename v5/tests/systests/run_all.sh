#!/bin/sh
# run_all.sh [suite.sh ...] -- run the v5 hardware system tests (POSIX sh).  Suites that need a board skip
# themselves cleanly when none is attached.  The expect-based engine suites arrive with the engine ports.
SYSTEST_DIR=$(cd "$(dirname "$0")" && pwd)
suites=${*:-"01_quick_rpc_test.sh 02_tcl_vfs_test.sh 03_source_stream_test.sh 04_heap_test.sh 05_zip_stream_test.sh 06_engines_test.sh 07_ports_test.sh 08_enhancements_test.sh"}
passed=0; failed=0; failed_list=
for s in $suites; do
  printf '\n>>> Running %s...\n' "$s"
  if sh "$SYSTEST_DIR/$s"; then passed=$((passed + 1)); echo ">>> $s PASSED"
  else failed=$((failed + 1)); failed_list="$failed_list $s"; echo ">>> $s FAILED" >&2; fi
done
printf '\n====================== Final System Test Summary ======================\n'
echo "  Suites passed: $passed   failed: $failed"
if [ "$failed" -gt 0 ]; then echo "  Failed:$failed_list"; exit 1; fi
echo "  SUCCESS"
