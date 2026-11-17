# tests/11-svgkit-py.sh — Python-Fassung des SVG-Generators (svgkit.py) und Plug-in svgapi.
# Track svgkit-py. Wird von test.sh eingelesen oder allein ausgeführt:
#   tests/run-one.sh tests/11-svgkit-py.sh 8811
# Nutzt ok / bad / say, $BASE, $PY. Startet keine eigenen Server (run-one.sh/test.sh stellen ihn bereit).
# Node ist nur für den Vergleich mit der Referenz (fmt, Zufallsspezifikationen) nötig; fehlt es, wird das gemeldet.

D11="$(mktemp -d)"
export BASE
HAS_NODE11=0; command -v node >/dev/null 2>&1 && HAS_NODE11=1

# Hilfe: Python-Abschnitt ausführen; seine Zeilen "  ✓ …" / "  ✗ …" werden zu ok / bad
run_py11() {
  local OUT L
  OUT="$("$PY" -I "$D11/t.py" "$@" 2>&1)"
  while IFS= read -r L; do
    case "$L" in
      "  ✓ "*) ok "${L#  ✓ }" ;;
      "  ✗ "*) bad "${L#  ✗ }" ;;
      "") ;;
      *) say "    $L" ;;
    esac
  done <<<"$OUT"
}

cat >"$D11/t.py" <<'PYEOF'
import concurrent.futures as cf
import http.client
import json
import os
import random
import re
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

ROOT = os.getcwd()
sys.path.insert(0, ROOT)
import svgkit  # noqa: E402

BASE = os.environ.get("BASE", "")
HOSTPORT = BASE.replace("http://", "")


def pr(ok, msg):
    print(("  ✓ " if ok else "  ✗ ") + msg)


def req(method, path, body=None, raw=None, timeout=30):
    c = http.client.HTTPConnection(HOSTPORT, timeout=timeout)
    try:
        data = raw if raw is not None else (None if body is None else json.dumps(body).encode("utf-8"))
        c.request(method, path, body=data, headers={"Content-Type": "application/json"})
        r = c.getresponse()
        txt = r.read()
        try:
            return r.status, json.loads(txt), r.getheader("Content-Type", "")
        except ValueError:
            return r.status, txt, r.getheader("Content-Type", "")
    finally:
        c.close()


def post(path, body=None, raw=None):
    s, j, _ = req("POST", path, body, raw)
    return s, j


def fixtures(impl="py"):
    d = os.path.join(ROOT, "svg", "fixtures")
    for f in sorted(os.listdir(d)):
        if f.endswith(".json"):
            spec = json.load(open(os.path.join(d, f), encoding="utf-8"))
            if impl in spec.get("_impl", []):
                yield f[:-5], spec


H = '<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10">'
T = '</svg>'
A = []
def add(name, svg, keep=None, gone=None):
    A.append((name, svg, keep, gone))

# ---- 49 Fälle aus tests/10-svgkit-js.sh (svg/test_sanitize.mjs): gleiche Eingaben ----
add('script-Element', H + '<script>alert(1)</script><rect width="5" height="5"/>' + T, '<rect')
add('script mit < im Code', H + '<script>if(a<b){x()}</script><rect width="5" height="5"/>' + T, '<rect')
add('script in CDATA', H + '<script><![CDATA[alert(1)]]></script><circle r="3"/>' + T, '<circle')
add('SCRIPT groß', H + '<SCRIPT>alert(1)</SCRIPT><rect width="1" height="1"/>' + T, '<rect')
add('Namespace-Präfix script', H + '<x:script xmlns:x="http://www.w3.org/2000/svg">alert(1)</x:script><rect width="1" height="1"/>' + T, '<rect')
add('html:script', H + '<html:script xmlns:html="http://www.w3.org/1999/xhtml">alert(1)</html:script>' + T)
add('foreignObject', H + '<foreignObject><body xmlns="http://www.w3.org/1999/xhtml"><img src=x onerror=alert(1)></body></foreignObject><rect width="1" height="1"/>' + T, '<rect')
add('onload am Wurzelelement', '<svg xmlns="http://www.w3.org/2000/svg" onload="alert(1)"><rect width="1" height="1"/></svg>', '<rect')
add('onclick', H + '<rect width="5" height="5" onclick="alert(1)"/>' + T, '<rect')
add('onmouseover mit Großschreibung', H + '<rect width="5" height="5" OnMouseOver="alert(1)"/>' + T, '<rect')
add('onerror an image', H + '<image href="x" onerror="alert(1)"/>' + T)
add('a href javascript', H + '<a href="javascript:alert(1)"><text>klick</text></a>' + T)
add('a xlink:href javascript', '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"><a xlink:href="javascript:alert(1)"><rect width="9" height="9"/></a></svg>')
add('use mit Fremdreferenz', H + '<use href="http://evil.example/x.svg#a"/><rect width="1" height="1"/>' + T, '<rect')
add('use mit data:', H + '<use href="data:image/svg+xml;base64,PHN2Zz4="/>' + T)
add('use mit lokalem Verweis bleibt', H + '<defs><rect id="a" width="2" height="2"/></defs><use href="#a"/>' + T, '<use href="#a"/>')
add('image mit externer URL', H + '<image href="https://evil.example/track.png" width="5" height="5"/>' + T)
add('Entity-verschleiertes javascript', H + '<a href="jav&#x61;script:alert(1)"><rect width="1" height="1"/></a>' + T)
add('Tab im javascript:', H + '<rect width="5" height="5" fill="java&#9;script:alert(1)"/>' + T, '<rect')
add('data:text/html in href', H + '<a href="data:text/html,&lt;script&gt;alert(1)&lt;/script&gt;"><rect width="1" height="1"/></a>' + T)
add('style mit url()', H + '<rect width="5" height="5" style="fill:url(http://evil.example/x)"/>' + T, '<rect')
add('style mit @import', H + '<style>@import url(http://evil.example/x.css);</style><rect width="1" height="1"/>' + T, '<rect')
add('style-Attribut mit expression', H + '<rect width="5" height="5" style="width:expression(alert(1))"/>' + T, '<rect')
add('fill url() extern', H + '<rect width="5" height="5" fill="url(http://evil.example/x#a)"/>' + T, '<rect')
add('fill url() lokal bleibt', H + '<defs><linearGradient id="g"><stop offset="0" stop-color="#fff"/></linearGradient></defs><rect width="5" height="5" fill="url(#g)"/>' + T, 'fill="url(#g)"')
add('animate setzt href', H + '<a><animate attributeName="href" values="javascript:alert(1)"/></a><rect width="1" height="1"/>' + T, '<rect')
add('set-Element', H + '<set attributeName="onmouseover" to="alert(1)"/><rect width="1" height="1"/>' + T, '<rect')
add('iframe', H + '<iframe src="javascript:alert(1)"/><rect width="1" height="1"/>' + T, '<rect')
add('DOCTYPE mit externer Entity', '<!DOCTYPE svg [<!ENTITY xxe SYSTEM "file:///etc/passwd">]>' + H + '<text>&xxe;</text>' + T)
add('Entity-Bombe (billion laughs)', '<!DOCTYPE svg [<!ENTITY a "aaaaaaaaaa"><!ENTITY b "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;">]>' + H + '<text>&b;&b;&b;</text>' + T)
add('Verarbeitungsanweisung', '<?xml-stylesheet href="http://evil.example/x.css"?>' + H + '<rect width="1" height="1"/>' + T, '<rect')
add('Kommentar mit Skript', H + '<!--><script>alert(1)</script>--><rect width="1" height="1"/>' + T, '<rect')
add('Doppelt verschachtelt', H + '<g><g><script>alert(1)</script><g onload="x"><rect width="1" height="1"/></g></g></g>' + T, '<rect')
add('Attribut mit > im Wert', H + '<rect width="5" height="5" fill="a>b" onclick="alert(1)"/>' + T, '<rect')
add('Attribut ohne Anführungszeichen', H + '<rect width=5 height=5 onclick=alert(1) />' + T)
add('xmlns-Trick (fremder Namespace)', H + '<rect xmlns="http://www.w3.org/1999/xhtml" width="1" height="1" onclick="x"/>' + T)
add('xmlns:xlink-Umbiegen', '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="javascript:alert(1)"><rect width="1" height="1"/></svg>', '<rect')
add('title mit Skript-Text bleibt Text', H + '<title>&lt;script&gt;alert(1)&lt;/script&gt;</title><rect width="1" height="1"/>' + T, '&lt;script&gt;')
add('Text mit eingeschleustem Tag', H + '<text>a<script>alert(1)</script>b</text>' + T)
add('textPath mit Fremdverweis', H + '<text><textPath href="http://evil.example/p">x</textPath></text>' + T)
add('Steuerzeichen im Wert', H + '<rect width="5" height="5" fill="re&#1;d"/>' + T)
add('Unicode-Tricks (Zero-Width) in javascript:', H + '<rect width="5" height="5" fill="java&#x200b;script:alert(1)"/>' + T, '<rect')
add('Kein svg-Element', '<div onclick="alert(1)">x</div>')
add('HTML statt SVG', '<img src=x onerror=alert(1)>')
add('Defektes Markup', H + '<rect width="5" <script>alert(1)</script> height="5"/>' + T)
add('unvollständiges Ende', H + '<rect width="5" height="5"/><script>alert(1)')
add('mask/pattern mit Fremd-href', H + '<pattern id="p" href="http://evil.example/p"><rect width="1" height="1"/></pattern>' + T)
add('Zu große Eingabe', H + 'x' * 1100000 + T)
add('Generiertes SVG mit Angriffs-Titel bleibt unverändert',
    svgkit.generate({'type': 'bar', 'title': '<script>alert(1)</script> onload=x javascript:'})['svg'].replace('\n', ''), '&lt;script&gt;')
