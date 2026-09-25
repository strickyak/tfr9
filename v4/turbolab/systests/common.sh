#!/bin/bash
# common.sh - Shared test harness for TurboLab system tests

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TURBOLAB_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Locate tether binary matching host architecture
TETHER_BIN=""
ARCH=$(uname -m)
case "$ARCH" in
    x86_64)  CANDIDATE="$TURBOLAB_DIR/build/tether.linux-amd64.exe" ;;
    aarch64) CANDIDATE="$TURBOLAB_DIR/build/tether.linux-arm64.exe" ;;
    arm*)    CANDIDATE="$TURBOLAB_DIR/build/tether.linux-arm-7.exe" ;;
    i*86)    CANDIDATE="$TURBOLAB_DIR/build/tether.linux-386.exe" ;;
    *)       CANDIDATE="" ;;
esac

if [ -n "$CANDIDATE" ] && [ -x "$CANDIDATE" ]; then
    TETHER_BIN="$CANDIDATE"
elif [ -x "$TURBOLAB_DIR/build/tether" ]; then
    TETHER_BIN="$TURBOLAB_DIR/build/tether"
else
    for f in "$TURBOLAB_DIR/build"/tether.*.exe; do
        if [ -x "$f" ]; then
            TETHER_BIN="$f"
            break
        fi
    done
fi

if [ -z "$TETHER_BIN" ]; then
    echo -e "${RED}ERROR: tether binary not found in $TURBOLAB_DIR/build${NC}" >&2
    exit 1
fi

DATA_DIR="$TURBOLAB_DIR/data"
BUILD_DIR="$TURBOLAB_DIR/build"
LISTINGS_DIR="$BUILD_DIR/listings"

# Helper for test title
log_test() {
    TESTS_RUN=$((TESTS_RUN + 1))
    echo -e "\n${CYAN}[TEST $TESTS_RUN]${NC} $1"
}

pass() {
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "  ${GREEN}PASS:${NC} $1"
}

fail() {
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo -e "  ${RED}FAIL:${NC} $1" >&2
}

# Run tether capturing stdout, stderr, and status
# Sets: TETHER_STDOUT, TETHER_STDERR, TETHER_OUTPUT (combined), TETHER_EXIT
run_tether() {
    local tmp_stdout
    local tmp_stderr
    tmp_stdout=$(mktemp)
    tmp_stderr=$(mktemp)

    "$TETHER_BIN" "$@" >"$tmp_stdout" 2>"$tmp_stderr"
    TETHER_EXIT=$?

    TETHER_STDOUT=$(cat "$tmp_stdout")
    TETHER_STDERR=$(cat "$tmp_stderr")
    TETHER_OUTPUT=$(cat "$tmp_stdout" "$tmp_stderr")

    rm -f "$tmp_stdout" "$tmp_stderr"
    return $TETHER_EXIT
}

assert_contains() {
    local haystack="$1"
    local needle="$2"
    local desc="$3"
    if echo "$haystack" | grep -q -- "$needle"; then
        pass "$desc"
    else
        fail "$desc (expected to find '$needle')"
        echo -e "  --- Output snippet ---\n$haystack\n  ----------------------" >&2
    fi
}

assert_not_contains() {
    local haystack="$1"
    local needle="$2"
    local desc="$3"
    if ! echo "$haystack" | grep -q -- "$needle"; then
        pass "$desc"
    else
        fail "$desc (expected NOT to find '$needle')"
        echo -e "  --- Output snippet ---\n$haystack\n  ----------------------" >&2
    fi
}

assert_matches_regex() {
    local haystack="$1"
    local regex="$2"
    local desc="$3"
    if echo "$haystack" | grep -E -q -- "$regex"; then
        pass "$desc"
    else
        fail "$desc (expected match for regex '$regex')"
        echo -e "  --- Output snippet ---\n$haystack\n  ----------------------" >&2
    fi
}

assert_exit_code() {
    local expected="$1"
    local actual="$2"
    local desc="$3"
    if [ "$expected" -eq "$actual" ]; then
        pass "$desc (exit code $actual)"
    else
        fail "$desc (expected exit code $expected, got $actual)"
    fi
}

print_summary() {
    local suite_name="$1"
    echo -e "\n========================================================"
    echo -e " Summary for ${YELLOW}${suite_name}${NC}:"
    echo -e " Total:  $TESTS_RUN"
    echo -e " Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e " Failed: ${RED}$TESTS_FAILED${NC}"
    echo -e "========================================================"

    if [ "$TESTS_FAILED" -eq 0 ]; then
        return 0
    else
        return 1
    fi
}
