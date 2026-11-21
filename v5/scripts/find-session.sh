#!/bin/sh
# find-session.sh [OPTIONS] [SESSION_OR_FILTER] -- discover active tether web sessions via `tether -just-list-sessions` (POSIX sh).
# No external curl/wget dependency: `tether -just-list-sessions` probes 127.0.0.1:8080..8099 directly in Go.
#
# Examples:
#   ./scripts/find-session.sh                # list active sessions (`<port> <session> <board_id> <board_name>`)
#   ./scripts/find-session.sh Bilbo          # print http://127.0.0.1:<port> for session "Bilbo"
#   ./scripts/find-session.sh -p Bilbo       # print <port> only (e.g. 8082)
#   ./scripts/find-session.sh id=z99         # print http://127.0.0.1:<port> for board_id "z99"
#   ./scripts/find-session.sh name=zGengar   # print http://127.0.0.1:<port> for board_name "zGengar"
#   ./scripts/find-session.sh -w 10 Bilbo    # wait up to 10s for session "Bilbo" to come online
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
TETHER=$("$V5/scripts/find-tether.sh") || exit 2
HOST=${FIND_SESSION_HOST:-127.0.0.1}

PORT_ONLY=0
WAIT_SEC=0
HAS_TARGET=0
TARGET=""

usage() {
  cat <<'EOF'
Usage: find-session.sh [-p|--port] [-w SEC|--wait=SEC] [SESSION_NAME | id=ID | name=NAME]

Options:
  -p, --port        Print only the TCP port number (e.g. 8082) instead of http://127.0.0.1:8082
  -w, --wait SEC    Retry for up to SEC seconds until a matching session appears
  -h, --help        Show this help message

Without a target argument, prints `<port> <session> <board_id> <board_name>` for each active session.
Exits 0 if found, 1 if no matching tether session is active.
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -p|--port)
      PORT_ONLY=1
      shift
      ;;
    -w|--wait)
      if [ "$#" -lt 2 ]; then
        echo "find-session.sh: $1 requires a seconds argument" >&2
        exit 2
      fi
      WAIT_SEC=$2
      shift 2
      ;;
    --wait=*)
      WAIT_SEC=${1#--wait=}
      shift
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "find-session.sh: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [ "$HAS_TARGET" -eq 1 ]; then
        echo "find-session.sh: unexpected extra argument: $1" >&2
        usage >&2
        exit 2
      fi
      HAS_TARGET=1
      TARGET=$1
      shift
      ;;
  esac
done

if [ "$#" -gt 0 ]; then
  if [ "$HAS_TARGET" -eq 1 ] || [ "$#" -gt 1 ]; then
    echo "find-session.sh: unexpected extra arguments: $*" >&2
    usage >&2
    exit 2
  fi
  HAS_TARGET=1
  TARGET=$1
fi

max_tries=1
case "$WAIT_SEC" in
  ''|*[!0-9]*)
    echo "find-session.sh: invalid --wait seconds: $WAIT_SEC" >&2
    exit 2
    ;;
  0)
    max_tries=1
    ;;
  *)
    max_tries=$((WAIT_SEC * 5))
    ;;
esac

try=0
while :; do
  try=$((try + 1))
  listing=$("$TETHER" -just-list-sessions)

  if [ "$HAS_TARGET" -eq 0 ]; then
    if [ -n "$listing" ]; then
      if [ "$PORT_ONLY" -eq 1 ]; then
        printf '%s\n' "$listing" | while IFS=' ' read -r p _rest; do
          [ -n "$p" ] && printf '%s\n' "$p"
        done
      else
        printf '%s\n' "$listing"
      fi
      exit 0
    fi
  elif [ -n "$listing" ]; then
    matched_port=$(
      printf '%s\n' "$listing" | while IFS=' ' read -r p sess bid bname _extra; do
        [ -n "$p" ] || continue
        ok=0
        case "$TARGET" in
          id=*)
            [ "$bid" = "${TARGET#id=}" ] && [ "$bid" != "." ] && ok=1
            ;;
          name=*)
            [ "$bname" = "${TARGET#name=}" ] && [ "$bname" != "." ] && ok=1
            ;;
          session=*)
            [ "$sess" = "${TARGET#session=}" ] && [ "$sess" != "." ] && ok=1
            ;;
          *)
            [ "$sess" = "$TARGET" ] && [ "$sess" != "." ] && ok=1
            ;;
        esac
        if [ "$ok" -eq 1 ]; then
          printf '%s\n' "$p"
          break
        fi
      done
    )
    if [ -n "$matched_port" ]; then
      if [ "$PORT_ONLY" -eq 1 ]; then
        printf '%s\n' "$matched_port"
      else
        printf 'http://%s:%s\n' "$HOST" "$matched_port"
      fi
      exit 0
    fi
  fi

  if [ "$try" -ge "$max_tries" ]; then
    exit 1
  fi
  sleep 0.2 2>/dev/null || sleep 1
done