N_JS = len(A)

# ---- eigene Fälle: Verschleierung, Namespace, CDATA, Kommentare, doppelte Kodierung ----
add('doppelt kodiert: &amp;#106;avascript:', H + '<rect width="5" height="5" fill="&amp;#106;avascript:alert(1)"/>' + T, '<rect', 'javascript')
add('doppelt kodiert: &amp;amp;#x6A;', H + '<rect width="5" height="5" fill="&amp;amp;#x6A;avascript:alert(1)"/>' + T, '<rect')
add('Entity ohne Semikolon: jav&#97script:', H + '<rect width="5" height="5" fill="jav&#97script:alert(1)"/>' + T, '<rect')
add('Entity mit führenden Nullen: &#0000106;', H + '<use href="&#0000106;avascript:alert(1)"/><rect width="1" height="1"/>' + T, '<rect', '<use')
add('Hex-Entities für das ganze Wort', H + '<rect width="5" height="5" fill="&#x6A;&#x61;&#x76;&#x61;&#x73;&#x63;&#x72;&#x69;&#x70;&#x74;&#x3A;alert(1)"/>' + T, '<rect')
add('benannte Entity &colon; / &Tab; (HTML-Tricks)', H + '<rect width="5" height="5" fill="javascript&colon;alert(1)"/><rect width="2" height="2" fill="java&Tab;script&colon;x"/>' + T, '<rect', 'colon')
add('riesige Zahlen-Entity (Int-Grenze)', H + '<text>&#' + '9' * 6000 + ';ok&#x' + 'f' * 6000 + ';</text>' + T, '<text>ok</text>')
add('Volle Breite (NFKC): ｊａｖａｓｃｒｉｐｔ:', H + '<rect width="5" height="5" fill="ｊａｖａｓｃｒｉｐｔ:alert(1)"/>' + T, '<rect', 'alert')
add('Zeilenumbruch mitten im Schema (da\\nta:)', H + '<rect width="5" height="5" fill="da\nta:text/html,x"/>' + T, '<rect', 'text/html')
add('Wortverbinder/Soft-Hyphen in javascript:', H + '<rect width="5" height="5" fill="java⁠scr­ipt:alert(1)"/>' + T, '<rect', 'alert')
add('CDATA zerschnitten: <scr + ipt>', H + '<text><![CDATA[<scr]]><![CDATA[ipt>alert(1)</scr]]>ipt></text>' + T, '&lt;scr', '<script')
add('CDATA nicht geschlossen', H + '<text><![CDATA[<script>alert(1)</script>' , None, '<script')
add('CDATA im Skript-Element mit Endmarke', H + '<script><![CDATA[</script><rect onload=x />]]></script><circle r="1"/>' + T, '<circle', 'alert')
add('Kommentar zerschneidet Tag: <scr<!-- -->ipt>', H + '<scr<!-- -->ipt>alert(1)</scr<!-- -->ipt><rect width="1" height="1"/>' + T, '<rect', '<script')
add('Kommentar --!> Trick', H + '<!-- x --!><script>alert(1)</script><!-- y --><rect width="1" height="1"/>' + T, '<rect', '<script')
add('Kommentar nicht geschlossen', H + '<rect width="1" height="1"/><!-- <script>alert(1)</script>', '<rect', '<script')
add('bedingter Kommentar / <![if>', H + '<!--[if IE]><script>alert(1)</script><![endif]--><rect width="1" height="1"/>' + T, '<rect', '<script')
add('Namespace-Präfix svg:script', '<svg:svg xmlns:svg="http://www.w3.org/2000/svg"><svg:script>alert(1)</svg:script></svg:svg>', None, 'script')
add('Namespace: x:iframe unter svg', '<svg xmlns="http://www.w3.org/2000/svg" xmlns:x="http://www.w3.org/1999/xhtml"><x:iframe src="javascript:alert(1)"/><rect width="1" height="1"/></svg>', '<rect', 'iframe')
add('Namespace: xlink-Präfix umdefiniert', '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://evil/"><use xlink:href="javascript:alert(1)"/><rect width="1" height="1"/></svg>', '<rect', 'javascript')
add('xmlns auf svg umgebogen', '<svg xmlns="http://www.w3.org/1999/xhtml" onload="alert(1)"><rect width="1" height="1"/></svg>', '<rect', 'onload')
add('Schrägstrich statt Leerzeichen: <rect/onclick=…>', H + '<rect/onclick=alert(1)><rect width="1" height="1"/>' + T, '<rect', 'onclick')
add('Nullbyte im Element- und Attributnamen', H + '<scr\x00ipt>alert(1)</scr\x00ipt><rect o\x00nclick="x" width="1" height="1"/>' + T, '<rect', 'alert')
add('Zeilenumbruch im Attributnamen: on\\nclick', H + '<rect on\nclick="alert(1)" width="1" height="1"/>' + T, '<rect', 'alert')
add('Backtick-Attribut', H + '<rect width=`5` onclick=`alert(1)` height="1"/>' + T, '<rect', 'alert')
add('nicht geschlossenes Anführungszeichen', H + '<rect fill="red onclick=alert(1)><rect width="1" height="1"/>' + T, None, 'onclick=')
add('style mit offener url( am Wertende (CSS schließt sie gültig)', H + '<rect width="5" height="5" style="fill:url(http://evil.example/x"/>' + T, '<rect', 'evil')
add('Attribut mit offener url(//host', H + '<rect width="5" height="5" fill="url(//evil.example/x#a"/>' + T, '<rect', 'evil')
add('url( gemischt: erste gut, zweite offen', H + '<rect width="5" height="5" fill="url(#a) url(http://evil.example/"/>' + T, '<rect', 'evil')
add('style mit CSS-Escape (\\6a avascript)', H + '<rect width="5" height="5" style="fill:\\6a avascript:alert(1)"/>' + T, '<rect', 'alert')
add('style mit image-set(): externer Abruf', H + '<rect width="5" height="5" style="background:image-set(\'http://evil.example/x\' 1x)"/>' + T, '<rect', 'evil')
add('style schließt Element: </style><script>', H + '<rect width="5" height="5" style="x:y</style><script>alert(1)</script>"/>' + T, '<rect', '<script')
add('style-Element mit Schließ-Trick (mXSS)', H + '<style><a title="</style><img src onerror=alert(1)>"></style><rect width="1" height="1"/>' + T, '<rect', 'onerror')
add('title mit HTML-Bild (HTML-Parser-Mutation)', H + '<title><img src=x onerror=alert(1)></title><rect width="1" height="1"/>' + T, '<rect', 'onerror')
add('FOREIGNOBJECT gemischte Schreibweise', H + '<ForeignObject><script>alert(1)</script></ForeignObject><rect width="1" height="1"/>' + T, '<rect', 'alert')
add('use: href und xlink:href gemischt', H + '<use href="#a" xlink:href="javascript:alert(1)"/>' + T, 'href="#a"', 'javascript')
add('use: href mit Leerraum + Fremdziel', H + '<use href=" //evil.example/x.svg#a"/>' + T, None, 'evil')
add('animateTransform / handler / listener', H + '<animateTransform attributeName="transform"/><handler type="load">alert(1)</handler><rect width="1" height="1"/>' + T, '<rect', 'alert')
add('verschachteltes svg mit onload', H + '<svg onload="alert(1)" width="2"><rect width="1" height="1"/></svg>' + T, '<rect', 'onload')
add('Inhalt nach dem Wurzelende', H + '<rect width="1" height="1"/>' + T + '<script>alert(1)</script><svg onload="alert(2)"></svg><img src=x onerror=alert(3)>', '<rect', 'alert')
add('Wurzel mit Präfix und Zusatzmüll davor', 'junk<script>alert(1)</script>' + H + '<rect width="1" height="1"/>' + T, '<rect', '<script')
add('<svg/onload=alert(1)>', '<svg/onload=alert(1)><rect width="1" height="1"/></svg>', '<rect', 'onload')
add('ENTITY außerhalb von DOCTYPE', H + '<!ENTITY x SYSTEM "file:///etc/passwd"><text>&x;</text>' + T, None, 'passwd')
add('xlink:href auf Skriptdatei in image', H + '<image xlink:href="//evil.example/x.js"/><rect width="1" height="1"/>' + T, '<rect', 'evil')
add('tabindex/focusable bleiben harmlos', H + '<rect width="1" height="1" tabindex="0" focusable="true"/>' + T, 'tabindex="0"')
add('Text in <g> (nicht erlaubter Textträger)', H + '<g>sichtbarer Text<script>alert(1)</script></g>' + T, '<g>', 'sichtbarer')
add('Entity in Elementtext wird neu escaped', H + '<text>&lt;img src=x onerror=alert(1)&gt; &amp; &#60;b&#62;</text>' + T, '&lt;img', '<img')
add('Anführungszeichen im Wert werden escaped', H + '<rect width="1" height="1" fill="a&quot; onclick=&quot;alert(1)"/>' + T, '<rect', '" onclick="')
add('aria-label mit Angriffstext bleibt Text', H + '<rect width="1" height="1" aria-label="javascript:alert(1) &lt;script&gt;"/>' + T, 'aria-label=', '<script')
add('tiefe Verschachtelung (500 Ebenen)', H + '<g>' * 500 + '<rect width="1" height="1"/>' + '</g>' * 500 + T, '<g>')
add('viele Elemente (6000)', H + '<rect width="1" height="1"/>' * 6000 + T, '<rect')
add('Pathologisch: 40000 x "<a "', H + '<a ' * 40000 + T)
add('Pathologisch: "<a <a …\\"x>\\""', H + '<a ' * 30000 + '"x>"')
add('Pathologisch: 30000 offene <g>', H + '<g>' * 30000)
add('leere Eingabe', '')
add('nur Leerraum', '   \n\t ')
add('Nicht-Text (None)', None)

