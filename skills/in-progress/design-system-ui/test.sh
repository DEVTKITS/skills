#!/usr/bin/env bash
# Design System UI — smoke test suite.
# Boots the zero-dependency backend on a scratch port and asserts every
# endpoint plus the execute→evaluate→iterate loop. Also builds the Go and
# Rust clients when toolchains are present and drives the backend with them.
# Exit code 0 = all green. Suitable for CI.
set -u
cd "$(dirname "$0")"

PORT="${TEST_PORT:-8765}"
BASE="http://localhost:${PORT}"
PASS=0; FAIL=0
LOG="$(mktemp)"

say()  { printf '%s\n' "$*"; }
ok()   { PASS=$((PASS+1)); say "  ✓ $1"; }
bad()  { FAIL=$((FAIL+1)); say "  ✗ $1"; }

# assert_contains <label> <needle> <haystack>
assert_contains() {
  case "$3" in *"$2"*) ok "$1";; *) bad "$1 — missing: $2";; esac
}

say "── boot ─────────────────────────────────────────────"
PORT="$PORT" python3 server.py >"$LOG" 2>&1 &
SRV=$!
trap 'kill "$SRV" 2>/dev/null' EXIT

ready=""
for _ in $(seq 1 50); do
  curl -sf "$BASE/health" >/dev/null 2>&1 && { ready=1; break; }
  python3 -c 'import time; time.sleep(0.1)'
done
[ -n "$ready" ] && ok "server boots (pid $SRV, port $PORT)" \
                || { bad "server did not become ready"; cat "$LOG"; exit 1; }

say "── static & agent surface ───────────────────────────"
assert_contains "GET / serves UI"        "<!DOCTYPE html>"      "$(curl -s "$BASE/")"
assert_contains "GET /llms.txt"          "llms.txt"             "$(curl -s "$BASE/llms.txt")"
assert_contains "GET /styles.css"        "--color-primary"      "$(curl -s "$BASE/styles.css")"
assert_contains "GET /app.js"            "DesignSystemApp"      "$(curl -s "$BASE/app.js")"
assert_contains "GET /health"            '"status": "healthy"'  "$(curl -s "$BASE/health")"
assert_contains "GET /api/patterns"      "rag-pipeline"         "$(curl -s "$BASE/api/patterns")"
assert_contains "404 is JSON"            '"error"'              "$(curl -s "$BASE/nope")"

say "── execute ──────────────────────────────────────────"
EXEC=$(curl -s -X POST "$BASE/api/workflow/execute" \
  -H 'Content-Type: application/json' -d @examples/text-classifier.json)
assert_contains "valid workflow succeeds"     '"status": "success"' "$EXEC"
assert_contains "topological order ran llm"   "llm1: LLM"           "$EXEC"

BAD=$(curl -s -X POST "$BASE/api/workflow/execute" \
  -H 'Content-Type: application/json' \
  -d '{"id":"bad","nodes":[{"id":"in1","type":"text-input"}],"edges":[]}')
assert_contains "invalid workflow rejected"   '"status": "error"'   "$BAD"
assert_contains "error names the problem"     "No output node"      "$BAD"

say "── evaluate / iterate loop ──────────────────────────"
GOOD_EVAL=$(curl -s -X POST "$BASE/api/workflow/evaluate" \
  -H 'Content-Type: application/json' \
  -d "{\"workflow\":$(cat examples/text-classifier.json),\"results\":{}}")
assert_contains "good graph verdict done"     '"verdict": "done"'    "$GOOD_EVAL"
assert_contains "score 100"                   '"score": 100'         "$GOOD_EVAL"
assert_contains "reports evaluator liveness"  '"gemma_live"'         "$GOOD_EVAL"

WEAK_EVAL=$(curl -s -X POST "$BASE/api/workflow/evaluate" \
  -H 'Content-Type: application/json' \
  -d '{"workflow":{"nodes":[{"id":"in1","type":"text-input"},{"id":"out1","type":"text-output"}],"edges":[]},"results":{}}')
assert_contains "weak graph verdict iterate"  '"verdict": "iterate"' "$WEAK_EVAL"
assert_contains "suggests adding LLM node"    "Add LLM node"         "$WEAK_EVAL"

PARAMS=$(curl -s -X POST "$BASE/api/workflow/suggest-params" \
  -H 'Content-Type: application/json' -d '{"nodeType":"llm-node"}')
assert_contains "param suggestions"           '"model": "gemma-4"'   "$PARAMS"

say "── polyglot clients ─────────────────────────────────"
if command -v go >/dev/null 2>&1; then
  if go build -o /tmp/dsui-test-go main.go 2>>"$LOG"; then
    ok "go build"
    OUT=$(BACKEND_URL="$BASE" /tmp/dsui-test-go run examples/text-classifier.json 2>&1)
    assert_contains "go client drives backend" '"status": "success"' "$OUT"
  else bad "go build"; fi
else say "  - go toolchain absent, skipped"; fi

if command -v rustc >/dev/null 2>&1; then
  if rustc client.rs -o /tmp/dsui-test-rs 2>>"$LOG"; then
    ok "rustc build"
    OUT=$(BACKEND_URL="$BASE" /tmp/dsui-test-rs execute examples/text-classifier.json 2>&1)
    assert_contains "rust client drives backend" '"status": "success"' "$OUT"
  else bad "rustc build"; fi
else say "  - rust toolchain absent, skipped"; fi

say "─────────────────────────────────────────────────────"
say "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
