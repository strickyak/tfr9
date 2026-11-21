#!/bin/sh
# find-session.sh [OPTIONS] [SESSION_OR_FILTERS...] -- discover active tether web sessions via `tether -just-list-sessions` (POSIX sh).
# No external curl/wget dependency: `tether -just-list-sessions` probes 127.0.0.1:8080..8099 directly in Go.
#
# Output format of `tether -just-list-sessions` (and GET /session):
#   <port> <session> <b> <v> <id> <name>
#   e.g.: 8090 BilboSession centipede 32z z99 zGengar
#
# Examples:
#   ./scripts/find-session.sh                     # list active sessions (`<port> <session> <b> <v> <id> <name>`)
#   ./scripts/find-session.sh BilboSession        # print http://127.0.0.1:<port> for session "BilboSession"
#   ./scripts/find-session.sh -p BilboSession     # print <port> only (e.g. 8090)
#   ./scripts/find-session.sh b=centipede v=32z   # print http://127.0.0.1:<port> matching board/version
#   ./scripts/find-session.sh id=z99              # print http://127.0.0.1:<port> for board id "z99"
#   ./scripts/find-session.sh name=zGengar        # print http://127.0.0.1:<port> for board name "zGengar"
#   ./scripts/find-session.sh -w 10 BilboSession  # wait up to 10s for session "BilboSession" to come online
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
TETHER=$("$V5/scripts/find-tether.sh") || exit 2
HOST=${FIND_SESSION_HOST:-127.0.0.1}

PORT_ONLY=0
WAIT_SEC=0

usage() {
  cat <<'EOF'
Usage: find-session.sh [-p|--port] [-w SEC|--wait=SEC] [SESSION_NAME | b=B | v=V | id=ID | name=NAME ...]

Options:
  -p, --port        Print only the TCP port number (e.g. 8090) instead of http://127.0.0.1:8090
  -w, --wait SEC    Retry for up to SEC seconds until a matching session appears
  -h, --help        Show this help message

Without filter arguments, prints `<port> <session> <b> <v> <id> <name>` for each active session.
Exits 0 if found, 1 if no matching tether session is active.
EOF
}

nargs=$#
i=0
while [ "$i" -lt "$nargs" ]; do
  arg=$1
  shift
  i=$((i + 1))
  case "$arg" in
    -h|--help)
      usage
      exit 0
      ;;
    -p|--port)
      PORT_ONLY=1
      ;;
    -w|--wait)
      if [ "$i" -ge "$nargs" ]; then
        echo "find-session.sh: $arg requires a seconds argument" >&2
        exit 2
      fi
      WAIT_SEC=$1
      shift
      i=$((i + 1))
      ;;
    --wait=*)
      WAIT_SEC=${arg#--wait=}
      ;;
    --)
      while [ "$i" -lt "$nargs" ]; do
        set -- "$@" "$1"
        shift
        i=$((i + 1))
      done
      break
      ;;
    -*)
      echo "find-session.sh: unknown option: $arg" >&2
      usage >&2
      exit 2
      ;;
    *)
      set -- "$@" "$arg"
      ;;
  esac
done

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

  if [ "$#" -eq 0 ]; then
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
    matched_port=""
    OLD_IFS=$IFS
    IFS='
'
    for line in $listing; do
      [ -n "$line" ] || continue
      IFS=' '
      # shellcheck disable=SC2086
      set_fields() {
        p=${1:-.}
        sess=${2:-.}
        brd=${3:-.}
        ver=${4:-.}
        bid=${5:-.}
        bname=${6:-.}
      }
      # Split line into the 6 fields safely without globbing
      set -f
      # shellcheck disable=SC2086
      set_fields $line
      set +f
      IFS=$OLD_IFS

      ok=1
      for crit in "$@"; do
        case "$crit" in
          b=*)
            if [ "$brd" != "${crit#b=}" ] || [ "$brd" = "." ]; then ok=0; fi
            ;;
          v=*)
            if [ "$ver" != "${crit#v=}" ] || [ "$ver" = "." ]; then ok=0; fi
            ;;
          id=*)
            if [ "$bid" != "${crit#id=}" ] || [ "$bid" = "." ]; then ok=0; fi
            ;;
          name=*)
            if [ "$bname" != "${crit#name=}" ] || [ "$bname" = "." ]; then ok=0; fi
            ;;
          session=*)
            if [ "$sess" != "${crit#session=}" ] || [ "$sess" = "." ]; then ok=0; fi
            ;;
          *)
            if [ "$sess" != "$crit" ] || [ "$sess" = "." ]; then ok=0; fi
            ;;
        esac
      done
      if [ "$ok" -eq 1 ]; then
        matched_port=$p
        break
      fi
    done
    IFS=$OLD_IFS

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
