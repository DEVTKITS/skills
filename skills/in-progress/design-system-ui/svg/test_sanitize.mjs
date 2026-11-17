// Angriffsbeispiele für SvgKit.sanitize. Aufruf: node svg/test_sanitize.mjs [--browser]
// Je Fall: Eingabe, Pflicht (Ausgabe enthält das Muster) und die allgemeine Unbedenklichkeitsprüfung.
// Mit --browser läuft zusätzlich die DOMParser-Variante in Chromium (CHROMIUM_PATH oder /opt/pw-browsers/chromium).
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';
const here = path.dirname(fileURLToPath(import.meta.url)), root = path.join(here, '..');
const require = createRequire(import.meta.url);
const Safe = require(path.join(root, 'svgkit-safe.js'));
const SK = require(path.join(root, 'svgkit.js'));
const H = '<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10">', T = '</svg>';
const A = []; const add = (name, svg, keep) => A.push({ name, svg, keep });
add('script-Element', H + '<script>alert(1)</script><rect width="5" height="5"/>' + T, '<rect');
add('script mit < im Code', H + '<script>if(a<b){x()}</script><rect width="5" height="5"/>' + T, '<rect');
add('script in CDATA', H + '<script><![CDATA[alert(1)]]></script><circle r="3"/>' + T, '<circle');
add('SCRIPT groß', H + '<SCRIPT>alert(1)</SCRIPT><rect width="1" height="1"/>' + T, '<rect');
add('Namespace-Präfix script', H + '<x:script xmlns:x="http://www.w3.org/2000/svg">alert(1)</x:script><rect width="1" height="1"/>' + T, '<rect');
add('html:script', H + '<html:script xmlns:html="http://www.w3.org/1999/xhtml">alert(1)</html:script>' + T);
add('foreignObject', H + '<foreignObject><body xmlns="http://www.w3.org/1999/xhtml"><img src=x onerror=alert(1)></body></foreignObject><rect width="1" height="1"/>' + T, '<rect');
add('onload am Wurzelelement', '<svg xmlns="http://www.w3.org/2000/svg" onload="alert(1)"><rect width="1" height="1"/></svg>', '<rect');
add('onclick', H + '<rect width="5" height="5" onclick="alert(1)"/>' + T, '<rect');
add('onmouseover mit Großschreibung', H + '<rect width="5" height="5" OnMouseOver="alert(1)"/>' + T, '<rect');
add('onerror an image', H + '<image href="x" onerror="alert(1)"/>' + T);
add('a href javascript', H + '<a href="javascript:alert(1)"><text>klick</text></a>' + T);
add('a xlink:href javascript', '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">' + '<a xlink:href="javascript:alert(1)"><rect width="9" height="9"/></a></svg>');
add('use mit Fremdreferenz', H + '<use href="http://evil.example/x.svg#a"/><rect width="1" height="1"/>' + T, '<rect');
add('use mit data:', H + '<use href="data:image/svg+xml;base64,PHN2Zz4="/>' + T);
add('use mit lokalem Verweis bleibt', H + '<defs><rect id="a" width="2" height="2"/></defs><use href="#a"/>' + T, '<use href="#a"/>');
add('image mit externer URL', H + '<image href="https://evil.example/track.png" width="5" height="5"/>' + T);
add('Entity-verschleiertes javascript', H + '<a href="jav&#x61;script:alert(1)"><rect width="1" height="1"/></a>' + T);
add('Tab im javascript:', H + '<rect width="5" height="5" fill="java&#9;script:alert(1)"/>' + T, '<rect');
add('data:text/html in href', H + '<a href="data:text/html,&lt;script&gt;alert(1)&lt;/script&gt;"><rect width="1" height="1"/></a>' + T);
add('style mit url()', H + '<rect width="5" height="5" style="fill:url(http://evil.example/x)"/>' + T, '<rect');
add('style mit @import', H + '<style>@import url(http://evil.example/x.css);</style><rect width="1" height="1"/>' + T, '<rect');
add('style-Attribut mit expression', H + '<rect width="5" height="5" style="width:expression(alert(1))"/>' + T, '<rect');
add('fill url() extern', H + '<rect width="5" height="5" fill="url(http://evil.example/x#a)"/>' + T, '<rect');
add('fill url() lokal bleibt', H + '<defs><linearGradient id="g"><stop offset="0" stop-color="#fff"/></linearGradient></defs><rect width="5" height="5" fill="url(#g)"/>' + T, 'fill="url(#g)"');
add('animate setzt href', H + '<a><animate attributeName="href" values="javascript:alert(1)"/></a><rect width="1" height="1"/>' + T, '<rect');
add('set-Element', H + '<set attributeName="onmouseover" to="alert(1)"/><rect width="1" height="1"/>' + T, '<rect');
add('iframe', H + '<iframe src="javascript:alert(1)"/><rect width="1" height="1"/>' + T, '<rect');
add('DOCTYPE mit externer Entity', '<!DOCTYPE svg [<!ENTITY xxe SYSTEM "file:///etc/passwd">]>' + H + '<text>&xxe;</text>' + T);
add('Entity-Bombe (billion laughs)', '<!DOCTYPE svg [<!ENTITY a "aaaaaaaaaa"><!ENTITY b "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;">]>' + H + '<text>&b;&b;&b;</text>' + T);
add('Verarbeitungsanweisung', '<?xml-stylesheet href="http://evil.example/x.css"?>' + H + '<rect width="1" height="1"/>' + T, '<rect');
add('Kommentar mit Skript', H + '<!--><script>alert(1)</script>--><rect width="1" height="1"/>' + T, '<rect');
add('Doppelt verschachtelt', H + '<g><g><script>alert(1)</script><g onload="x"><rect width="1" height="1"/></g></g></g>' + T, '<rect');
add('Attribut mit > im Wert', H + '<rect width="5" height="5" fill="a>b" onclick="alert(1)"/>' + T, '<rect');
add('Attribut ohne Anführungszeichen', H + '<rect width=5 height=5 onclick=alert(1) />' + T);
add('xmlns-Trick (fremder Namespace)', H + '<rect xmlns="http://www.w3.org/1999/xhtml" width="1" height="1" onclick="x"/>' + T);
add('xmlns:xlink-Umbiegen', '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="javascript:alert(1)"><rect width="1" height="1"/></svg>', '<rect');
add('title mit Skript-Text bleibt Text', H + '<title>&lt;script&gt;alert(1)&lt;/script&gt;</title><rect width="1" height="1"/>' + T, '&lt;script&gt;');
add('Text mit eingeschleustem Tag', H + '<text>a<script>alert(1)</script>b</text>' + T);
add('textPath mit Fremdverweis', H + '<text><textPath href="http://evil.example/p">x</textPath></text>' + T);
add('Steuerzeichen im Wert', H + '<rect width="5" height="5" fill="re&#1;d"/>' + T);
add('Unicode-Tricks (Zero-Width) in javascript:', H + '<rect width="5" height="5" fill="java&#x200b;script:alert(1)"/>' + T, '<rect');
add('Kein svg-Element', '<div onclick="alert(1)">x</div>');
add('HTML statt SVG', '<img src=x onerror=alert(1)>');
add('Defektes Markup', H + '<rect width="5" <script>alert(1)</script> height="5"/>' + T);
add('unvollständiges Ende', H + '<rect width="5" height="5"/><script>alert(1)');
add('mask/pattern mit Fremd-href', H + '<pattern id="p" href="http://evil.example/p"><rect width="1" height="1"/></pattern>' + T);
add('Zu große Eingabe', H + 'x'.repeat(1.1e6) + T);
add('Generiertes SVG mit Angriffs-Titel bleibt unverändert', SK.generate({ type: 'bar', title: '<script>alert(1)</script> onload=x javascript:' }).svg.replace(/\n/g, ''), '&lt;script&gt;');

