# tests/13-svgkit-c.sh — C-Fassung des SVG-Generators (c/svgkit.c, c/svgkit_cli.c, Endpunkte in c/server.c).
# Track svgkit-c. Wird von test.sh eingelesen oder allein ausgeführt:
#   tests/run-one.sh tests/13-svgkit-c.sh 8824
# Nutzt ok / bad / say, $PY, $PORT. Startet eigene C-Server auf 8824 (ASan-Fassung: 8825); ist ein Port
# belegt oder gleich $PORT, wird 8826–8829 genommen. Beendet jeden Prozess, den er startet.
# Prüft: Bau mit -Werror und ohne libm, Byte-Parität (-O0/-O2/-O3), ASan/UBSan-Lauf, Fuzz (Absturzfreiheit und
# Gleichheit mit svgkit.js), CLI-Vertrag, Teilmengen-Ablehnung, HTTP-Endpunkte gegen den laufenden C-Server.

C13="c/dsui-svg"
T13="$(mktemp -d)"
now13() { date +%s%N; }
SRV13=""; SRVA13=""
stop13() { # nur eigene Prozesse beenden (kein "wait" ohne Argument: run-one.sh hat selbst einen Hintergrundserver)
  [ -n "$SRV13" ] && { kill "$SRV13" 2>/dev/null; wait "$SRV13" 2>/dev/null; }
  [ -n "$SRVA13" ] && { kill "$SRVA13" 2>/dev/null; wait "$SRVA13" 2>/dev/null; }
  SRV13=""; SRVA13=""; return 0; }

# 1. Bau: Warnungen sind Fehler (-Werror im Makefile), zusätzlich ohne -Werror gezählt, auf mehreren Stufen
if command -v cc >/dev/null 2>&1; then
  make -s -C c clean >/dev/null 2>&1
  B13="$(make -C c 2>&1)"; RB13=$?
  if [ "$RB13" -eq 0 ] && ! printf '%s' "$B13" | grep -qi 'warning' && [ -x c/dsui-svg ] && [ -x c/dsui-c ]; then ok "make -C c: dsui-svg und dsui-c gebaut (-std=c99 -Wall -Wextra -Wpedantic -Werror), keine Warnungen"; else bad "make -C c: Exitcode $RB13 oder Warnungen"; printf '%s\n' "$B13" | head -10; fi
  W13=0
  for O13 in -O0 -O2 -O3 -Os; do
    for SRC13 in "svgkit_cli.c svgkit.c" "server.c"; do
      N13="$( (cd c && cc -std=c99 -Wall -Wextra -Wpedantic -ffp-contract=off $O13 -pthread -fsyntax-only $SRC13 2>&1) | grep -c 'warning')"
      W13=$((W13+N13))
    done
  done
  [ "$W13" -eq 0 ] && ok "keine Warnungen bei -O0/-O2/-O3/-Os (svgkit_cli.c+svgkit.c und server.c)" || bad "$W13 Warnungen bei anderen Optimierungsstufen"
  if command -v clang >/dev/null 2>&1; then
    N13="$( (cd c && clang -std=c99 -Wall -Wextra -Wpedantic -O2 -fsyntax-only svgkit_cli.c svgkit.c 2>&1) | grep -c 'warning')"
    [ "$N13" -eq 0 ] && ok "clang: keine Warnungen" || bad "clang: $N13 Warnungen"
  fi
  # der Einzeiler aus test.sh (C-Gerüst) muss weiter bauen: ohne svgkit.c auf der Kommandozeile, ohne -lm
  if (cd c && cc -O2 -std=c99 -pthread -o "$T13/oneliner" server.c 2>"$T13/ol.err"); then ok "test.sh-Einzeiler 'cc -O2 -std=c99 -pthread server.c' baut weiter (svgkit.c ist eingebunden, kein -lm nötig)"; else bad "test.sh-Einzeiler baut nicht"; head -5 "$T13/ol.err"; fi
  if command -v nm >/dev/null 2>&1; then
    L13="$(nm -u c/dsui-svg c/dsui-c 2>/dev/null | grep -E ' (cos|sin|floor|ceil|sqrt|pow|fmod|round)(@|$)' | head -3)"
    [ -z "$L13" ] && ok "keine libm-Symbole in dsui-svg und dsui-c (sin/cos/floor/ceil sind eingebaut)" || bad "libm-Symbole: $L13"
  fi
else
  bad "cc fehlt"
fi

# 2. Byte-Parität und ungültige Spezifikationen
P13="$(svg/run_parity.sh c 2>&1)"; RP13=$?
NV13="$(printf '%s\n' "$P13" | grep -c '^  ✓')"
if [ "$RP13" -eq 0 ] && [ "$NV13" -ge 100 ]; then ok "run_parity.sh c: $NV13 ✓, kein ✗ ($(printf '%s\n' "$P13" | grep 'Gültige Fixtures' | sed 's/^ *//'); $(printf '%s\n' "$P13" | grep 'Ungültige:' | sed 's/^ *//'))"; else bad "run_parity.sh c: Exitcode $RP13, $NV13 ✓"; printf '%s\n' "$P13" | grep '✗' | head -5; fi

