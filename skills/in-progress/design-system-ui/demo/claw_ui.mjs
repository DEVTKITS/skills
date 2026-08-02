// Belegt: OpenCLAW lässt sich aus der Oberfläche starten und der Mensch sieht zu.
import { chromium } from 'playwright';
const BASE = process.argv[2] || 'http://localhost:8000';
const SHOTS = process.argv[3] || '.';
const problems = [];

const b = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH });
const p = await b.newPage({ viewport: { width: 1500, height: 940 } });
p.on('pageerror', e => problems.push('pageerror: ' + e.message));
p.on('console', m => { if (m.type() === 'error') problems.push('console: ' + m.text()); });

await p.goto(BASE, { waitUntil: 'networkidle' });
await p.click('#hamburger'); await p.waitForSelector('body.drawer-open');
await p.click('.drawer-item[data-view="agents"]');
await p.waitForTimeout(600);

const status = await p.textContent('#clawStatusText');
console.log('   Status:', status.trim());

await p.fill('#clawTask', 'Baue einen RAG-Workflow und optimiere ihn');
await p.click('#clawRun');

// Während des Laufs muss der rote Monitorrand aktiv sein
await p.waitForSelector('body.agent-active', { timeout: 8000 })
  .catch(() => problems.push('roter Monitorrand erschien nicht'));
console.log('   ✓ roter Monitorrand aktiv, während OpenCLAW arbeitet');
await p.screenshot({ path: `${SHOTS}/20-openclaw-arbeitet.png` });

await p.waitForFunction(() =>
  document.getElementById('clawOutput').textContent.includes('Urteil'),
  { timeout: 30000 }).catch(() => problems.push('kein Urteil im Ergebnisfeld'));

const out = await p.textContent('#clawOutput');
console.log('   Ergebnis:', out.split('\n').slice(0, 3).join(' | '));
const agentLog = await p.textContent('#agentLog');
if (!agentLog.includes('openclaw')) problems.push('Aktivitätsprotokoll bleibt leer');
else console.log('   ✓ Aktivitätsprotokoll:', agentLog.split('\n').length, 'Zeilen');

const enabled = await p.isEnabled('#clawRun');
if (!enabled) problems.push('Start-Button bleibt nach dem Lauf gesperrt');
await p.screenshot({ path: `${SHOTS}/21-openclaw-fertig.png` });

await b.close();
if (problems.length) { console.log('\n✗'); problems.forEach(x => console.log('  - ' + x)); process.exit(1); }
console.log('\n✓ OpenCLAW aus der Oberfläche heraus fehlerfrei');
