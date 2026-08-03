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
trap 'kill "$SRV" ${CLAW_SRV:-} 2>/dev/null' EXIT

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

say "── agent visibility / chat / stock / integrations ───"
POSTED=$(curl -s -X POST "$BASE/api/agent/activity" \
  -H 'Content-Type: application/json' \
  -d '{"actor":"test-agent","action":"click","target":"#executeWorkflow","detail":"smoke","state":"working"}')
assert_contains "activity event accepted"     '"ok": true'           "$POSTED"
FEED=$(curl -s "$BASE/api/agent/activity?since=0")
assert_contains "glow active while working"   '"active": true'       "$FEED"
assert_contains "event in feed"               '#executeWorkflow'     "$FEED"
CHAT=$(curl -s -X POST "$BASE/api/chat" \
  -H 'Content-Type: application/json' -d '{"message":"hi","history":[]}')
assert_contains "chat replies (live or offline)" '"reply"'           "$CHAT"
STOCK=$(curl -s "$BASE/api/stock/status")
assert_contains "stock status lists pexels"   '"pexels"'             "$STOCK"
SEARCH=$(curl -s -X POST "$BASE/api/stock/search" \
  -H 'Content-Type: application/json' -d '{"provider":"pexels","query":"x"}')
assert_contains "unconnected stock says how to connect" 'PEXELS_API_KEY' "$SEARCH"
INTEG=$(curl -s "$BASE/api/integrations")
assert_contains "integrations manifest"       '"adapter_ready"'      "$INTEG"
assert_contains "unverified names stay honest" 'unverified_roadmap'  "$INTEG"

say "── providers / help / debug / repo ──────────────────"
PROV=$(curl -s "$BASE/api/providers")
assert_contains "provider registry"           '"openrouter"'         "$PROV"
assert_contains "nvidia cloud present"        '"nvidia"'             "$PROV"
assert_contains "huggingface present"         '"huggingface"'        "$PROV"
assert_contains "ampel status per provider"   '"ampel"'              "$PROV"
assert_contains "connect hint given"          'connect_hint'         "$PROV"

HELP=$(curl -s "$BASE/api/help?topic=canvas")
assert_contains "help topic has guide"        '"guide"'              "$HELP"
assert_contains "help topic has rotating tips" '"tips"'              "$HELP"
PREF=$(curl -s "$BASE/api/prefixes")
assert_contains "prefix syntax registry"      'pattern'              "$PREF"

SELF=$(curl -s "$BASE/api/debug/selftest")
assert_contains "selftest all green"          '"ampel": "green"'     "$SELF"
assert_contains "selftest reports failures"   '"failed": 0'          "$SELF"
STRESS=$(curl -s -X POST "$BASE/api/debug/stress" \
  -H 'Content-Type: application/json' -d '{"runs":20}')
assert_contains "stress test runs clean"      '"errors": 0'          "$STRESS"
assert_contains "stress reports latency"      '"p95"'                "$STRESS"

REPO=$(curl -s "$BASE/api/github/status")
assert_contains "repo status has branch"      '"branch"'             "$REPO"
SKILLS=$(curl -s "$BASE/api/github/skills")
assert_contains "repo skills discovered"      '"skills"'             "$SKILLS"

PFX=$(curl -s -X POST "$BASE/api/chat" -H 'Content-Type: application/json' \
  -d '{"message":"#rag-pipeline","history":[]}')
assert_contains "chat prefix loads pattern"   'load_pattern'         "$PFX"
CMD=$(curl -s -X POST "$BASE/api/chat" -H 'Content-Type: application/json' \
  -d '{"message":"/execute","history":[]}')
assert_contains "chat prefix runs command"    '"command"'            "$CMD"

say "── UI surfaces present ──────────────────────────────"
HTML=$(curl -s "$BASE/")
assert_contains "hamburger menu"              'id="hamburger"'       "$HTML"
assert_contains "drawer with all views"       'data-view="providers"' "$HTML"
assert_contains "info buttons with help keys" 'data-help="canvas"'   "$HTML"
assert_contains "ampel indicator"             'id="headerAmpel"'     "$HTML"
assert_contains "tts toggle"                  'id="ttsToggle"'       "$HTML"
assert_contains "split chat+canvas view"      'id="view-chat"'       "$HTML"
assert_contains "skill builder view"          'id="view-skills"'     "$HTML"
assert_contains "debug view"                  'id="view-debug"'      "$HTML"

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