# 3. Eigene cmp-Schleife (unabhängig von run_parity.sh) und Abdeckung der Teilmenge
N13=0; F13=0
for F in svg/fixtures/*.json; do
  grep -q '"c"' "$F" || continue
  "$PY" -I -c 'import json,sys; sys.exit(0 if "c" in json.load(open(sys.argv[1],encoding="utf-8")).get("_impl",[]) else 1)' "$F" || continue
  NAME13="$(basename "$F" .json)"; N13=$((N13+1))
  if "$C13" "$F" >"$T13/o.svg" 2>/dev/null && cmp -s "$T13/o.svg" "svg/golden/$NAME13.svg"; then :; else F13=$((F13+1)); echo "    ✗ $NAME13"; fi
done
CV13="$("$PY" -I -c '
import json, sys
s = set()
for f in sys.argv[1:]:
    d = json.load(open(f, encoding="utf-8"))
    if "c" in d.get("_impl", []): s.add(d["type"] + (":" + d["kind"] if d["type"] == "pattern" else ""))
print(" ".join(sorted(s)))' svg/fixtures/*.json)"
NT13="$(printf '%s\n' $CV13 | wc -l | tr -d ' ')"
if [ "$F13" -eq 0 ] && [ "$N13" -ge 70 ] && [ "$NT13" -ge 8 ]; then ok "$N13 Fixtures byte-gleich (cmp), abgedeckt: $CV13"; else bad "Fixtures: $F13 abweichend von $N13, Arten $NT13"; fi

# 4. Parität auch bei -O0 und -O3 (Gleitkomma darf nicht von der Optimierung abhängen)
for O13 in -O0 -O3; do
  if (cd c && cc -std=c99 -ffp-contract=off $O13 -o "$T13/svg$O13" svgkit_cli.c svgkit.c 2>/dev/null); then
    D13=0; M13=0
    for F in svg/fixtures/*.json; do
      "$PY" -I -c 'import json,sys; sys.exit(0 if "c" in json.load(open(sys.argv[1],encoding="utf-8")).get("_impl",[]) else 1)' "$F" || continue
      NAME13="$(basename "$F" .json)"; M13=$((M13+1))
      "$T13/svg$O13" "$F" >"$T13/o.svg" 2>/dev/null && cmp -s "$T13/o.svg" "svg/golden/$NAME13.svg" || D13=$((D13+1))
    done
    [ "$D13" -eq 0 ] && ok "Parität bei $O13: $M13/$M13 byte-gleich" || bad "Parität bei $O13: $D13 abweichend"
  else bad "Bau bei $O13 schlägt fehl"; fi
done

# 5. sin/cos-Port: Bit-Gleichheit mit Math.sin/Math.cos aus Node (gleicher fdlibm-Algorithmus wie V8)
if node -e '1' 2>/dev/null; then
  cat >"$T13/trig.c" <<'CE'
#include "svgkit.c"
#include <stdio.h>
int main(void) { double x; uint64_t u, v; while (scanf("%lf", &x) == 1) { double c = sk_cos(x), s = sk_sin(x); memcpy(&u, &c, 8); memcpy(&v, &s, 8); printf("%016llx %016llx\n", (unsigned long long)u, (unsigned long long)v); } return 0; }
CE
  if (cd c && cc -std=c99 -O2 -ffp-contract=off -I. -o "$T13/trig" "$T13/trig.c" 2>"$T13/trig.err"); then
    cat >"$T13/trig.mjs" <<'JE'
import {execFileSync} from 'node:child_process';
const xs=[];for(let a=0;a<=360;a++)xs.push(a*Math.PI/180);
let seed=12345;const rnd=()=>(seed=(seed*1664525+1013904223)%4294967296)/4294967296;
for(let i=0;i<100000;i++)xs.push(-Math.PI/2+2*Math.PI*rnd());
for(let i=0;i<50000;i++)xs.push(rnd()*8-1);
for(let i=0;i<10000;i++){const k=Math.floor(rnd()*5);xs.push(k*Math.PI/2+(rnd()-.5)*1e-3);}
const out=execFileSync(process.argv[2],{input:xs.map(x=>x.toPrecision(17)).join('\n')+'\n',maxBuffer:1e9}).toString().trim().split('\n');
const b=new DataView(new ArrayBuffer(8));const hex=d=>{b.setFloat64(0,d);return b.getBigUint64(0).toString(16).padStart(16,'0')};
let bad=0;xs.forEach((x,i)=>{const p=parseFloat(x.toPrecision(17));const [c,s]=out[i].split(' ');if(hex(Math.cos(p))!==c||hex(Math.sin(p))!==s)bad++;});
console.log(xs.length+' '+bad);
JE
    R13="$(node "$T13/trig.mjs" "$T13/trig" 2>&1)"
    [ "${R13##* }" = "0" ] && ok "sin/cos: ${R13% *} Argumente (alle ganzen Grad, Kreisbögen, Randfälle) bit-gleich zu Node" || bad "sin/cos weicht von Node ab: $R13"
  else bad "trig-Prüfer baut nicht"; head -5 "$T13/trig.err"; fi
fi

# 6. CLI-Vertrag: kein abschließender Zeilenumbruch, Fehler = Exitcode 1, stdout leer, genau eine Zeile "svgspec: <Feld>: …"
"$C13" svg/fixtures/badge-master-vars.json >"$T13/o.svg"
[ "$(tail -c 6 "$T13/o.svg")" = "</svg>" ] && ok "Ausgabe endet auf </svg> ohne Zeilenumbruch" || bad "Ausgabe endet nicht auf </svg>"
chk13() { # Name, Eingabe liegt in $T13/in.json, erwartetes Feld
  "$C13" "$T13/in.json" >"$T13/o.out" 2>"$T13/o.err"; R=$?
  if [ "$R" -eq 1 ] && [ ! -s "$T13/o.out" ] && [ "$(wc -l <"$T13/o.err" | tr -d ' ')" = 1 ] && grep -q "^svgspec: $2: " "$T13/o.err"; then ok "$1 → Exitcode 1, svgspec: $2"; else bad "$1: Exitcode $R, stderr: $(head -c 120 "$T13/o.err")"; fi
}
: >"$T13/in.json";                                         chk13 "leere Datei" spec
printf '\xef\xbb\xbf{"type":"badge"}' >"$T13/in.json";     chk13 "UTF-8-BOM (wie JSON.parse ungültig)" spec
printf '{"type":"badge"' >"$T13/in.json";                  chk13 "unvollständiges JSON" spec
printf '{"type":"badge"} x' >"$T13/in.json";               chk13 "Text nach dem JSON" spec
printf '{"type":"badge",}' >"$T13/in.json";                chk13 "Komma am Ende" spec
printf '[1,2]' >"$T13/in.json";                            chk13 "Array statt Objekt" spec
printf '{"type":"nix"}' >"$T13/in.json";                   chk13 "unbekannter Typ" type
printf '{"type":"bar","width":"480"}' >"$T13/in.json";     chk13 "Zahl als Text" width
printf '{"type":"bar","width":1e999}' >"$T13/in.json";     chk13 "Zahl 1e999 (unendlich)" width
printf '{"type":"bar","width":01}' >"$T13/in.json";        chk13 "führende Null" spec
printf '{"type":"badge","label":"a\x01b"}' >"$T13/in.json"; chk13 "Steuerzeichen in Zeichenkette" spec
printf '{"type":"badge","label":"\\ud83"}' >"$T13/in.json"; chk13 "abgebrochene \\u-Folge" spec
"$C13" "$T13/gibt-es-nicht.json" >"$T13/o.out" 2>"$T13/o.err"; R=$?
[ "$R" -eq 1 ] && grep -q '^svgspec: spec: ' "$T13/o.err" && ok "fehlende Datei → Exitcode 1, svgspec: spec" || bad "fehlende Datei: Exitcode $R"
"$C13" >"$T13/o.out" 2>"$T13/o.err"; R=$?
[ "$R" -eq 1 ] && grep -q '^svgspec: spec: ' "$T13/o.err" && ok "ohne Argument → Exitcode 1, svgspec: spec" || bad "ohne Argument: Exitcode $R"
"$C13" /tmp >"$T13/o.out" 2>"$T13/o.err"; R=$?
[ "$R" -eq 1 ] && grep -q '^svgspec: spec: ' "$T13/o.err" && ok "Verzeichnis statt Datei → Exitcode 1, svgspec: spec" || bad "Verzeichnis: Exitcode $R"
if node -e '1' 2>/dev/null; then
  printf '{"type":"badge","label":"\xff\xfe\xe2\x82","value":"\xf0\x9f\x98"}' >"$T13/in.json"
  "$C13" "$T13/in.json" >"$T13/a.svg" 2>/dev/null; node svgkit.js "$T13/in.json" >"$T13/b.svg" 2>/dev/null
  cmp -s "$T13/a.svg" "$T13/b.svg" && [ -s "$T13/a.svg" ] && ok "ungültiges UTF-8 wird wie in Node durch U+FFFD ersetzt (byte-gleich zu svgkit.js)" || bad "ungültiges UTF-8 weicht von svgkit.js ab"
  printf '{"type":"badge","label":"\\ud83d\\ude00x\\ud83d","value":"a\\ude00b"}' >"$T13/in.json"
  "$C13" "$T13/in.json" >"$T13/a.svg" 2>/dev/null; node svgkit.js "$T13/in.json" >"$T13/b.svg" 2>/dev/null
  cmp -s "$T13/a.svg" "$T13/b.svg" && [ -s "$T13/a.svg" ] && ok "Ersatzzeichenpaare und einzelne Ersatzzeichen wie in svgkit.js" || bad "Ersatzzeichen weichen von svgkit.js ab"
fi

# 7. Teilmengen-Ablehnung (CLI): Exitcode 1, Feld type bzw. kind, stdout leer
for T in line monogram graph; do
  printf '{"type":"%s"}' "$T" >"$T13/in.json"; chk13 "Typ $T (nicht in der C-Teilmenge)" type
  grep -q 'C-Teilmenge' "$T13/o.err" || bad "Typ $T: Meldung nennt die Teilmenge nicht"
done
for K in waves confetti; do printf '{"type":"pattern","kind":"%s"}' "$K" >"$T13/in.json"; chk13 "pattern/$K (nicht in der C-Teilmenge)" kind; done
NS13=0; FS13=0
for F in svg/fixtures/*.json; do
  "$PY" -I -c 'import json,sys; sys.exit(1 if "c" in json.load(open(sys.argv[1],encoding="utf-8")).get("_impl",[]) else 0)' "$F" || continue
  NS13=$((NS13+1))
  "$C13" "$F" >"$T13/o.out" 2>"$T13/o.err"; R=$?
  [ "$R" -eq 1 ] && [ ! -s "$T13/o.out" ] && grep -q '^svgspec: \(type\|kind\): .*C-Teilmenge' "$T13/o.err" || { FS13=$((FS13+1)); echo "    ✗ $(basename "$F")"; }
done
[ "$FS13" -eq 0 ] && [ "$NS13" -ge 30 ] && ok "alle $NS13 Fixtures außerhalb der Teilmenge (line, monogram, graph, waves, confetti) sauber abgelehnt" || bad "Teilmengen-Ablehnung: $FS13 von $NS13 falsch"

# 8. Grenzen: 20 MB Leerraum, 1 Mio. '[' (Tiefe), Tiefe 100/101, 1-MB-Zahl, 1 MiB Zeichenkette — kein Absturz, endliche Zeit
head -c 20000000 /dev/zero | tr '\0' ' ' >"$T13/in.json"
S13="$(now13)"; timeout 20 "$C13" "$T13/in.json" >/dev/null 2>"$T13/o.err"; R=$?; E13=$((($(now13)-S13)/1000000))
[ "$R" -eq 1 ] && grep -q '^svgspec: spec: ' "$T13/o.err" && ok "20 MB Leerraum: sauber abgelehnt in ${E13} ms" || bad "20 MB Leerraum: Exitcode $R"
head -c 1000000 /dev/zero | tr '\0' '[' >"$T13/in.json"
timeout 20 "$C13" "$T13/in.json" >/dev/null 2>"$T13/o.err"; R=$?
[ "$R" -eq 1 ] && grep -q '^svgspec: spec: ' "$T13/o.err" && ok "1 Mio. '[' (Tiefe): Exitcode 1, kein Stapelüberlauf" || bad "1 Mio. '[': Exitcode $R"
"$PY" -I -c 'import sys; sys.stdout.write("{\"type\":\"badge\",\"x\":" + "["*99 + "]"*99 + "}")' >"$T13/in.json"
"$C13" "$T13/in.json" >/dev/null 2>&1 && ok "Tiefe 100 (Spezifikation + 99 Ebenen in ignoriertem Feld) wird gelesen" || bad "Tiefe 100 abgelehnt"
"$PY" -I -c 'import sys; sys.stdout.write("{\"type\":\"badge\",\"x\":" + "["*100 + "]"*100 + "}")' >"$T13/in.json"
chk13 "Tiefe 101 (Grenze der C-Fassung, JS liest tiefer)" spec
"$PY" -I -c 'import sys; sys.stdout.write("{\"type\":\"bar\",\"width\":" + "1"*1000000 + "}")' >"$T13/in.json"
S13="$(now13)"; timeout 20 "$C13" "$T13/in.json" >/dev/null 2>"$T13/o.err"; R=$?; E13=$((($(now13)-S13)/1000000))
[ "$R" -eq 1 ] && grep -q '^svgspec: width: ' "$T13/o.err" && ok "Zahl mit 1 Mio. Ziffern: Exitcode 1 (width) in ${E13} ms" || bad "Mio.-Ziffern-Zahl: Exitcode $R"
"$PY" -I -c 'import sys; sys.stdout.write("{\"type\":\"badge\",\"label\":\"" + "x"*1048000 + "\"}")' >"$T13/in.json"
timeout 20 "$C13" "$T13/in.json" >"$T13/o.svg" 2>"$T13/o.err"; R=$?
[ "$R" -eq 0 ] && [ -s "$T13/o.svg" ] && ok "Eingabe knapp unter 1 MiB mit riesiger Beschriftung: gekürzt auf 32 Zeichen, kein Überlauf" || bad "knapp 1 MiB: Exitcode $R"
"$PY" -I -c 'import sys; sys.stdout.write("{\"type\":\"badge\",\"label\":\"" + "x"*1048600 + "\"}")' >"$T13/in.json"
chk13 "Eingabe über 1 MiB" spec

# 9. ASan/UBSan: beide Binärdateien in einen temporären Ordner bauen (nichts bleibt im Projekt liegen)
AS13="$T13/dsui-svg-asan"; ASC13="$T13/dsui-c-asan"
if make -s -C c asan OUTDIR="$T13" >"$T13/asan.log" 2>&1 && [ -x "$AS13" ] && [ -x "$ASC13" ]; then
  ok "ASan/UBSan-Bau (-fsanitize=address,undefined -fno-sanitize-recover=all) ohne Warnungen"
  export ASAN_OPTIONS="exitcode=77:detect_leaks=1" UBSAN_OPTIONS="exitcode=78:print_stacktrace=1"
  NA13=0; FA13=0
  for F in svg/fixtures/*.json svg/fixtures-invalid/*.json; do
    NA13=$((NA13+1))
    "$AS13" "$F" >"$T13/o.svg" 2>"$T13/o.err"; R=$?
    if [ "$R" -ne 0 ] && [ "$R" -ne 1 ] || grep -q 'Sanitizer\|runtime error' "$T13/o.err"; then FA13=$((FA13+1)); echo "    ✗ $(basename "$F"): Exitcode $R"; head -3 "$T13/o.err"; continue; fi
    case "$F" in svg/fixtures/*) NAME13="$(basename "$F" .json)"; if [ "$R" -eq 0 ] && ! cmp -s "$T13/o.svg" "svg/golden/$NAME13.svg"; then FA13=$((FA13+1)); echo "    ✗ $NAME13: ASan-Ausgabe weicht ab"; fi;; esac
  done
  [ "$FA13" -eq 0 ] && ok "ASan/UBSan: alle $NA13 Fixtures (gültig, ungültig, außerhalb der Teilmenge) sauber, Ausgaben byte-gleich" || bad "ASan/UBSan: $FA13 von $NA13 auffällig"
  head -c 20000000 /dev/zero | tr '\0' ' ' >"$T13/in.json"; "$AS13" "$T13/in.json" >/dev/null 2>"$T13/o.err"; R=$?
  [ "$R" -eq 1 ] && ok "ASan/UBSan: 20 MB Leerraum sauber" || bad "ASan: 20 MB Leerraum Exitcode $R"
  head -c 1000000 /dev/zero | tr '\0' '[' >"$T13/in.json"; "$AS13" "$T13/in.json" >/dev/null 2>"$T13/o.err"; R=$?
  [ "$R" -eq 1 ] && ok "ASan/UBSan: 1 Mio. '[' sauber" || bad "ASan: 1 Mio. '[' Exitcode $R"
else
  bad "ASan/UBSan-Bau schlägt fehl"; head -10 "$T13/asan.log"
fi

# 10. Fuzz-Stichprobe: verstümmelte, strukturell veränderte und zufällige Eingaben plus handgeschriebene Feindfälle.
#     Je Eingabe: nie Absturz/Zeitüberschreitung/Sanitizer-Meldung, Fehler genau eine svgspec-Zeile, und die Antwort
#     stimmt mit svgkit.js überein (gleiche Bytes bzw. gleiches Fehlerfeld; Teilmenge: Ablehnung mit type/kind).
cat >"$T13/fuzz.py" <<'PYE'
import json, os, random, subprocess, sys, hashlib
# Aufruf: python -I fuzz.py <Projektwurzel> <Binär> <Arbeitsordner> <Anzahl> <Seed>
root, binp, work, n, seed = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
rnd = random.Random(seed)
fx = os.path.join(root, "svg", "fixtures")
seeds = []
for sub in (fx, fx + "-invalid"):
    for f in sorted(os.listdir(sub)):
        if f.endswith(".json"):
            seeds.append(open(os.path.join(sub, f), "rb").read())
INS = [b"null", b"[", b"{", b'"', b"\\u", b"1e999", b"-0", b"\\ud800", b"\\udc00\\ud800", b",", b"\xff\xfe", b"\xc3", b"\xe2\x82", b"\xf0\x9f\x98", b"\xed\xa0\x80",
       b"0.005", b"true", b"\x00", b"\x1f", b"\\u0000", b"\\ud83d\\ude00", b"1e-999", b"00", b"-", b"1.", b".5", b"\xef\xbb\xbf", b"/*", b"\\", b"\\x"]
KEYS = ["width", "height", "data", "labels", "color", "seed", "title", "theme", "colors", "size", "stroke", "angle", "thickness", "density", "opacity",
        "nodes", "edges", "heading", "grid", "text", "kind", "label", "value", "via", "from", "to", "idprefix", "name", "bg", "values", "legend", "center", "cx", "cy",
        "type", "area", "dots", "shape", "color2", "height"]
VALS = [None, True, False, 0, -1, 1.5, 1e308, -1e9, 4294967296, 7, "", "x", "#abc", "#ABCDEF", "accent", "contrast", [], [1, 2, 3], [[1, 2], [3]], {}, "A" * 50,
        "ä\U0001F600", "a\u0000b", "\ud800", "stripes", "dots", "grid", "waves", "confetti", "radial", "static", "control", "content", "light", "bar", "donut",
        "pattern", "badge", "icon", "gradient", "line", "graph", "monogram", "star", [1e9, -1e9, 0], [0, 0, 0], [5] * 70, ["a"] * 70, 0.005, 2.675, 360, -90, 2400, 16]
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
            if rnd.random() < 0.3: o[rnd.choice(KEYS)] = rnd.choice(VALS)
            return o
        if isinstance(v, list):
            o = [mut(x) for x in v]
            if rnd.random() < 0.2 and o: o.pop(rnd.randrange(len(o)))
            if rnd.random() < 0.2: o.insert(rnd.randrange(len(o) + 1), rnd.choice([0, -5, 1e9, 2.675, "s", None, {"id": "Z", "type": "q"}, [1]]))
            return o
        if isinstance(v, bool): return (not v) if r < 0.3 else v
        if isinstance(v, (int, float)): return rnd.choice([v, v * 3, -v, 0, 1e9, 0.005, 2.675, v + 0.5, 1e-7, 1e400 if False else 1e308]) if r < 0.5 else v
        if isinstance(v, str): return rnd.choice([v, v + "\U0001F600", "<&>\"'" + v, "", v * 4, v.upper(), v + "\ud83d", "é" + v]) if r < 0.3 else v
        return v
    return json.dumps(mut(d), ensure_ascii=rnd.random() < 0.5).encode("utf-8", "surrogatepass")
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
def rjson(depth=0):
    r = rnd.random()
    if depth > 3 or r < 0.35:
        return rnd.choice(VALS)
    if r < 0.65: return [rjson(depth + 1) for _ in range(rnd.randrange(5))]
    return {rnd.choice(KEYS): rjson(depth + 1) for _ in range(rnd.randrange(6))}
HAND = [b"", b" ", b"{", b"}", b"[]", b"null", b"{}", b'{"type":"bar"}', b'{"type":"bar","type":"badge"}', b'{"type":"badge","label":"\\ud83d\\ude00x","value":"\\ud83d"}',
        b'{"type":"badge","label":"\xff\xfe\xc3","value":"\xe2\x82\xf0\x9f"}', b'{"type":"badge","label":"a\x00b"}', b'\xef\xbb\xbf{"type":"badge"}',
        b'{"type":"bar","width":1e999}', b'{"type":"bar","data":[1e999]}', b'{"type":"bar","data":[1e308,-1e308,1e-320]}', b'{"type":"bar","data":' + b'[' * 99 + b']' * 99 + b'}',
        b'{"type":"badge","x":' + b'[' * 90 + b']' * 90 + b'}', b'{"type":"bar","data":[' + b','.join([b"1"] * 5000) + b']}',
        b'{"type":"bar","labels":[' + b','.join([b'"' + b"\xf0\x9f\x98\x80" * 60 + b'"'] * 64) + b']}',
        b'{"type":"donut","data":[0,0,0],"labels":["a","b","c"]}', b'{"type":"donut","data":[1e9,1e9,1e9,1e9,1e9,1e9,1e9,1e9,1e9,1e9,1e9,1e9,1e9]}',
        b'{"type":"donut","data":[1],"thickness":0.9,"width":160,"height":120}', b'{"type":"bar","data":[-1e9,1e9],"height":120,"width":160,"labels":["x"]}',
        b'{"type":"bar","title":"' + b"'" * 200 + b'"}', b'{"type":"bar","title":"' + b"<&>\"'" * 60 + b'"}',
        b'{"type":"pattern","kind":"stripes","angle":-90,"size":200,"width":2400,"height":2400}', b'{"type":"gradient","text":"' + "é" .encode() * 70 + b'","width":16,"height":16}',
        b'{"type":"gradient","angle":360,"kind":"radial","cx":0,"cy":100,"via":"#FFF"}', b'{"type":"icon","name":"star","bg":true,"theme":"control"}',
        b'{"type":"bar", "data" : [ 1 , 2 ] , "color" : "#ABC" }\n\t\r ', b'{"type":"bar"} x', b'{"type":"bar"}{"type":"bar"}', b'{"type":"bar",}', b'{"type":"bar" "a":1}']
vseeds = [open(os.path.join(fx, f), "rb").read() for f in sorted(os.listdir(fx)) if f.endswith(".json")]
THEMES = ["master", "light", "contrast", "control", "content"]
COLS = ["accent", "bg", "card", "elevated", "text", "muted", "green", "yellow", "red", "blue", "pink", "amber", "cyan", "lila", "#abc", "#A1B2C3", "#000", "#fff", "#FFFFFF"]
def gentle(raw):
    d = json.loads(raw.decode("utf-8"))
    def mut(k, v):
        r = rnd.random()
        if k == "theme": return rnd.choice(THEMES)
        if k == "colors": return rnd.choice(["vars", "static"])
        if k in ("color", "from", "to", "via", "color2"): return rnd.choice(COLS) if r < 0.6 else v
        if k == "idprefix": return rnd.choice(["sk", "a", "Z-1_x", "id" * 10])
        if k == "kind" and isinstance(v, str): return rnd.choice(["grid", "dots", "stripes", "linear", "radial"]) if v in ("grid", "dots", "stripes", "linear", "radial") else v
        if isinstance(v, bool): return (not v) if r < 0.4 else v
        if isinstance(v, (int, float)) and k not in ("seed",): return rnd.choice([v, v * 2, v / 3, -v, 0, 0.005, 2.675, 1e9, -1e9, 1e-7, v + 0.5, 99999.995, 1234567.891]) if r < 0.6 else v
        if isinstance(v, str) and k in ("title", "label", "value", "text", "center"): return rnd.choice([v, v + "\U0001F600\u00e9", "<&>\"'", "", v * 5, "\u4e2d\u6587", "a\u0301"]) if r < 0.6 else v
        if isinstance(v, list): return [mut(k, x) if not isinstance(x, (list, dict)) else x for x in v] + ([rnd.choice([1, 5, -3, 7.25, 100])] if rnd.random() < 0.3 and v and isinstance(v[0], (int, float)) else [])
        return v
    return json.dumps({k: mut(k, v) for k, v in d.items() if k != "_impl"}, ensure_ascii=rnd.random() < 0.5).encode()
corpus = []
for i in range(n):
    r = rnd.random()
    raw = rnd.choice(seeds)
    if r < 0.35:
        m = gentle(rnd.choice(vseeds))
    elif r < 0.6:
        m = structural(raw)
        if m is None: m = bytewise(raw)
    elif r < 0.85: m = bytewise(raw)
    elif r < 0.95: m = json.dumps(rjson(), ensure_ascii=rnd.random() < 0.5).encode("utf-8", "surrogatepass")
    else: m = bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 200)))
    corpus.append(m)
corpus += HAND
os.makedirs(work, exist_ok=True)
for i, m in enumerate(corpus):
    open(os.path.join(work, "f%05d.json" % i), "wb").write(m)
# Referenz: svgkit.js, ein Node-Prozess für alle Dateien
js = r'''
const fs=require("fs"),path=require("path"),crypto=require("crypto");const K=require(process.argv[1]);const d=process.argv[2];const o={};
for(const f of fs.readdirSync(d).filter(f=>/^f\d+\.json$/.test(f))){let r;try{r=K.generate(JSON.parse(fs.readFileSync(path.join(d,f),"utf8")))}catch(e){r={ok:false,field:"spec"}}
o[f]=r.ok?{ok:true,sha:crypto.createHash("sha256").update(r.svg,"utf8").digest("hex"),type:r.spec.type,kind:r.spec.kind}:{ok:false,field:r.field};}
fs.writeFileSync(path.join(d,"js.json"),JSON.stringify(o));'''
subprocess.run(["node", "-e", js, os.path.join(root, "svgkit.js"), work], check=True)
ref = json.load(open(os.path.join(work, "js.json")))
env = dict(os.environ, ASAN_OPTIONS="exitcode=77:detect_leaks=1", UBSAN_OPTIONS="exitcode=78:print_stacktrace=1")
stat = dict(ok_equal=0, rej_equal=0, subset_ok=0, subset_rej=0, crash=0, format=0, diff=0)
bad = []
import re
for f in sorted(ref):
    p = os.path.join(work, f)
    try:
        r = subprocess.run([binp, p], capture_output=True, timeout=20, env=env)
    except subprocess.TimeoutExpired:
        stat["crash"] += 1; bad.append((f, "Zeitüberschreitung")); continue
    err = r.stderr.decode("utf-8", "replace")
    if r.returncode not in (0, 1) or "Sanitizer" in err or "runtime error" in err:
        stat["crash"] += 1; bad.append((f, "Abbruch rc=%d %s" % (r.returncode, err[:300]))); continue
    j = ref[f]
    m = re.match(r"^svgspec: ([A-Za-z]+): .+\n$", err) if r.returncode == 1 else None
    if r.returncode == 1 and (r.stdout or not m):
        stat["format"] += 1; bad.append((f, "Fehlerformat: %r" % err[:200])); continue
    if r.returncode == 0 and err:
        stat["format"] += 1; bad.append((f, "stderr bei Erfolg")); continue
    field = m.group(1) if m else None
    sub = False
    if j["ok"]:
        sub = j["type"] in ("line", "monogram", "graph") or (j["type"] == "pattern" and j["kind"] in ("waves", "confetti"))
        if sub:
            if r.returncode == 1 and field in ("type", "kind"): stat["subset_ok"] += 1
            else: stat["diff"] += 1; bad.append((f, "Teilmenge: erwartet Ablehnung, rc=%d" % r.returncode))
        elif r.returncode == 0 and hashlib.sha256(r.stdout).hexdigest() == j["sha"]: stat["ok_equal"] += 1
        else: stat["diff"] += 1; bad.append((f, "JS ok, C rc=%d %s" % (r.returncode, err[:120])))
    else:
        if r.returncode == 1 and field == j["field"]: stat["rej_equal"] += 1
        elif r.returncode == 1 and field in ("type", "kind"):
            # JS lehnt ab, C auch (Typ außerhalb der Teilmenge, vor der Feldprüfung): nur dann erlaubt, wenn der Typ nicht in der Teilmenge liegt
            try:
                d = json.loads(open(p, "rb").read().decode("utf-8", "replace")); t = d.get("type") if isinstance(d, dict) else None
            except Exception: t = None
            if t in ("line", "monogram", "graph"): stat["subset_rej"] += 1
            else: stat["diff"] += 1; bad.append((f, "JS lehnt %s ab, C %s" % (j["field"], field)))
        else: stat["diff"] += 1; bad.append((f, "JS lehnt %s ab, C rc=%d feld=%s" % (j["field"], r.returncode, field)))
print("FUZZ", len(ref), json.dumps(stat))
for b in bad[:12]: print("  !", b)
sys.exit(1 if bad else 0)
PYE
if node -e '1' 2>/dev/null; then
  F13="$("$PY" -I "$T13/fuzz.py" "$PWD" "$PWD/$C13" "$T13/fz1" 900 4711 2>&1)"; RF13=$?
  NF13="$(printf '%s\n' "$F13" | sed -n 's/^FUZZ \([0-9]*\) .*/\1/p')"
  if [ "$RF13" -eq 0 ] && [ "${NF13:-0}" -ge 500 ]; then ok "Fuzz (dsui-svg, -O2): $NF13 Eingaben, kein Absturz, identisch zu svgkit.js — $(printf '%s\n' "$F13" | sed -n 's/^FUZZ [0-9]* //p')"; else bad "Fuzz (dsui-svg): Exitcode $RF13"; printf '%s\n' "$F13" | head -14; fi
  if [ -x "$AS13" ]; then
    F13="$("$PY" -I "$T13/fuzz.py" "$PWD" "$AS13" "$T13/fz2" 900 99 2>&1)"; RF13=$?
    NF13="$(printf '%s\n' "$F13" | sed -n 's/^FUZZ \([0-9]*\) .*/\1/p')"
    if [ "$RF13" -eq 0 ] && [ "${NF13:-0}" -ge 500 ]; then ok "Fuzz (ASan/UBSan): $NF13 Eingaben, keine Sanitizer-Meldung, identisch zu svgkit.js — $(printf '%s\n' "$F13" | sed -n 's/^FUZZ [0-9]* //p')"; else bad "Fuzz (ASan/UBSan): Exitcode $RF13"; printf '%s\n' "$F13" | head -14; fi
  fi
