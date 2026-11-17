// Liest "Wert<TAB>Erwartung" von stdin (aus svg/test_fmt.py) und vergleicht mit SvgKit.fmt.
import { createRequire } from 'module'; import fs from 'fs'; import path from 'path'; import { fileURLToPath } from 'url';
const SK = createRequire(import.meta.url)(path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'svgkit.js'));
let n = 0, bad = 0;
for (const l of fs.readFileSync(0, 'utf8').split('\n')) {
  if (!l) continue; const [v, e] = l.split('\t'); n++;
  const g = SK.fmt(Number(v)); if (g !== e) { bad++; if (bad < 10) console.log(`✗ fmt(${v}) = ${g}, erwartet ${e}`); }
}
console.log(`fmt: ${n - bad}/${n} gleich`); process.exit(bad ? 1 : 0);
