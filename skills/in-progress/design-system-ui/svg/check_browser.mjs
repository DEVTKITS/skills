// Prüft im echten Chromium: window.SvgKit existiert (UMD), erzeugt für alle Fixtures byte-gleiche Ausgabe wie golden,
// window.SvgKitSafe ist geladen. Aufruf: node svg/check_browser.mjs  (CHROMIUM_PATH oder /opt/pw-browsers/chromium)
import { createRequire } from 'module'; import fs from 'fs'; import path from 'path'; import { fileURLToPath } from 'url';
const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const { chromium } = createRequire(path.join(root, 'demo/'))('playwright');
const FX = path.join(root, 'svg/fixtures'), GO = path.join(root, 'svg/golden');
const names = fs.readdirSync(FX).filter(f => f.endsWith('.json')).map(f => f.slice(0, -5));
const specs = names.map(n => JSON.parse(fs.readFileSync(path.join(FX, n + '.json'), 'utf8')));
const b = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || '/opt/pw-browsers/chromium' });
const p = await b.newPage(); const errs = []; p.on('pageerror', e => errs.push(String(e))); p.on('console', m => m.type() === 'error' && errs.push(m.text()));
await p.setContent('<html><body></body></html>');
await p.addScriptTag({ path: path.join(root, 'svgkit-safe.js') }); await p.addScriptTag({ path: path.join(root, 'svgkit.js') });
const out = await p.evaluate(list => ({ v: window.SvgKit.version, safe: typeof window.SvgKitSafe.sanitize, r: list.map(s => window.SvgKit.generate(s)).map(r => r.ok ? r.svg : 'ERR ' + r.field),
  san: window.SvgKit.sanitize('<svg xmlns="http://www.w3.org/2000/svg"><script>1</script><rect width="1" height="1" onclick="x"/></svg>') }), specs);
let bad = 0;
out.r.forEach((s, i) => { if (s !== fs.readFileSync(path.join(GO, names[i] + '.svg'), 'utf8')) { bad++; console.log('✗ ' + names[i]); } });
if (out.v !== 'svgspec/1' || out.safe !== 'function' || out.san.svg.includes('script') || errs.length) { bad++; console.log('✗ Browser-API', out.v, out.safe, errs); }
console.log(`Browser: ${names.length - bad}/${names.length} Fixtures byte-gleich zu golden, window.SvgKit/SvgKitSafe vorhanden, keine Konsolenfehler`);
await b.close(); process.exit(bad ? 1 : 0);