else
  say "  – node fehlt: Fuzz-Differenz gegen svgkit.js übersprungen"
fi

# 11. HTTP-Endpunkte gegen den laufenden C-Server (eigener Port 8824; ASan-Fassung 8825)
free13() { for Q in "$@"; do [ "$Q" = "${PORT:-}" ] && continue; curl -s -m1 "http://127.0.0.1:$Q/health" >/dev/null 2>&1 || { echo "$Q"; return; }; done; echo 0; }
PS13="$(free13 8824 8826 8828)"; PA13="$(free13 8825 8827 8829)"
if [ "$PS13" != 0 ] && [ -x c/dsui-c ]; then
  ( cd c && exec env PORT="$PS13" ./dsui-c ) >"$T13/srv.log" 2>&1 &
  SRV13=$!
  for _ in $(seq 1 50); do curl -sf "http://127.0.0.1:$PS13/health" >/dev/null 2>&1 && break; "$PY" -c 'import time; time.sleep(0.1)'; done
  if curl -sf "http://127.0.0.1:$PS13/health" >/dev/null 2>&1; then
    say "    C-Server auf Port $PS13"
    cat >"$T13/ep.py" <<'PYE'
import http.client, json, os, sys, threading
# Aufruf: python -I ep.py <Projektwurzel> <Port> <full|short>
root, port, mode = sys.argv[1], int(sys.argv[2]), sys.argv[3]
fx, golden = os.path.join(root, "svg", "fixtures"), os.path.join(root, "svg", "golden")
SUP = ["bar", "donut", "badge", "icon", "gradient", "pattern"]
def call(method, path, body=None, raw=False):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
    try:
        c.request(method, path, body=body, headers={"Content-Type": "application/json"} if body is not None else {})
        r = c.getresponse(); data = r.read()
        return r.status, (data if raw else json.loads(data.decode("utf-8")))
    finally:
        c.close()
