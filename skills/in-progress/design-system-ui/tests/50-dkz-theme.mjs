// DkZ-Theme-Test (Playwright). Aufruf:
//   node tests/50-dkz-theme.mjs <baseURL> <screenshotDir>
// Der Server läuft schon (eigene DKZ_DB, eigener Port). Der Test prüft für alle
// fünf Themes und alle Oberflächen des Menüs:
//   - keine Konsolenfehler, keine Anfragen nach außen (Google Fonts bleiben aus)
//   - Token (§1 des Vertrags) und --accent/--bg je Theme
//   - Fließtext mindestens 4.5:1 (WCAG selbst gerechnet)
//   - Theme contrast: alle berechneten Farben nur Schwarz und #00ff88
//   - Theme-Wechsel per Knopf und per Einstellung, Wahl bleibt nach Neuladen
//   - 320 px ohne waagerechten Überlauf
//   - roter Monitorrand / Aktivitätssignal in allen Themes (echter POST)
// Ein roter Test wird repariert, nicht abgeschwächt. Exitcode 1 bei Fehlern.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..');
const BASE = (process.argv[2] || 'http://127.0.0.1:8880').replace(/\/$/, '');
const SHOTS = path.resolve(process.argv[3] || path.join(HERE, '..', 'demo', 'shots', 'dkz-theme'));
fs.mkdirSync(SHOTS, { recursive: true });

let chromium;
try { ({ chromium } = await import('playwright')); }
catch { ({ chromium } = await import(path.join(ROOT, 'demo/node_modules/playwright/index.mjs'))); }
const exe = process.env.CHROMIUM_PATH || (fs.existsSync('/opt/pw-browsers/chromium') ? '/opt/pw-browsers/chromium' : undefined);

const THEMES = ['master', 'light', 'contrast', 'control', 'content'];
// Erwartete Kernwerte je Theme (CONTRACT §1), Farben als #rrggbb
const EXPECT = {
  master:   { accent: '#fa1e4e', bg: '#060608' },
  light:    { accent: '#fa1e4e', bg: '#f6f6f7' },
  contrast: { accent: '#00ff88', bg: '#000000' },
  control:  { accent: '#fa1e4e', bg: '#060608' },
  content:  { accent: '#fa1e4e', bg: '#060608' },
};
const REQUIRED_VIEWS = ['builder', 'chat', 'studio', 'svg', 'templates', 'systems', 'skills',
  'providers', 'stock', 'github', 'agents', 'debug', 'settings'];   // die 13 Oberflächen des Plans v7

let passed = 0; const failures = [];
const ok = (cond, name, detail = '') => {
  if (cond) { passed++; }
  else { failures.push(`${name}${detail ? ' — ' + detail : ''}`); console.log('  FAIL', name, detail); }
};
const norm = (s) => String(s).replace(/\s+/g, '').toLowerCase().replace(/(\D)0\./g, '$1.');
const hex = (rgb) => '#' + rgb.map((v) => Math.round(v).toString(16).padStart(2, '0')).join('');