say "── OpenCLAW On-Demand-Adapter ───────────────────────"
CLAW=$(BASE_URL="$BASE" python3 adapters/openclaw_runner.py \
         "Baue einen RAG-Workflow und optimiere ihn" 2>&1)
assert_contains "runner erreicht ein Urteil"      '"verdict"'      "$CLAW"
assert_contains "runner waehlt Muster aus Auftrag" 'rag-pipeline'  "$CLAW"
assert_contains "runner laeuft ohne Shell weiter"  '"fallback"'    "$CLAW"
FEED=$(curl -s "$BASE/api/agent/activity?since=0")
assert_contains "runner meldet sich als openclaw"  '"openclaw"'    "$FEED"
assert_contains "runner meldet Sitzungsstart"      'session-start' "$FEED"

say "── Nanobot & Agent-zu-Agent ─────────────────────────"
CARD=$(curl -s "$BASE/api/a2a/card")
assert_contains "nanobot stellt sich vor"     '"kind": "nanobot"'   "$CARD"
assert_contains "nennt sein protokoll"        'design-system-ui/1'  "$CARD"
A2A1=$(curl -s -X POST "$BASE/api/a2a/message" -H 'Content-Type: application/json' \
  -d '{"from":"claude","to":"nanobot","intent":"load-pattern","body":"#rag-pipeline"}')
assert_contains "nanobot antwortet direkt"    '"delivered": "direct"' "$A2A1"
assert_contains "praefix wird zur aktion"     'load_pattern'          "$A2A1"
A2A2=$(curl -s -X POST "$BASE/api/a2a/message" -H 'Content-Type: application/json' \
  -d '{"from":"claude","to":"opencode","intent":"handoff","body":"uebernimm bitte"}')
assert_contains "nachricht wird zugestellt"   '"delivered": "queued"' "$A2A2"
POLL=$(curl -s "$BASE/api/a2a/poll?agent=opencode")
assert_contains "posteingang wird geleert"    'uebernimm bitte'       "$POLL"
DIR=$(curl -s "$BASE/api/a2a/directory")
assert_contains "verzeichnis kennt nanobot"   'nanobot'               "$DIR"
FEED2=$(curl -s "$BASE/api/agent/activity?since=0")
assert_contains "a2a ist fuer menschen sichtbar" '"a2a"'              "$FEED2"
OAI=$(curl -s "$BASE/api/providers")
assert_contains "openai/gpt als provider"     'OPENAI_API_KEY'        "$OAI"

say "── C-Gerüst ─────────────────────────────────────────"
if command -v cc >/dev/null 2>&1; then
  if (cd c && cc -O2 -std=c99 -pthread -o /tmp/dsui-c-test server.c 2>>"$LOG"); then
    ok "c backend kompiliert"
    ( cd c && PORT=$((PORT + 2)) /tmp/dsui-c-test >/dev/null 2>&1 & echo $! > /tmp/dsui-c.pid )
    CBASE="http://localhost:$((PORT + 2))"
    for _ in $(seq 1 40); do
      curl -sf "$CBASE/health" >/dev/null 2>&1 && break
      python3 -c 'import time; time.sleep(0.1)'
    done
    assert_contains "c health"        '"engine":"c"'   "$(curl -s "$CBASE/health")"
    CEX=$(curl -s -X POST "$CBASE/api/workflow/execute" -H 'Content-Type: application/json' \
      -d @examples/text-classifier.json)
    assert_contains "c fuehrt workflow aus"  '"status":"success"'  "$CEX"
    CEV=$(curl -s -X POST "$CBASE/api/workflow/evaluate" -H 'Content-Type: application/json' \
      -d "{\"workflow\":$(cat examples/text-classifier.json),\"results\":{}}")
    assert_contains "c bewertet mit urteil"  '"verdict":"done"'    "$CEV"
    kill "$(cat /tmp/dsui-c.pid)" 2>/dev/null
  else bad "c backend kompiliert nicht"; fi
else say "  - kein C-Compiler, uebersprungen"; fi

say "── Versionen ────────────────────────────────────────"
VCOUNT=$(ls -d versions/v*/ 2>/dev/null | wc -l)
[ "$VCOUNT" -ge 5 ] && ok "eigenstaendige Versionen archiviert ($VCOUNT)" \
                    || bad "nur $VCOUNT Versionen archiviert"
