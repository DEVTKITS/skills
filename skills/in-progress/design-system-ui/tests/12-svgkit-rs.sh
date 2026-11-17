# tests/12-svgkit-rs.sh — Rust-Fassung des SVG-Generators (rust/, Binär rust/target/release/dsui-svg).
# Track svgkit-rs. Wird von test.sh eingelesen oder allein ausgeführt:
#   tests/run-one.sh tests/12-svgkit-rs.sh 8820
# Nutzt ok / bad / say, $PY. Startet keine Server, nur kurzlebige Prozesse (cargo, dsui-svg, node).
# Prüft: Bau ohne Warnungen (+ clippy), cargo test, Byte-Parität, CLI-Vertrag, Fuzz (Absturzfreiheit und
# Gleichheit mit svgkit.js), Laufzeit.

RS12="rust/target/release/dsui-svg"
T12="$(mktemp -d)"
now12() { date +%s%N; }

# 1. Bau ohne Warnungen (Quelle neu übersetzen, damit Warnungen wirklich erscheinen)
if command -v cargo >/dev/null 2>&1; then
  touch rust/src/lib.rs rust/src/main.rs
  B12="$(cargo build --release --manifest-path rust/Cargo.toml 2>&1)"; RB12=$?
  if [ "$RB12" -eq 0 ] && ! printf '%s' "$B12" | grep -q '^warning'; then ok "cargo build --release: keine Warnungen"; else bad "cargo build --release: Exitcode $RB12 oder Warnungen"; printf '%s\n' "$B12" | head -10; fi
  if cargo clippy --version >/dev/null 2>&1; then
    touch rust/src/lib.rs
    C12="$(cargo clippy --release --all-targets --manifest-path rust/Cargo.toml 2>&1)"; RC12=$?
    if [ "$RC12" -eq 0 ] && ! printf '%s' "$C12" | grep -q '^warning'; then ok "cargo clippy: keine Meldungen"; else bad "cargo clippy: Meldungen"; printf '%s\n' "$C12" | head -10; fi
  else say "  – clippy nicht installiert, übersprungen"; fi
  if ! grep -q '^\[dependencies\]' rust/Cargo.toml || [ -z "$(sed -n '/^\[dependencies\]/,/^\[/p' rust/Cargo.toml | grep -v '^\[' | grep -v '^$')" ]; then ok "Cargo.toml: keine Abhängigkeiten"; else bad "Cargo.toml: enthält Abhängigkeiten"; fi
else
  bad "cargo fehlt"
fi

# 2. Unit-Tests (JSON-Parser, fmt, PRNG, Text, Randfälle, Golden-Parität, Fuzz mit 3000 verstümmelten Eingaben)
U12="$(cargo test --release --manifest-path rust/Cargo.toml -- --nocapture 2>&1)"; RU12=$?
NU12="$(printf '%s\n' "$U12" | sed -n 's/^test result: ok\. \([0-9]*\) passed.*/\1/p' | head -1)"
if [ "$RU12" -eq 0 ] && [ "${NU12:-0}" -ge 20 ]; then ok "cargo test: ${NU12} Tests bestanden"; else bad "cargo test: Exitcode $RU12"; printf '%s\n' "$U12" | grep -E 'FAILED|panicked' | head -5; fi
FZ12="$(printf '%s\n' "$U12" | grep '^Fuzz' | head -1)"; [ -n "$FZ12" ] && say "    $FZ12"

# 3. Byte-Parität gegen svg/golden und ungültige Spezifikationen
P12="$(svg/run_parity.sh rs 2>&1)"; RP12=$?
NV12="$(printf '%s\n' "$P12" | grep -c '^  ✓')"
if [ "$RP12" -eq 0 ] && [ "$NV12" -ge 100 ]; then ok "run_parity.sh rs: $NV12 ✓, kein ✗ ($(printf '%s\n' "$P12" | grep 'Gültige Fixtures' | sed 's/^ *//'))"; else bad "run_parity.sh rs: Exitcode $RP12, $NV12 ✓"; printf '%s\n' "$P12" | grep '✗' | head -5; fi