add('einzelne Ersatzzeichen-Hälfte im Text', H + '<text>a\ud800b</text>' + T, '<text>ab</text>')

OKEL = set('svg g defs title desc path rect circle ellipse line polyline polygon text tspan textPath linearGradient radialGradient stop pattern clipPath mask marker symbol use'.split())
OKAT = set(('id class style transform opacity role lang xml:space aria-label aria-hidden aria-labelledby aria-describedby tabindex focusable x y x1 y1 x2 y2 cx cy r rx ry fx fy dx dy width height d points viewBox preserveAspectRatio version xmlns fill fill-opacity fill-rule clip-rule stroke stroke-width stroke-opacity stroke-linecap stroke-linejoin stroke-miterlimit stroke-dasharray stroke-dashoffset vector-effect paint-order font-family font-size font-weight font-style text-anchor dominant-baseline letter-spacing text-decoration offset stop-color stop-opacity gradientUnits gradientTransform spreadMethod patternUnits patternContentUnits patternTransform clipPathUnits maskUnits maskContentUnits clip-path mask marker-start marker-mid marker-end markerWidth markerHeight markerUnits refX refY orient startOffset href').split())
NS = '{http://www.w3.org/2000/svg}'

def verify(out):
    """Unabhängige Prüfung: Ausgabe muss wohlgeformtes XML sein, nur erlaubte Elemente/Attribute, keine Gefahrenwerte."""
    errs = []
    if out == '':
        return errs
    try:
        root = ET.fromstring(out)   # strenger Parser (expat): Wohlgeformtheit, keine Entities außer den fünf
    except ET.ParseError as e:
        return ['nicht wohlgeformt: %s' % e]
    if root.tag != NS + 'svg':
        errs.append('Wurzel ' + root.tag)
    for el in root.iter():
        tag = el.tag
        if not tag.startswith(NS) or tag[len(NS):] not in OKEL:
            errs.append('Element ' + tag)
        for k, v in el.attrib.items():
            if k not in OKAT:
                errs.append('Attribut ' + k)
            if k.lower().startswith('on'):
                errs.append('on*')
            flat = re.sub(r'[\s\x00-\x1f​-‏­⁠﻿]', '', v).lower()
            if k != 'aria-label' and re.search(r'javascript:|vbscript:|data:|expression\(|@import|image-set\(', flat):
                errs.append('Schema in ' + k)
            if re.search(r'url\((?![\'"]?#[^)]*\))', v, re.I):
                errs.append('url() extern in ' + k)
            if k == 'href' and not v.startswith('#'):
                errs.append('href extern')
        if el.tag.endswith('}text') or el.tag[len(NS):] in ('tspan', 'textPath', 'title', 'desc'):
            pass
        elif (el.text or '').strip():
            errs.append('Text in ' + el.tag)
    # keine rohen Gefahrenmuster außerhalb von Text/aria-label
    probe = re.sub(r'aria-label="[^"]*"', '', re.sub(r'>[^<]*<', '><', out))
    if re.search(r'<(?:script|foreignObject|iframe|object|embed|style|image|a|animate|set)[\s>/]|javascript:', probe, re.I):
        errs.append('Gefahrenmuster')
    return errs



