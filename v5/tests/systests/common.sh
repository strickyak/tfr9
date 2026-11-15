# common.sh -- shared harness for the v5 hardware system tests.  POSIX sh (works with dash and macOS sh).
# Source it:  . "$(dirname "$0")/common.sh"
# Environment:  TETHER=/path/to/tether (default: build/tether, built by `make tether`)
#               WIRE=/dev/ttyACM1  (passed to tether as -wire if set; overrides label search)
#               BOARD_MATCH="b=tfr9 v=911h"  (default) label fields used to find the board when
#               WIRE is unset.  /dev/ttyACM* numbers are NOT stable; boards are found by label.

SYSTEST_DIR=$(cd "$(dirname "$0")" && pwd)
V5=$(cd "$SYSTEST_DIR/../.." && pwd)
BUILD_DIR=$V5/build
TETHER=${TETHER:-$BUILD_DIR/tether}

if [ -t 1 ]; then
  RED=$(printf '\033[0;31m'); GREEN=$(printf '\033[0;32m'); YELLOW=$(printf '\033[0;33m'); CYAN=$(printf '\033[0;36m'); NC=$(printf '\033[0m')
else
  RED=; GREEN=; YELLOW=; CYAN=; NC=
fi

TESTS_RUN=0; TESTS_PASSED=0; TESTS_FAILED=0; TESTS_SKIPPED=0

if [ ! -x "$TETHER" ]; then
  echo "${RED}ERROR: tether not found at $TETHER (run: make tether)${NC}" >&2
  exit 1
fi

log_test() { TESTS_RUN=$((TESTS_RUN + 1)); printf '\n%s[TEST %s]%s %s\n' "$CYAN" "$TESTS_RUN" "$NC" "$1"; }
pass() { TESTS_PASSED=$((TESTS_PASSED + 1)); printf '  %sPASS:%s %s\n' "$GREEN" "$NC" "$1"; }
fail() { TESTS_FAILED=$((TESTS_FAILED + 1)); printf '  %sFAIL:%s %s\n' "$RED" "$NC" "$1" >&2; }
skip() { TESTS_SKIPPED=$((TESTS_SKIPPED + 1)); printf '  %sSKIP:%s %s\n' "$YELLOW" "$NC" "$1"; }

# Resolve WIRE by label unless the caller fixed it.  Never fall back to "any /dev/ttyACM*":
# with several boards attached that could run a test against the wrong one.
if [ -z "${WIRE:-}" ]; then
  # shellcheck disable=SC2086
  WIRE=$("$V5/scripts/find-board.sh" ${BOARD_MATCH:-b=tfr9 v=911h}) || WIRE=
fi
# board_present: did we find the board we are testing?
board_present() { [ -n "${WIRE:-}" ] && [ -e "$WIRE" ]; }
# need_board "description": skip (and return 1) when no board is attached
need_board() {
  if board_present; then return 0; fi
  skip "$1 (no board attached)"
  return 1
}

# md5_of FILE : print the md5 of a file on Linux (md5sum) or macOS (md5 -q)
md5_of() {
  if command -v md5sum >/dev/null 2>&1; then md5sum "$1" | awk '{print $1}'; else md5 -q "$1"; fi
}
# size_of FILE : bytes, portable
size_of() { wc -c < "$1" | tr -d ' '; }

# run_tether ARGS... : sets TETHER_STDOUT, TETHER_STDERR, TETHER_EXIT
run_tether() {
  _o=$(mktemp "${TMPDIR:-/tmp}/tether_out.XXXXXX"); _e=$(mktemp "${TMPDIR:-/tmp}/tether_err.XXXXXX")
  if [ -n "${WIRE:-}" ]; then "$TETHER" -bind="" -wire="$WIRE" "$@" >"$_o" 2>"$_e"; else "$TETHER" -bind="" "$@" >"$_o" 2>"$_e"; fi
  TETHER_EXIT=$?
  TETHER_STDOUT=$(cat "$_o"); TETHER_STDERR=$(cat "$_e")
  rm -f "$_o" "$_e"
  return 0
}

assert_contains() {   # haystack needle description
  if printf '%s\n' "$1" | grep -q -- "$2"; then pass "$3"
  else fail "$3 (expected to find '$2')"; printf '  --- output ---\n%s\n  ---------------\n' "$1" >&2; fi
}
assert_not_contains() {
  if printf '%s\n' "$1" | grep -q -- "$2"; then fail "$3 (expected NOT to find '$2')"; printf '  --- output ---\n%s\n  ---------------\n' "$1" >&2
  else pass "$3"; fi
}
assert_matches_regex() {
  if printf '%s\n' "$1" | grep -E -q -- "$2"; then pass "$3"
  else fail "$3 (expected match for regex '$2')"; printf '  --- output ---\n%s\n  ---------------\n' "$1" >&2; fi
}
assert_exit_code() {  # expected actual description
  if [ "$1" -eq "$2" ]; then pass "$3 (exit code $2)"; else fail "$3 (expected exit code $1, got $2)"; fi
}

# print_summary NAME : returns non-zero if anything failed
print_summary() {
  printf '\n========================================================\n'
  printf ' Summary for %s%s%s:\n' "$YELLOW" "$1" "$NC"
  printf ' Total: %s   Passed: %s%s%s   Failed: %s%s%s   Skipped: %s\n' "$TESTS_RUN" "$GREEN" "$TESTS_PASSED" "$NC" "$RED" "$TESTS_FAILED" "$NC" "$TESTS_SKIPPED"
  printf '========================================================\n'
  [ "$TESTS_FAILED" -eq 0 ]
}
