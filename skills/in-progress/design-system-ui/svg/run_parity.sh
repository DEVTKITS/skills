#!/usr/bin/env bash
# Byte-Parität gegen svg/golden — Aufruf: svg/run_parity.sh <js|py|rs|c>
#   js: node svgkit.js · py: .venv/bin/python svgkit.py · rs: rust/target/release/dsui-svg · c: c/dsui-svg
# Fährt jede Fixture, deren "_impl" die Fassung nennt, und vergleicht die Ausgabe mit cmp (byte-genau).
# Danach: ungültige Spezifikationen (svg/fixtures-invalid) — erwartet Exitcode 1 und auf stderr "svgspec: <Feld>:".
# Exitcode 0 nur, wenn alles übereinstimmt.
set -u
IMPL="${1:-}"
cd "$(dirname "$0")/.."
ROOT="$PWD"
PY="python3"; [ -x .venv/bin/python ] && PY="$ROOT/.venv/bin/python"
case "$IMPL" in
  js) CMD=(node "$ROOT/svgkit.js") ;;
  py) CMD=("$PY" "$ROOT/svgkit.py") ;;
  rs) cargo build --release --quiet --manifest-path rust/Cargo.toml >&2 || { echo "✗ cargo build schlägt fehl"; exit 1; }
      CMD=("$ROOT/rust/target/release/dsui-svg") ;;
  c)  make -s -C c dsui-svg >&2 || { echo "✗ make -C c dsui-svg schlägt fehl"; exit 1; }
      CMD=("$ROOT/c/dsui-svg") ;;
  *) echo "Aufruf: svg/run_parity.sh <js|py|rs|c>" >&2; exit 2 ;;
esac
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
PASS=0; FAIL=0

# Liste: gültige Fixtures dieser Fassung / ungültige mit erwartetem Feld
"$PY" -I - "$IMPL" "$ROOT/svg" >"$TMP/list.txt" <<'PYE'
import json, os, sys
impl, base = sys.argv[1], sys.argv[2]
CT = {"bar", "donut", "badge", "icon", "gradient", "pattern"}
fx = os.path.join(base, "fixtures")
for f in sorted(os.listdir(fx)):
    if f.endswith(".json"):
        d = json.load(open(os.path.join(fx, f), encoding="utf-8"))
        if impl in d.get("_impl", []):
            print("V\t" + f[:-5])
bad = os.path.join(base, "fixtures-invalid")
for f in sorted(os.listdir(bad)):
    if not f.endswith(".json"):
        continue
    n = f[:-5]
    ex = os.path.join(bad, n + ".expect")
    try:
        d = json.load(open(os.path.join(bad, f), encoding="utf-8"))
    except Exception:
        d = None
    if os.path.exists(ex):
        field = open(ex, encoding="utf-8").read().strip()
    else:
        field = d.get("_error_field", "")
    if impl == "c" and isinstance(d, dict) and isinstance(d.get("type"), str) and d["type"] not in CT and d["type"] in ("line", "graph", "monogram"):
        continue
    print("I\t" + n + "\t" + field)
PYE

echo "Fassung: $IMPL — Byte-Vergleich mit svg/golden"
while IFS=$'\t' read -r KIND NAME FIELD; do
  [ "$KIND" = V ] || continue
  if "${CMD[@]}" "svg/fixtures/$NAME.json" >"$TMP/out.svg" 2>"$TMP/err.txt"; then
    if cmp -s "$TMP/out.svg" "svg/golden/$NAME.svg"; then PASS=$((PASS+1)); echo "  ✓ $NAME"
    else FAIL=$((FAIL+1)); echo "  ✗ $NAME — Ausgabe weicht ab ($(cmp "$TMP/out.svg" "svg/golden/$NAME.svg" 2>&1 | head -1))"; fi
  else FAIL=$((FAIL+1)); echo "  ✗ $NAME — Exitcode ≠ 0: $(head -c 200 "$TMP/err.txt")"; fi
done <"$TMP/list.txt"
NV=$((PASS+FAIL)); echo "  Gültige Fixtures: $PASS/$NV identisch"

echo "Ungültige Spezifikationen (erwartet: Exitcode 1 und \"svgspec: <Feld>:\" auf stderr)"
IP=0; IF=0
while IFS=$'\t' read -r KIND NAME FIELD; do
  [ "$KIND" = I ] || continue
  "${CMD[@]}" "svg/fixtures-invalid/$NAME.json" >"$TMP/out.svg" 2>"$TMP/err.txt"; RC=$?
  if [ "$RC" -eq 1 ] && grep -q "^svgspec: $FIELD:" "$TMP/err.txt" && [ ! -s "$TMP/out.svg" ]; then IP=$((IP+1)); echo "  ✓ $NAME → $FIELD"
  else IF=$((IF+1)); echo "  ✗ $NAME — Exitcode $RC, stderr: $(head -c 160 "$TMP/err.txt"), erwartet Feld $FIELD"; fi
done <"$TMP/list.txt"
echo "  Ungültige: $IP/$((IP+IF)) korrekt abgelehnt"

TOTAL_FAIL=$((FAIL+IF))
echo "Ergebnis ($IMPL): $((PASS+IP)) ✓, $TOTAL_FAIL ✗"
[ "$TOTAL_FAIL" -eq 0 ]