# --------------------------------------------------------------------------- #
# Abschnitt: Schema, fmt und Zufallsspezifikationen gegen die Referenz (Node)   #
# --------------------------------------------------------------------------- #
NODE_FMT = "const K=require('./svgkit.js');const v=JSON.parse(require('fs').readFileSync(process.argv[1],'utf8'));" \
           "process.stdout.write(JSON.stringify(v.map(K.fmt)))"
NODE_GEN = "const K=require('./svgkit.js');const v=JSON.parse(require('fs').readFileSync(process.argv[1],'utf8'));" \
           "process.stdout.write(JSON.stringify(v.map(s=>{const r=K.generate(s);return r.ok?{svg:r.svg,spec:r.spec}:{field:r.field}})))"


def node(code, payload, tmp):
    p = os.path.join(tmp, "in.json")
    with open(p, "w", encoding="utf-8") as fh:
        json.dump(payload, fh)
    r = subprocess.run(["node", "-e", code, p], cwd=ROOT, capture_output=True, text=True, timeout=120)
    if r.returncode:
        raise RuntimeError(r.stderr[:300])
    return json.loads(r.stdout)


def sec_schema(tmp):
    t = json.load(open(os.path.join(ROOT, "svg", "types.json"), encoding="utf-8"))
    pr(json.dumps(svgkit.SCHEMA, sort_keys=True) == json.dumps(t["types"], sort_keys=True),
       "eingebautes Schema == svg/types.json (alle 9 Typen, alle Felder, Grenzen, Standardwerte)")
    pr(svgkit.THEME_NAMES == t["themes"] and all(svgkit.tokens(n) == t["tokens"][n] for n in t["themes"]),
       "Themes und Token-Hex == svg/types.json")
    pr(svgkit.VERSION == t["spec_version"] == "svgspec/1", "Version svgspec/1")
    c = svgkit.tokens("contrast")
    pr(set(c.values()) == {"#000000", "#00ff88"}, "contrast: nur #000000 und #00ff88")


def sec_fmt(tmp):
    cases = [0.005, 1.005, -0.005, 2.675, 1e-7, 123456.789, -0.0, 0.07, 0.5, 1.5, 99.995, 0.015, 0.025, 1e13, -1e13 + 0.5]
    exp = ["0.01", "1", "-0.01", "2.68", "0", "123456.79", "0", "0.07", "0.5", "1.5", "100", "0.02", "0.03"]
    pr([svgkit.fmt(c) for c in cases[:13]] == exp, "fmt: Grenzfälle aus SPEC §2 (0.005 1.005 -0.005 2.675 1e-7 …)")
    pr(svgkit.fmt(float("nan")) == "0" and svgkit.fmt(float("inf")) == "0" and svgkit.fmt(-float("inf")) == "0", "fmt: NaN/±Inf -> 0")
    r = random.Random(11)
    vals = list(cases)
    for _ in range(6000):
        k = r.choice([0, 1, 2, 3, 4, 6, 9])
        vals.append(round(r.uniform(-10 ** r.choice([1, 2, 3, 5, 9]), 10 ** r.choice([1, 2, 3, 5, 9])), k))
        vals.append(r.randint(-10 ** 7, 10 ** 7) / r.choice([100, 1000, 8, 3, 7, 200]))
    vals = [v for v in vals if abs(v) < 9e13]
    if not shutil_which("node"):
        print("  – fmt-Vergleich mit Node übersprungen (node fehlt)")
        return
    ref = node(NODE_FMT, vals, tmp)
    mine = [svgkit.fmt(v) for v in vals]
    bad = [(v, a, b) for v, a, b in zip(vals, mine, ref) if a != b]
    pr(not bad, "fmt == svgkit.js bei %d Werten (inkl. Rundungsgrenzen)%s" % (len(vals), "" if not bad else " — z. B. %r" % (bad[0],)))


def shutil_which(n):
    import shutil
    return shutil.which(n)


def rand_spec(rnd):
    TH = ["master", "light", "contrast", "control", "content"]
    TOKS = svgkit.TOK
    ICONS = ["check", "cross", "play", "pause", "arrow-right", "arrow-left", "star", "heart", "menu", "plus", "minus", "info",
             "search", "download"]

    def num(lo, hi, intish=False):
        c = rnd.random()
        if c < .1:
            return rnd.choice([lo, hi, 0, lo - 5, hi + 5])
        if c < .5 or intish:
            return rnd.randint(int(lo), int(hi)) if hi - lo > 1 else lo
        return round(rnd.uniform(lo, hi), rnd.choice([0, 1, 2, 3, 6]))

    def txt():
        return rnd.choice(["", "a", "Hallo Welt", "A&B <x> \"q\" 's'", "Ünï€ \U0001F600 中文", "x" * 50,
                           "lang er Text mit vielen Zeichen drin", "\n\t ctl\x01", "\ud800lone", "Teil"])

    def col():
        return rnd.choice(TOKS + ["#fff", "#A1B2C3", "#123456", "#abc"])

    t = rnd.choice(["bar", "line", "donut", "pattern", "badge", "icon", "monogram", "gradient", "graph"])
    s = {"type": t, "theme": rnd.choice(TH), "colors": rnd.choice(["vars", "static"]),
         "seed": rnd.choice([1, 0, 4294967295, rnd.randint(0, 10 ** 9)]), "idprefix": rnd.choice(["sk", "a-b_c", "X9"])}
    if rnd.random() < .5:
        s["title"] = txt()
    if rnd.random() < .7:
        s["width"] = num(100, 2500)
    if rnd.random() < .7:
        s["height"] = num(100, 2500)
    for b in ["values", "grid", "heading", "area", "dots", "legend", "bg"]:
        if rnd.random() < .4:
            s[b] = rnd.random() < .5
    if t == "bar":
        n = rnd.choice([0, 1, 2, 5, 12, 64, 70])
        s["data"] = [num(-50, 500) if rnd.random() < .5 else num(-2e9, 2e9) for _ in range(n)]
        s["labels"] = [txt() for _ in range(rnd.randint(0, n + 2))]
        s["color"] = col()
    elif t == "line":
        k, L = rnd.choice([0, 1, 2, 4, 5]), rnd.choice([0, 1, 2, 7, 30, 45, 250])
        s["data"] = ([[num(-100, 1000) for _ in range(rnd.choice([0, 1, L, L]))] for _ in range(k)]
                     if rnd.random() < .7 else [num(-9, 99) for _ in range(L)])
        s["labels"] = [txt()[:rnd.choice([3, 12, 40])] for _ in range(rnd.randint(0, L + 1))]
        s["color"] = col()
    elif t == "donut":
        n = rnd.choice([0, 1, 2, 3, 7, 12, 14])
        s["data"] = [rnd.choice([0, 1, 2, 3, 10, num(0, 1e5), num(-5, 500)]) for _ in range(n)]
        s["labels"] = [txt() for _ in range(rnd.randint(0, n))]
        s["thickness"] = num(0, 1)
        s["center"] = rnd.choice(["", txt()])
    elif t == "pattern":
        s.update(kind=rnd.choice(["grid", "dots", "stripes", "waves", "confetti"]), size=num(1, 300), stroke=num(0, 15),
                 angle=num(-100, 100), density=num(0, 450, True), opacity=num(0, 1.2), color=col())
    elif t == "badge":
        s.update(height=num(10, 70), label=txt(), value=txt(), color=col())
    elif t == "icon":
        s.update(name=rnd.choice(ICONS), size=num(5, 600), stroke=num(0, 5), color=col())
    elif t == "monogram":
        s.update(text=txt(), size=num(10, 600), shape=rnd.choice(["circle", "square", "hex"]), color=col(), color2=col())
    elif t == "gradient":
        s.update(kind=rnd.choice(["linear", "radial"]), angle=num(-10, 400), cx=num(-5, 105), cy=num(-5, 105), text=txt())
        s["from"], s["to"], s["via"] = col(), col(), rnd.choice(["", col()])
    else:
        n = rnd.choice([0, 1, 2, 3, 6, 12, 61])
        ids = ["n%d" % i for i in range(n)]
        s["nodes"] = [{"id": i, "type": rnd.choice(["input", "model", "output", "", "x" * 30, "中"]),
                       "label": rnd.choice(["", txt()])} for i in ids]
        if rnd.random() < .3 and n:
            for nd in s["nodes"]:
                nd["x"], nd["y"] = num(-10, 900), num(-10, 900)
        if ids and rnd.random() < .9:
            s["edges"] = [{"from": rnd.choice(ids), "to": rnd.choice(ids)} for _ in range(rnd.randint(0, n * 2))]
    return s