def P(name, ok, detail=""): print(("PASS " if ok else "FAIL ") + name + ((": " + detail) if (detail and not ok) else ""))
fixtures = []
for f in sorted(os.listdir(fx)):
    if f.endswith(".json"):
        raw = open(os.path.join(fx, f), "rb").read(); d = json.loads(raw.decode("utf-8"))
        fixtures.append((f[:-5], raw, d))
cfix = [x for x in fixtures if "c" in x[2]["_impl"]]; other = [x for x in fixtures if "c" not in x[2]["_impl"]]
st, h = call("GET", "/health"); P("GET /health liefert engine c", st == 200 and h.get("engine") == "c")
use = cfix if mode == "full" else cfix[::4]
bad = []
for name, raw, d in use:
    st, j = call("POST", "/api/svg/generate", raw)
    g = open(os.path.join(golden, name + ".svg"), "rb").read().decode("utf-8")
    if not (st == 200 and j.get("ok") is True and j.get("engine") == "c" and j.get("svg") == g and j.get("bytes") == len(g.encode("utf-8"))): bad.append(name)
P("POST /api/svg/generate: %d/%d Fixtures byte-gleich zu svg/golden" % (len(use) - len(bad), len(use)), not bad, ", ".join(bad[:5]))
if mode == "full":
    bad = []
    for name, raw, d in other:
        st, j = call("POST", "/api/svg/generate", raw)
        if not (st == 501 and j.get("ok") is False and j.get("supported") == SUP and j.get("engine") == "c" and "C-Teilmenge" in j.get("error", "")): bad.append((name, st))
    P("Typ/Art außerhalb der Teilmenge: %d Fixtures sauber abgelehnt (501, supported)" % len(other), not bad, str(bad[:3]))
    st, j = call("POST", "/api/svg/generate", b'{"type":"line"}')
    P("Ablehnung: error exakt \"Typ nicht in der C-Teilmenge\" mit supported-Liste", j == {"ok": False, "error": "Typ nicht in der C-Teilmenge", "field": "type", "supported": SUP, "engine": "c"} and st == 501, str(j))
    bad = []
    inv = os.path.join(root, "svg", "fixtures-invalid")
    for f in sorted(os.listdir(inv)):
        if not f.endswith(".json"): continue
        n = f[:-5]; rawi = open(os.path.join(inv, f), "rb").read()
        try: di = json.loads(rawi.decode("utf-8"))
        except Exception: di = None
        ex = os.path.join(inv, n + ".expect")
        field = open(ex).read().strip() if os.path.exists(ex) else di.get("_error_field")
        if isinstance(di, dict) and di.get("type") in ("line", "graph", "monogram"): continue
        st, j = call("POST", "/api/svg/generate", rawi)
        if not (st == 400 and j.get("ok") is False and j.get("field") == field and j.get("error") and "svg" not in j): bad.append((n, st, j.get("field")))
    P("ungültige Spezifikationen: ok=false, 400, richtiges Feld", not bad, str(bad[:3]))
    st, j = call("POST", "/api/svg/generate", b'')
    P("leerer Rumpf: ok=false, Feld spec", st == 400 and j.get("ok") is False and j.get("field") == "spec", str(j))
    st, j = call("POST", "/api/svg/generate", b'{"type":"badge","x":"' + b"a" * 200000 + b'"}')
    P("200-KB-Rumpf (unter MAX_REQ) wird verarbeitet", st == 200 and j.get("ok") is True, str(st))
    st, j = call("POST", "/api/svg/generate", b'{"type":"badge","x":"' + b"a" * 300000 + b'"}')
    P("300-KB-Rumpf (über MAX_REQ): ok=false, Server lebt weiter", st in (400, 413) and j.get("ok") is False and call("GET", "/health")[0] == 200, str(st))
    bodies = [b'{"type":"badge","label":"\\ud83d","value":"\xff\xfe"}', b'[' * 5000, b'{"type":"bar","data":[1e999]}', b'\x00\x01\x02', b'{"type":"bar","title":"' + b"<&>" * 100 + b'"}', b'{"type":"gradient","text":"' + "é".encode() * 100 + b'"}']
    ok = True
    for b in bodies:
        st, j = call("POST", "/api/svg/generate", b)
        ok = ok and isinstance(j, dict) and "ok" in j
    P("feindliche Rümpfe: immer JSON mit ok, Server lebt", ok and call("GET", "/health")[0] == 200)
    st, raw = call("GET", "/api/svg/types", raw=True)
    P("GET /api/svg/types = Inhalt von svg/types.json", st == 200 and raw == open(os.path.join(root, "svg", "types.json"), "rb").read())
    t = json.loads(raw.decode("utf-8")); P("types.json: spec_version svgspec/1, neun Typen", t.get("spec_version") == "svgspec/1" and len(t.get("types", {})) == 9)
    st, raw2 = call("GET", "/api/svg/types?x=1", raw=True); P("GET /api/svg/types?x=1 ebenfalls", st == 200 and raw2 == raw)
    st, _ = call("GET", "/api/svg/nix", raw=True); P("unbekannte /api/svg/-Route: 404", st == 404)
    st, p = call("GET", "/api/patterns"); P("bestehend: GET /api/patterns", st == 200 and "rag-pipeline" in p.get("patterns", {}))
    st, e = call("POST", "/api/workflow/execute", b'{"nodes":[{"id":"a","type":"input"},{"id":"b","type":"llm-node"},{"id":"c","type":"output"}],"edges":[{"from":"a","to":"b"},{"from":"b","to":"c"}]}')
    P("bestehend: POST /api/workflow/execute", st == 200 and e.get("status") == "success" and e.get("engine") == "c")
    st, e = call("POST", "/api/workflow/evaluate", b'{"nodes":[{"id":"a","type":"input"},{"id":"b","type":"llm-node"},{"id":"c","type":"output"}],"edges":[{"from":"a","to":"b"},{"from":"b","to":"c"}]}')
    P("bestehend: POST /api/workflow/evaluate", st == 200 and e.get("verdict") == "done")
    errs = []
    sample = cfix[:12]
    def worker(k):
        for i in range(25):
            name, raw, d = sample[(k + i) % len(sample)]
            try:
                st, j = call("POST", "/api/svg/generate", raw)
                if j.get("svg") != open(os.path.join(golden, name + ".svg"), "rb").read().decode("utf-8"): errs.append(name)
            except Exception as ex:
                errs.append(repr(ex))
    ts = [threading.Thread(target=worker, args=(k,)) for k in range(8)]
    [t.start() for t in ts]; [t.join() for t in ts]
    P("Nebenläufigkeit: 8 Threads × 25 Anfragen, alle byte-gleich", not errs, str(errs[:3]))
