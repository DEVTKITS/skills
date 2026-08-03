// Design System UI — Playwright demo agent.
//
// Drives the real UI in Chromium exactly like the user described: every step
// is broadcast via the activity protocol, so a human watching the page sees
// the red monitor-edge glow while the AI works and each clicked element
// flash. Takes screenshots as proof.
//
//   node demo/agent_demo.mjs [baseURL] [screenshotDir]
//
// Requires: `npm install playwright` next to this file (chromium itself is
// preinstalled in this environment via PLAYWRIGHT_BROWSERS_PATH).

import { chromium } from 'playwright';

const BASE = process.argv[2] || 'http://localhost:8000';
const SHOTS = process.argv[3] || '.';
const ACTOR = 'playwright-demo';

async function broadcast(page, action, target = '', detail = '', state = 'working') {
  await page.evaluate(async ({ action, target, detail, state, actor }) => {
    await fetch('/api/agent/activity', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ actor, action, target, detail, state })
    });
  }, { action, target, detail, state, actor: ACTOR });
}

async function visibleClick(page, selector, detail) {
  await broadcast(page, 'click', selector, detail);
  await page.waitForTimeout(1200); // let the human see the flash + glow
  await page.click(selector);
}

const run = async () => {
  const browser = await chromium.launch({
    executablePath: process.env.CHROMIUM_PATH || undefined,
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  console.log(`→ opening ${BASE}`);
  await page.goto(BASE, { waitUntil: 'networkidle' });

  await broadcast(page, 'session-start', '', 'Playwright agent taking over the builder');
  await page.waitForTimeout(1500); // glow fades in, ticker appears
  await page.screenshot({ path: `${SHOTS}/01-agent-active-glow.png` });

  // Load the eval-loop template — pattern-based, like a human would click.
  await visibleClick(page, '[data-tab="patterns"]', 'open templates tab');
  await visibleClick(page, '[data-pattern="eval-loop"] .btn', 'load eval-loop template');
  await page.waitForTimeout(800);
  await page.screenshot({ path: `${SHOTS}/02-template-loaded.png` });

  // Execute the workflow and wait for the evaluation verdict.
  await visibleClick(page, '#executeWorkflow', 'execute workflow');
  await page.waitForSelector('.eval-container:not(.hidden)', { timeout: 15000 });
  await page.waitForTimeout(1000);
  await broadcast(page, 'evaluate', '#evalResults', 'reading Gemma verdict');
  await page.screenshot({ path: `${SHOTS}/03-executed-evaluated.png` });

  const verdict = await page.textContent('#evalResults');
  console.log('→ evaluation panel says:', verdict.replace(/\s+/g, ' ').slice(0, 160));

  await broadcast(page, 'session-end', '', 'demo complete', 'done');
  await page.waitForTimeout(500);
  await browser.close();
  console.log('✓ demo finished — screenshots written');
};

run().catch((err) => { console.error(err); process.exit(1); });