def sec_fuzz(tmp):
    if not shutil_which("node"):
        print("  – Zufallsvergleich mit svgkit.js übersprungen (node fehlt)")
        return
    n = 2500
    rnd = random.Random(20260517)
    specs = [rand_spec(rnd) for _ in range(n)]
    ref = node(NODE_GEN, specs, tmp)
    okc = errc = bad = 0
    first = None
    for s, j in zip(specs, ref):
        r = svgkit.generate(json.loads(json.dumps(s)))
        if "svg" in j:
            same = r["ok"] and r["svg"] == j["svg"] and json.dumps(r["spec"], sort_keys=True) == json.dumps(j["spec"], sort_keys=True)
            okc += same
        else:
            same = (not r["ok"]) and r["field"] == j["field"]
            errc += same
        if not same:
            bad += 1
            first = first or json.dumps(s)[:200]
    pr(bad == 0, "%d Zufallsspezifikationen (alle Typen/Themes/Modi, Grenzwerte, Unicode): SVG und normalisierte Spezifikation "
                 "byte-gleich zu svgkit.js (%d gültig, %d gleiche Fehlerfelder)%s" % (n, okc, errc, "" if not bad else " — Abweichung: " + str(first)))


# --------------------------------------------------------------------------- #
# Abschnitt: HTTP-Routen                                                       #
# --------------------------------------------------------------------------- #
def sec_api(tmp):
    s, j, ct = req("GET", "/api/svg/types")
    t = json.load(open(os.path.join(ROOT, "svg", "types.json"), encoding="utf-8"))
    pr(s == 200 and "json" in ct and j.get("ok") is True and j.get("engine") == "py" and j.get("spec_version") == "svgspec/1",
       "GET /api/svg/types: 200, JSON, ok, engine=py, spec_version=svgspec/1")
    pr(isinstance(j, dict) and list(j.get("types", {})) == list(t["types"]) and j["types"] == t["types"] and j.get("themes") == t["themes"]
       and j.get("tokens") == t["tokens"], "types == svg/types.json (9 Typen, Themes, Tokens)")
    s, j, _ = req("GET", "/api/plugins")
    ent = [p for p in (j.get("plugins") or []) if p.get("name") == "svgapi"] if isinstance(j, dict) else []
    routes = ent[0]["routes"] if ent else []
    pr(bool(ent) and ent[0]["loaded"] and all(r in routes for r in ("GET /api/svg/types", "POST /api/svg/generate", "POST /api/svg/sanitize")),
       "/api/plugins: svgapi geladen mit den drei Routen")

    # Parität API <-> golden
    n = bad = 0
    first = None
    for name, spec in fixtures("py"):
        s, j = post("/api/svg/generate", {"spec": spec})
        gold = open(os.path.join(ROOT, "svg", "golden", name + ".svg"), "rb").read()
        good = (s == 200 and j.get("ok") is True and j["svg"].encode("utf-8") == gold and j["bytes"] == len(gold)
                and j["engine"] == "py" and isinstance(j["spec"], dict) and j["spec"]["type"] == spec["type"])
        n += 1
        if not good:
            bad += 1
            first = first or name
    pr(bad == 0 and n >= 100, "POST /api/svg/generate: %d Fixtures, svg byte-gleich zu svg/golden, bytes/engine/spec stimmen%s"
       % (n, "" if not bad else " — erste Abweichung: " + str(first)))

    # Spezifikation direkt als Anfragetext (ohne Umhüllung)
    s, j = post("/api/svg/generate", {"type": "badge", "label": "API", "value": "ok"})
    pr(s == 200 and j.get("ok") and "<svg" in j["svg"], "generate akzeptiert die Spezifikation auch direkt als Anfragetext")

    # Normalisierte Spezifikation: Zahlen wie in JS (ganzzahlig ohne ".0")
    s, j = post("/api/svg/generate", {"spec": {"type": "bar", "width": 480.0, "data": [1, 2.5, 3]}})
    raw = json.dumps(j["spec"])
    pr(s == 200 and '"width": 480,' in raw and "480.0" not in raw and j["spec"]["data"] == [1, 2.5, 3], "normalisierte spec: Ganzzahlen ohne .0")

    # Ungültige Spezifikationen -> 400 mit error und field
    d = os.path.join(ROOT, "svg", "fixtures-invalid")
    n = bad = 0
    first = None
    for f in sorted(os.listdir(d)):
        if not f.endswith(".json"):
            continue
        name = f[:-5]
        try:
            spec = json.load(open(os.path.join(d, f), encoding="utf-8"))
        except ValueError:
            continue
        ex = os.path.join(d, name + ".expect")
        field = open(ex, encoding="utf-8").read().strip() if os.path.exists(ex) else spec.get("_error_field", "")
        s, j = post("/api/svg/generate", {"spec": spec})
        n += 1
        if not (s == 400 and j.get("ok") is False and j.get("field") == field and isinstance(j.get("error"), str) and j["error"]):
            bad += 1
            first = first or "%s: %s %s" % (name, s, j)
    pr(bad == 0 and n >= 30, "ungültige Spezifikationen: %d Fixtures -> 400 {ok:false, error, field} mit dem erwarteten Feld%s"
       % (n, "" if not bad else " — " + str(first)[:200]))
    for label, body, fld in [("fehlender Anfragetext", None, "spec"), ("Anfragetext ist Liste", [1, 2], "spec"),
                             ("spec ist Text", {"spec": "bar"}, "spec"), ("spec ist null", {"spec": None}, "spec"),
                             ("spec ohne type", {"spec": {"width": 5}}, "type"), ("unbekannter Typ", {"spec": {"type": "constructor"}}, "type"),
                             ("Farbe mit Skript", {"spec": {"type": "bar", "color": "red;}</style><script>alert(1)</script>"}}, "color"),
                             ("Zahl als Text", {"spec": {"type": "bar", "width": "480"}}, "width"),
                             ("Zahl als Bool", {"spec": {"type": "bar", "width": True}}, "width"),
                             ("idprefix mit Leerzeichen", {"spec": {"type": "bar", "idprefix": "a b"}}, "idprefix")]:
        s, j = post("/api/svg/generate", body)
        pr(s == 400 and j.get("ok") is False and j.get("field") == fld and j.get("error"), "generate 400: " + label + " (field=%s)" % fld)
    s, j = post("/api/svg/generate", raw=b"{kaputt")
    pr(s == 400 and j.get("ok") is False, "generate: ungültiges JSON im Anfragetext -> 400")
    s, j = post("/api/svg/generate", {"spec": {"type": "bar", "width": 1e999}})
    pr(s == 400 and j.get("field") == "width", "generate: unendliche Zahl (1e999) -> 400 width")
    s, j = post("/api/svg/generate", {"spec": {"type": "bar", "width": 10 ** 400}})
    pr(s == 400 and j.get("field") == "width", "generate: riesige Ganzzahl (10^400) -> 400 width, kein Absturz")

    # Klemmen statt Fehler
    s, j = post("/api/svg/generate", {"spec": {"type": "bar", "width": 99999, "height": -5, "title": "t" * 500}})
    pr(s == 200 and j["spec"]["width"] == 2400 and j["spec"]["height"] == 120 and len(j["spec"]["title"]) == 120,
       "generate: Grenzen werden geklemmt (width 2400, height 120, Titel 120 Zeichen)")

    # Angriffsstrings erscheinen nur als escapter Text
    atk = '<script>alert(1)</script>" onload="x" javascript:'
    s, j = post("/api/svg/generate", {"spec": {"type": "badge", "title": atk, "label": atk, "value": atk}})
    safe = j["svg"]
    pr(s == 200 and "<script" not in safe and "&lt;script&gt;" in safe and not [k for e in ET.fromstring(safe).iter() for k in e.attrib if k.startswith("on")],
       "generate: Angriffsstrings in title/label/value nur als escapter Text, SVG wohlgeformt")
    bad = 0
    for ty in ["bar", "line", "donut", "pattern", "badge", "icon", "monogram", "gradient", "graph"]:
        for th in ["master", "light", "contrast", "control", "content"]:
            for cm in ["vars", "static"]:
                s, j = post("/api/svg/generate", {"spec": {"type": ty, "theme": th, "colors": cm}})
                v = j.get("svg", "") if s == 200 else ""
                if not v or re.search(r"<script|foreignObject| on[a-z]+=|javascript:|<use|<image|<style", v) or ET.fromstring(v) is None:
                    bad += 1
                if cm == "static" and "var(" in v:
                    bad += 1
                if th == "contrast" and set(re.findall(r"#[0-9a-fA-F]{3,8}", v)) - {"#000000", "#00ff88"}:
                    bad += 1
    pr(bad == 0, "generate: 9 Typen x 5 Themes x 2 Modi — wohlgeformt, nichts Ausführbares, static ohne var(), contrast nur 2 Farben")

    # sanitize-Route
    s, j = post("/api/svg/sanitize", {"svg": '<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script><rect width="1" height="1" onclick="x"/></svg>'})
    pr(s == 200 and j.get("ok") is True and "<script" not in j["svg"] and "onclick" not in j["svg"] and "<rect" in j["svg"]
       and "element:script" in j["removed"] and "attr:onclick" in j["removed"], "POST /api/svg/sanitize: bereinigt, removed nennt Element und Attribut")
    for label, body in [("svg fehlt", {}), ("svg ist Zahl", {"svg": 5}), ("svg ist Liste", {"svg": ["<svg/>"]}), ("Anfragetext Liste", ["x"])]:
        s, j = post("/api/svg/sanitize", body)
        pr(s == 400 and j.get("ok") is False and j.get("field") == "svg", "sanitize 400: " + label)
    s, j = post("/api/svg/sanitize", {"svg": ""})
    pr(s == 200 and j["svg"] == "" and j["removed"], "sanitize: leere Eingabe -> leeres Ergebnis mit Begründung")

    # Eingabegrößen
    s, j = post("/api/svg/generate", {"spec": {"type": "bar", "title": "x" * 300000}})
    pr(s == 413 and j.get("ok") is False, "generate: Spezifikation > 256 KB -> 413")
    s, j = post("/api/svg/generate", {"spec": {"type": "bar", "data": [123456.789] * 30000}})
    pr(s == 413 and j.get("ok") is False, "generate: riesige Datenliste (> 256 KB) -> 413")
    s, j = post("/api/svg/generate", {"spec": {"type": "bar", "title": "x" * 200000}})
    pr(s == 200 and len(j["spec"]["title"]) == 120 and j["bytes"] < 5000, "generate: 200 KB Titel (unter dem Limit) -> 200, auf 120 Zeichen gekürzt")
    s, j = post("/api/svg/generate", {"spec": {"type": "bar", "title": "ä" * 150000}})
    pr(s == 413, "generate: Grenze zählt Bytes, nicht Zeichen (150000 x ä = 300 KB) -> 413")
    big = '<svg xmlns="http://www.w3.org/2000/svg"><text>' + "a" * 300000 + "</text></svg>"
    s, j = post("/api/svg/sanitize", {"svg": big})
    pr(s == 413 and j.get("ok") is False and j.get("field") == "svg", "sanitize: Eingabe > 256 KB -> 413")
    t0 = time.time()
    ok200 = '<svg xmlns="http://www.w3.org/2000/svg"><text>' + "a" * 200000 + "</text></svg>"
    s, j = post("/api/svg/sanitize", {"svg": ok200})
    pr(s == 200 and len(j["svg"]) > 200000 and time.time() - t0 < 5, "sanitize: 200 KB (unter dem Limit) -> 200 in %.2fs" % (time.time() - t0))
    s, j = post("/api/svg/sanitize", {"svg": "'" * 262000})
    pr(s in (200, 413), "sanitize: 262000 Apostrophe (Escaping bläht auf 6x) -> beherrschte Antwort (%s)" % s)
    s, j = post("/api/svg/sanitize", {"svg": '<svg xmlns="http://www.w3.org/2000/svg"><text>' + "'" * 250000 + "</text></svg>"})
    pr(s == 200 and j["svg"].count("&apos;") == 250000, "sanitize: 250000 Apostrophe -> 200, Ergebnis 1,5 MB bleibt unter der Antwortgrenze")
    s, j = post("/api/svg/sanitize", raw=b'{"svg":"' + b"x" * (3 * 1024 * 1024) + b'"}')
    pr(s == 413, "sanitize: 3 MB-Anfragetext -> 413")
    t0 = time.time()
    s, j = post("/api/svg/sanitize", {"svg": "<svg>" * 40000})
    pr(s == 200 and time.time() - t0 < 5 and ET.fromstring(j["svg"]) is not None, "sanitize: 40000 verschachtelte öffnende Tags -> 200, wohlgeformt, %.2fs" % (time.time() - t0))
    s, j = post("/api/svg/sanitize", {"svg": "<svg>" * 150000})
    pr(s == 413, "sanitize: 150000 verschachtelte Tags (750 KB) -> 413")
    s, j, _ = req("GET", "/health")
    pr(s == 200, "Server lebt nach den Größentests (/health 200)")