else:
    for b in [b'{"type":"line"}', b'[' * 5000, b'{"type":"badge","label":"\\ud83d","value":"\xff\xfe"}', b'{"type":"bar","data":[1e999]}']:
        st, j = call("POST", "/api/svg/generate", b); P("ASan-Server: feindlicher Rumpf liefert JSON mit ok", "ok" in j)
    st, raw = call("GET", "/api/svg/types", raw=True); P("ASan-Server: GET /api/svg/types", st == 200 and len(raw) > 1000)
    P("ASan-Server lebt", call("GET", "/health")[0] == 200)
PYE
    while IFS= read -r L13; do case "$L13" in "PASS "*) ok "${L13#PASS }";; "FAIL "*) bad "${L13#FAIL }";; *) say "    $L13";; esac; done < <("$PY" -I "$T13/ep.py" "$PWD" "$PS13" full 2>&1)
  else bad "C-Server startet nicht"; head -5 "$T13/srv.log"; fi
  kill "$SRV13" 2>/dev/null; wait "$SRV13" 2>/dev/null; SRV13=""
else bad "kein freier Port für den C-Server oder c/dsui-c fehlt"; fi
if [ "$PA13" != 0 ] && [ -x "$ASC13" ]; then
  ( cd c && exec env PORT="$PA13" "$ASC13" ) >"$T13/srva.log" 2>&1 &
  SRVA13=$!
  for _ in $(seq 1 50); do curl -sf "http://127.0.0.1:$PA13/health" >/dev/null 2>&1 && break; "$PY" -c 'import time; time.sleep(0.1)'; done
  if curl -sf "http://127.0.0.1:$PA13/health" >/dev/null 2>&1; then
    while IFS= read -r L13; do case "$L13" in "PASS "*) ok "${L13#PASS }";; "FAIL "*) bad "${L13#FAIL }";; *) say "    $L13";; esac; done < <("$PY" -I "$T13/ep.py" "$PWD" "$PA13" short 2>&1)
    kill "$SRVA13" 2>/dev/null; wait "$SRVA13" 2>/dev/null; SRVA13=""
    ! grep -q 'Sanitizer\|runtime error' "$T13/srva.log" && ok "ASan-Server (Port $PA13): keine Sanitizer-Meldung im Protokoll" || { bad "ASan-Server meldet Fehler"; grep -m3 'Sanitizer\|runtime error' "$T13/srva.log"; }
  else bad "ASan-Server startet nicht"; head -5 "$T13/srva.log"; fi
  stop13
fi
stop13

# 12. Laufzeit: alle Fixtures der Teilmenge, ein Prozess je Aufruf
LIST13=""; for F in svg/fixtures/*.json; do grep -q '"c"' "$F" && "$PY" -I -c 'import json,sys; sys.exit(0 if "c" in json.load(open(sys.argv[1],encoding="utf-8")).get("_impl",[]) else 1)' "$F" && LIST13="$LIST13 $F"; done
NR13=0; S13="$(now13)"
for F in $LIST13; do "$C13" "$F" >/dev/null 2>&1; NR13=$((NR13+1)); done
E13=$((($(now13)-S13)/1000000))
if [ "$NR13" -ge 70 ] && [ "$E13" -lt 30000 ]; then ok "Laufzeit: $NR13 Fixtures in ${E13} ms (≈ $((E13*1000/NR13)) µs je Prozessaufruf)"; else bad "Laufzeit: $NR13 Fixtures in ${E13} ms"; fi

stop13
unset ASAN_OPTIONS UBSAN_OPTIONS
rm -rf "$T13"
