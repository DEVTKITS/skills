# tests/10-svgkit-js.sh — kanonischer SVG-Generator (svgkit.js), Sanitizer (svgkit-safe.js), Golden-Dateien.
# Track svgkit-js. Wird von test.sh eingelesen oder allein ausgeführt:
#   tests/run-one.sh tests/10-svgkit-js.sh 8810
# Nutzt ok / bad / say, $BASE, $PY. Startet keine eigenen Prozesse außer kurzlebigen node-Aufrufen.

# 1. Auslieferung über den Server, byte-gleich zur Datei
for F10 in svgkit.js svgkit-safe.js; do
  T10="$(mktemp)"
  CODE10="$(curl -s -o "$T10" -w '%{http_code}' "$BASE/$F10")"
  if [ "$CODE10" = 200 ] && cmp -s "$T10" "$F10"; then ok "/$F10 wird ausgeliefert (200, identisch zur Datei)"; else bad "/$F10 Auslieferung: HTTP $CODE10"; fi
  rm -f "$T10"
done
assert_contains "svgkit.js nennt svgspec/1" "svgspec/1" "$(curl -s "$BASE/svgkit.js")"

# 2. Größenregel (18 KB roh), je Datei einzeln
for F10 in svgkit.js svgkit-safe.js; do
  S10="$(wc -c <"$F10" | tr -d ' ')"
  if [ "$S10" -le 18432 ]; then ok "$F10: $S10 Byte ≤ 18432"; else bad "$F10: $S10 Byte > 18432 (18-KB-Regel)"; fi
done

# 3. Parität der Referenz gegen golden (Fixtures und ungültige Spezifikationen)
P10="$(svg/run_parity.sh js 2>&1)"; R10=$?
NV10="$(printf '%s\n' "$P10" | grep -c '^  ✓')"
if [ "$R10" -eq 0 ] && [ "$NV10" -ge 45 ]; then ok "run_parity.sh js: $NV10 ✓, kein ✗"; else bad "run_parity.sh js: Exitcode $R10, $NV10 ✓"; printf '%s\n' "$P10" | grep '✗' | head -5; fi
NF10="$(ls svg/fixtures/*.json | wc -l | tr -d ' ')"
[ "$NF10" -ge 45 ] && ok "$NF10 Fixtures (≥ 45)" || bad "nur $NF10 Fixtures"
NG10="$(ls svg/golden/*.svg | wc -l | tr -d ' ')"
[ "$NG10" = "$NF10" ] && ok "zu jeder Fixture gibt es genau eine Golden-Datei" || bad "Fixtures $NF10, Golden $NG10"

# 4. Golden ist aktuell: neu erzeugte Ausgabe == Datei; types.json ist aktuell
D10="$(mktemp -d)"
node -e '
const fs=require("fs"),SK=require("./svgkit.js");let bad=0;
for(const f of fs.readdirSync("svg/fixtures").filter(f=>f.endsWith(".json"))){
  const r=SK.generate(JSON.parse(fs.readFileSync("svg/fixtures/"+f,"utf8")));
  if(!r.ok||r.svg!==fs.readFileSync("svg/golden/"+f.replace(/json$/,"svg"),"utf8")){bad++;console.log("abweichend: "+f);}
  if(!JSON.parse(fs.readFileSync("svg/fixtures/"+f,"utf8"))._impl)bad++;
}
const T=JSON.parse(fs.readFileSync("svg/types.json","utf8"));
const exp={ok:true,spec_version:SK.version,types:SK.schema,themes:SK.themes,modes:["vars","static"],tokens:Object.fromEntries(SK.themes.map(t=>[t,SK.tokens(t)]))};
if(JSON.stringify(T)!==JSON.stringify(exp)){bad++;console.log("svg/types.json ist veraltet (node svg/make_types.mjs)");}
process.exit(bad?1:0)' >"$D10/golden.txt" 2>&1 \
  && ok "golden und types.json entsprechen svgkit.js; jede Fixture hat _impl" || { bad "golden/types.json veraltet"; head -5 "$D10/golden.txt"; }

