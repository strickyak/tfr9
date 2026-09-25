#!/bin/bash
# run_all.sh - Master system test runner for TurboLab
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
echo -e "       Starting TurboLab System Test Suite"
echo -e "========================================================${NC}"

SUITES=(
    "01_loading_test.sh"
    "02_max_limits_test.sh"
    "03_triggers_test.sh"
    "04_trace_flags_test.sh"
    "05_watchpoints_test.sh"
    "06_debug_packets_test.sh"
)

# Optional interactive test
RUN_INTERACTIVE=1
if [ "$1" == "--quick" ] || [ "$1" == "-q" ]; then
    RUN_INTERACTIVE=0
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

# If specific tests given on command line, run only those:
if [ "$#" -gt 0 ]; then
    for target in "$@"; do
        base=$(basename "$target")
        if [ -x "$SCRIPT_DIR/$base" ]; then
            run_suite "$base"
        else
            echo -e "${RED}ERROR: Test script $base not found in $SCRIPT_DIR${NC}" >&2
            exit 1
        fi
    done
else
    # Run standard automated suites
    for suite in "${SUITES[@]}"; do
        run_suite "$suite"
    done

    # Run expect interactive suite if enabled
    if [ "$RUN_INTERACTIVE" -eq 1 ] && [ -x "$(which expect 2>/dev/null)" ]; then
        run_suite "07_interactive.exp"
    elif [ "$RUN_INTERACTIVE" -eq 1 ]; then
        echo -e "\n${YELLOW}Note: 'expect' not found; skipping 07_interactive.exp${NC}"
    fi
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