for v in versions/v*/; do
  [ -f "$v/server.py" ] || bad "$(basename $v) ist nicht eigenstaendig lauffaehig"
done
ok "jede Version bringt ihr eigenes Backend mit"

say "── Editionen ────────────────────────────────────────"
# Der Testserver läuft in der Normal-Edition: OpenCLAW muss dort inaktiv sein.
ED=$(curl -s "$BASE/api/edition")
assert_contains "edition endpoint"              '"edition"'      "$ED"
assert_contains "normal ist die Voreinstellung" '"id": "normal"' "$ED"
assert_contains "beide Editionen gelistet"      'openclaw'       "$ED"
N404=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/api/openclaw/status")
[ "$N404" = "404" ] && ok "openclaw in Normal-Edition abgeschaltet" \
                    || bad "openclaw antwortet in Normal-Edition ($N404)"
# Zweite Instanz in der OpenCLAW-Edition hochfahren
CLAW_PORT=$((PORT + 1))
PORT="$CLAW_PORT" EDITION=openclaw python3 server.py >/dev/null 2>&1 &
CLAW_SRV=$!
CLAW_BASE="http://localhost:${CLAW_PORT}"
for _ in $(seq 1 50); do
  curl -sf "$CLAW_BASE/health" >/dev/null 2>&1 && break
  python3 -c 'import time; time.sleep(0.1)'
done
CED=$(curl -s "$CLAW_BASE/api/edition")
assert_contains "openclaw-edition meldet sich"  '"id": "openclaw"' "$CED"
C200=$(curl -s -o /dev/null -w "%{http_code}" "$CLAW_BASE/api/openclaw/status")
[ "$C200" = "200" ] && ok "openclaw in OpenCLAW-Edition aktiv" \
                    || bad "openclaw fehlt in OpenCLAW-Edition ($C200)"
CHTML=$(curl -s "$CLAW_BASE/")
assert_contains "menue in beiden Editionen"     'id="hamburger"'   "$CHTML"
assert_contains "kacheln in beiden Editionen"   'id="providerGrid"' "$CHTML"

say "── OpenCLAW aus der Oberfläche ──────────────────────"
BASE="$CLAW_BASE"   # die folgenden Prüfungen brauchen die OpenCLAW-Edition
CS=$(curl -s "$BASE/api/openclaw/status")
assert_contains "openclaw status endpoint"     '"mode"'          "$CS"
assert_contains "meldet Fallback ohne Shell"   '"fallback"'      "$CS"
START=$(curl -s -X POST "$BASE/api/openclaw/run" -H 'Content-Type: application/json' \
          -d '{"task":"Baue einen RAG-Workflow und optimiere ihn"}')
assert_contains "lauf startet ueber die API"   '"started": true' "$START"
for _ in $(seq 1 60); do
  curl -s "$BASE/api/openclaw/status" | grep -q '"running": false' && break
  python3 -c 'import time; time.sleep(0.4)'
done
DONE=$(curl -s "$BASE/api/openclaw/status")
assert_contains "lauf erreicht ein Urteil"     '"verdict"'       "$DONE"
assert_contains "waehlt Muster aus Auftrag"    'rag-pipeline'    "$DONE"
HELP_CLAW=$(curl -s "$BASE/api/help?topic=openclaw")
assert_contains "hilfe kennt die Schreibweise" 'opencloude'      "$HELP_CLAW"
UIHTML=$(curl -s "$BASE/")
assert_contains "steuerung in der Oberflaeche" 'id="clawRun"'    "$UIHTML"

BASE="http://localhost:${PORT}"   # zurück zur Normal-Edition
say "── Sicherheit ───────────────────────────────────────"
if command -v node >/dev/null 2>&1 && [ -d demo/node_modules/playwright ]; then
  if (cd demo && CHROMIUM_PATH="${CHROMIUM_PATH:-/opt/pw-browsers/chromium}" \
        node xss_check.mjs "$BASE" >/dev/null 2>&1); then
    ok "importiertes Workflow-JSON kann kein Skript ausführen (DOM-XSS)"
  else
    bad "DOM-XSS über importiertes Workflow-JSON möglich"
  fi
else
  say "  - playwright fehlt (cd demo && npm install playwright), XSS-Test übersprungen"
fi

say "─────────────────────────────────────────────────────"
say "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