# --------------------------------------------------------------------------- #
# Abschnitt: Angriffe auf den Sanitizer (Bibliothek und HTTP)                   #
# --------------------------------------------------------------------------- #


def sec_attacks(tmp):
    slow = 0
    for name, svg, keep, gone in A:
        t1 = time.time()
        r = svgkit.sanitize(svg)
        dt = time.time() - t1
        errs = verify(r["svg"])
        ok = not errs and isinstance(r["removed"], list) and isinstance(r["svg"], str)
        why = ", ".join(errs)
        if ok and keep is not None and keep not in r["svg"]:
            ok, why = False, "erwartet: " + keep
        if ok and gone is not None and gone.lower() in r["svg"].lower():
            ok, why = False, "darf nicht vorkommen: " + gone
        if dt > 3:
            ok, why = False, "zu langsam: %.1fs" % dt
        pr(ok, "Angriff: " + name + ("" if ok else " — " + why + " → " + r["svg"][:120]))
    pr(len(A) >= 30, "Angriffsbeispiele insgesamt: %d (davon %d aus tests/10-svgkit-js.sh/svg/test_sanitize.mjs, %d eigene)" % (len(A), N_JS, len(A) - N_JS))

    # dieselben Eingaben durch die HTTP-Route
    bad = 0
    first = None
    for name, svg, keep, gone in A:
        if not isinstance(svg, str) or len(svg.encode("utf-8", "surrogatepass")) > 250000:
            continue
        s, j = post("/api/svg/sanitize", {"svg": svg})
        errs = verify(j.get("svg", "")) if s == 200 else ["Status %s" % s]
        if errs or (keep is not None and keep not in j.get("svg", "")) or (gone is not None and gone.lower() in j.get("svg", "").lower()):
            bad += 1
            first = first or name
    pr(bad == 0, "dieselben Angriffe über POST /api/svg/sanitize: alle bereinigt%s" % ("" if not bad else " — z. B. " + str(first)))

    # harmlose Eingaben bleiben, Idempotenz
    g = svgkit.generate({"type": "donut", "colors": "vars", "theme": "content"})["svg"]
    r1 = svgkit.sanitize(g)
    r2 = svgkit.sanitize(r1["svg"])
    pr(r1["removed"] == [] and r1["svg"] == g.replace("\n", ""), "generiertes SVG: nichts entfernt, nur Zeilenumbrüche")
    pr(r2["svg"] == r1["svg"], "Sanitizer ist idempotent")
    allgen = True
    for ty in ["bar", "line", "donut", "pattern", "badge", "icon", "monogram", "gradient", "graph"]:
        for th in svgkit.THEME_NAMES:
            for cm in ("vars", "static"):
                s_ = svgkit.generate({"type": ty, "theme": th, "colors": cm})["svg"]
                q = svgkit.sanitize(s_)
                if q["removed"] or q["svg"] != s_.replace("\n", "") or verify(q["svg"]):
                    allgen = False
    pr(allgen, "alle 9 Typen x 5 Themes x 2 Modi: vom Sanitizer unverändert (90 SVGs)")
    pr(svgkit.sanitize(b'<svg xmlns="http://www.w3.org/2000/svg"><rect width="1" height="1"/></svg>')["svg"].startswith("<svg"),
       "bytes-Eingabe wird als UTF-8 gelesen")

    # Mutationen der Angriffe: nie eine Ausnahme, nie unsichere Ausgabe, nie langsam
    rnd = random.Random(777)
    seeds = [a[1] for a in A if isinstance(a[1], str) and len(a[1]) < 5000]
    frag = ["<", ">", '"', "'", "&#", "&amp;", "<!--", "-->", "<![CDATA[", "]]>", "<script>", "</script>", " onload=", "javascript:",
            "<foreignObject>", "xlink:href=", "\x00", "​", "\\", "url(", "<?", "<!", "/>", "=", "\ud800"]
    bad = 0
    first = None
    t0 = time.time()
    N = 4000
    for _ in range(N):
        s_ = rnd.choice(seeds)
        for _k in range(rnd.randint(1, 6)):
            p = rnd.randint(0, len(s_))
            c = rnd.random()
            if c < .5:
                s_ = s_[:p] + rnd.choice(frag) + s_[p:]
            elif c < .8:
                s_ = s_[:p] + s_[min(len(s_), p + rnd.randint(1, 8)):]
            else:
                q = rnd.randint(0, len(s_))
                s_ = s_[:p] + s_[q:q + rnd.randint(1, 30)] + s_[p:]
        try:
            r = svgkit.sanitize(s_)
            errs = verify(r["svg"])
        except Exception as e:  # noqa: BLE001
            errs = ["Ausnahme " + repr(e)]
        if errs:
            bad += 1
            first = first or (errs[0] + " :: " + repr(s_[:100]))
    pr(bad == 0, "%d zufällige Mutationen der Angriffe: keine Ausnahme, Ausgabe stets wohlgeformt und erlaubt (%.1fs)%s"
       % (N, time.time() - t0, "" if not bad else " — " + str(first)))