// unabhängige Prüfung der Ausgabe (nicht dieselbe Logik wie der Sanitizer)
const OKEL = new Set(Safe.allow.elements), OKAT = new Set(Safe.allow.attributes);
function verify(out) {
  const errs = [];
  if (out === '') return errs;
  const markup = out.replace(/&[a-z]+;/g, '');
  if (/<[^>]*<|<!|<\?/.test(out.replace(/&lt;/g, ''))) errs.push('verdächtiges Markup');
  for (const m of out.matchAll(/<(\/?)([^\s\/>]+)([^>]*)>/g)) {
    if (!OKEL.has(m[2])) errs.push('Element ' + m[2]);
    for (const a of m[3].matchAll(/\s([^\s=]+)="([^"]*)"/g)) {
      if (!OKAT.has(a[1]) && a[1] !== 'xmlns') errs.push('Attribut ' + a[1]);
      if (/^on/i.test(a[1])) errs.push('on*');
      const v = a[2].replace(/&[a-z#0-9]+;/gi, '');
      if (a[1] !== 'aria-label' && /javascript:|data:|@import|expression\(/i.test(v.replace(/\s/g, ''))) errs.push('Schema in ' + a[1]);
      if (/url\(/i.test(a[2]) && /url\((?!#)/i.test(a[2])) errs.push('url() extern');
      if ((a[1] === 'href') && !/^#/.test(a[2])) errs.push('href extern');
    }
  }
  return errs;
}
let pass = 0, fail = 0;
const res = (ok, msg) => { ok ? pass++ : fail++; console.log((ok ? '  ✓ ' : '  ✗ ') + msg); };
function check(label, sanitize) {
  console.log(label + ' (' + A.length + ' Fälle)');
  for (const c of A) {
    const r = sanitize(c.svg); const errs = verify(r.svg);
    let ok = errs.length === 0 && Array.isArray(r.removed);
    if (ok && c.keep != null) ok = r.svg.includes(c.keep);
    res(ok, c.name + (ok ? '' : ' — ' + (errs.join(', ') || 'erwartet: ' + c.keep) + ' → ' + r.svg.slice(0, 120)));
  }
}
check('Zeichenketten-Variante (Node)', s => Safe.sanitize(s, 'str'));
check('SvgKit.sanitize (Standardweg)', s => SK.sanitize(s));
// harmlose Eingaben bleiben unversehrt, Ergebnis ist idempotent
const g = SK.generate({ type: 'donut', colors: 'vars', theme: 'content' }).svg;
const r1 = Safe.sanitize(g, 'str'), r2 = Safe.sanitize(r1.svg, 'str');
res(r1.removed.length === 0 && r1.svg === g.replace(/\n/g, ''), 'generiertes SVG: nichts entfernt, nur Zeilenumbrüche');
res(r2.svg === r1.svg, 'idempotent');
if (process.argv.includes('--browser')) {
  const { chromium } = createRequire(path.join(root, 'demo/'))('playwright');
  const b = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || '/opt/pw-browsers/chromium' });
  const p = await b.newPage();
  await p.setContent('<html><body></body></html>');
  await p.addScriptTag({ path: path.join(root, 'svgkit-safe.js') }); await p.addScriptTag({ path: path.join(root, 'svgkit.js') });
  const outs = await p.evaluate(list => list.map(s => { const r = window.SvgKit.sanitize(s); return { svg: r.svg, removed: r.removed }; }), A.map(c => c.svg));
  let i = 0; console.log('DOMParser-Variante (Chromium)');
  const same = [];
  for (const c of A) {
    const r = outs[i++]; const errs = verify(r.svg); let ok = errs.length === 0;
    if (ok && c.keep != null) ok = r.svg.includes(c.keep);
    res(ok, c.name + (ok ? '' : ' — ' + errs.join(', ') + ' → ' + r.svg.slice(0, 120)));
    if (r.svg !== Safe.sanitize(c.svg, 'str').svg) same.push(c.name);
  }
  console.log('  Hinweis: DOM- und Zeichenketten-Ausgabe unterscheiden sich bei ' + same.length + ' Fällen' + (same.length ? ': ' + same.join('; ') : ''));
  await b.close();
}
console.log(`PASS=${pass} FAIL=${fail}`);
process.exit(fail ? 1 : 0);