# 5. Rundungsregel fmt gegen unabhängige Python-Umsetzung (4000+ Werte inkl. Grenzfälle)
python3 svg/test_fmt.py | node svg/check_fmt.mjs >"$D10/fmt.txt" 2>&1 && ok "fmt: $(cat "$D10/fmt.txt")" || { bad "fmt weicht ab"; head -5 "$D10/fmt.txt"; }
FM10="$(node -e 'const K=require("./svgkit.js");console.log([0.005,1.005,-0.005,2.675,1e-7,123456.789,-0].map(K.fmt).join(" "))')"
assert_contains "fmt-Grenzfälle (0.005 1.005 -0.005 2.675 1e-7 123456.789 -0)" "0.01 1 -0.01 2.68 0 123456.79 0" "$FM10"

# 6. Sanitizer: Angriffsbeispiele (≥ 25), Zeichenketten-Variante und Standardweg; Browser-Variante, wenn Chromium da ist
SA10="$(node svg/test_sanitize.mjs 2>&1)"; RS10=$?
NA10="$(printf '%s\n' "$SA10" | grep -c '^  ✓')"
if [ "$RS10" -eq 0 ] && [ "$NA10" -ge 50 ]; then ok "Sanitizer-Angriffe: $NA10 Prüfungen bestanden"; else bad "Sanitizer-Angriffe: Exitcode $RS10"; printf '%s\n' "$SA10" | grep '✗' | head -5; fi
if [ -d demo/node_modules/playwright ] && [ -x "${CHROMIUM_PATH:-/opt/pw-browsers/chromium}" ]; then
  SB10="$(timeout 90 node svg/test_sanitize.mjs --browser 2>&1)"; RB10=$?
  if [ "$RB10" -eq 0 ]; then ok "Sanitizer in Chromium (DOMParser-Variante): bestanden"; else bad "Sanitizer in Chromium"; printf '%s\n' "$SB10" | grep '✗' | head -5; fi
else say "  – Sanitizer-Browserprüfung übersprungen (Playwright/Chromium fehlt)"; fi

