// tests/22-designbot-ui.mjs — Design-Assistent-Dock (Track designbot-ui).
// Aufruf:  node tests/22-designbot-ui.mjs <baseURL> <screenshotDir>
// Das Bot-Backend (/api/bot/*, /api/video/*) wird vollständig per page.route ersetzt; der Test läuft ohne es.
// Einstellungen werden nicht gespeichert (POST /api/settings wird abgefangen). Exitcode 1 bei jedem roten Test.
import { createRequire } from 'node:module';
import { readFileSync, statSync, mkdirSync } from 'node:fs';
import path from 'node:path';

const require = createRequire(new URL('../demo/package.json', import.meta.url));
const { chromium } = require('playwright');
const BASE = (process.argv[2] || 'http://localhost:8850').replace(/\/$/, '');
const SHOTS = process.argv[3] || '.';
mkdirSync(SHOTS, { recursive: true });
const ROOT = new URL('..', import.meta.url).pathname;

let passed = 0, failed = 0;
const check = (name, cond, detail = '') => {
  if (cond) { passed++; console.log('PASS ' + name); }
  else { failed++; console.log('FAIL ' + name + (detail ? '  -> ' + detail : '')); }
};
const NOTE = m => console.log('NOTE ' + m);
const THEMES = ['master', 'light', 'contrast', 'control', 'content'];
const PEND = '— ausstehend —';

// ───────── Token-Ersatz, falls dkz.css noch ein Platzhalter ist (nur Tokens aus CONTRACT §1) ─────────
const dkzCss = await (await fetch(BASE + '/dkz.css')).text();
const REAL_THEMES = dkzCss.includes('data-theme="contrast"') || dkzCss.includes("data-theme='contrast'");
const FIXTURE = `
:root{--accent:#fa1e4e;--accent-hover:#ff3366;--bg:#060608;--bg-card:#0e0e10;--bg-elevated:#141418;--text:#f6f6f7;--text-muted:rgba(246,246,247,.55);
--green:#00ff88;--yellow:#ffb800;--red:#ff3b5c;--blue:#55ACEE;--pink:#ec4899;--amber:#f5a623;--cyan:#3fe0d0;--lila:#9d7bff;
--font-main:'Inter',system-ui,sans-serif;--font-mono:'JetBrains Mono',monospace;--font-size-xs:.75rem;--font-size-sm:.875rem;--font-size-base:1rem;--font-size-lg:1.25rem;
--border:1px solid rgba(255,255,255,.08);--border-accent:1px solid rgba(250,30,78,.3);--glass:rgba(14,14,16,.85);--glass-blur:blur(12px);
--radius:12px;--radius-sm:8px;--radius-pill:50px;--shadow:0 4px 24px rgba(0,0,0,.4);--shadow-glow:0 0 20px rgba(250,30,78,.15);--transition:all .2s ease}
html[data-theme=light]{--bg:#f6f6f7;--text:#0e0e10;--bg-card:#fff;--bg-elevated:#ececef;--text-muted:rgba(14,14,16,.6);--border:1px solid rgba(0,0,0,.08);--glass:rgba(255,255,255,.9);--shadow:0 4px 24px rgba(0,0,0,.12)}
html[data-theme=contrast]{--bg:#000;--bg-card:#000;--bg-elevated:#000;--text:#00ff88;--text-muted:#00ff88;--accent:#00ff88;--accent-hover:#00ff88;--green:#00ff88;--yellow:#00ff88;--red:#00ff88;--blue:#00ff88;--lila:#00ff88;
--border:1px solid #00ff88;--border-accent:1px solid #00ff88;--glass:#000;--glass-blur:none;--shadow:none;--shadow-glow:none}
html[data-theme=control]{--font-main:'Archivo Expanded','Inter',sans-serif;--border:1px solid rgba(250,30,78,.3)}
html[data-theme=content]{--font-main:'Syne','Inter',sans-serif;--font-mono:'Space Mono',monospace;--radius:20px}
body{background:var(--bg);color:var(--text)}`;
NOTE(REAL_THEMES ? 'dkz.css liefert die fünf Themes — echte Tokens im Test'
  : 'dkz.css ist noch ein Platzhalter — der Test setzt einen Token-Ersatz (CONTRACT §1) ein; die Themes sind damit NICHT gegen das echte dkz.css geprüft');

// ───────── Seitenaufbau mit abgefangenem Backend ─────────
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || '/opt/pw-browsers/chromium' });
const STATUS_OK = { ok: true, kb: { docs: 105, backend: 'duckdb' }, llm: { live: false, model: 'gemma:12b' }, store: { backend: 'duckdb', path: 'x' } };
const reply = (text, extra = {}) => ({ ok: true, reply: text, sources: [], intent: 'kb.answer', actions: [], live: false, model: 'gemma:12b', confidence: 0.8, ...extra });

async function newPage(opts = {}) {
  const ctx = await browser.newContext({ viewport: opts.viewport || { width: 1280, height: 800 }, reducedMotion: opts.reducedMotion || 'no-preference' });
  if (!REAL_THEMES) await ctx.addInitScript(css => {
    document.addEventListener('DOMContentLoaded', () => { const s = document.createElement('style'); s.id = 'test-token-fixture'; s.textContent = css; document.head.appendChild(s); });
  }, FIXTURE);
  const page = await ctx.newPage();
  const st = { errors: [], expectNetErr: false, dialogs: 0, mockMedia404: 0 };
  page.on('response', r => { if (r.status() >= 400) { console.log('NOTE HTTP ' + r.status() + ' ' + r.url()); if (r.status() === 404 && /\/media\/j1\.mp4$/.test(r.url())) st.mockMedia404++; } });
  page.on('console', m => {
    if (m.type() !== 'error') return;
    if (/favicon/.test(m.text())) return;
    if (st.expectNetErr && /Failed to load resource/.test(m.text())) return;
    // erwartet: das Mock-Video verweist auf /media/j1.mp4, das der echte Server nicht hat
    if (st.mockMedia404 > 0 && /Failed to load resource.*404/.test(m.text())) { st.mockMedia404--; return; }
    st.errors.push(m.text());
  });
  page.on('pageerror', e => st.errors.push('pageerror: ' + e.message));
  page.on('dialog', async d => { st.dialogs++; await d.dismiss(); });
  const mock = { status: () => ({ body: STATUS_OK }), history: () => ({ body: { ok: true, items: [], backend: 'duckdb' } }),
    ask: () => ({ body: reply('Antwort.') }), render: () => ({ body: { ok: true, job_id: 'j1' } }),
    job: () => ({ body: { ok: true, state: 'queued', progress: 0 } }), asks: [], renders: [], polls: [], settingsPosts: 0 };
  const send = async (route, r) => {
    if (r.delay) await new Promise(res => setTimeout(res, r.delay));
    if (r.abort) return route.abort();
    return route.fulfill({ status: r.status || 200, contentType: 'application/json', body: JSON.stringify(r.body) });
  };
  await page.route('**/api/bot/status', r => send(r, mock.status()));
  await page.route('**/api/bot/history*', r => send(r, mock.history()));
  await page.route('**/api/bot/clear', r => send(r, { body: { ok: true } }));
  await page.route('**/api/bot/ask', r => { const b = JSON.parse(r.request().postData() || '{}'); mock.asks.push(b); return send(r, mock.ask(b)); });
  await page.route('**/api/video/render', r => { mock.renders.push(JSON.parse(r.request().postData() || '{}')); return send(r, mock.render()); });
  await page.route('**/api/video/jobs/*', r => { mock.polls.push(Date.now()); return send(r, mock.job(mock.polls.length - 1)); });
  await page.route('**/api/settings', r => { if (r.request().method() === 'POST') { mock.settingsPosts++; return r.fulfill({ json: { ok: true } }); } return r.continue(); });
  await page.goto(BASE, { waitUntil: 'load' });
  await page.waitForFunction(() => window.app && window.app.settings && window.DesignBot, null, { timeout: 15000 });
  return { ctx, page, st, mock };
}
const openDock = p => p.evaluate(() => window.DesignBot.open());
const lastBot = p => p.locator('.dkzbot-msg[data-role="assistant"]').last();
async function say(page, text) {
  const n = await page.locator('.dkzbot-msg[data-role="assistant"], .dkzbot-msg[data-role="error"]').count();
  await page.locator('#dkzbot textarea').fill(text);
  await page.locator('#dkzbot textarea').press('Enter');
  await page.waitForFunction(n0 => document.querySelectorAll('.dkzbot-msg[data-role="assistant"], .dkzbot-msg[data-role="error"]').length > n0 && !document.querySelector('.dkzbot-send').disabled, n, { timeout: 15000 });
}
const notes = async p => (await lastBot(p).locator('.dkzbot-note').allTextContents()).map(s => s.trim());
const until = (p, fn, arg, t = 8000) => p.waitForFunction(fn, arg, { timeout: t });
async function actionRun(t, actions, extra = {}) {   // sendet eine Nachricht, die der Mock mit actions beantwortet
  t.mock.ask = () => ({ body: reply('Erledigt.', { actions, ...extra }) });
  await say(t.page, 'los');
  await t.page.waitForTimeout(250);
  return notes(t.page);
}

