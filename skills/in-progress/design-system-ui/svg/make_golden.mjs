// Erzeugt svg/golden/<name>.svg aus svg/fixtures/<name>.json mit svgkit.js (die Referenz). Aufruf: node svg/make_golden.mjs
// Die Ausgabe hat KEINEN abschließenden Zeilenumbruch (wie die CLI). Eine Fixture, die fehlschlägt, bricht den Lauf ab.
import { createRequire } from 'module'; import fs from 'fs'; import path from 'path'; import { fileURLToPath } from 'url';
const here = path.dirname(fileURLToPath(import.meta.url));
const SK = createRequire(import.meta.url)(path.join(here, '..', 'svgkit.js'));
const FX = path.join(here, 'fixtures'), GO = path.join(here, 'golden');
fs.mkdirSync(GO, { recursive: true });
const names = fs.readdirSync(FX).filter(f => f.endsWith('.json')).map(f => f.slice(0, -5)).sort();
for (const f of fs.readdirSync(GO)) if (f.endsWith('.svg') && !names.includes(f.slice(0, -4))) fs.unlinkSync(path.join(GO, f));
let n = 0, bytes = 0;
for (const nm of names) {
  const r = SK.generate(JSON.parse(fs.readFileSync(path.join(FX, nm + '.json'), 'utf8')));
  if (!r.ok) { console.error(`✗ ${nm}: ${r.field}: ${r.error}`); process.exit(1); }
  if (/NaN|Infinity|undefined|null/.test(r.svg.replace(/aria-label="[^"]*"|<title>[^<]*<\/title>|<text[^>]*>[^<]*<\/text>/g, ''))) { console.error(`✗ ${nm}: NaN/Infinity/undefined/null in der Ausgabe`); process.exit(1); }
  fs.writeFileSync(path.join(GO, nm + '.svg'), r.svg); n++; bytes += r.svg.length;
}
console.log(`${n} Golden-Dateien (${bytes} Byte)`);
