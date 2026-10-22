#!/bin/bash
# run_all.sh - Master system test runner for TFR9/v4 (tfr911h + tether)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors
CYAN='\033[0;36m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BOLD}${CYAN}========================================================"
echo -e "     Starting TFR9/v4 (TFR911h) System Test Suite"
echo -e "========================================================${NC}"

QUICK_SUITES=(
    "01_quick_rpc_test.sh"
    "02_tcl_vfs_test.sh"
)

ENGINE_SUITES=(
    "03_engine_turbo9os_test.exp"
    "04_engine_flat65280_test.exp"
    "05_engine_deep65280_test.exp"
    "06_engine_fuzix_test.exp"
)

RUN_ENGINES=1
if [ "$1" == "--quick" ] || [ "$1" == "-q" ]; then
    RUN_ENGINES=0
    shift
fi

SUITES_PASSED=0
SUITES_FAILED=0
FAILED_SUITES=()

run_suite() {
    local suite="$1"
    echo -e "\n${BOLD}${YELLOW}>>> Running $suite...${NC}"
    if "$SCRIPT_DIR/$suite"; then
        SUITES_PASSED=$((SUITES_PASSED + 1))
        echo -e "${GREEN}>>> $suite PASSED${NC}"
    else
        SUITES_FAILED=$((SUITES_FAILED + 1))
        FAILED_SUITES+=("$suite")
        echo -e "${RED}>>> $suite FAILED${NC}" >&2
    fi
}

if [ "$#" -gt 0 ]; then
    for target in "$@"; do
        base=$(basename "$target")
        if [ -x "$SCRIPT_DIR/$base" ]; then
            run_suite "$base"
        else
            echo -e "${RED}ERROR: Test script $base not found or not executable in $SCRIPT_DIR${NC}" >&2
            exit 1
        fi
    done
else
    for suite in "${QUICK_SUITES[@]}"; do
        run_suite "$suite"
    done

    if [ "$RUN_ENGINES" -eq 1 ]; then
        if [ -x "$(which expect 2>/dev/null)" ]; then
            for suite in "${ENGINE_SUITES[@]}"; do
                run_suite "$suite"
            done
        else
            echo -e "\n${YELLOW}Note: 'expect' not found; skipping 6309 engine expect suites${NC}"
        fi
    fi
fi

# Leave board cleanly in Phase 1 (mode 0) after tests finish
"$SCRIPT_DIR/common.sh" >/dev/null 2>&1 || true
if [ -x "$SCRIPT_DIR/../build/tether.linux-amd64.exe" ]; then
    "$SCRIPT_DIR/../build/tether.linux-amd64.exe" -bind="" --quick-restart=0 >/dev/null 2>&1 || true
fi

echo -e "\n${BOLD}${CYAN}========================================================"
echo -e "              Final System Test Summary"
echo -e "========================================================${NC}"
echo -e "  Suites Passed: ${GREEN}$SUITES_PASSED${NC}"
echo -e "  Suites Failed: ${RED}$SUITES_FAILED${NC}"

if [ "$SUITES_FAILED" -gt 0 ]; then
    echo -e "  Failed suites: ${RED}${FAILED_SUITES[*]}${NC}"
    echo -e "${RED}FAILURE: Some test suites failed.${NC}"
    exit 1
else
    echo -e "${GREEN}SUCCESS: All test suites passed!${NC}"
    exit 0
fi