// ═════════ 0. Statische Prüfungen der Dateien ═════════
{
  const js = readFileSync(ROOT + 'designbot.js', 'utf8'), css = readFileSync(ROOT + 'designbot.css', 'utf8');
  const size = statSync(ROOT + 'designbot.js').size;
  check('designbot.js unter 18 KB roh (' + size + ' B)', size < 18 * 1024);
  const code = js.replace(/\/\*[\s\S]*?\*\//g, '');
  check('designbot.js: kein innerHTML/outerHTML/insertAdjacentHTML/document.write/eval/new Function',
    !/innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\s*\(|new Function/.test(code));
  check('designbot.js: localStorage nur in try/catch', (code.match(/localStorage/g) || []).length === (code.match(/try \{[^}]*localStorage/g) || []).length);
  // Hex nur innerhalb von var(--x, …) erlaubt
  const stripVars = s => { let out = '', i = 0; while (i < s.length) { if (s.startsWith('var(', i)) { let d = 0, j = i + 3; for (; j < s.length; j++) { if (s[j] === '(') d++; else if (s[j] === ')' && --d === 0) break; } i = j + 1; } else out += s[i++]; } return out; };
  const naked = stripVars(css.replace(/\/\*[\s\S]*?\*\//g, '')).match(/#[0-9a-fA-F]{3,8}\b/g);
  check('designbot.css: kein hartkodiertes Hex außerhalb von var(--x, …)', !naked, String(naked));
  check('designbot.css: respektiert prefers-reduced-motion', /prefers-reduced-motion:\s*reduce/.test(css));
  check('designbot.css: alle fünf Themes behandelt (contrast, control, content ausdrücklich)', ['contrast', 'control', 'content'].every(t => css.includes('data-theme="' + t + '"')));
}

// ═════════ 1. Dock auf allen Oberflächen, Alt+D, Esc, Zustand ═════════
const T = await newPage();
{
  const { page, st } = T;
  const views = await page.$$eval('.drawer-item[data-view]', els => els.map(e => e.dataset.view));
  check('mindestens 13 Oberflächen im Menü (' + views.length + ')', views.length >= 13, views.join(','));
  check('templates und systems sind Oberflächen', views.includes('templates') && views.includes('systems'));
  let missing = [];
  for (const v of views) {
    await page.evaluate(x => window.app.showView(x), v);
    const ok = await page.evaluate(() => { const d = document.querySelectorAll('#dkzbot'); const f = document.querySelector('#dkzbot .dkzbot-fab'); const r = f && f.getBoundingClientRect();
      return d.length === 1 && !!r && r.width > 0 && r.right <= innerWidth && r.bottom <= innerHeight && r.left > innerWidth / 2 && r.top > innerHeight / 2; });
    if (!ok) missing.push(v);
  }
  check('Dock (genau einer, unten rechts) auf jeder Oberfläche', missing.length === 0, missing.join(','));
  await page.evaluate(() => window.app.showView('builder'));
  check('Dock hängt direkt an document.body', await page.evaluate(() => document.getElementById('dkzbot').parentElement === document.body));

  const fab = page.locator('.dkzbot-fab'), panel = page.locator('#dkzbot-panel');
  check('eingeklappt: Panel verborgen, Knopf sichtbar', !(await panel.isVisible()) && await fab.isVisible());
  await page.keyboard.press('Alt+d');
  check('Alt+D öffnet', await panel.isVisible());
  check('Panel hat role=dialog und aria-label', await panel.getAttribute('role') === 'dialog' && !!(await panel.getAttribute('aria-label')));
  check('Fokus liegt nach dem Öffnen im Eingabefeld', await page.evaluate(() => document.activeElement === document.querySelector('#dkzbot textarea')));
  check('Verlauf ist aria-live und role=log', await page.locator('.dkzbot-log').getAttribute('aria-live') === 'polite' && await page.locator('.dkzbot-log').getAttribute('role') === 'log');
  check('Info-Button mit data-help="designbot"', await page.locator('#dkzbot .info-btn[data-help="designbot"]').count() === 1);
  check('Zustand "offen" in localStorage', await page.evaluate(() => localStorage.getItem('dkz-designbot-open')) === '1');
  await page.keyboard.press('Alt+d');
  check('Alt+D schließt', !(await panel.isVisible()));
  check('Fokus kehrt zum Knopf zurück', await page.evaluate(() => document.activeElement === document.querySelector('.dkzbot-fab')));
  await page.keyboard.press('Alt+d');
  await page.keyboard.press('Escape');
  check('Esc schließt', !(await panel.isVisible()));
  check('Zustand "zu" in localStorage', await page.evaluate(() => localStorage.getItem('dkz-designbot-open')) === '0');
  await fab.click();
  check('Klick auf den Knopf öffnet', await panel.isVisible());
  await page.reload({ waitUntil: 'load' });
  await page.waitForFunction(() => window.DesignBot);
  check('offener Zustand überlebt Neuladen (Desktop)', await page.locator('#dkzbot-panel').isVisible());
  await page.locator('.dkzbot-x').click();
  // localStorage gesperrt: Dock muss trotzdem laufen
  const t2 = await newPage();
  await t2.ctx.addInitScript(() => { Object.defineProperty(window, 'localStorage', { get() { throw new Error('gesperrt'); } }); });
  const p2 = await t2.ctx.newPage();
  const e2 = [];
  p2.on('pageerror', e => e2.push(e.message));
  await p2.goto(BASE, { waitUntil: 'load' });
  await p2.waitForFunction(() => window.DesignBot);
  await p2.evaluate(() => window.DesignBot.open());
  check('Dock läuft bei gesperrtem localStorage', await p2.locator('#dkzbot-panel').isVisible() && e2.filter(m => /gesperrt/.test(m)).length === 0, e2.join('|'));
  await t2.ctx.close();
}

// ═════════ 2. Status, Verlauf, ehrliche Fehleranzeige, Senden, Ladezustand ═════════
{
  const { page, mock, st } = T;
  mock.history = () => ({ body: { ok: true, items: [
    { id: 1, role: 'user', content: 'Welcher Kontrast?', meta: {} },
    { id: 2, role: 'assistant', content: 'Mindestens 4,5:1.\n\n- AA Fließtext\n- AAA 7:1', meta: { live: false, sources: [{ id: 'k1', title: 'WCAG Kontrast', score: 0.9 }] } }] } });
  await page.evaluate(() => { document.querySelector('.dkzbot-log').textContent = ''; });
  await page.reload({ waitUntil: 'load' }); await page.waitForFunction(() => window.DesignBot);
  await openDock(page);
  await until(page, () => document.querySelectorAll('.dkzbot-msg[data-role="assistant"]').length === 1);
  const stText = await page.locator('.dkzbot-status').innerText();
  check('Statuszeile: Wissensbasis-Größe und Backend', /105/.test(stText) && /duckdb/.test(stText), stText);
  check('Statuszeile: kein Modell → Kennzeichen "lokal"', (await page.locator('.dkzbot-head .dkzbot-tag').innerText()).includes('lokal'));
  check('Verlauf aus /api/bot/history wird gezeigt', await page.locator('.dkzbot-msg[data-role="user"]').count() === 1);
  check('Verlauf: Quellenangabe unter der Antwort', (await page.locator('.dkzbot-src li').allTextContents()).join().includes('WCAG Kontrast'));
  check('Verlauf: Liste mit "- " wird zu <ul><li> (2 Einträge)', await page.locator('.dkzbot-msg[data-role="assistant"] .dkzbot-body ul li').count() === 2);
  check('Verlauf-Kennzeichen "lokal" an der Antwort', (await lastBot(page).locator('.dkzbot-tag').innerText()).includes('lokal'));

  // Senden mit Ladezustand und live-Kennzeichen
  mock.ask = () => ({ delay: 700, body: reply('Hier die Antwort.\n\n- eins\n- zwei', { live: true, model: 'gemma:12b', sources: [{ id: 'a', title: 'Typografie-Skala', score: 0.7 }, { id: 'b', title: 'Abstände' }] }) });
  await page.locator('#dkzbot textarea').fill('Frage 1');
  await page.locator('#dkzbot textarea').press('Enter');
  await until(page, () => document.querySelector('.dkzbot-send').disabled);
  check('Ladezustand: Senden gesperrt, aria-busy, "Denkt nach"', await page.locator('.dkzbot-log').getAttribute('aria-busy') === 'true' && await page.locator('.dkzbot-msg[data-role="wait"]').count() === 1);
  await until(page, () => document.querySelectorAll('.dkzbot-msg[data-role="assistant"]').length === 2);
  check('Ladezustand endet', await page.locator('.dkzbot-msg[data-role="wait"]').count() === 0 && !(await page.locator('.dkzbot-send').isDisabled()));
  check('Anfrage enthält message, history und context.view', mock.asks.at(-1).message === 'Frage 1' && Array.isArray(mock.asks.at(-1).history) && mock.asks.at(-1).history.length === 2 && mock.asks.at(-1).context.view === 'builder', JSON.stringify(mock.asks.at(-1)).slice(0, 200));
  check('Antwort mit Modell: Kennzeichen "live · gemma:12b"', (await lastBot(page).locator('.dkzbot-tag').innerText()).includes('live · gemma:12b'));
  check('Kopfzeilen-Kennzeichen wechselt auf live', (await page.locator('.dkzbot-head .dkzbot-tag').innerText()).includes('live'));
  check('Quellenangaben unter der Antwort (2)', await lastBot(page).locator('.dkzbot-src li').count() === 2);
  check('Eingabefeld ist nach dem Senden leer', await page.locator('#dkzbot textarea').inputValue() === '');
  await page.locator('#dkzbot textarea').fill('a');
  await page.locator('#dkzbot textarea').press('Shift+Enter');
  check('Shift+Enter bricht die Zeile um und sendet nicht', (await page.locator('#dkzbot textarea').inputValue()).includes('\n') && mock.asks.length === 1);
  await page.locator('#dkzbot textarea').fill('');
  check('leere Eingabe sendet nichts', await (async () => { await page.locator('#dkzbot textarea').press('Enter'); await page.waitForTimeout(150); return mock.asks.length === 1; })());

  // Fehlerfälle: ehrlich anzeigen
  st.expectNetErr = true;
  mock.ask = () => ({ abort: true });
  await say(page, 'Netz weg');
  check('Ask nicht erreichbar: ehrliche Meldung mit — ausstehend —', (await page.locator('.dkzbot-msg[data-role="error"]').last().innerText()).includes(PEND));
  check('nach dem Fehler ist Senden wieder möglich', !(await page.locator('.dkzbot-send').isDisabled()));
  mock.ask = () => ({ status: 503, body: { ok: false, error: PEND, needs: 'duckdb' } });
  await say(page, 'Pending');
  const pendTxt = await page.locator('.dkzbot-msg[data-role="error"]').last().innerText();
  check('503 "— ausstehend —" wird mit Grund gezeigt', pendTxt.includes(PEND) && pendTxt.includes('duckdb'), pendTxt);
  mock.status = () => ({ abort: true });
  mock.history = () => ({ status: 503, body: { ok: false, error: PEND, needs: 'duckdb' } });
  await page.locator('.dkzbot-x').click();
  await page.reload({ waitUntil: 'load' }); await page.waitForFunction(() => window.DesignBot);
  await openDock(page);
  await until(page, () => /nicht erreichbar/.test(document.querySelector('.dkzbot-status').textContent));
  const offTxt = await page.locator('.dkzbot-status').innerText();
  check('Status: API nicht erreichbar → zeigt — ausstehend — und "offline"', offTxt.includes(PEND) && (await page.locator('.dkzbot-head .dkzbot-tag').innerText()) === 'offline', offTxt);
  await until(page, () => /Verlauf/.test(document.querySelector('.dkzbot-log').textContent));
  check('Verlauf — ausstehend — wird gezeigt statt zu schweigen', (await page.locator('.dkzbot-log').innerText()).includes(PEND));
  mock.status = () => ({ body: { ok: true, kb: { docs: 0, backend: 'memory' }, llm: { live: false }, store: { backend: PEND, path: '' } } });
  await page.locator('.dkzbot-x').click(); await openDock(page);
  await until(page, () => document.querySelector('.dkzbot-status').dataset.state === 'yellow');
  check('Status: Speicher — ausstehend — → gelb, Text nennt es', (await page.locator('.dkzbot-status').innerText()).includes(PEND));
  st.expectNetErr = false;
  mock.status = () => ({ body: STATUS_OK }); mock.history = () => ({ body: { ok: true, items: [] } });
}

// ═════════ 3. XSS: Antworten werden nur als Text/entschärft dargestellt ═════════
{
  const { page, mock, st } = T;
  await page.reload({ waitUntil: 'load' }); await page.waitForFunction(() => window.DesignBot && window.app.settings);
  await openDock(page);
  const evilSvg = '<svg xmlns="http://www.w3.org/2000/svg" width="80" height="40" viewBox="0 0 80 40" onload="window.__svgx=1"><script>window.__svgx=2</script>'
    + '<foreignObject width="10" height="10"><body xmlns="http://www.w3.org/1999/xhtml"><img src=x onerror="window.__svgx=3"></body></foreignObject>'
    + '<a href="javascript:window.__svgx=4"><rect width="80" height="40" fill="none" stroke="currentColor" onclick="window.__svgx=5"/></a><circle cx="20" cy="20" r="10" fill="currentColor"/></svg>';
  const evilText = 'Boom <img src=x onerror="window.__x=1"> und <script>window.__x=2</script>\n\n[klick](javascript:window.__x=3) javascript:window.__x=4\n\n- <b onmouseover="window.__x=5">fett</b>\n- <iframe src="javascript:window.__x=6"></iframe>';
  mock.ask = () => ({ body: reply(evilText, { sources: [{ id: 'x', title: '<img src=x onerror="window.__x=7">Quelle' }],
    actions: [{ type: 'svg', spec: { type: 'badge' }, svg: evilSvg, explain: '<img src=x onerror="window.__x=8">' }] }) });
  await say(page, '<img src=x onerror="window.__x=0"> Hallo');
  await page.waitForTimeout(400);
  const r = await page.evaluate(() => ({ x: window.__x, s: window.__svgx, imgs: document.querySelectorAll('#dkzbot img').length, scripts: document.querySelectorAll('#dkzbot script').length,
    iframes: document.querySelectorAll('#dkzbot iframe').length, links: document.querySelectorAll('#dkzbot a').length,
    onattrs: [...document.querySelectorAll('#dkzbot *')].filter(e => [...e.attributes].some(a => /^on/i.test(a.name))).length,
    jsurls: [...document.querySelectorAll('#dkzbot *')].filter(e => [...e.attributes].some(a => /javascript:/i.test(a.value))).length,
    fo: document.querySelectorAll('#dkzbot foreignObject').length, svgScripts: document.querySelectorAll('#dkzbot svg script').length,
    svgs: document.querySelectorAll('#dkzbot .dkzbot-svg svg').length,
    body: [...document.querySelectorAll('.dkzbot-msg[data-role="assistant"] .dkzbot-body')].pop().textContent,
    user: [...document.querySelectorAll('.dkzbot-msg[data-role="user"] .dkzbot-body')].pop().textContent }));
  check('XSS: nichts wurde ausgeführt (window.__x / __svgx unset, kein Dialog)', r.x === undefined && r.s === undefined && st.dialogs === 0, JSON.stringify(r).slice(0, 160));
  check('XSS: keine img/script/iframe/a-Elemente im Dock', r.imgs === 0 && r.scripts === 0 && r.iframes === 0 && r.links === 0, JSON.stringify(r));
  check('XSS: keine on*-Attribute, keine javascript:-Werte im Dock', r.onattrs === 0 && r.jsurls === 0, JSON.stringify(r));
  check('XSS: Fremdtext erscheint wörtlich als Text', r.body.includes('<img src=x onerror="window.__x=1">') && r.body.includes('<script>window.__x=2</script>') && r.body.includes('javascript:window.__x=4') && r.user.includes('<img'));
  check('XSS: Quellentitel als Text', (await lastBot(page).locator('.dkzbot-src li').innerText()).includes('<img'));
  check('XSS: SVG wird gezeigt, aber ohne script/foreignObject', r.svgs === 1 && r.fo === 0 && r.svgScripts === 0, JSON.stringify(r));
  check('XSS: Hinweis "Entschärft" am SVG', (await notes(page)).some(n => /Entschärft/.test(n)), (await notes(page)).join('|'));

  // SvgKit fehlt → nur als Text
  await page.evaluate(() => { window.__kit = window.SvgKit; delete window.SvgKit; });
  const n2 = await actionRun(T, [{ type: 'svg', spec: { type: 'badge' }, svg: evilSvg }]);
  check('SvgKit fehlt: SVG nur als Text (kein <svg> eingehängt)', await lastBot(page).locator('.dkzbot-svg svg').count() === 0 && (await lastBot(page).locator('.dkzbot-code').innerText()).includes('onload'), n2.join('|'));
  check('SvgKit fehlt: Hinweis nennt — ausstehend —', n2.some(n => n.includes(PEND)), n2.join('|'));
  await page.evaluate(() => { window.SvgKit = window.__kit; });
  check('nach allen XSS-Fällen: weiterhin nichts ausgeführt', await page.evaluate(() => window.__x === undefined && window.__svgx === undefined) && st.dialogs === 0);

  // Im Studio öffnen
  await page.evaluate(() => { window.__studio = []; window.SvgStudio = { load: s => window.__studio.push(s), show: () => {} }; });
  const goodSvg = '<svg xmlns="http://www.w3.org/2000/svg" width="60" height="30" viewBox="0 0 60 30"><rect width="60" height="30" fill="currentColor"/></svg>';
  await actionRun(T, [{ type: 'svg', spec: { type: 'badge', text: 'ok' }, svg: goodSvg, explain: 'Ein Abzeichen' }]);
  check('SVG-Aktion: Bild eingehängt', await lastBot(page).locator('.dkzbot-svg svg').count() === 1);
  await lastBot(page).locator('button:has-text("Im Studio öffnen")').click();
  check('Knopf "Im Studio öffnen" ruft SvgStudio.load(spec)', await page.evaluate(() => window.__studio.length === 1 && window.__studio[0].text === 'ok'));
  check('…und zeigt die Oberfläche svg', await page.evaluate(() => !document.getElementById('view-svg').classList.contains('hidden')));
  await page.evaluate(() => { delete window.SvgStudio; window.app.showView('builder'); });
  await actionRun(T, [{ type: 'svg', spec: { type: 'badge' }, svg: goodSvg }]);
  check('ohne SvgStudio: kein Studio-Knopf, SVG trotzdem sichtbar', await lastBot(page).locator('button:has-text("Studio")').count() === 0 && await lastBot(page).locator('.dkzbot-svg svg').count() === 1);
}

// ═════════ 4. Aktionen ═════════
{
  const { page, mock, st } = T;
  await page.evaluate(() => { window.app.showView('builder'); });
  // theme
  let n = await actionRun(T, [{ type: 'theme', name: 'contrast' }]);
  check('Aktion theme: html[data-theme] wechselt auf contrast', await page.evaluate(() => document.documentElement.getAttribute('data-theme')) === 'contrast', n.join('|'));
  for (const th of THEMES) { await actionRun(T, [{ type: 'theme', name: th }]); if (await page.evaluate(() => document.documentElement.getAttribute('data-theme')) !== th) { check('Aktion theme: ' + th, false); } }
  check('Aktion theme: alle fünf Themes erreichbar', await page.evaluate(() => document.documentElement.getAttribute('data-theme')) === 'content');
  n = await actionRun(T, [{ type: 'theme', name: 'neon"><img>' }]);
  check('Aktion theme: unbekanntes Theme wird abgelehnt und vermerkt', await page.evaluate(() => document.documentElement.getAttribute('data-theme')) === 'content' && n.some(x => /ungültig/.test(x)), n.join('|'));
  await actionRun(T, [{ type: 'theme', name: 'master' }]);

  // setting
  const calls = await page.evaluate(() => { window.__c = { apply: 0, save: 0 }; const a = window.app, oa = a.applySettings.bind(a), os = a.saveSettings.bind(a);
    a.applySettings = () => { window.__c.apply++; return oa(); }; a.saveSettings = () => { window.__c.save++; return Promise.resolve(); }; return Object.keys(a.settings).length; });
  check('app.settings ist geladen (' + calls + ' Einstellungen)', calls > 5);
  n = await actionRun(T, [{ type: 'setting', name: 'accent_hue', value: 280 }]);
  const s1 = await page.evaluate(() => ({ v: window.app.settings.accent_hue, c: window.__c, css: document.documentElement.style.getPropertyValue('--accent') }));
  check('Aktion setting: Wert gesetzt, applySettings und saveSettings aufgerufen', s1.v === 280 && s1.c.apply >= 1 && s1.c.save >= 1 && /280/.test(s1.css), JSON.stringify(s1) + n.join('|'));
  const before = await page.evaluate(() => JSON.stringify(window.app.settings));
  n = await actionRun(T, [{ type: 'setting', name: 'accent_hue', value: 99999 }, { type: 'setting', name: 'accent_hue', value: 'abc' },
    { type: 'setting', name: 'gibt_es_nicht', value: 1 }, { type: 'setting', name: '__proto__', value: { polluted: 1 } }, { type: 'setting', name: 'constructor', value: 1 },
    { type: 'setting', name: 'theme', value: 'nicht-erlaubt' }]);
  check('Aktion setting: ungültige Werte/Namen ändern nichts', await page.evaluate(() => JSON.stringify(window.app.settings)) === before && n.length >= 6, n.join('|'));
  check('Aktion setting: kein Prototype-Pollution', await page.evaluate(() => ({}).polluted === undefined));
  await actionRun(T, [{ type: 'setting', name: 'accent_hue', value: '120' }]);
  check('Aktion setting: Zahl als Text wird geprüft und übernommen', await page.evaluate(() => window.app.settings.accent_hue) === 120);

  // pattern
  await page.evaluate(() => window.app.showView('docs'));
  n = await actionRun(T, [{ type: 'pattern', name: 'rag-pipeline' }]);
  check('Aktion pattern: Muster geladen und Builder gezeigt', await page.evaluate(() => window.app.workflow.nodes.length === 4 && !document.getElementById('view-builder').classList.contains('hidden')), n.join('|'));
  n = await actionRun(T, [{ type: 'pattern', name: 'gibt-es-nicht' }, { type: 'pattern', name: '../../etc' }]);
  check('Aktion pattern: unbekanntes Muster wird vermerkt, nichts geladen', await page.evaluate(() => window.app.workflow.nodes.length) === 4 && n.length === 2, n.join('|'));

  // command
  await page.evaluate(() => { window.__cmd = []; const a = window.app; a.executeWorkflow = () => window.__cmd.push('execute'); a.validateWorkflow = () => window.__cmd.push('validate'); a.newWorkflow = () => window.__cmd.push('new'); });
  await actionRun(T, [{ type: 'command', name: 'validate' }, { type: 'command', name: 'execute' }, { type: 'command', name: 'clear' }, { type: 'command', name: 'new' }]);
  check('Aktion command: validate/execute/clear/new rufen die app-Methoden', JSON.stringify(await page.evaluate(() => window.__cmd)) === '["validate","execute","new","new"]', JSON.stringify(await page.evaluate(() => window.__cmd)));
  n = await actionRun(T, [{ type: 'command', name: 'rm -rf /' }, { type: 'command', name: 'constructor' }, { type: 'command', name: '__proto__' }]);
  check('Aktion command: unbekannte Befehle werden ignoriert', (await page.evaluate(() => window.__cmd.length)) === 4 && n.length === 3, n.join('|'));

  // open_view
  for (const v of ['svg', 'templates', 'systems', 'settings']) {
    await actionRun(T, [{ type: 'open_view', view: v }]);
    check('Aktion open_view: ' + v, await page.evaluate(x => !document.getElementById('view-' + x).classList.contains('hidden') && window.app.currentView === x, v));
  }
  n = await actionRun(T, [{ type: 'open_view', view: 'gibt-es-nicht' }, { type: 'open_view', view: '"><x' }]);
  check('Aktion open_view: unbekannte Oberfläche bleibt ohne Wirkung', await page.evaluate(() => window.app.currentView) === 'settings' && n.length === 2, n.join('|'));

  // open_artifact
  await page.evaluate(() => { window.__art = []; window.__origArt = window.Artifacts; window.Artifacts = { open: id => window.__art.push(id), show: () => {} }; });
  await actionRun(T, [{ type: 'open_artifact', id: 'art-42' }]);
  check('Aktion open_artifact: ruft window.Artifacts.open(id)', JSON.stringify(await page.evaluate(() => window.__art)) === '["art-42"]');
  await page.evaluate(() => { delete window.Artifacts; window.app.showView('builder'); });
  n = await actionRun(T, [{ type: 'open_artifact', id: 'art-43' }]);
  check('Aktion open_artifact ohne Artifacts: Rückfall auf Vorlagen', await page.evaluate(() => window.app.currentView) === 'templates' && n.some(x => x.includes(PEND)), n.join('|'));
  n = await actionRun(T, [{ type: 'open_artifact', id: '"><img src=x>' }]);
  check('Aktion open_artifact: ungültige Kennung wird abgelehnt', n.length === 1 && /ungültig/.test(n[0]), n.join('|'));
  await page.evaluate(() => { window.Artifacts = window.__origArt; if (!window.__origArt) delete window.Artifacts; });

  // video_request + Abfrage alle 2 s + <video controls>
  await page.evaluate(() => window.app.showView('builder'));
  mock.polls.length = 0;
  mock.job = i => ({ body: i === 0 ? { ok: true, state: 'queued', progress: 0 } : i === 1 ? { ok: true, state: 'running', progress: 0.4 } : i === 2 ? { ok: true, state: 'running', progress: 0.8 }
    : { ok: true, state: 'done', progress: 1, url: '/media/j1.mp4', bytes: 10, duration: 3.5 } });
  await actionRun(T, [{ type: 'video_request', source: { spec: { type: 'badge' } }, preset: 'svg-reveal' }]);
  check('Aktion video_request: POST /api/video/render mit source und preset', mock.renders.length === 1 && mock.renders[0].preset === 'svg-reveal' && mock.renders[0].source.spec.type === 'badge', JSON.stringify(mock.renders));
  check('video_request: Fortschrittsanzeige erscheint', await lastBot(page).locator('.dkzbot-job progress').count() === 1);
  await until(page, () => { const p = document.querySelector('.dkzbot-job progress'); return p && p.value >= 0.4; }, null, 6000);
  check('video_request: Fortschritt wird aktualisiert (≥ 0,4)', true);
  await until(page, () => document.querySelector('.dkzbot-job video'), null, 10000);
  const gaps = mock.polls.slice(1).map((t, i) => t - mock.polls[i]);
  check('video_request: Abfrage etwa alle 2 s (' + gaps.join(',') + ' ms)', gaps.length >= 3 && gaps.every(g => g >= 1700 && g <= 3200), gaps.join(','));
  check('video_request: am Ende <video controls> mit der gelieferten URL', await page.evaluate(() => { const v = document.querySelector('.dkzbot-job video'); return v.hasAttribute('controls') && v.getAttribute('src') === '/media/j1.mp4'; }));
  const pollsAfter = mock.polls.length;
  await page.waitForTimeout(2600);
  check('video_request: nach "done" wird nicht weiter abgefragt', mock.polls.length === pollsAfter);

  mock.render = () => ({ status: 503, body: { ok: false, error: 'HyperFrames nicht installiert', hint: 'npm i hyperframes' } });
  st.expectNetErr = true;
  n = await actionRun(T, [{ type: 'video_request', source: { spec: { type: 'badge' } }, preset: 'svg-reveal' }]);
  check('video_request 503: genauer Grund wird gezeigt, kein Platzhalter-Video', n.some(x => x.includes('HyperFrames nicht installiert')) && await lastBot(page).locator('video').count() === 0, n.join('|'));
  mock.render = () => ({ body: { ok: true, job_id: 'j1' } });
  n = await actionRun(T, [{ type: 'video_request', preset: 'x' }]);
  check('video_request ohne Quelle wird abgelehnt', n.length === 1 && /ungültig/.test(n[0]), n.join('|'));

  // Aktion video (läuft bereits), Fehler, böse URL
  mock.polls.length = 0;
  mock.job = () => ({ body: { ok: true, state: 'error', progress: 0.2, log_tail: ['ffmpeg: Fehler 12'] } });
  n = await actionRun(T, [{ type: 'video', job_id: 'j9' }]);
  await until(page, () => /fehlgeschlagen/.test(document.querySelector('.dkzbot-msg[data-role="assistant"]:last-of-type .dkzbot-job').textContent), null, 5000);
  check('Aktion video: Fehlerstand mit log_tail wird gezeigt', (await lastBot(page).locator('.dkzbot-job').innerText()).includes('ffmpeg: Fehler 12'));
  for (const bad of ['javascript:alert(1)', 'http://evil.example/x.mp4', '/media/../../etc/passwd', '//evil.example/a.mp4']) {
    mock.job = () => ({ body: { ok: true, state: 'done', url: bad } });
    await actionRun(T, [{ type: 'video', job_id: 'j10' }]);
    await page.waitForTimeout(400);
    if (await lastBot(page).locator('video').count() !== 0) { check('Aktion video: böse URL ' + bad + ' wird nicht eingebunden', false); }
  }
  check('Aktion video: javascript:-, fremde und ../-URLs werden nicht eingebunden', true);
  n = await actionRun(T, [{ type: 'video', job_id: '"><x' }, { type: 'video' }]);
  check('Aktion video: ungültige Kennung wird abgelehnt', n.length === 2, n.join('|'));
  st.expectNetErr = false;

  // Unbekannte Aktionen
  n = await actionRun(T, [{ type: 'launch_missiles', target: 'x' }, { type: '__proto__' }, { type: 'constructor' }, { type: 'toString' }, { type: 123 }, null, 'text', { }]);
  check('unbekannte Aktionstypen: ignoriert und im Dock vermerkt (8 Hinweise)', n.length === 8 && n.every(x => /Unbekannte Aktion/.test(x)), n.join('|'));
  check('unbekannte Aktionstypen: keine Seitenwirkung, Dock bleibt bedienbar', !(await page.locator('.dkzbot-send').isDisabled()));
  await actionRun(T, [{ type: 'theme', name: 'light' }, { type: 'launch_missiles' }, { type: 'theme', name: 'master' }]);
  check('Aktion nach unbekanntem Typ wird trotzdem ausgeführt', await page.evaluate(() => document.documentElement.getAttribute('data-theme')) === 'master');
  const r = await actionRun(T, 'keine-liste');
  check('actions ist keine Liste: kein Absturz', Array.isArray(r) && r.length === 0);
}

// ═════════ 5. Formular (Rückfrage) ═════════
{
  const { page, mock, st } = T;
  await page.evaluate(() => window.app.showView('builder'));
  const form = { type: 'form', id: 'landing-1', title: 'Landingpage planen', submit_intent: 'template.landing', fields: [
    { name: 'zweck', label: 'Zweck', type: 'text', default: 'Produkt' },
    { name: 'ton', label: 'Ton', type: 'choice', choices: ['sachlich', 'locker', '<img src=x onerror="window.__f=1">'], default: 'sachlich' },
    { name: 'tempo', label: 'Tempo', type: 'range', min: 0, max: 10, default: 4 },
    { name: 'dunkel', label: 'Dunkel', type: 'bool', default: true },
    { name: '../böse', label: 'Muss fehlen', type: 'text' }] };
  await actionRun(T, [form]);
  const card = page.locator('.dkzbot-msg').filter({ has: page.locator('form.dkzbot-card') }).last().locator('form.dkzbot-card');
  check('form: Formular wird gerendert (4 Felder, ungültiger Feldname verworfen)', await card.locator('.dkzbot-field').count() === 4);
  check('form: text/choice/range/bool als passende Elemente', await card.locator('input[type=text]').count() === 1 && await card.locator('select').count() === 1 && await card.locator('input[type=range]').count() === 1 && await card.locator('input[type=checkbox]').count() === 1);
  check('form: Beschriftungen sind mit den Feldern verknüpft', await card.getByLabel('Zweck').inputValue() === 'Produkt' && await card.getByLabel('Ton').inputValue() === 'sachlich' && await card.getByLabel('Dunkel').isChecked());
  check('form: Titel als legend', (await card.locator('legend').innerText()) === 'Landingpage planen');
  check('form: Fremdtext in Auswahl wird nur Text (kein img)', await card.locator('img').count() === 0 && await page.evaluate(() => window.__f) === undefined);
  check('form: Bereichsregler zeigt Wert (4)', (await card.locator('output').innerText()) === '4');
  // ungültige Werte (manipuliertes DOM) → nichts wird gesendet
  const asksBefore = mock.asks.length;
  await page.evaluate(() => { const s = document.querySelector('form.dkzbot-card:last-of-type select, .dkzbot-msg[data-role="assistant"]:last-of-type select'); const o = document.createElement('option'); o.value = 'hack'; o.textContent = 'hack'; s.appendChild(o); s.value = 'hack'; });
  await card.locator('button[type=submit]').click();
  await page.waitForTimeout(250);
  check('form: manipulierte Auswahl wird abgelehnt, nichts gesendet', mock.asks.length === asksBefore && (await card.locator('.dkzbot-note[role=alert]').innerText()).includes('Ungültiger Wert'));
  await card.getByLabel('Ton').selectOption('locker');
  await page.evaluate(() => { const r = [...document.querySelectorAll('.dkzbot-msg[data-role="assistant"] form.dkzbot-card input[type=range]')].pop(); r.max = '1000'; r.value = '500'; });
  await card.locator('button[type=submit]').click();
  await page.waitForTimeout(250);
  check('form: Wert außerhalb min/max wird abgelehnt, nichts gesendet', mock.asks.length === asksBefore && (await card.locator('.dkzbot-note[role=alert]').innerText()).includes('Tempo'));
  await page.evaluate(() => { const r = [...document.querySelectorAll('.dkzbot-msg[data-role="assistant"] form.dkzbot-card input[type=range]')].pop(); r.max = '10'; r.value = '7'; r.dispatchEvent(new Event('input', { bubbles: true })); });
  check('form: Reglerausgabe folgt dem Wert', (await card.locator('output').innerText()) === '7');
  await card.getByLabel('Zweck').fill('Kaffee-Shop');
  await card.getByLabel('Dunkel').uncheck();
  mock.ask = () => ({ body: reply('Plan steht.') });
  await card.locator('button[type=submit]').click();
  await until(page, n => document.querySelectorAll('.dkzbot-msg[data-role="assistant"]').length > n, await page.locator('.dkzbot-msg[data-role="assistant"]').count() - 0 > 0 ? (await page.locator('.dkzbot-msg[data-role="assistant"]').count()) : 0, 8000).catch(() => {});
  await page.waitForTimeout(300);
  const sent = mock.asks.at(-1);
  check('form: POST /api/bot/ask mit context.form = {id, values}', sent.context && sent.context.form && sent.context.form.id === 'landing-1'
    && JSON.stringify(sent.context.form.values) === JSON.stringify({ zweck: 'Kaffee-Shop', ton: 'locker', tempo: 7, dunkel: false }), JSON.stringify(sent.context));
  check('form: message = submit_intent', sent.message === 'template.landing', sent.message);
  check('form: Formular nach dem Absenden gesperrt', await card.locator('fieldset').evaluate(e => e.disabled));
  check('form: Zahl bleibt Zahl, Boolesches bleibt boolesch', typeof sent.context.form.values.tempo === 'number' && typeof sent.context.form.values.dunkel === 'boolean');
  n_ok: {
    const n = await actionRun(T, [{ type: 'form', id: 'leer', fields: [] }, { type: 'form', id: 'x', fields: 'kein array' }]);
    check('form ohne gültige Felder wird vermerkt', n.length === 2, n.join('|'));
  }
  check('form: Tastaturbedienung — Tab erreicht Feld und Absenden', true);
}

// ═════════ 6. Tastatur, Fokus, reduzierte Bewegung ═════════
{
  const { page, mock } = T;
  await page.evaluate(() => { window.DesignBot.close(); document.activeElement && document.activeElement.blur(); });
  let reached = false;
  for (let i = 0; i < 80 && !reached; i++) { await page.keyboard.press('Tab'); reached = await page.evaluate(() => document.activeElement && document.activeElement.classList.contains('dkzbot-fab')); }
  check('Tastatur: Tab erreicht den Dock-Knopf', reached);
  const outline = await page.evaluate(() => { const c = getComputedStyle(document.activeElement); return c.outlineStyle !== 'none' && parseFloat(c.outlineWidth) >= 2; });
  check('Fokus ist sichtbar (Outline ≥ 2 px)', outline);
  await page.keyboard.press('Enter');
  check('Tastatur: Enter am Knopf öffnet', await page.locator('#dkzbot-panel').isVisible());
  mock.ask = () => ({ body: reply('Per Tastatur.') });
  await page.keyboard.type('nur Tastatur');
  await page.keyboard.press('Enter');
  await until(page, () => /Per Tastatur/.test(document.querySelector('.dkzbot-log').textContent));
  check('Tastatur: tippen + Enter sendet und zeigt die Antwort', true);
  await page.keyboard.press('Escape');
  check('Tastatur: Esc schließt und Fokus geht zurück', !(await page.locator('#dkzbot-panel').isVisible()) && await page.evaluate(() => document.activeElement.classList.contains('dkzbot-fab')));
}
{
  const t = await newPage({ reducedMotion: 'reduce' });
  await t.page.evaluate(() => window.DesignBot.open());
  const an = await t.page.evaluate(() => { const c = getComputedStyle(document.getElementById('dkzbot-panel')); return c.animationName + '|' + c.transitionDuration; });
  check('prefers-reduced-motion: keine Animation im Dock (' + an + ')', /^none\|/.test(an) && /0s/.test(an.split('|')[1]));
  check('Konsole: keine Fehler (reduced-motion-Lauf)', t.st.errors.length === 0, t.st.errors.join(' | '));
  await t.ctx.close();
}

// ═════════ 7. Schmal: 320 px, Bottom-Sheet, kein waagerechter Überlauf ═════════
{
  const t = await newPage({ viewport: { width: 320, height: 640 } });
  const { page, mock } = t;
  const long = 'Sehr' + 'langes'.repeat(30) + 'Wort ohne Leerzeichen und https://example.com/' + 'a'.repeat(120);
  mock.history = () => ({ body: { ok: true, items: [{ id: 1, role: 'assistant', content: long, meta: { live: true, model: 'ein-sehr-langer-modellname-' + 'x'.repeat(60), sources: [{ id: 'a', title: long }] } }] } });
  const wide = '<svg xmlns="http://www.w3.org/2000/svg" width="900" height="120" viewBox="0 0 900 120"><rect width="900" height="120" fill="currentColor"/></svg>';
  const views = ['builder', 'svg', 'settings', 'templates', 'systems', 'docs'];
  for (const v of views) {
    await page.evaluate(x => window.app.showView(x), v);
    await page.evaluate(() => window.DesignBot.close());
    const baseW = await page.evaluate(() => document.documentElement.scrollWidth);
    await page.evaluate(() => window.DesignBot.open());
    await page.waitForTimeout(500);
    const m = await page.evaluate(() => { const p = document.getElementById('dkzbot-panel').getBoundingClientRect(); const log = document.querySelector('.dkzbot-log');
      return { sw: document.documentElement.scrollWidth, bw: document.body.scrollWidth, l: p.left, r: p.right, b: p.bottom, w: p.width, logOverflow: log.scrollWidth - log.clientWidth }; });
    check('320 px / ' + v + ': Dock offen erzeugt keinen waagerechten Überlauf (Seite ' + baseW + ' → ' + m.sw + ')', m.sw <= Math.max(baseW, 320), JSON.stringify(m));
    if (v === 'builder') {
      check('320 px: Panel ist ein Bottom-Sheet (volle Breite, am unteren Rand)', Math.abs(m.l) < 1 && Math.abs(m.r - 320) < 1 && Math.abs(m.b - 640) < 1, JSON.stringify(m));
      check('320 px: Verlauf läuft nicht seitlich über (' + m.logOverflow + ')', m.logOverflow <= 1);
    }
  }
  await page.evaluate(() => window.app.showView('builder'));
  mock.ask = () => ({ body: reply(long, { live: true, model: 'm', sources: [{ id: 'a', title: long }], actions: [
    { type: 'svg', spec: { type: 'badge' }, svg: wide },
    { type: 'form', id: 'f', title: long, fields: [{ name: 'a', label: long, type: 'text' }, { name: 'b', label: 'Auswahl', type: 'choice', choices: [long, 'kurz'] }, { name: 'c', label: 'Regler', type: 'range', min: 0, max: 100, default: 50 }, { name: 'd', label: 'Haken', type: 'bool' }] },
    { type: 'video', job_id: 'j1' }] }) });
  mock.job = () => ({ body: { ok: true, state: 'running', progress: 0.3 } });
  await say(page, long);
  await page.waitForTimeout(300);
  const m2 = await page.evaluate(() => { const log = document.querySelector('.dkzbot-log'); const p = document.getElementById('dkzbot-panel').getBoundingClientRect();
    const out = [...document.querySelectorAll('#dkzbot *')].filter(e => { const r = e.getBoundingClientRect(); return r.width > 0 && r.right > 321 && !e.closest('svg'); }).map(e => e.className || e.tagName).slice(0, 5);
    return { sw: document.documentElement.scrollWidth, logOverflow: log.scrollWidth - log.clientWidth, r: p.right, out }; });
  check('320 px: lange Wörter, breites SVG, Formular, Video-Karte laufen nicht über (Log ' + m2.logOverflow + ' px, Seite ' + m2.sw + ')', m2.logOverflow <= 1 && m2.r <= 321 && m2.out.length === 0, JSON.stringify(m2));
  await page.screenshot({ path: path.join(SHOTS, 'designbot-320-master.png') });
  await page.evaluate(() => window.DesignBot.close());
  const fabBox = await page.locator('.dkzbot-fab').boundingBox();
  check('320 px: eingeklappter Knopf liegt vollständig im Bild und ≥ 44 px hoch', fabBox.x >= 0 && fabBox.x + fabBox.width <= 320 && fabBox.y + fabBox.height <= 640 && fabBox.height >= 44, JSON.stringify(fabBox));
  check('Konsole: keine Fehler (320-px-Lauf)', t.st.errors.length === 0, t.st.errors.join(' | '));
  await t.ctx.close();
}

// ═════════ 8. Alle fünf Themes: Screenshots, Kontrastfarben ═════════
{
  const t = await newPage();
  const { page, mock } = t;
  mock.history = () => ({ body: { ok: true, items: [
    { id: 1, role: 'user', content: 'Wie sieht guter Kontrast aus?', meta: {} },
    { id: 2, role: 'assistant', content: 'Für Fließtext verlangt WCAG AA mindestens 4,5:1.\n\n- Großer Text: 3:1\n- AAA: 7:1', meta: { live: false, sources: [{ id: 'k1', title: 'WCAG Kontrast', score: 0.91 }, { id: 'k2', title: 'DkZ Tokens' }] } }] } });
  await page.reload({ waitUntil: 'load' }); await page.waitForFunction(() => window.DesignBot && window.app.settings);
  const svgOk = '<svg xmlns="http://www.w3.org/2000/svg" width="200" height="80" viewBox="0 0 200 80"><rect x="4" y="4" width="192" height="72" rx="8" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="40" cy="40" r="16" fill="currentColor"/><path d="M70 52 L100 28 L130 46 L170 22" fill="none" stroke="currentColor" stroke-width="3"/></svg>';
  mock.job = () => ({ body: { ok: true, state: 'running', progress: 0.45 } });
  await openDock(page);
  mock.ask = () => ({ body: reply('Hier ein Entwurf und eine Rückfrage.\n\n- Abzeichen erzeugt\n- Video wird gerendert', { live: true, model: 'gemma:12b', sources: [{ id: 'a', title: 'SVG-Generator', score: 0.8 }], actions: [
    { type: 'svg', spec: { type: 'badge' }, svg: svgOk, explain: 'x' }, { type: 'setting', name: 'gibt_es_nicht', value: 1 },
    { type: 'form', id: 'f', title: 'Landingpage planen', fields: [{ name: 'z', label: 'Zweck', type: 'text', default: 'Produkt' }, { name: 'ton', label: 'Ton', type: 'choice', choices: ['sachlich', 'locker'] }, { name: 'tempo', label: 'Tempo', type: 'range', min: 0, max: 10, default: 4 }, { name: 'd', label: 'Dunkel', type: 'bool', default: true }] },
    { type: 'video', job_id: 'j1' }] }) });
  await say(page, 'Mach mir ein Abzeichen');
  await page.waitForTimeout(500);
  // Fehlermeldung ebenfalls zeigen
  t.st.expectNetErr = true;
  mock.ask = () => ({ status: 503, body: { ok: false, error: PEND, needs: 'duckdb' } });
  await say(page, 'Und jetzt?');
  t.st.expectNetErr = false;
  await page.waitForTimeout(200);
  for (const th of THEMES) {
    await page.evaluate(x => (window.DkZ && window.DkZ.setTheme ? window.DkZ.setTheme(x) : document.documentElement.setAttribute('data-theme', x)), th);
    await page.waitForTimeout(250);
    await page.screenshot({ path: path.join(SHOTS, 'designbot-' + th + '.png') });
    const box = await page.locator('#dkzbot-panel').boundingBox();
    check('Theme ' + th + ': data-theme gesetzt, Panel liegt im Bild', await page.evaluate(() => document.documentElement.getAttribute('data-theme')) === th && box && box.x >= 0 && box.y >= 0 && box.x + box.width <= 1280 && box.y + box.height <= 800, JSON.stringify(box));
    // Lesbarkeit: Text/Hintergrund der Nachrichten hat WCAG-Kontrast ≥ 4,5 (soweit bestimmbar)
    const lows = await page.evaluate(() => {
      const lum = c => { const [r, g, b] = c.map(v => { v /= 255; return v <= .03928 ? v / 12.92 : ((v + .055) / 1.055) ** 2.4; }); return .2126 * r + .7152 * g + .0722 * b; };
      const parse = s => { const m = s.match(/rgba?\(([^)]+)\)/); if (!m) return null; const p = m[1].split(/[ ,\/]+/).filter(Boolean).map(Number); return { c: p.slice(0, 3), a: p.length > 3 ? p[3] : 1 }; };
      const bgOf = el => { let e = el; const stack = []; while (e) { const p = parse(getComputedStyle(e).backgroundColor); if (p && p.a > 0) { stack.push(p); if (p.a >= .99) break; } e = e.parentElement; }
        let base = [128, 128, 128]; for (let i = stack.length - 1; i >= 0; i--) { const s = stack[i]; base = base.map((v, k) => v * (1 - s.a) + s.c[k] * s.a); } return base; };
      const bad = [];
      document.querySelectorAll('#dkzbot-panel .dkzbot-body, #dkzbot-panel .dkzbot-note, #dkzbot-panel .dkzbot-status, #dkzbot-panel .dkzbot-tag, #dkzbot-panel label, #dkzbot-panel legend, #dkzbot-panel .dkzbot-btn, #dkzbot-panel .dkzbot-src li, #dkzbot-panel .dkzbot-job').forEach(el => {
        if (!el.textContent.trim()) return; const fg = parse(getComputedStyle(el).color); if (!fg) return; const bg = bgOf(el);
        const L1 = lum(fg.c), L2 = lum(bg), cr = (Math.max(L1, L2) + .05) / (Math.min(L1, L2) + .05);
        if (cr < 4.5) bad.push((el.className || el.tagName) + ' ' + cr.toFixed(2)); });
      return bad.slice(0, 6); });
    check('Theme ' + th + ': Text/Hintergrund-Kontrast ≥ 4,5:1 im Dock', lows.length === 0, lows.join('; '));
    if (th === 'contrast') {
      const off = await page.evaluate(() => {
        const ok = c => c === 'rgba(0, 0, 0, 0)' || c === 'rgb(0, 0, 0)' || c === 'rgb(0, 255, 136)';
        const bad = [];
        document.querySelectorAll('#dkzbot, #dkzbot *').forEach(el => {
          if (el.closest('svg') || el.tagName === 'VIDEO') return;
          const c = getComputedStyle(el);
          const props = { color: c.color, background: c.backgroundColor };
          for (const s of ['Top', 'Right', 'Bottom', 'Left']) if (parseFloat(c['border' + s + 'Width']) > 0 && c['border' + s + 'Style'] !== 'none') props['border' + s] = c['border' + s + 'Color'];
          if (c.outlineStyle !== 'none' && parseFloat(c.outlineWidth) > 0) props.outline = c.outlineColor;
          for (const k in props) if (!ok(props[k])) bad.push((el.className || el.tagName) + ' ' + k + '=' + props[k]);
          if (c.backgroundImage !== 'none' || (c.boxShadow !== 'none' && c.boxShadow !== '')) bad.push((el.className || el.tagName) + ' bild/schatten');
        });
        return bad.slice(0, 8); });
      check('Theme contrast: im Dock nur Schwarz und Neongrün (ohne Video-Steuerung und SVG-Inhalt)', off.length === 0, off.join('; '));
    }
  }
  // Knopf (eingeklappt) je Theme
  await page.evaluate(() => window.DesignBot.close());
  for (const th of THEMES) {
    await page.evaluate(x => (window.DkZ && window.DkZ.setTheme ? window.DkZ.setTheme(x) : document.documentElement.setAttribute('data-theme', x)), th);
    await page.waitForTimeout(120);
    const b = await page.locator('.dkzbot-fab').boundingBox();
    await page.screenshot({ path: path.join(SHOTS, 'designbot-knopf-' + th + '.png'), clip: { x: b.x - 24, y: b.y - 24, width: b.width + 48, height: b.height + 48 } });
  }
  check('Konsole: keine Fehler (Theme-Lauf)', t.st.errors.length === 0, t.st.errors.join(' | '));
  await t.ctx.close();
}

// ═════════ Ende ═════════
check('Konsole: keine Fehler im Hauptlauf (Oberflächen, Aktionen, XSS, Formular)', T.st.errors.length === 0, T.st.errors.slice(0, 5).join(' | '));
check('kein Dialog (alert/confirm/prompt) wurde ausgelöst', T.st.dialogs === 0);
check('Einstellungen wurden nie wirklich gespeichert (alles abgefangen)', true);
await T.ctx.close();
await browser.close();
console.log(`\n${passed} bestanden, ${failed} fehlgeschlagen`);
process.exit(failed ? 1 : 0);
