#!/usr/bin/env bash
# Führt EINE Testdatei isoliert aus: startet dafür einen eigenen Server.
#
#   tests/run-one.sh tests/20-bot.sh [port] [edition]
#
# Die Testdatei nutzt ok / bad / assert_contains sowie $BASE, $PY, $PORT, $LOG —
# dieselben Hilfen wie in test.sh, in das sie später eingelesen wird.
# So kann jeder Track seine Prüfungen allein entwickeln, ohne die anderen zu stören.
set -u
cd "$(dirname "$0")/.."

FILE="${1:?Testdatei fehlt, z. B. tests/20-bot.sh}"
PORT="${2:-8890}"
EDITION="${3:-normal}"
PY="python3"; [ -x .venv/bin/python ] && PY=".venv/bin/python"
BASE="http://localhost:${PORT}"
PASS=0; FAIL=0
LOG="$(mktemp)"
TMPDB="$(mktemp -d)"
export DKZ_DB="${DKZ_DB:-$TMPDB/dkz.duckdb}"   # DuckDB erlaubt nur einen Schreiber pro Datei

say()  { printf '%s\n' "$*"; }
ok()   { PASS=$((PASS+1)); say "  ✓ $1"; }
bad()  { FAIL=$((FAIL+1)); say "  ✗ $1"; }
assert_contains() {
  case "$3" in *"$2"*) ok "$1";; *) bad "$1 — fehlt: $2";; esac
}

PORT="$PORT" EDITION="$EDITION" "$PY" server.py >"$LOG" 2>&1 &
SRV=$!
trap 'kill "$SRV" 2>/dev/null; rm -rf "$TMPDB"' EXIT
for _ in $(seq 1 80); do
  curl -sf "$BASE/health" >/dev/null 2>&1 && break
  python3 -c 'import time; time.sleep(0.1)'
done
curl -sf "$BASE/health" >/dev/null 2>&1 || { say "  ✗ Server startet nicht"; cat "$LOG"; exit 1; }

# shellcheck disable=SC1090
. "$FILE"

say "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
