// Erzeugt die Demo-Bilder für die Präsentation und prüft dabei die neuen
// Oberflächen: Content Studio, Einstellungen, Skill-Import.
import { chromium } from 'playwright';
const [BASE, SHOTS] = process.argv.slice(2);
const problems = [];
const b = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH });
const p = await b.newPage({ viewport: { width: 1500, height: 940 } });
p.on('pageerror', e => problems.push('pageerror: ' + e.message));
p.on('console', m => { if (m.type() === 'error') problems.push('console: ' + m.text()); });

const open = async (view) => {
  const isOpen = await p.evaluate(() => document.body.classList.contains('drawer-open'));
  if (!isOpen) await p.click('#hamburger');
  await p.waitForSelector('body.drawer-open'); await p.waitForTimeout(360);
  await p.click(`.drawer-item[data-view="${view}"]`);
  await p.waitForTimeout(600);
};

await p.goto(BASE, { waitUntil: 'networkidle' });
await p.waitForTimeout(700);

// 01 Builder + Info-Popover
await p.click('.info-btn[data-help="canvas"]');
await p.waitForSelector('#infoPopover:not(.hidden)');
await p.screenshot({ path: `${SHOTS}/01-builder-hilfe.png` });
await p.keyboard.press('Escape');

// 02 Menü
const wasOpen = await p.evaluate(() => document.body.classList.contains('drawer-open'));
if (!wasOpen) await p.click('#hamburger');
await p.waitForSelector('body.drawer-open'); await p.waitForTimeout(400);
await p.screenshot({ path: `${SHOTS}/02-menue.png` });

// 03 Content Studio — Playwright-Lauf
// Erst einen kleinen Lauf erzeugen, ueber den berichtet werden kann
await p.evaluate(async () => {
  await window.agentActivity.start('Showcase-Durchgang');
  await window.agentActivity.click('#executeWorkflow', 'Lauf starten');
  await window.agentActivity.post('evaluate', '#evalResults', 'Ergebnis lesen');
  await window.agentActivity.stop('fertig');
});
await open('studio');
await p.waitForTimeout(400);
await p.click('#studioMake');
await p.waitForFunction(() => document.getElementById('studioResult').textContent.includes('Bericht:'), { timeout: 15000 })
  .catch(() => problems.push('Studio erzeugte keinen Bericht'));
await p.selectOption('#embedForm', 'pip');
await p.click('#embedMake');
await p.waitForFunction(() => document.getElementById('embedResult').textContent.includes('dsui-pip'), { timeout: 8000 })
  .catch(() => problems.push('Einbettung Video-im-Video fehlt'));
await p.screenshot({ path: `${SHOTS}/03-studio-playwright.png` });
const pod = await p.textContent('#studioResult');
if (/undefined|\[object Object\]/.test(pod)) problems.push('Bericht enthält undefined/[object Object]');
console.log('   Studio:', pod.split('\n')[0], '|', pod.split('\n')[1]);

// 04 Einstellungen — Regler bewegen, Wirkung zeigen
await open('settings');
const sliders = await p.locator('.setting-row input[type="range"]').count();
if (sliders < 10) problems.push(`nur ${sliders} Regler`);
await p.evaluate(() => {
  const hue = document.querySelector('[data-setting="accent_hue"]');
  hue.value = 285; hue.dispatchEvent(new Event('input', { bubbles: true }));
  const r = document.querySelector('[data-setting="radius"]');
  r.value = 20; r.dispatchEvent(new Event('input', { bubbles: true }));
});
await p.waitForTimeout(700);
const applied = await p.evaluate(() =>
  getComputedStyle(document.documentElement).getPropertyValue('--color-primary').trim());
if (!applied.includes('285')) problems.push(`Akzentfarbe wirkt nicht: ${applied}`);
console.log(`   Einstellungen: ${sliders} Regler, Akzent → ${applied}`);
await p.screenshot({ path: `${SHOTS}/04-einstellungen.png` });

// 05 Provider-Kacheln (mit neuer Akzentfarbe)
await open('providers');
await p.waitForSelector('.provider-card');
const cards = await p.locator('.provider-card').count();
console.log(`   Provider: ${cards} Kacheln`);
await p.screenshot({ path: `${SHOTS}/05-provider-kacheln.png` });

// 06 GitHub + Skill-Import
await open('github');
await p.waitForTimeout(800);
await p.screenshot({ path: `${SHOTS}/06-github-skills.png` });

// 07 Agenten
await open('agents');
await p.waitForTimeout(500);
await p.screenshot({ path: `${SHOTS}/07-agenten.png` });

// 08 Debug
await open('debug');
await p.click('#runSelftest');
await p.waitForFunction(() => document.getElementById('debugOutput').textContent.includes('Ampel'), { timeout: 8000 });
await p.screenshot({ path: `${SHOTS}/08-debug.png` });

// Einstellungen zurücksetzen, damit die Bilder reproduzierbar bleiben
await open('settings');
await p.click('#settingsReset');
await p.waitForTimeout(500);

await b.close();
if (problems.length) { console.log('\n✗'); problems.forEach(x => console.log('  - ' + x)); process.exit(1); }
console.log('\n✓ Alle neuen Oberflächen funktionieren, Demo-Bilder erzeugt');