# 4. Jede Fixture mit "rs" in _impl: eigene cmp-Schleife (unabhängig von run_parity.sh) und Typenabdeckung
N12=0; F12=0
for F in svg/fixtures/*.json; do
  grep -q '"rs"' "$F" || continue
  NAME12="$(basename "$F" .json)"; N12=$((N12+1))
  if "$RS12" "$F" >"$T12/o.svg" 2>/dev/null && cmp -s "$T12/o.svg" "svg/golden/$NAME12.svg"; then :; else F12=$((F12+1)); echo "    ✗ $NAME12"; fi
done
NT12="$("$PY" -I -c '
import json, sys
print(len({d["type"] for d in (json.load(open(f, encoding="utf-8")) for f in sys.argv[1:]) if "rs" in d.get("_impl", [])}))' svg/fixtures/*.json)"
if [ "$F12" -eq 0 ] && [ "$N12" -ge 100 ] && [ "$NT12" -ge 9 ]; then ok "$N12 Fixtures byte-gleich (cmp), $NT12 Typen abgedeckt"; else bad "Fixtures: $F12 abweichend von $N12, Typen $NT12"; fi

# 5. CLI-Vertrag: kein abschließender Zeilenumbruch, Fehler = Exitcode 1, stdout leer, genau eine Zeile "svgspec: <Feld>: …" auf stderr
"$RS12" svg/fixtures/badge-master-vars.json >"$T12/o.svg"
[ "$(tail -c 6 "$T12/o.svg")" = "</svg>" ] && ok "Ausgabe endet auf </svg> ohne Zeilenumbruch" || bad "Ausgabe endet nicht auf </svg>"
chk12() { # Name, Datei-Erzeuger-Ausgabe liegt in $T12/in.json, erwartetes Feld
  "$RS12" "$T12/in.json" >"$T12/o.out" 2>"$T12/o.err"; R=$?
  if [ "$R" -eq 1 ] && [ ! -s "$T12/o.out" ] && [ "$(wc -l <"$T12/o.err" | tr -d ' ')" = 1 ] && grep -q "^svgspec: $2: " "$T12/o.err"; then ok "$1 → Exitcode 1, svgspec: $2"; else bad "$1: Exitcode $R, stderr: $(head -c 120 "$T12/o.err")"; fi
}
: >"$T12/in.json";                                         chk12 "leere Datei" spec
printf '\xef\xbb\xbf{"type":"badge"}' >"$T12/in.json";     chk12 "UTF-8-BOM (wie JSON.parse ungültig)" spec
printf '{"type":"badge"' >"$T12/in.json";                  chk12 "unvollständiges JSON" spec
printf '{"type":"badge"} x' >"$T12/in.json";               chk12 "Text nach dem JSON" spec
printf '[1,2]' >"$T12/in.json";                            chk12 "Array statt Objekt" spec
printf '{"type":"nix"}' >"$T12/in.json";                   chk12 "unbekannter Typ" type
printf '{"type":"bar","width":"480"}' >"$T12/in.json";     chk12 "Zahl als Text" width
printf '{"type":"bar","width":1e999}' >"$T12/in.json";     chk12 "Zahl 1e999 (unendlich)" width
"$RS12" "$T12/gibt-es-nicht.json" >"$T12/o.out" 2>"$T12/o.err"; R=$?
[ "$R" -eq 1 ] && grep -q '^svgspec: spec: ' "$T12/o.err" && ok "fehlende Datei → Exitcode 1, svgspec: spec" || bad "fehlende Datei: Exitcode $R"
"$RS12" >"$T12/o.out" 2>"$T12/o.err"; R=$?
[ "$R" -eq 1 ] && grep -q '^svgspec: spec: ' "$T12/o.err" && ok "ohne Argument → Exitcode 1, svgspec: spec" || bad "ohne Argument: Exitcode $R"
printf '{"type":"badge","label":"\xff\xfe"}' >"$T12/in.json"
if node -e '1' 2>/dev/null; then
  "$RS12" "$T12/in.json" >"$T12/a.svg" 2>/dev/null; node svgkit.js "$T12/in.json" >"$T12/b.svg" 2>/dev/null
  cmp -s "$T12/a.svg" "$T12/b.svg" && ok "ungültiges UTF-8 wird wie in Node durch U+FFFD ersetzt (gleich zu svgkit.js)" || bad "ungültiges UTF-8 weicht von svgkit.js ab"
fi
# Böse Eingaben aus dem Dateisystem: 20 MB Leerraum, 1 Mio. verschachtelte Klammern, 1 Mio. Schlüssel — kein Absturz, endlich
head -c 20000000 /dev/zero | tr '\0' ' ' >"$T12/in.json"
S12="$(now12)"; timeout 20 "$RS12" "$T12/in.json" >/dev/null 2>&1; R=$?; E12=$((($(now12)-S12)/1000000))
[ "$R" -eq 1 ] && ok "20 MB Leerraum: sauber abgelehnt in ${E12} ms" || bad "20 MB Leerraum: Exitcode $R"
head -c 1000000 /dev/zero | tr '\0' '[' >"$T12/in.json"
timeout 20 "$RS12" "$T12/in.json" >/dev/null 2>"$T12/o.err"; R=$?
[ "$R" -eq 1 ] && grep -q '^svgspec: spec: ' "$T12/o.err" && ok "1 Mio. '[' (Tiefe): Exitcode 1, kein Stapelüberlauf" || bad "1 Mio. '[': Exitcode $R"

# 6. Fuzz-Stichprobe über die CLI: 400 verstümmelte oder strukturell veränderte Fixtures
#    (a) nie Absturz (Exitcode nur 0 oder 1), nie Zeitüberschreitung, Fehler genau eine svgspec-Zeile;
#    (b) wo node vorhanden ist: gleicher Exitcode, gleiche Bytes, gleiches Fehlerfeld wie svgkit.js
"$PY" -I - svg/fixtures "$T12/fz" 400 4711 <<'PYE'
import json, os, random, sys
fx, out, n, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
rnd = random.Random(seed)
seeds = []
for sub in (fx, fx + "-invalid"):
    for f in sorted(os.listdir(sub)):
        if f.endswith(".json"):
            seeds.append(open(os.path.join(sub, f), "rb").read())
INS = [b"null", b"[", b"{", b'"', b"\\u", b"1e999", b"-0", b"\\ud800", b",", b"\xff\xfe", b"\xc3", b"0.005", b"true"]
KEYS = ["width", "height", "data", "labels", "color", "seed", "title", "theme", "colors", "size", "stroke", "angle", "thickness",
        "density", "opacity", "nodes", "edges", "heading", "grid", "text", "kind", "label", "value", "via", "from", "to", "idprefix"]
VALS = [None, True, False, 0, -1, 1.5, 1e308, -1e9, 4294967296, 7, "", "x", "#abc", "#ABCDEF", "accent", "contrast", [], [1, 2, 3],
        [[1, 2], [3]], {}, "A" * 50, "ä\U0001F600", "a\u0000b"]
def structural(raw):
    try:
        d = json.loads(raw.decode("utf-8", "replace"))
    except Exception:
        return None
    def mut(v):
        r = rnd.random()
        if isinstance(v, dict):
            o = {k: mut(x) for k, x in v.items()}
            if rnd.random() < 0.2 and o: o.pop(rnd.choice(list(o)))
            if rnd.random() < 0.2: o[rnd.choice(KEYS)] = rnd.choice(VALS)
            return o
        if isinstance(v, list):
            o = [mut(x) for x in v]
            if rnd.random() < 0.2 and o: o.pop(rnd.randrange(len(o)))
            if rnd.random() < 0.2: o.insert(rnd.randrange(len(o) + 1), rnd.choice([0, -5, 1e9, 2.675, "s", None, {"id": "Z", "type": "q"}, [1]]))
            return o
        if isinstance(v, bool): return (not v) if r < 0.3 else v
        if isinstance(v, (int, float)): return rnd.choice([v, v * 3, -v, 0, 1e9, 0.005, 2.675, v + 0.5, 1e-7]) if r < 0.5 else v
        if isinstance(v, str): return rnd.choice([v, v + "\U0001F600", "<&>\"'" + v, "", v * 4, v.upper()]) if r < 0.3 else v
        return v
    return json.dumps(mut(d), ensure_ascii=rnd.random() < 0.5).encode()
def bytewise(raw):
    b = bytearray(raw)
    for _ in range(1 + int(rnd.random() * 4)):
        at = rnd.randrange(max(len(b), 1)); k = rnd.randrange(5)
        if k == 0 and b: b[at] = rnd.randrange(256)
        elif k == 1: del b[at:]
        elif k == 2: b[at:at] = rnd.choice(INS)
        elif k == 3 and b: del b[at]
        else: b[at:at] = b[at:at + rnd.randrange(40)]
    return bytes(b)
os.makedirs(out, exist_ok=True)
for i in range(n):
    raw = rnd.choice(seeds)
    m = structural(raw) if rnd.random() < 0.65 else None
    if m is None: m = bytewise(raw)
    open(os.path.join(out, "f%04d.json" % i), "wb").write(m)
PYE
HAVE_NODE12=0; node -e '1' 2>/dev/null && HAVE_NODE12=1
NF12=0; CR12=0; OK12=0; ER12=0; DV12=0
for F in "$T12"/fz/f*.json; do
  NF12=$((NF12+1))
  timeout 10 "$RS12" "$F" >"$T12/a.out" 2>"$T12/a.err"; RA=$?
  if [ "$RA" -ne 0 ] && [ "$RA" -ne 1 ]; then CR12=$((CR12+1)); echo "    ✗ $(basename "$F"): Exitcode $RA"; continue; fi
  if [ "$RA" -eq 1 ] && { [ -s "$T12/a.out" ] || [ "$(wc -l <"$T12/a.err" | tr -d ' ')" != 1 ] || ! grep -q '^svgspec: [a-z]*: ' "$T12/a.err"; }; then CR12=$((CR12+1)); echo "    ✗ $(basename "$F"): Fehlerformat"; continue; fi
  [ "$RA" -eq 0 ] && OK12=$((OK12+1)) || ER12=$((ER12+1))
  if [ "$HAVE_NODE12" -eq 1 ]; then
    timeout 10 node svgkit.js "$F" >"$T12/b.out" 2>"$T12/b.err"; RB=$?
    FA="$(sed -n 's/^svgspec: \([^:]*\):.*/\1/p' "$T12/a.err")"; FB="$(sed -n 's/^svgspec: \([^:]*\):.*/\1/p' "$T12/b.err")"
    if [ "$RA" -ne "$RB" ] || ! cmp -s "$T12/a.out" "$T12/b.out" || [ "$FA" != "$FB" ]; then DV12=$((DV12+1)); echo "    ≠ $(basename "$F"): rs=$RA ($FA) js=$RB ($FB)"; fi
  fi