# --------------------------------------------------------------------------- #
# Abschnitt: Parallelität                                                      #
# --------------------------------------------------------------------------- #
def sec_conc(tmp):
    specs = [{"type": t, "theme": th, "colors": cm, "seed": 3 + i}
             for i, (t, th, cm) in enumerate([("bar", "contrast", "vars"), ("bar", "control", "static"), ("line", "light", "vars"),
                                              ("donut", "content", "static"), ("pattern", "master", "vars"),
                                              ("pattern", "control", "static"), ("badge", "contrast", "static"),
                                              ("graph", "content", "vars"), ("gradient", "light", "static"),
                                              ("monogram", "contrast", "vars"), ("icon", "control", "vars"),
                                              ("donut", "contrast", "vars")])]
    specs[4]["kind"] = "confetti"
    specs[5]["kind"] = "confetti"
    specs[5]["density"] = 400
    expect = [svgkit.generate(sp)["svg"] for sp in specs]
    pr(len(set(expect)) == len(expect), "Parallelität: %d verschiedene Spezifikationen (Themes/Modi/Konfetti gemischt)" % len(specs))

    # a) im Prozess: viele Threads rufen generate gleichzeitig (prüft gemeinsamen Zustand)
    errs = []
    barrier = threading.Barrier(16)

    def worker(k):
        barrier.wait()
        for i in range(300):
            j = (k * 7 + i) % len(specs)
            if svgkit.generate(specs[j])["svg"] != expect[j]:
                errs.append((k, i, j))

    ths = [threading.Thread(target=worker, args=(k,)) for k in range(16)]
    t0 = time.time()
    [t.start() for t in ths]
    [t.join() for t in ths]
    pr(not errs, "16 Threads x 300 generate-Aufrufe im Prozess: alle Ergebnisse identisch zur Einzelausführung (%.1fs)%s"
       % (time.time() - t0, "" if not errs else " — " + str(errs[:3])))

    # b) über HTTP: viele gleichzeitige Anfragen
    def call(i):
        j = i % len(specs)
        s, r = post("/api/svg/generate", {"spec": specs[j]})
        return s == 200 and r.get("svg") == expect[j]

    N = 600
    t0 = time.time()
    with cf.ThreadPoolExecutor(max_workers=8) as ex:
        res = list(ex.map(call, range(N)))
    dt = time.time() - t0
    pr(all(res), "%d gleichzeitige POST /api/svg/generate (8 Threads, 12 Spezifikationen): alle 200 und byte-gleich (%.1fs)%s"
       % (N, dt, "" if all(res) else " — %d falsch" % res.count(False)))

    # c) gemischt: generate + sanitize + types + Fehlerfälle gleichzeitig
    atk = [a[1] for a in A[:20] if isinstance(a[1], str) and len(a[1]) < 100000]

    def mixed(i):
        c = i % 4
        if c == 0:
            return call(i)
        if c == 1:
            s, r = post("/api/svg/sanitize", {"svg": atk[i % len(atk)]})
            return s == 200 and not verify(r["svg"])
        if c == 2:
            s, r, _ = req("GET", "/api/svg/types")
            return s == 200 and r.get("engine") == "py"
        s, r = post("/api/svg/generate", {"spec": {"type": "bar", "theme": "nope"}})
        return s == 400 and r.get("field") == "theme"

    with cf.ThreadPoolExecutor(max_workers=8) as ex:
        res = list(ex.map(mixed, range(400)))
    pr(all(res), "400 gemischte gleichzeitige Anfragen (generate, sanitize, types, Fehlerfälle): alle korrekt")
    s, j, _ = req("GET", "/health")
    pr(s == 200, "Server lebt nach dem Parallelitätstest")


