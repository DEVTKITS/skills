import { createRequire } from 'module';
const { chromium } = createRequire(import.meta.url)('playwright');
const base = process.env.BASE_URL || 'http://127.0.0.1:8123';
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : (fail++, console.log('FAIL', m)); };
const b = await chromium.launch(), p = await b.newPage();
const errs = []; p.on('pageerror', e => errs.push(e.message));
await p.goto(base); await p.waitForTimeout(300);
await p.evaluate(() => window.app.showView('svg'));
ok(await p.locator('#view-svg .ss').count() === 1, 'studio gebaut');
ok(await p.locator('#view-svg .ss-out svg').count() === 1, 'preview svg');
ok(await p.locator('.ss-themes figure svg').count() === 5, 'fuenf themes');
for (const t of ['bar','line','donut','pattern','badge','icon','monogram','gradient','graph']) {
  await p.selectOption('#ss-type', t);
  ok(await p.locator('.ss-out svg').count() === 1, 'type ' + t);
  ok(!(await p.locator('.ss-err').count()), 'kein Fehler ' + t);
}
await p.selectOption('#ss-type', 'bar');
await p.fill('#ss-f-data', '1,2,3');
ok((await p.locator('.ss-out svg rect').count()) >= 3, 'daten wirken');
await p.fill('#ss-f-title', '<img src=x onerror=window.__x=1>');
ok(!(await p.evaluate(() => window.__x)), 'kein XSS');
await p.setInputFiles('#view-svg input[type=file]', { name: 'a.svg', mimeType: 'image/svg+xml', buffer: Buffer.from('<svg xmlns="http://www.w3.org/2000/svg"><script>window.__y=1</script><rect onclick="window.__y=1"/></svg>') });
await p.waitForTimeout(200);
ok((await p.locator('.ss-msg').textContent()).includes('entfernt'), 'import bereinigt');
ok(!(await p.evaluate(() => window.__y)), 'import ohne script');
ok(!errs.length, 'keine pageerror ' + errs.join('|'));
await b.close(); console.log(`PASS=${pass} FAIL=${fail}`); process.exit(fail ? 1 : 0);
