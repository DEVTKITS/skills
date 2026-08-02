// Belegt: präpariertes Workflow-JSON darf beim Node-Klick keinen Code ausführen.
import { chromium } from 'playwright';
const BASE = process.argv[2];
const b = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH });
const p = await b.newPage();
let fired = false;
p.on('dialog', async d => { fired = true; await d.dismiss(); });
await p.goto(BASE, { waitUntil: 'networkidle' });

// Angreifer-Payload in einem importierten Workflow
await p.evaluate(() => {
  window.app.workflow = {
    id: 'evil',
    nodes: [{ id: 'n1', type: 'llm-node', x: 60, y: 60,
              params: { model: '"><img src=x onerror="window.__pwned=1;alert(1)">' } }],
    edges: []
  };
  window.app.redrawCanvas();
  window.app.selectNode(window.app.workflow.nodes[0]);
});
await p.waitForTimeout(900);

const pwned = await p.evaluate(() => !!window.__pwned);
const injected = await p.evaluate(() =>
  document.querySelectorAll('#selectedNodeParams img').length);
const shownValue = await p.evaluate(() =>
  document.querySelector('#selectedNodeParams input')?.value || '');

console.log('alert ausgelöst      :', fired);
console.log('window.__pwned gesetzt:', pwned);
console.log('injizierte <img>-Tags :', injected);
console.log('Wert steht als Text   :', shownValue.slice(0, 45));
await b.close();
process.exit(fired || pwned || injected > 0 ? 1 : 0);