if __name__ == "__main__":
    tmp = os.environ.get("D11", ".")
    {"schema": sec_schema, "fmt": sec_fmt, "fuzz": sec_fuzz, "api": sec_api, "attacks": sec_attacks, "conc": sec_conc}[sys.argv[1]](tmp)
PYEOF

# 1. Parität: jede Fixture byte-gleich zu svg/golden (CLI), danach die ungültigen Spezifikationen
P11="$(svg/run_parity.sh py 2>&1)"; R11=$?
NOK11="$(printf '%s\n' "$P11" | grep -c '^  ✓')"
NBAD11="$(printf '%s\n' "$P11" | grep -c '^  ✗')"
NF11="$(ls svg/fixtures/*.json | wc -l | tr -d ' ')"
if [ "$R11" -eq 0 ] && [ "$NBAD11" -eq 0 ] && [ "$NOK11" -ge 150 ]; then
  ok "run_parity.sh py: $NOK11 ✓, kein ✗ ($(printf '%s\n' "$P11" | grep 'Gültige Fixtures' | sed 's/^ *//'); $(printf '%s\n' "$P11" | grep 'Ungültige:' | sed 's/^ *//'))"
else
  bad "run_parity.sh py: Exitcode $R11, $NOK11 ✓, $NBAD11 ✗"; printf '%s\n' "$P11" | grep '✗' | head -8
fi
[ "$NF11" -ge 100 ] && ok "$NF11 Fixtures vorhanden (alle mit py in _impl: $(grep -l '"py"' svg/fixtures/*.json | wc -l | tr -d ' '))" || bad "nur $NF11 Fixtures"

# 2. Abhängigkeiten: nur Standardbibliothek
IMP11="$(grep -E '^\s*(import|from) ' svgkit.py plugins/svgapi.py | sed -E 's/^[^:]*:\s*//' | awk '{print $2}' | sed 's/\..*//' | sort -u | tr '\n' ' ')"
BADIMP11="$("$PY" -I -c '
import sys
std=set(sys.stdlib_module_names)|{"svgkit","__future__"}
print(" ".join(m for m in sys.argv[1:] if m not in std))' $IMP11)"
if [ -z "$BADIMP11" ]; then ok "svgkit.py und plugins/svgapi.py importieren nur die Standardbibliothek ($IMP11)"; else bad "fremde Importe: $BADIMP11"; fi

# 3. CLI-Vertrag: exakt das SVG, ohne abschließenden Zeilenumbruch; Fehler → Exitcode 1, stderr, stdout leer
"$PY" svgkit.py svg/fixtures/badge-master-vars.json >"$D11/o.svg" 2>"$D11/e.txt"; RC11=$?
if [ "$RC11" -eq 0 ] && [ "$(tail -c 6 "$D11/o.svg")" = "</svg>" ] && [ ! -s "$D11/e.txt" ] && cmp -s "$D11/o.svg" svg/golden/badge-master-vars.svg; then ok "CLI: SVG ohne abschließenden Zeilenumbruch, stderr leer, byte-gleich zu golden"; else bad "CLI-Ausgabe (Exitcode $RC11)"; fi
"$PY" svgkit.py svg/fixtures-invalid/theme-unknown.json >"$D11/o.svg" 2>"$D11/e.txt"; RC11=$?
if [ "$RC11" -eq 1 ] && [ ! -s "$D11/o.svg" ] && [ "$(wc -l <"$D11/e.txt" | tr -d ' ')" = 1 ] && grep -q '^svgspec: theme: ' "$D11/e.txt"; then ok "CLI: Fehler → Exitcode 1, genau eine Zeile 'svgspec: theme: …' auf stderr, stdout leer"; else bad "CLI-Fehlerfall: Exitcode $RC11: $(cat "$D11/e.txt")"; fi
"$PY" svgkit.py /nonexistent.json >"$D11/o.svg" 2>"$D11/e.txt"; RC11=$?
if [ "$RC11" -eq 1 ] && [ ! -s "$D11/o.svg" ] && grep -q '^svgspec: spec: ' "$D11/e.txt"; then ok "CLI: fehlende Datei → Exitcode 1, Feld spec"; else bad "CLI: fehlende Datei (Exitcode $RC11)"; fi
"$PY" svgkit.py >"$D11/o.svg" 2>"$D11/e.txt"; RC11=$?
[ "$RC11" -eq 1 ] && grep -q '^svgspec: spec: ' "$D11/e.txt" && ok "CLI: ohne Argument → Exitcode 1, Feld spec" || bad "CLI ohne Argument (Exitcode $RC11)"
for J11 in '{"type":"bar","data":[NaN]}' '{"type":"bar","data":[Infinity]}' '[1,2' '' '"bar"' 'null'; do
  printf '%s' "$J11" >"$D11/x.json"
  "$PY" svgkit.py "$D11/x.json" >"$D11/o.svg" 2>"$D11/e.txt"; RC11=$?
  if [ "$RC11" -eq 1 ] && [ ! -s "$D11/o.svg" ] && grep -q '^svgspec: spec: ' "$D11/e.txt"; then ok "CLI: Eingabe '$J11' → Exitcode 1, Feld spec"; else bad "CLI: Eingabe '$J11' (Exitcode $RC11): $(cat "$D11/e.txt")"; fi
done
printf '\xef\xbb\xbf{"type":"badge"}' >"$D11/bom.json"
"$PY" svgkit.py "$D11/bom.json" >/dev/null 2>"$D11/e.txt"; [ $? -eq 1 ] && ok "CLI: Datei mit BOM wird wie in der Referenz abgelehnt" || bad "CLI: BOM"
if [ "$HAS_NODE11" = 1 ]; then
  node svgkit.js "$D11/bom.json" >/dev/null 2>&1; [ $? -eq 1 ] && ok "Referenz svgkit.js lehnt BOM ebenfalls ab (gleiches Verhalten)" || bad "Referenz akzeptiert BOM, Python nicht"
fi

# 4. Schema, fmt, Zufallsvergleich mit der Referenz
D11="$D11" run_py11 schema
if [ "$HAS_NODE11" = 1 ]; then D11="$D11" run_py11 fmt; D11="$D11" run_py11 fuzz; else say "  – fmt-/Zufallsvergleich mit Node übersprungen (node fehlt)"; fi

# 5. Routen, Fehlerpfade, Eingabegrößen, Parität API ↔ golden
D11="$D11" run_py11 api

# 6. Sanitizer: Angriffsbeispiele (Bibliothek und HTTP), Mutationen
D11="$D11" run_py11 attacks

# 7. Parallelität
D11="$D11" run_py11 conc

# 8. Keine Ausnahmen im Serverprotokoll
if [ -n "${LOG:-}" ] && [ -f "$LOG" ]; then
  if grep -q 'Traceback\|fehlgeschlagen' "$LOG" 2>/dev/null; then bad "Serverprotokoll enthält Ausnahmen"; grep -B2 -A6 'Traceback' "$LOG" | head -20; else ok "Serverprotokoll: keine Ausnahmen"; fi
fi
rm -rf "$D11"
