#!/usr/bin/env bash
# 42 Webserv - isolated HTTP DELETE tests.
# Usage: bash tmp/test_delete.sh CONFIG_FILE PORT URI_PREFIX
# Example: bash tmp/test_delete.sh config/example.conf 8080 /uploads
# URI_PREFIX must map to the real PROJECT_DIR/uploads directory in the config.

set -u

RED=$'\033[31m'
GREEN=$'\033[32m'
YELLOW=$'\033[33m'
CYAN=$'\033[36m'
RESET=$'\033[0m'
if [[ ! -t 1 ]]; then
    RED=''; GREEN=''; YELLOW=''; CYAN=''; RESET=''
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER="$PROJECT_DIR/webserv"
UPLOADS="$PROJECT_DIR/uploads"
CONFIG="${1:-}"
PORT="${2:-}"
URI_PREFIX="${3:-}"
SERVER_PID=''
CASE_DIR=''
LOG_FILE=''
PASSED=0
FAILED=0

usage() {
    printf 'Usage: bash %s CONFIG_FILE PORT URI_PREFIX\n' "$0"
    printf 'Example: bash %s config/default.conf 8080 /uploads\n' "$0"
    printf 'NOTE: URI_PREFIX must resolve to %s in your server config.\n' "$UPLOADS"
}

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    # Remove temporary test files, but preserve survivor.txt for inspection.
    if [[ -n "$CASE_DIR" && -d "$CASE_DIR" ]]; then
        rm -f -- "$CASE_DIR/hello.txt" "$CASE_DIR/keep.txt"

        # Keep the directory if survivor.txt exists.
        if [[ ! -e "$CASE_DIR/survivor.txt" ]]; then
            rmdir -- "$CASE_DIR" 2>/dev/null || true
        fi
    fi
    if [[ -n "$LOG_FILE" ]]; then
        rm -f -- "$LOG_FILE"
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ -z "$CONFIG" || -z "$PORT" || -z "$URI_PREFIX" ]]; then
    usage
    exit 2
