// Belegt: beide Editionen zeigen Hamburger-Menü und Kacheln; nur die
// OpenCLAW-Edition zeigt zusätzlich die Agenten-Shell.
import { chromium } from 'playwright';
const [NORMAL, CLAW, SHOTS] = process.argv.slice(2);
const problems = [];
const b = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH });

for (const [label, url] of [['normal', NORMAL], ['openclaw', CLAW]]) {
  const p = await b.newPage({ viewport: { width: 1500, height: 940 } });
  p.on('pageerror', e => problems.push(`${label} pageerror: ${e.message}`));
  p.on('console', m => { if (m.type() === 'error') problems.push(`${label} console: ${m.text()}`); });
  await p.goto(url, { waitUntil: 'networkidle' });
  await p.waitForTimeout(700);

  const chip = (await p.textContent('#editionChip')).trim();
  if (chip.toLowerCase() !== label) problems.push(`${label}: Chip zeigt "${chip}"`);

  // Hamburger + Kacheln müssen in BEIDEN Editionen da sein
  await p.click('#hamburger');
  await p.waitForSelector('body.drawer-open');
  await p.waitForTimeout(400); // Slide-in ausklingen lassen
  const items = await p.locator('.drawer-item').count();
  if (items !== 9) problems.push(`${label}: ${items} Menüeinträge statt 9`);
  await p.screenshot({ path: `${SHOTS}/30-${label}-menue.png` });

  await p.click('.drawer-item[data-view="providers"]');
  await p.waitForSelector('.provider-card');
  const cards = await p.locator('.provider-card').count();
  if (cards !== 10) problems.push(`${label}: ${cards} Kacheln statt 10`);
  await p.screenshot({ path: `${SHOTS}/31-${label}-kacheln.png` });

  // OpenCLAW-Bereich: nur in der OpenCLAW-Edition sichtbar
  await p.click('#hamburger');
  await p.waitForTimeout(400);
  await p.click('.drawer-item[data-view="agents"]');
  await p.waitForTimeout(500);
  const clawVisible = await p.isVisible('#clawRun').catch(() => false);
  if (label === 'openclaw' && !clawVisible) problems.push('openclaw: Shell-Bereich fehlt');
  if (label === 'normal' && clawVisible) problems.push('normal: Shell-Bereich darf nicht erscheinen');

  console.log(`   ${label.padEnd(8)} Chip=${chip}  Menü=${items}  Kacheln=${cards}  OpenCLAW-Bereich=${clawVisible}`);
  await p.close();
}

await b.close();
if (problems.length) { console.log('\n✗'); problems.forEach(x => console.log('  - ' + x)); process.exit(1); }
console.log('\n✓ Beide Editionen korrekt: Menü und Kacheln überall, Shell nur in OpenCLAW');
