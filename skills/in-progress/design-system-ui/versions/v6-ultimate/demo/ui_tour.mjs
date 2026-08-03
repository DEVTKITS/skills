// UI-Rundgang: öffnet jede Oberfläche, testet Info-Popover, Ampel und TTS-Markierung.
// Belegt mit Screenshots, dass die Fusion wirklich funktioniert.
//
//   node demo/ui_tour.mjs [baseURL] [shotDir]

import { chromium } from 'playwright';

const BASE = process.argv[2] || 'http://localhost:8000';
const SHOTS = process.argv[3] || '.';
const problems = [];

// Drawer zuverlässig öffnen (der geschlossene Drawer ist visibility:hidden).
const openDrawer = async (page) => {
  const isOpen = await page.evaluate(() => document.body.classList.contains('drawer-open'));
  if (!isOpen) await page.click('#hamburger');
  await page.waitForSelector('body.drawer-open', { timeout: 3000 });
  await page.waitForTimeout(320); // Slide-in abwarten
};

const run = async () => {
  const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH });
  const page = await browser.newPage({ viewport: { width: 1500, height: 940 } });
  page.on('pageerror', (e) => problems.push(`pageerror: ${e.message}`));
  page.on('console', (m) => { if (m.type() === 'error') problems.push(`console: ${m.text()}`); });

  await page.goto(BASE, { waitUntil: 'networkidle' });
  await page.waitForTimeout(600);

  // 1) Info-Popover mit wechselnden Tipps
  await page.click('.info-btn[data-help="canvas"]');
  await page.waitForSelector('#infoPopover:not(.hidden)');
  const tip1 = await page.textContent('#infoTip');
  await page.click('#infoNextTip');
  const tip2 = await page.textContent('#infoTip');
  if (!tip1 || tip1 === tip2) problems.push('Tipps wechseln nicht');
  console.log(`   Tipp 1: ${tip1.slice(0, 60)}…`);
  console.log(`   Tipp 2: ${tip2.slice(0, 60)}…`);
  await page.screenshot({ path: `${SHOTS}/10-info-popover.png` });
  await page.keyboard.press('Escape');

  // 2) Hamburger-Menü
  await openDrawer(page);
  await page.screenshot({ path: `${SHOTS}/11-hamburger-drawer.png` });

  // 3) Jede Oberfläche öffnen
  const views = ['chat', 'skills', 'providers', 'stock', 'github', 'agents', 'debug', 'docs'];
  for (const v of views) {
    await openDrawer(page);
    await page.click(`.drawer-item[data-view="${v}"]`);
    await page.waitForTimeout(500);
    const visible = await page.isVisible(`#view-${v}`);
    if (!visible) problems.push(`Oberfläche '${v}' wurde nicht sichtbar`);
    console.log(`   ✓ Oberfläche ${v}`);
  }

  // 4) Provider-Ampel prüfen
  await openDrawer(page);
  await page.click('.drawer-item[data-view="providers"]');
  await page.waitForSelector('.provider-card', { timeout: 5000 });
  const cards = await page.locator('.provider-card').count();
  if (cards < 8) problems.push(`nur ${cards} Provider-Karten gerendert`);
  console.log(`   ✓ ${cards} Provider mit Ampel`);
  await page.screenshot({ path: `${SHOTS}/12-providers-ampel.png` });

  // 5) Debug: Selbsttest + Stresstest über die Oberfläche
  await openDrawer(page);
  await page.click('.drawer-item[data-view="debug"]');
  await page.click('#runSelftest');
  await page.waitForFunction(() =>
    document.getElementById('debugOutput').textContent.includes('Ampel'), { timeout: 8000 });
  await page.click('#runStress');
  await page.waitForFunction(() =>
    document.getElementById('debugOutput').textContent.includes('Durchsatz'), { timeout: 15000 });
  const dbg = await page.textContent('#debugOutput');
  console.log(`   ✓ Debug: ${dbg.split('\n')[0]} | ${dbg.split('\n').pop()}`);
  await page.screenshot({ path: `${SHOTS}/13-debug-stress.png` });

  // 6) TTS-Markierung (ein Klick markiert)
  await openDrawer(page);
  await page.click('.drawer-item[data-view="agents"]');
  await page.waitForTimeout(300);
  await page.click('#view-agents p.text-muted');
  const marked = await page.locator('.tts-selected').count();
  if (!marked) problems.push('TTS-Markierung per Einfachklick fehlt');
  console.log(`   ✓ TTS-Markierung aktiv (${marked} Block)`);

  // 7) Chat-Präfix treibt den Builder
  await openDrawer(page);
  await page.click('.drawer-item[data-view="chat"]');
  await page.fill('#chatInput', '#rag-pipeline');
  await page.click('#chatSend');
  await page.waitForTimeout(1200);
  const nodes = await page.evaluate(() => window.app.workflow.nodes.length);
  if (nodes < 3) problems.push(`Präfix lud kein Template (nodes=${nodes})`);
  console.log(`   ✓ Chat-Präfix lud Template mit ${nodes} Nodes`);
  await page.screenshot({ path: `${SHOTS}/14-chat-split.png` });

  await browser.close();

  if (problems.length) {
    console.log('\n✗ Probleme:'); problems.forEach(p => console.log('   - ' + p));
    process.exit(1);
  }
  console.log('\n✓ UI-Rundgang komplett fehlerfrei');
};

run().catch((e) => { console.error(e); process.exit(1); });