// ───────────────────────── statische Prüfungen (Dateien) ─────────────────────────
const stripComments = (css) => css.replace(/\/\*[\s\S]*?\*\//g, '');
{
  const dkzJs = fs.readFileSync(path.join(ROOT, 'dkz.js'));
  ok(dkzJs.length < 18 * 1024, 'dkz.js unter 18 KB', `${dkzJs.length} Byte`);
  const styles = stripComments(fs.readFileSync(path.join(ROOT, 'styles.css'), 'utf8'));
  const hexOutsideFallback = styles.replace(/var\([^)]*\)/g, '').match(/#[0-9a-fA-F]{3,8}\b/g);
  ok(!hexOutsideFallback, 'styles.css: kein hartkodiertes Hex in Regeln', String(hexOutsideFallback));
  const rgbaRules = styles.replace(/var\([^)]*\)/g, '').match(/\brgba?\(/g);
  ok(!rgbaRules, 'styles.css: kein hartkodiertes rgb()/rgba()', String(rgbaRules));
  ok(!/prefers-color-scheme/.test(styles), 'styles.css: keine prefers-color-scheme-Weiche');
  ok(!/data-theme="(hell|dunkel)"/.test(styles), 'styles.css: keine alten Themes hell/dunkel');
  // dkz.css: Farbliterale nur in Token-Zeilen (--name: …) und Kommentaren
  const bad = stripComments(fs.readFileSync(path.join(ROOT, 'dkz.css'), 'utf8')).split('\n')
    .filter((l) => /#[0-9a-fA-F]{3,8}\b|\brgba?\(/.test(l) && !/^\s*--[\w-]+\s*:/.test(l));
  ok(bad.length === 0, 'dkz.css: Farbliterale nur in Token-Definitionen', bad.slice(0, 3).join(' | '));
  const app = fs.readFileSync(path.join(ROOT, 'app.js'), 'utf8');
  const jsHex = app.match(/['"`]#[0-9a-fA-F]{3,8}['"`]/g);
  ok(!jsHex, 'app.js: keine hartkodierten Hex-Farben', String(jsHex));
}

// §1-Tokenliste aus dem Vertrag lesen
const contract = fs.readFileSync(path.join(ROOT, 'docs/CONTRACT.md'), 'utf8');
const tokenBlock = contract.split('**Tokens (verbindlich):**')[1].split('```css')[1].split('```')[0];
const CONTRACT_TOKENS = {};
for (const m of tokenBlock.matchAll(/(--[\w-]+)\s*:\s*([^;]+);/g)) CONTRACT_TOKENS[m[1]] = m[2].trim();

// ───────────────────────── Browser ─────────────────────────
const browser = await chromium.launch({ executablePath: exe });
const consoleErrors = [];
const external = [];
const mkPage = async (width, height) => {
  const ctx = await browser.newContext({ viewport: { width, height } });
  const page = await ctx.newPage();
  page.on('pageerror', (e) => consoleErrors.push(`pageerror: ${e.message}`));
  page.on('console', (m) => { if (m.type() === 'error') consoleErrors.push(`console: ${m.text()}`); });
  page.on('request', (r) => { if (!r.url().startsWith(BASE) && !r.url().startsWith('data:') && !r.url().startsWith('blob:')) external.push(r.url()); });
  return { ctx, page };
};
const api = async (page, p, body) => page.evaluate(async ([u, b]) => (await fetch(u, b ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(b) } : undefined)).json(), [p, body]);
const clickTheme = (page, t) => page.click(`.theme-btn[data-theme="${t}"]`);
const showView = (page, v) => page.evaluate((n) => document.querySelector(`.drawer-item[data-view="${n}"]`).click(), v);
const settle = (page) => page.waitForTimeout(260);

// Im Browser ausgeführte Messungen ---------------------------------------------------
// Sichtbare Elemente, ohne Inhalte, die ein anderes Theme darstellen sollen
// (SVG/Canvas/Bilder/iframes sind Nutzerinhalt, keine Oberflächenfarben).
const MEASURE = () => {
  const lum = (c) => { const l = c.map((v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }); return 0.2126 * l[0] + 0.7152 * l[1] + 0.0722 * l[2]; };
  const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
  const parse = (s) => {
    let m = /^rgba?\(\s*([\d.]+)[ ,]+([\d.]+)[ ,]+([\d.]+)(?:[ ,/]+([\d.]+%?))?/.exec(s);
    if (m) { let a = m[4] === undefined ? 1 : (m[4].endsWith('%') ? parseFloat(m[4]) / 100 : +m[4]); return [+m[1], +m[2], +m[3], a]; }
    m = /^color\(srgb\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)(?:\s*\/\s*([\d.]+%?))?/.exec(s);
    if (m) { let a = m[4] === undefined ? 1 : (m[4].endsWith('%') ? parseFloat(m[4]) / 100 : +m[4]); return [m[1] * 255, m[2] * 255, m[3] * 255, a]; }
    return null;
  };
  const SKIP = new Set(['svg', 'canvas', 'img', 'video', 'iframe', 'script', 'style', 'head', 'title', 'meta', 'link', 'option', 'noscript']);
  const inSkipped = (el) => { for (let e = el; e; e = e.parentElement) if (SKIP.has(e.tagName.toLowerCase()) && e.tagName.toLowerCase() !== 'html') return true; return false; };
  const visible = (el) => {
    if (inSkipped(el)) return false;
    const r = el.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) return false;
    for (let e = el; e && e !== document.documentElement; e = e.parentElement) {
      const cs = getComputedStyle(e);
      if (cs.display === 'none' || cs.visibility === 'hidden' || +cs.opacity === 0) return false;
    }
    return true;
  };
  const sel = (el) => el.tagName.toLowerCase() + (el.id ? '#' + el.id : '') + (el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/).slice(0, 2).join('.') : '');
  const effBg = (el) => {   // Hintergrund hinter dem Text: Schichten von innen nach außen mischen
    const layers = [];
    for (let e = el; e; e = e.parentElement) {
      const c = parse(getComputedStyle(e).backgroundColor);
      if (c && c[3] > 0) { layers.push(c); if (c[3] >= 1) break; }
    }
    let base = [0, 0, 0];
    const rootBg = parse(getComputedStyle(document.documentElement).backgroundColor);
    if (rootBg && rootBg[3] > 0) base = rootBg.slice(0, 3);
    for (let i = layers.length - 1; i >= 0; i--) {
      const [r, g, b, a] = layers[i];
      base = [r * a + base[0] * (1 - a), g * a + base[1] * (1 - a), b * a + base[2] * (1 - a)];
    }
    return base;
  };
  const effOpacity = (el) => { let o = 1; for (let e = el; e; e = e.parentElement) o *= +getComputedStyle(e).opacity; return o; };
  const all = [...document.body.querySelectorAll('*')].filter(visible);
  // 1) Fließtext-Kontrast
  const low = [];
  let texts = 0;
  for (const el of all) {
    const own = [...el.childNodes].some((n) => n.nodeType === 3 && n.textContent.trim());
    if (!own || el.disabled || el.closest('[disabled],.info-btn,#agentGlow')) continue;
    if (effOpacity(el) < 0.99) continue;
    const cs = getComputedStyle(el);
    const fg = parse(cs.color); if (!fg) continue;
    const bg = effBg(el);
    const a = fg[3];
    const fgc = [fg[0] * a + bg[0] * (1 - a), fg[1] * a + bg[1] * (1 - a), fg[2] * a + bg[2] * (1 - a)];
    const px = parseFloat(cs.fontSize), bold = +cs.fontWeight >= 700;
    const need = (px >= 24 || (px >= 18.66 && bold)) ? 3 : 4.5;
    texts++;
    const r = ratio(fgc, bg);
    if (r < need) low.push(`${sel(el)} ${r.toFixed(2)}<${need} "${el.textContent.trim().slice(0, 24)}"`);
  }
  // 2) Palette (für contrast): jede berechnete Farbe
  const bad = [];
  const props = ['color', 'backgroundColor', 'outlineColor', 'borderTopColor', 'borderRightColor', 'borderBottomColor', 'borderLeftColor'];
  for (const el of [document.documentElement, document.body, ...all]) {
    const cs = getComputedStyle(el);
    for (const p of props) {
      const c = parse(cs[p]);
      if (!c || c[3] === 0) continue;                      // transparent ausgenommen
      const okc = c[3] === 1 && ((c[0] === 0 && c[1] === 0 && c[2] === 0) || (c[0] === 0 && c[1] === 255 && c[2] === 136));
      if (!okc) bad.push(`${sel(el)} ${p}=${cs[p]}`);
    }
  }
  // 3) Überlauf
  const over = [];
  const W = document.documentElement.clientWidth;
  for (const el of all) {
    const r = el.getBoundingClientRect();
    if (r.right <= W + 1) continue;
    let clipped = false;
    for (let e = el.parentElement; e && e !== document.body; e = e.parentElement) {
      const o = getComputedStyle(e).overflowX;
      if (o !== 'visible') { clipped = true; break; }
    }
    if (!clipped) over.push(`${sel(el)} right=${Math.round(r.right)}`);
  }
  return { low, texts, bad, over, scrollW: document.documentElement.scrollWidth, clientW: W, nodes: all.length };
};

// ───────────────────────── 1) Start, Token, Knöpfe, API ─────────────────────────
console.log('== DkZ-Theme-Test gegen', BASE);
let { ctx, page } = await mkPage(1440, 900);
await page.route(/fonts\.(googleapis|gstatic)\.com/, (r) => r.fulfill({ status: 200, contentType: 'text/css', body: '' }));
await page.goto(BASE, { waitUntil: 'networkidle' });
await page.evaluate(() => { try { localStorage.clear(); } catch (e) { /* */ } });
await api(page, '/api/settings/reset', {});
await page.reload({ waitUntil: 'networkidle' });
await settle(page);

const initial = await page.evaluate(() => document.documentElement.getAttribute('data-theme'));
ok(initial === 'master', 'Standard-Theme ist master', initial);
const viewsInMenu = await page.$$eval('.drawer-item', (els) => els.map((e) => e.dataset.view));
for (const v of REQUIRED_VIEWS) ok(viewsInMenu.includes(v), `Oberfläche im Menü: ${v}`);
console.log('  Oberflächen im Menü:', viewsInMenu.length, viewsInMenu.join(' '));

// window.DkZ-Schnittstelle
const dkz = await page.evaluate(() => {
  const D = window.DkZ;
  if (!D) return null;
  const e = D.esc('<img src=x onerror="a(\'b\')">&');
  const t = D.setTheme('gibt-es-nicht');
  D.setTheme('master');
  return { keys: Object.keys(D), themes: D.THEMES, esc: e, bogus: t, escNull: D.esc(null), tok: D.token('--accent') };
});
ok(!!dkz, 'window.DkZ vorhanden');
if (dkz) {
  for (const k of ['esc', 'setTheme', 'theme', 'THEMES']) ok(dkz.keys.includes(k), `DkZ.${k} vorhanden`);
  ok(JSON.stringify(dkz.themes) === JSON.stringify(THEMES), 'DkZ.THEMES = fünf Themes', JSON.stringify(dkz.themes));
  ok(!/[<>"']/.test(dkz.esc), 'DkZ.esc entschärft < > " \'', dkz.esc);
  ok(dkz.bogus === 'master', 'DkZ.setTheme(unbekannt) fällt auf master', dkz.bogus);
  ok(dkz.escNull === '', 'DkZ.esc(null) = ""');
  ok(norm(dkz.tok) === '#fa1e4e', 'DkZ.token(--accent) liest den Wert', dkz.tok);
}

// Alle §1-Token im Theme master exakt wie im Vertrag
const masterTokens = await page.evaluate((names) => Object.fromEntries(names.map((n) => [n, getComputedStyle(document.documentElement).getPropertyValue(n)])), Object.keys(CONTRACT_TOKENS));
for (const [k, v] of Object.entries(CONTRACT_TOKENS)) ok(norm(masterTokens[k]) === norm(v), `Token ${k} = Vertrag (master)`, `${masterTokens[k].trim()} ≠ ${v}`);
console.log('  Vertrags-Token geprüft:', Object.keys(CONTRACT_TOKENS).length);

// Theme-Leiste: fünf Knöpfe, richtige Reihenfolge und Symbole
const barBtns = await page.$$eval('.theme-bar .theme-btn', (b) => b.map((x) => ({ t: x.dataset.theme, ico: x.querySelector('.theme-ico')?.textContent, label: x.title })));
ok(barBtns.length === 5, 'Theme-Leiste hat fünf Knöpfe', String(barBtns.length));
ok(barBtns.map((b) => b.ico).join('') === '🔴⚪🟢🟠🟣', 'Symbole Master 🔴 Hell ⚪ Kontrast 🟢 Control 🟠 Content 🟣', barBtns.map((b) => b.ico).join(''));
ok(await page.$eval('header .theme-bar', () => true).catch(() => false), 'Theme-Leiste liegt in der Kopfzeile');

// Keine Anfragen an Google Fonts im Standardbetrieb
ok(external.length === 0, 'keine Anfragen nach außen (Standard)', external.join(', '));
ok(!(await page.$('#dkz-fonts')), 'DkZ-Schriften standardmäßig aus (kein Link)');

// Spezielle Theme-Werte aus dem Vertrag
for (const t of THEMES) {
  await clickTheme(page, t);
  const v = await page.evaluate(() => {
    const cs = getComputedStyle(document.documentElement);
    const g = (n) => cs.getPropertyValue(n).trim();
    return { theme: document.documentElement.dataset.theme, accent: g('--accent'), bg: g('--bg'), text: g('--text'), card: g('--bg-card'), border: g('--border'), borderA: g('--border-accent'), font: g('--font-main'), mono: g('--font-mono'), pill: g('--radius-pill'), stored: localStorage.getItem('dkz-theme'),
      bodyFont: getComputedStyle(document.body).fontFamily, active: [...document.querySelectorAll('.theme-btn.active')].map((b) => b.dataset.theme) };
  });
  ok(v.theme === t, `Knopf ${t} setzt data-theme`, v.theme);
  ok(v.stored === t, `Knopf ${t} speichert localStorage['dkz-theme']`, v.stored);
  ok(v.active.length === 1 && v.active[0] === t, `Knopf ${t} ist als einziger aktiv`, v.active.join(','));
  ok(norm(v.accent) === EXPECT[t].accent.replace('#', '#'), `${t}: --accent = ${EXPECT[t].accent}`, v.accent);
  const bgHex = hex((await page.evaluate((c) => { const s = document.createElement('i'); s.style.color = c; document.body.appendChild(s); const x = getComputedStyle(s).color; s.remove(); return x.match(/[\d.]+/g).slice(0, 3).map(Number); }, v.bg)));
  ok(bgHex === EXPECT[t].bg, `${t}: --bg = ${EXPECT[t].bg}`, v.bg);
  if (t === 'light') { ok(norm(v.text) === '#0e0e10' && norm(v.card) === '#ffffff', 'light: --text #0e0e10, --bg-card #ffffff', `${v.text} ${v.card}`); ok(norm(v.border) === norm('1px solid rgba(0,0,0,.08)'), 'light: Border 1px solid rgba(0,0,0,.08)', v.border); }
  if (t === 'contrast') { ok(norm(v.text) === '#00ff88', 'contrast: --text #00ff88', v.text); ok(norm(v.border) === norm('1px solid #00ff88'), 'contrast: Border 1px solid #00ff88', v.border); ok(norm(v.borderA) === norm('1px solid #00ff88'), 'contrast: Border-Akzent 1px solid #00ff88', v.borderA); }
  if (t === 'control') { ok(/Archivo Expanded/.test(v.font), 'control: Font Archivo Expanded', v.font); ok(norm(v.border) === norm('1px solid rgba(250,30,78,.3)'), 'control: Border 1px solid rgba(250,30,78,.3)', v.border); }
  if (t === 'content') { ok(/Syne/.test(v.font) && /Space Mono/.test(v.mono), 'content: Syne + Space Mono', `${v.font} / ${v.mono}`); }
  if (t !== 'master') ok(!/Inter/.test(v.font) || t === 'light' || t === 'contrast', `${t}: eigene Schrift`, v.font);
}
// Pill-Form im Theme content, geometrische clip-paths + Scanlines im Theme control
await clickTheme(page, 'content');
ok(await page.$eval('.btn', (b) => parseFloat(getComputedStyle(b).borderTopLeftRadius) >= 40), 'content: Knöpfe sind Pills (border-radius 50px)');
await clickTheme(page, 'control');
ok(await page.$eval('.btn', (b) => getComputedStyle(b).clipPath !== 'none'), 'control: Knöpfe haben clip-path');
ok(await page.evaluate(() => getComputedStyle(document.body, '::after').backgroundImage.includes('repeating-linear-gradient')), 'control: Scanline-Overlay vorhanden');
await clickTheme(page, 'master');
ok(await page.evaluate(() => getComputedStyle(document.body, '::after').backgroundImage === 'none'), 'master: kein Scanline-Overlay');

// ───────────────────────── 2) Alle Themes × alle Oberflächen ─────────────────────────
const matrixIssues = { low: 0, bad: 0 };
for (const t of THEMES) {
  await clickTheme(page, t);
  for (const v of viewsInMenu) {
    await showView(page, v);
    await settle(page);
    const m = await page.evaluate(MEASURE);
    ok(m.low.length === 0, `Kontrast ≥ 4.5:1 [${t}/${v}]`, `${m.low.length} Stellen: ${m.low.slice(0, 4).join(' | ')}`);
    if (m.low.length) matrixIssues.low += m.low.length;
    if (t === 'contrast') {
      ok(m.bad.length === 0, `Palette nur Schwarz/#00ff88 [contrast/${v}]`, `${m.bad.length} Stellen: ${m.bad.slice(0, 4).join(' | ')}`);
      if (m.bad.length) matrixIssues.bad += m.bad.length;
    }
    ok(m.texts > 0 || v === 'templates' || v === 'systems' || v === 'svg', `Oberfläche ${v} zeigt Text [${t}]`, `${m.texts} Textknoten`);
    await page.screenshot({ path: path.join(SHOTS, `${t}-${v}-1440.png`) });
  }
}
console.log('  Matrix fertig: Kontrastverstöße', matrixIssues.low, ' Paletteverstöße', matrixIssues.bad);
await showView(page, 'builder');

// Hamburger-Menü echt bedienen (nicht nur per Skript)
await page.click('#hamburger');
await page.waitForSelector('body.drawer-open');
await page.waitForTimeout(300);
await page.screenshot({ path: path.join(SHOTS, 'contrast-drawer-1440.png') });   // aktuelles Theme: master
await page.click('.drawer-item[data-view="settings"]');
ok(await page.evaluate(() => !document.getElementById('view-settings').classList.contains('hidden')), 'Hamburger → Einstellungen öffnet die Oberfläche');

// ───────────────────────── 3) Theme-Wechsel per Einstellung, Persistenz ─────────────────────────
await page.waitForSelector('select[data-setting="theme"]');
const choices = await page.$$eval('select[data-setting="theme"] option', (o) => o.map((x) => x.value));
ok(JSON.stringify(choices) === JSON.stringify(THEMES), 'Einstellung theme bietet die fünf DkZ-Themes', choices.join(','));
for (const t of ['light', 'contrast', 'control', 'content', 'master']) {
  await page.selectOption('select[data-setting="theme"]', t);
  await page.waitForTimeout(120);
  const now = await page.evaluate(() => ({ t: document.documentElement.dataset.theme, a: [...document.querySelectorAll('.theme-btn.active')].map((b) => b.dataset.theme).join() }));
  ok(now.t === t, `Einstellung → Theme ${t}`, now.t);
  ok(now.a === t, `Einstellung → Theme-Leiste folgt (${t})`, now.a);
}
await page.waitForTimeout(500);   // Speichern wartet kurz (Debounce)
// Knopf → Einstellung wird gespiegelt und serverseitig gesichert
await clickTheme(page, 'contrast');
await page.waitForTimeout(700);
let srv = await api(page, '/api/settings');
ok(srv.settings.theme === 'contrast', 'Theme-Knopf wird als Einstellung gesichert (Server)', srv.settings.theme);
ok(await page.$eval('select[data-setting="theme"]', (s) => s.value) === 'contrast', 'Theme-Knopf aktualisiert das Auswahlfeld der Einstellung');
// Neuladen: Wahl bleibt
await page.reload({ waitUntil: 'networkidle' }); await settle(page);
ok(await page.evaluate(() => document.documentElement.dataset.theme) === 'contrast', 'Wahl bleibt nach Neuladen (contrast)');
await clickTheme(page, 'content');
await page.reload({ waitUntil: 'networkidle' }); await settle(page);
ok(await page.evaluate(() => document.documentElement.dataset.theme) === 'content', 'Wahl bleibt nach Neuladen (content)');
// Ohne localStorage gilt die Server-Einstellung
await api(page, '/api/settings', { settings: { theme: 'control' } });
await page.evaluate(() => localStorage.removeItem('dkz-theme'));
await page.reload({ waitUntil: 'networkidle' }); await settle(page);
ok(await page.evaluate(() => document.documentElement.dataset.theme) === 'control', 'Ohne lokale Wahl gilt die Server-Einstellung (control)');
// Alte Werte (auto/hell/dunkel) fallen auf master zurück
await api(page, '/api/settings', { settings: { theme: 'dunkel' } });
srv = await api(page, '/api/settings');
ok(srv.settings.theme === 'master', 'Alter Wert „dunkel" wird zu master', srv.settings.theme);

// Akzentfarbe: Regler setzen --accent, aber nicht im Theme contrast
await page.evaluate(() => localStorage.removeItem('dkz-theme'));
await api(page, '/api/settings/reset', {});
await page.reload({ waitUntil: 'networkidle' }); await settle(page);
const accentOf = () => page.evaluate(() => getComputedStyle(document.documentElement).getPropertyValue('--accent').trim());
ok(norm(await accentOf()) === '#fa1e4e', 'Standard-Regler lassen den Theme-Akzent unberührt');
await showView(page, 'settings');
await page.evaluate(() => { const r = document.querySelector('[data-setting="accent_hue"]'); r.value = 120; r.dispatchEvent(new Event('input', { bubbles: true })); });
await page.waitForTimeout(100);
ok(/^hsl\(120/.test(await accentOf()), 'accent_hue ändert --accent', await accentOf());
const onAcc = await page.evaluate(() => { const D = window.DkZ; const a = D.rgb(D.token('--accent')), o = D.rgb(D.token('--on-accent')); const L = (c) => { const l = c.map((v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }); return 0.2126 * l[0] + 0.7152 * l[1] + 0.0722 * l[2]; }; const x = L(a), y = L(o); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); });
ok(onAcc >= 4.5, 'Text auf eigener Akzentfarbe ≥ 4.5:1', onAcc.toFixed(2));
await clickTheme(page, 'contrast');
await page.waitForTimeout(350);   // Übergänge (transition) abwarten
ok(norm(await accentOf()) === '#00ff88', 'Theme contrast ignoriert die Akzent-Regler (#00ff88)', await accentOf());
const cm = await page.evaluate(MEASURE);
ok(cm.bad.length === 0, 'contrast mit verstellten Reglern: Palette bleibt Schwarz/#00ff88', cm.bad.slice(0, 3).join(' | '));
await api(page, '/api/settings/reset', {});

// DkZ-Schriften: standardmäßig aus, schaltbar über dkz_fonts
await page.reload({ waitUntil: 'networkidle' }); await settle(page);
await showView(page, 'settings');
ok(await page.$eval('[data-setting="dkz_fonts"]', (c) => c.type === 'checkbox' && !c.checked), 'Einstellung dkz_fonts existiert und ist aus');
await page.check('[data-setting="dkz_fonts"]');
await page.waitForTimeout(150);
const fl = await page.$eval('#dkz-fonts', (l) => l.href).catch(() => null);
ok(fl && /fonts\.googleapis\.com/.test(fl) && /Inter/.test(fl) && /JetBrains/.test(fl) && /Archivo/.test(fl) && /Syne/.test(fl) && /Space\+Mono/.test(fl), 'dkz_fonts an: Link auf Inter, JetBrains Mono, Archivo, Syne, Space Mono', String(fl));
await page.uncheck('[data-setting="dkz_fonts"]');
await page.waitForTimeout(150);
ok(!(await page.$('#dkz-fonts')), 'dkz_fonts aus: Link wieder entfernt');
await api(page, '/api/settings/reset', {});
await ctx.close();

// ───────────────────────── 4) 320 px: kein waagerechter Überlauf ─────────────────────────
({ ctx, page } = await mkPage(320, 640));
await page.goto(BASE, { waitUntil: 'networkidle' });
await page.evaluate(() => { try { localStorage.clear(); } catch (e) { /* */ } });
let narrowIssues = 0;
for (const t of THEMES) {
  await clickTheme(page, t);
  for (const v of viewsInMenu) {
    await showView(page, v);
    await settle(page);
    const m = await page.evaluate(MEASURE);
    const noScroll = m.scrollW <= m.clientW;
    ok(noScroll, `320 px: kein waagerechter Überlauf [${t}/${v}]`, `scrollWidth ${m.scrollW} > ${m.clientW}`);
    ok(m.over.length === 0, `320 px: nichts ragt rechts heraus [${t}/${v}]`, m.over.slice(0, 4).join(' | '));
    if (!noScroll || m.over.length) narrowIssues++;
    if (['builder', 'settings', 'providers'].includes(v) || t === 'contrast') await page.screenshot({ path: path.join(SHOTS, `${t}-${v}-320.png`) });
  }
}
console.log('  320 px: Oberflächen mit Überlauf:', narrowIssues);
await ctx.close();

// ───────────────────────── 5) Agentensignal in allen Themes (echter POST) ─────────────────────────
({ ctx, page } = await mkPage(1200, 800));
await page.goto(BASE, { waitUntil: 'networkidle' });
await page.evaluate(() => { try { localStorage.clear(); } catch (e) { /* */ } });
for (const t of THEMES) {
  await clickTheme(page, t);
  await api(page, '/api/agent/activity', { actor: 'dkz-theme-test', action: 'prüfe', target: '#newWorkflow', detail: `Theme ${t}`, state: 'working' });
  await page.waitForSelector('body.agent-active', { timeout: 4000 }).catch(() => {});
  await page.waitForTimeout(700);
  const g = await page.evaluate(() => {
    const el = document.getElementById('agentGlow'), cs = getComputedStyle(el);
    const bar = getComputedStyle(document.getElementById('activityBar'));
    const dot = getComputedStyle(document.querySelector('.activity-dot'));
    const flash = document.querySelector('.agent-flash');
    return { active: document.body.classList.contains('agent-active'), opacity: +cs.opacity, shadow: cs.boxShadow, barDisplay: bar.display, barBorder: bar.borderBottomColor, dot: dot.backgroundColor, flash: flash ? getComputedStyle(flash).outlineColor : null, z: cs.zIndex };
  });
  ok(g.active, `Agentensignal: body.agent-active nach POST [${t}]`);
  ok(g.opacity > 0.5, `Monitorrand sichtbar (opacity ${g.opacity.toFixed(2)}) [${t}]`);
  ok(g.barDisplay !== 'none', `Aktivitätsleiste „KI arbeitet" sichtbar [${t}]`, g.barDisplay);
  const cols = [...g.shadow.matchAll(/rgba?\(([\d., ]+)\)/g)].map((m) => m[1].split(',').map((x) => +x));
  ok(cols.length >= 1, `Monitorrand hat Farbe [${t}]`, g.shadow);
  if (t === 'contrast') {
    ok(cols.every((c) => (c[0] === 0 && c[1] === 255 && c[2] === 136) || (c[0] === 0 && c[1] === 0 && c[2] === 0)) && !/rgba\(/.test(g.shadow), 'contrast: Monitorrand nur #00ff88', g.shadow);
    ok(/rgb\(0, 255, 136\)/.test(g.barBorder) && /rgb\(0, 255, 136\)/.test(g.dot), 'contrast: Aktivitätsleiste/Punkt nur #00ff88', `${g.barBorder} ${g.dot}`);
    ok(!g.flash || g.flash === 'rgb(0, 255, 136)', 'contrast: Aktions-Blitz nur #00ff88', String(g.flash));
  } else {
    ok(cols.some((c) => c[0] > 180 && c[1] < 90), `Monitorrand ist rot [${t}]`, g.shadow);
  }
  await page.screenshot({ path: path.join(SHOTS, `${t}-agent-active.png`) });
}
await ctx.close();
await browser.close();

// ───────────────────────── Ergebnis ─────────────────────────
ok(consoleErrors.length === 0, 'keine Konsolenfehler im gesamten Lauf', consoleErrors.slice(0, 5).join(' | '));
ok(external.length === 0 || external.every((u) => /fonts\.(googleapis|gstatic)\.com/.test(u)), 'nur die erlaubte Fonts-Anfrage verlässt den Rechner (und nur mit dkz_fonts)', external.join(', '));
console.log(`\n${passed} bestanden, ${failures.length} fehlgeschlagen`);
if (failures.length) { console.log('Fehlschläge:\n - ' + failures.slice(0, 60).join('\n - ')); process.exit(1); }