# 7. Golden-SVGs enthalten nichts Ausführbares — Marken-genau (Elemente, Attribute) und als Zeichenkettensuche
G10="$(node -e '
const fs=require("fs"),A=new Set(require("./svgkit-safe.js").allow.elements);let bad=0,n=0;
for(const f of fs.readdirSync("svg/golden").filter(f=>f.endsWith(".svg"))){
  const s=fs.readFileSync("svg/golden/"+f,"utf8");n++;
  const fx=JSON.parse(fs.readFileSync("svg/fixtures/"+f.replace(/svg$/,"json"),"utf8"));
  const attack=/<script|onload|onclick|onerror|javascript:|foreignObject/i.test(JSON.stringify(fx));
  if(!attack&&/script|on[a-z]+=|javascript:|foreignObject/.test(s)){bad++;console.log("Muster in "+f);}
  for(const m of s.matchAll(/<(\/?)([^\s\/>]+)([^>]*)>/g)){
    if(!A.has(m[2])){bad++;console.log("Element "+m[2]+" in "+f);}
    for(const a of m[3].matchAll(/\s([^\s=]+)="/g))if(/^on/i.test(a[1])){bad++;console.log("on* in "+f);}
    if(/(?:href|src)=|javascript:|url\((?!#)/.test(m[3].replace(/aria-label="[^"]*"/,""))){bad++;console.log("Verweis in "+f);}
  }
  if(/<(?:script|foreignObject)/i.test(s)){bad++;console.log("gefährliches Element in "+f);}
}
console.log(n+" Dateien geprüft, "+bad+" Funde");process.exit(bad?1:0)' 2>&1)" && ok "golden: $G10" || { bad "golden enthält Verdächtiges"; printf '%s\n' "$G10" | head -5; }

# 8. CLI-Vertrag: exakt das SVG, ohne Zeilenumbruch am Ende; Fehler → Exitcode 1, Meldung auf stderr, stdout leer
node svgkit.js svg/fixtures/badge-master-vars.json >"$D10/o.svg" 2>"$D10/e.txt"; RC10=$?
if [ "$RC10" -eq 0 ] && [ "$(tail -c 6 "$D10/o.svg")" = "</svg>" ] && [ ! -s "$D10/e.txt" ]; then ok "CLI: SVG ohne abschließenden Zeilenumbruch"; else bad "CLI-Ausgabe"; fi
node svgkit.js svg/fixtures-invalid/theme-unknown.json >"$D10/o.svg" 2>"$D10/e.txt"; RC10=$?
if [ "$RC10" -eq 1 ] && [ ! -s "$D10/o.svg" ] && grep -q '^svgspec: theme: ' "$D10/e.txt"; then ok "CLI: Fehler → Exitcode 1 und 'svgspec: theme: …' auf stderr"; else bad "CLI-Fehlerfall: Exitcode $RC10"; fi
node svgkit.js /nonexistent.json >/dev/null 2>"$D10/e.txt"; [ $? -eq 1 ] && ok "CLI: fehlende Datei → Exitcode 1" || bad "CLI: fehlende Datei"

# 9. API: Schema, Themes, Tokens, Version, Eigenschaften der Typen
A10="$(node -e '
const K=require("./svgkit.js"),errs=[];
const chk=(c,m)=>{if(!c)errs.push(m)};
chk(K.version==="svgspec/1","version");
chk(JSON.stringify(Object.keys(K.schema))===JSON.stringify(["bar","line","donut","pattern","badge","icon","monogram","gradient","graph"]),"Typen");
chk(K.themes.length===5,"Themes");
for(const t in K.schema){const f=K.schema[t].felder;chk(K.schema[t].label&&f.length>5,"Schema "+t);for(const x of f){chk(x.name&&x.type&&x.label&&"default" in x,"Feld "+t+"."+x.name);if(x.type==="int"||x.type==="num")chk(x.min!=null&&x.max!=null&&x.step,"Regler "+t+"."+x.name)}}
chk(K.schema.icon.felder.find(f=>f.name==="name").choices.length>=14,"≥14 Symbole");
const tk=K.tokens("contrast");chk(Object.values(tk).every(v=>v==="#000000"||v==="#00ff88"),"contrast: nur #000000/#00ff88");
for(const t of ["bar","line","donut","pattern","badge","icon","monogram","gradient","graph"])for(const th of K.themes)for(const c of ["vars","static"]){
 const r=K.generate({type:t,theme:th,colors:c});chk(r.ok,"generate "+t+th+c);if(!r.ok)continue;
 chk(/^<svg [^>]*role="img" aria-label="[^"]+"/.test(r.svg)&&/<title>[^<]+<\/title>/.test(r.svg),"A11y "+t);
 chk(c==="static"?!/var\(/.test(r.svg):/var\(--/.test(r.svg)||t==="icon"&&false,"Modus "+t+th+c);
 if(th==="contrast"){const hx=r.svg.match(/#[0-9a-fA-F]{3,8}/g)||[];chk(hx.every(h=>/^#(000000|00ff88)$/i.test(h)),"contrast-Palette "+t+c)}
}
const g=K.generate({type:"graph",nodes:[{id:"a"},{id:"b"}],edges:[{from:"a",to:"b"}]});
const g2=K.generate({type:"graph",nodes:[{id:"a"},{id:"b"}],edges:[{from:"a",to:"b"}]});chk(g.svg===g2.svg,"Determinismus");
chk(K.generate({type:"nope"}).ok===false&&K.generate({type:"nope"}).field==="type","Fehlerfeld");
console.log(errs.length?errs.join("; "):"ok");process.exit(errs.length?1:0)' 2>&1)" && ok "API: Schema, Themes, Tokens, A11y, Modi, contrast-Palette, Determinismus" || { bad "API-Prüfung: $A10"; }

# 10. Browser: window.SvgKit (UMD) erzeugt in Chromium byte-gleich zu golden, ohne Konsolenfehler
if [ -d demo/node_modules/playwright ] && [ -x "${CHROMIUM_PATH:-/opt/pw-browsers/chromium}" ]; then
  BR10="$(timeout 90 node svg/check_browser.mjs 2>&1)"; RR10=$?
  if [ "$RR10" -eq 0 ]; then ok "$BR10"; else bad "Browser-Fassung weicht ab"; printf '%s\n' "$BR10" | head -5; fi
else say "  – Browserprüfung übersprungen (Playwright/Chromium fehlt)"; fi
rm -rf "$D10"
