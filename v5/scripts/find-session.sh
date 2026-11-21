#!/bin/sh
# find-session.sh [OPTIONS] [SESSION_NAME] -- discover active tether web sessions on localhost:8080..8099 (POSIX sh).
#
# Examples:
#   ./scripts/find-session.sh                # list all active tether web sessions
#   ./scripts/find-session.sh Bilbo          # print http://127.0.0.1:<port> for session "Bilbo"
#   ./scripts/find-session.sh -p Bilbo       # print <port> only (e.g. 8081)
#   ./scripts/find-session.sh -w 10 Bilbo    # wait up to 10s for session "Bilbo" to come online
#   ./scripts/find-session.sh --json         # print /session.json lines for active sessions
#
# Environment overrides:
#   FIND_SESSION_HOST   (default: 127.0.0.1)
#   FIND_SESSION_START  (default: 8080)
#   FIND_SESSION_END    (default: 8099)
set -eu

HOST=${FIND_SESSION_HOST:-127.0.0.1}
START_PORT=${FIND_SESSION_START:-8080}
END_PORT=${FIND_SESSION_END:-8099}

PORT_ONLY=0
JSON_OUT=0
WAIT_SEC=0
HAS_TARGET=0
TARGET=""

usage() {
  cat <<'EOF'
Usage: find-session.sh [-p|--port] [-j|--json] [-w SEC|--wait=SEC] [SESSION_NAME]

Options:
  -p, --port        Print only the TCP port number (e.g. 8081) instead of http://127.0.0.1:8081
  -j, --json        Print the /session.json object instead of plain text
  -w, --wait SEC    Retry for up to SEC seconds until a matching session appears
  -h, --help        Show this help message

Without SESSION_NAME, lists all active tether web servers in 8080..8099.
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
    -j|--json)
      JSON_OUT=1
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

if command -v curl >/dev/null 2>&1; then
  http_get() { curl -fsS --max-time 1 "$1"; }
elif command -v wget >/dev/null 2>&1; then
  http_get() { wget -qO- -T 1 "$1"; }
else
  echo "find-session.sh: neither curl nor wget found in PATH" >&2
  exit 2
fi

json_str_field() {
  printf '%s\n' "$1" | sed -n "s/.*\"$2\":\"\\([^\"]*\\)\".*/\\1/p"
}

json_int_field() {
  printf '%s\n' "$1" | sed -n "s/.*\"$2\":\\([0-9][0-9]*\\).*/\\1/p"
}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/find-session.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT INT TERM

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
  rm -f "$tmp"/*.json

  p=$START_PORT
  while [ "$p" -le "$END_PORT" ]; do
    (
      http_get "http://$HOST:$p/session.json" > "$tmp/$p.json" 2>/dev/null || rm -f "$tmp/$p.json"
    ) &
    p=$((p + 1))
  done
  wait

  found=0
  p=$START_PORT
  while [ "$p" -le "$END_PORT" ]; do
    if [ -s "$tmp/$p.json" ]; then
      jline=$(tr -d '\r\n' < "$tmp/$p.json")
      case "$jline" in
        *'"tether":true'*)
          sess=$(http_get "http://$HOST:$p/session" 2>/dev/null || true)
          if [ "$HAS_TARGET" -eq 1 ]; then
            if [ "$sess" = "$TARGET" ]; then
              if [ "$JSON_OUT" -eq 1 ]; then
                printf '%s\n' "$jline"
              elif [ "$PORT_ONLY" -eq 1 ]; then
                printf '%s\n' "$p"
              else
                printf 'http://%s:%s\n' "$HOST" "$p"
              fi
              exit 0
            fi
          else
            found=1
            if [ "$JSON_OUT" -eq 1 ]; then
              printf '%s\n' "$jline"
            elif [ "$PORT_ONLY" -eq 1 ]; then
              printf '%s\n' "$p"
            else
              pid_val=$(json_int_field "$jline" pid)
              wire_val=$(json_str_field "$jline" wire)
              label_val=$(json_str_field "$jline" label)
              sess_disp=${sess:--}
              printf 'http://%s:%s  session=%s  pid=%s  wire=%s  label=%s\n' \
                "$HOST" "$p" "$sess_disp" "${pid_val:-?}" "${wire_val:--}" "${label_val:--}"
            fi
          fi
          ;;
      esac
    fi
    p=$((p + 1))
  done

  if [ "$found" -eq 1 ]; then
    exit 0
  fi

  if [ "$try" -ge "$max_tries" ]; then
    exit 1
  fi

  sleep 0.2 2>/dev/null || sleep 1
done