fi
if [[ ! "$PORT" =~ ^[0-9]+$ ]] || (( 10#$PORT < 1 || 10#$PORT > 65535 )); then
    printf '%s[ERROR]%s Invalid port\n' "$RED" "$RESET" >&2
    exit 2
fi
if [[ "$URI_PREFIX" != /* || "$URI_PREFIX" == *'?'* || "$URI_PREFIX" == *'#'* ]]; then
    printf '%s[ERROR]%s URI_PREFIX must start with / and contain no query/fragment\n' "$RED" "$RESET" >&2
    exit 2
fi
URI_PREFIX="${URI_PREFIX%/}"
if [[ -z "$URI_PREFIX" ]]; then URI_PREFIX='/'; fi
if [[ "$CONFIG" != /* ]]; then CONFIG="$PROJECT_DIR/$CONFIG"; fi
if [[ ! -f "$CONFIG" || ! -x "$SERVER" || ! -d "$UPLOADS" ]]; then
    printf '%s[ERROR]%s Missing config, executable webserv, or uploads directory\n' "$RED" "$RESET" >&2
    exit 2
fi
if ! command -v curl >/dev/null 2>&1; then
    printf '%s[ERROR]%s curl is required\n' "$RED" "$RESET" >&2
    exit 2
fi

# Avoid sending DELETE to another process already using this address/port.
if (echo >"/dev/tcp/127.0.0.1/$PORT") >/dev/null 2>&1; then
    printf '%s[ERROR]%s Port %s is already in use. Stop the existing server first.\n' "$RED" "$RESET" "$PORT" >&2
    exit 2
fi

CASE_DIR="$(mktemp -d "$UPLOADS/.delete-test-XXXXXXXX")" || exit 1
CASE_NAME="${CASE_DIR##*/}"
LOG_FILE="$(mktemp /tmp/webserv-delete-log-XXXXXXXX)" || exit 1
printf 'Hello from DELETE test\n' > "$CASE_DIR/hello.txt"
printf 'This file should survive\n' > "$CASE_DIR/keep.txt"
# This file must survive both HTTP DELETE and test cleanup.
printf 'I survived the DELETE tests!\n' > "$CASE_DIR/survivor.txt"

printf '%s[START]%s %s %s\n' "$CYAN" "$RESET" "$SERVER" "$CONFIG"
"$SERVER" "$CONFIG" >"$LOG_FILE" 2>&1 &
SERVER_PID=$!

# Wait for the server to accept connections; do not proceed if it exited.
ready=0
for ((i=0; i<30; i++)); do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        printf '%s[ERROR]%s Server exited. Log:\n' "$RED" "$RESET" >&2
        cat "$LOG_FILE" >&2
        exit 1
    fi
    if (echo >"/dev/tcp/127.0.0.1/$PORT") >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 0.1
done
if (( ready == 0 )); then
    printf '%s[ERROR]%s Server did not start listening. Log:\n' "$RED" "$RESET" >&2
    cat "$LOG_FILE" >&2
    exit 1
fi

base="http://127.0.0.1:$PORT"
if [[ "$URI_PREFIX" == '/' ]]; then
    uri_base="/$CASE_NAME"
else
    uri_base="$URI_PREFIX/$CASE_NAME"
fi

run_test() {
    local label="$1" uri="$2" expected="$3" file="$4" should_exist="$5"
    local code ok=1
    code="$(curl --silent --show-error --max-time 5 --output /dev/null --write-out '%{http_code}' -X DELETE "$base$uri" 2>/dev/null)" || code='CURL_ERROR'
    [[ "$code" == "$expected" ]] || ok=0
    if [[ "$should_exist" == 'yes' && ! -f "$file" ]]; then ok=0; fi
    if [[ "$should_exist" == 'no' && -e "$file" ]]; then ok=0; fi
    if (( ok == 1 )); then
        printf '%s[PASS]%s %s (HTTP %s)\n' "$GREEN" "$RESET" "$label" "$code"
        ((PASSED+=1))
    else
        printf '%s[FAIL]%s %s (got HTTP %s, expected %s)\n' "$RED" "$RESET" "$label" "$code" "$expected"
        ((FAILED+=1))
    fi
}

printf '\n%s=== HTTP DELETE TESTS ===%s\n' "$CYAN" "$RESET"
printf 'Fixture: %s\nURL prefix: %s\n\n' "$CASE_DIR" "$URI_PREFIX"
run_test 'Delete existing file' "$uri_base/hello.txt" '204' "$CASE_DIR/hello.txt" 'no'
run_test 'Delete missing file' "$uri_base/missing.txt" '404' "$CASE_DIR/keep.txt" 'yes'
if [[ -f "$CASE_DIR/keep.txt" ]]; then
    printf '%s[PASS]%s Unrelated file remains intact\n' "$GREEN" "$RESET"
    ((PASSED+=1))
else
    printf '%s[FAIL]%s Unrelated file was changed/deleted\n' "$RED" "$RESET"
    ((FAILED+=1))
fi
if [[ -f "$CASE_DIR/survivor.txt" ]]; then
    printf '%s[PASS]%s Survivor file remains intact\n' "$GREEN" "$RESET"
    ((PASSED+=1))
else
    printf '%s[FAIL]%s Survivor file is missing\n' "$RED" "$RESET"
    ((FAILED+=1))
fi

printf '\n%s[INFO]%s Preserved test file: %s\n' \
    "$CYAN" "$RESET" "$CASE_DIR/survivor.txt"
printf '\nResult: %s%d passed%s, %s%d failed%s\n' "$GREEN" "$PASSED" "$RESET" "$RED" "$FAILED" "$RESET"
if (( FAILED > 0 )); then
    printf '%s[INFO]%s Server log:\n' "$YELLOW" "$RESET"
    cat "$LOG_FILE"
    exit 1
fi
exit 0