done
if [ "$CR12" -eq 0 ]; then ok "Fuzz: $NF12 Eingaben ($OK12 gültig, $ER12 abgelehnt), kein Absturz, keine Zeitüberschreitung"; else bad "Fuzz: $CR12 Abstürze oder Formatfehler"; fi
if [ "$HAVE_NODE12" -eq 1 ]; then
  if [ "$DV12" -eq 0 ] && [ "$OK12" -ge 50 ]; then ok "Fuzz-Differenz gegen svgkit.js: $NF12 Eingaben, Exitcode, Bytes und Fehlerfeld identisch"; else bad "Fuzz-Differenz: $DV12 Abweichungen, $OK12 gültige Eingaben"; fi
fi

# 7. Laufzeit-Stichprobe: alle Fixtures mit "rs" (jeweils ein Prozess), Gesamtzeit und Zeit je Lauf
LIST12=""; for F in svg/fixtures/*.json; do grep -q '"rs"' "$F" && LIST12="$LIST12 $F"; done
NR12=0; S12="$(now12)"
for F in $LIST12; do "$RS12" "$F" >/dev/null 2>&1; NR12=$((NR12+1)); done
E12=$((($(now12)-S12)/1000000))
if [ "$NR12" -ge 100 ] && [ "$E12" -lt 30000 ]; then ok "Laufzeit: $NR12 Fixtures in ${E12} ms (≈ $((E12*1000/NR12)) µs je Prozessaufruf)"; else bad "Laufzeit: $NR12 Fixtures in ${E12} ms"; fi
if [ "$HAVE_NODE12" -eq 1 ]; then
  S12="$(now12)"; for F in $LIST12; do node svgkit.js "$F" >/dev/null 2>&1; done; EJ12=$((($(now12)-S12)/1000000))
  say "    zum Vergleich node svgkit.js: ${EJ12} ms für dieselben $NR12 Fixtures (Prozessstart eingeschlossen)"
fi
S12="$(now12)"; for F in svg/fixtures-invalid/*.json; do "$RS12" "$F" >/dev/null 2>&1; done; E12=$((($(now12)-S12)/1000000))
say "    ungültige Fixtures: $(ls svg/fixtures-invalid/*.json | wc -l | tr -d ' ') Läufe in ${E12} ms"

rm -rf "$T12"
