/* designbot.js — Design-Assistent-Dock (Alt+D). Fremdtext nur per textContent, SVG nur nach SvgKit.sanitize. */
(function () {
'use strict';
if (window.DesignBot) return;

const PEND = '— ausstehend —', KEY = 'dkz-designbot-open';
const THEMES = ['master', 'light', 'contrast', 'control', 'content'];
const COMMANDS = { execute: 'executeWorkflow', validate: 'validateWorkflow', new: 'newWorkflow', clear: 'newWorkflow' };
const WELCOME = 'Frag mich nach Kontrast, Farben oder Typo. Auch: „Wechsle ins Theme contrast“.';
const S = { open: false, busy: false, turns: [], seq: 0 };
let root, fab, panel, log, input, send, statusEl, badge;

function h(tag, cls, text, attrs) {
	const e = document.createElement(tag);
	if (cls) e.className = cls.replace(/(^| )/g, '$1dkzbot-');
	if (text != null) e.textContent = String(text);
	for (const k in attrs || {}) e.setAttribute(k, attrs[k]);
	return e;
}
const clip = (v, n) => String(v == null ? '—' : v).slice(0, n || 60);
const own = (o, k) => !!o && typeof o === 'object' && Object.prototype.hasOwnProperty.call(o, k);
const num = (v, d) => (typeof v === 'number' && isFinite(v) ? v : d);
const app = () => window.app || null;
const isFn = f => typeof f === 'function';
const liveText = (live, model) => (live ? 'live · ' + clip(model || PEND) : 'lokal');
const errText = r => clip((r.error || r.reason || r.hint || PEND) + (r.needs ? ' (benötigt: ' + r.needs + ')' : ''), 300);

async function api(path, opt) {
	opt = opt || {};
	const ctl = new AbortController(), t = setTimeout(() => ctl.abort(), opt.timeout || 60000);
	try {
		const r = await fetch(path, { signal: ctl.signal, ...(opt.body ? { method: 'POST',
			headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(opt.body) } : {}) });
		const j = await r.json().catch(() => null);
		if (!j || typeof j !== 'object') return { ok: false, error: 'Antwort ohne JSON (HTTP ' + r.status + ')' };
		return r.ok || j.ok != null ? j : { ...j, ok: false };
	} catch (e) {
		return { ok: false, network: true, error: e && e.name === 'AbortError' ? 'Zeitüberschreitung' : 'Server nicht erreichbar' };
	} finally { clearTimeout(t); }
}

/* Text -> Absätze und "- "-Listen, nur per createElement/textContent */
function renderText(box, text) {
	let ul = null, p = null;
	String(text == null ? '' : text).slice(0, 8000).split(/\r?\n/).forEach(l => {
		const m = /^\s*[-*•]\s+(.*)$/.exec(l);
		if (m) { p = null; if (!ul) box.appendChild(ul = h('ul')); ul.appendChild(h('li', null, m[1])); }
		else if (!l.trim()) ul = p = null;
		else { ul = null; if (p) p.textContent += '\n' + l; else box.appendChild(p = h('p', null, l)); }
	});
}

const down = () => { log.scrollTop = log.scrollHeight; };
function addMsg(role, text, meta) {
	meta = meta && typeof meta === 'object' ? meta : {};
	const m = h('div', 'msg', null, { 'data-role': role }), body = h('div', 'body'), info = h('div', 'meta');
	const src = Array.isArray(meta.sources) ? meta.sources.slice(0, 6) : [];
	renderText(body, text);
	m.appendChild(body);
	if (role === 'assistant' && typeof meta.live === 'boolean') info.appendChild(h('span', 'tag', liveText(meta.live, meta.model), { 'data-live': String(meta.live) }));
	if (role === 'assistant' && src.length) {
		const ul = info.appendChild(h('ul', 'src', null, { 'aria-label': 'Quellen' }));
		src.forEach(s => ul.appendChild(h('li', null, clip(s && typeof s === 'object' ? s.title || s.id : s, 80))));
	}
	m.append(info, h('div', 'actions'));
	log.appendChild(m);
	down();
	return m;
}
const put = (m, el) => { m.querySelector('.dkzbot-actions').appendChild(el); down(); return el; };
const note = (m, text, kind) => put(m, h('div', 'note', text, { 'data-kind': kind || 'ok' }));
const ign = (m, what) => note(m, what + ' — ignoriert.', 'warn');
const bad = (m, a) => ign(m, '«' + clip(a.type, 20) + ' ' + clip(a.name || a.view || a.id || a.job_id, 30) + '» ungültig oder nicht verfügbar');

function setStatus(state, text, tag, live) {
	statusEl.setAttribute('data-state', state);
	statusEl.textContent = text;
	badge.textContent = tag;
	badge.setAttribute('data-live', String(!!live));
}
async function loadStatus() {
	const r = await api('/api/bot/status', { timeout: 8000 });
	if (!r.ok) return setStatus('red', 'Assistent: ' + (r.network ? 'API nicht erreichbar ' + PEND : errText(r)), 'offline');
	const kb = r.kb || {}, llm = r.llm || {}, kbB = clip(kb.backend || PEND, 20), stB = clip((r.store || {}).backend || PEND, 20);
	setStatus(kbB === PEND || stB === PEND ? 'yellow' : 'green', 'Wissensbasis: ' + (kb.docs != null ? kb.docs + ' Einträge' : PEND) +
		' (' + kbB + ', ' + stB + ') · ' + (llm.live ? 'Modell ' + clip(llm.model || PEND) : 'kein Modell'), liveText(llm.live, llm.model), llm.live);
}
async function loadHistory() {
	const r = await api('/api/bot/history?limit=50', { timeout: 8000 }), items = r.ok && Array.isArray(r.items) ? r.items : [];
	log.textContent = '';
	S.turns = [];
	items.forEach(it => {
		if (!it || !/^(user|assistant)$/.test(it.role) || typeof it.content !== 'string') return;
		addMsg(it.role, it.content, it.meta);
		S.turns.push({ role: it.role, content: it.content });
	});
	if (!r.ok) addMsg('system', 'Verlauf: ' + PEND + ' (' + errText(r) + ')');
	else if (!items.length) addMsg('system', WELCOME);
}

const showView = v => app() && isFn(app().showView) ? app().showView(v)
	: document.querySelectorAll('.view').forEach(e => e.classList.toggle('hidden', e.id !== 'view-' + v));
const ACTIONS = {
	svg(a, m) {
		if (typeof a.svg !== 'string' || !a.svg.trim()) return ign(m, 'SVG ohne Bild');
		const box = h('figure', 'svg'), kit = window.SvgKit;
		let shown = false;
		try {
			const res = kit.sanitize(a.svg), doc = new DOMParser().parseFromString(String(res.svg || res), 'image/svg+xml');
			if (doc.documentElement.localName === 'svg' && !doc.querySelector('parsererror')) {
				box.appendChild(document.importNode(doc.documentElement, true));
				shown = true;
				if (res.removed && res.removed.length) note(m, 'Entschärft: ' + res.removed.length + ' Teile entfernt.', 'warn');
			}
		} catch (_) {}
		if (!shown) {
			box.appendChild(h('pre', 'code', a.svg.slice(0, 4000)));
			note(m, 'SVG nur als Text' + (kit ? '.' : ' (SvgKit fehlt ' + PEND + ').'), 'warn');
		}
		put(m, box);
		const st = window.SvgStudio;
		if (shown && st && isFn(st.load) && a.spec && typeof a.spec === 'object')
			put(m, h('button', 'btn', 'Im Studio öffnen', { type: 'button' })).addEventListener('click', () => {
				try { showView('svg'); st.load(a.spec); } catch (e) { note(m, 'Studio: ' + clip(e && e.message), 'err'); }
			});
	},
	theme(a, m) {
		const D = window.DkZ, list = D && Array.isArray(D.THEMES) && D.THEMES.length ? D.THEMES : THEMES;
		if (list.indexOf(a.name) < 0) return bad(m, a);
		if (D && isFn(D.setTheme)) D.setTheme(a.name);
		else document.documentElement.setAttribute('data-theme', a.name);
		note(m, 'Theme gewechselt: ' + a.name);
	},
	async setting(a, m) {
		const ap = app(), s = ap && ap.settings, k = a.name;
		if (typeof k !== 'string' || !own(s, k)) return bad(m, a);
		const c = document.querySelector('[data-setting="' + CSS.escape(k) + '"]'), t = typeof s[k];
		let v = a.value;
		if (t === 'number' && typeof v === 'string' && v.trim()) v = Number(v);
		const ok = typeof v === t && (t === 'number' ? isFinite(v) && (!c || (v >= +c.min && v <= +c.max))
			: t === 'string' ? v.length < 200 && (!c || !c.options || [...c.options].some(o => o.value === v)) : true);
		if (!ok) return ign(m, 'Wert für «' + k + '» ungültig oder außerhalb des Bereichs');
		s[k] = v;
		if (isFn(ap.applySettings)) ap.applySettings();
		if (isFn(ap.saveSettings)) ap.saveSettings();
		note(m, 'Einstellung ' + k + ' = ' + clip(v, 40));
	},
	pattern(a, m) {
		const ap = app(), before = ap && ap.workflow && ap.workflow.nodes;
		if (typeof a.name !== 'string' || !/^[\w.-]{1,60}$/.test(a.name) || !ap || !isFn(ap.loadPattern)) return bad(m, a);
		ap.loadPattern(a.name);
		if (ap.workflow && ap.workflow.nodes !== before) { showView('builder'); note(m, 'Muster geladen: ' + a.name); }
		else bad(m, a);
	},
	command(a, m) {
		const ap = app(), fn = own(COMMANDS, a.name) && ap && ap[COMMANDS[a.name]];
		if (!isFn(fn)) return bad(m, a);
		fn.call(ap);
		note(m, 'Befehl ausgeführt: ' + a.name);
	},
	open_view(a, m) {
		if (typeof a.view !== 'string' || !document.getElementById('view-' + a.view)) return bad(m, a);
		showView(a.view);
		note(m, 'Oberfläche geöffnet: ' + a.view);
	},
	open_artifact(a, m) {
		if (typeof a.id !== 'string' || !/^[\w.:-]{1,80}$/.test(a.id)) return bad(m, a);
		const ar = window.Artifacts;
		if (ar && isFn(ar.open)) { ar.open(a.id); note(m, 'Artefakt geöffnet: ' + a.id); }
		else { showView('templates'); note(m, 'Vorschau ' + PEND + ', Vorlagen geöffnet.', 'warn'); }
	},
	async video_request(a, m) {
		if (!a.source || typeof a.source !== 'object') return bad(m, a);
		const body = { source: a.source, preset: String(a.preset || 'svg-reveal').slice(0, 40) };
			const r = await api('/api/video/render', { body });
		if (!r.ok || !r.job_id) return note(m, 'Video nicht gestartet: ' + errText(r), 'err');
		pollJob(String(r.job_id), m);
	},
	video: (a, m) => pollJob(a.job_id == null ? '' : String(a.job_id), m),
	form: renderForm
};
async function runActions(list, m) {
	for (const a of Array.isArray(list) ? list.slice(0, 8) : []) {
		const type = a && typeof a === 'object' ? a.type : null;
		if (typeof type !== 'string' || !own(ACTIONS, type)) { note(m, 'Unbekannte Aktion «' + clip(type, 30) + '» ignoriert.', 'warn'); continue; }
		try { await ACTIONS[type](a, m); } catch (e) { note(m, 'Aktion «' + type + '» fehlgeschlagen: ' + clip(e && e.message, 120), 'err'); }
	}
}

function pollJob(id, m) {
	if (!/^[\w.-]{1,80}$/.test(id)) return bad(m, { type: 'video', id });
	const card = h('div', 'job'), txt = h('div'), bar = h('progress', null, null, { max: '1', value: '0', 'aria-label': 'Fortschritt' });
	const say = (t, kind) => { txt.textContent = 'Video ' + id + ': ' + t; txt.setAttribute('data-kind', kind || 'info'); };
	const end = (t, kind) => { say(t, kind); bar.remove(); };
	let n = 0;
	say('wartet …');
	card.append(txt, bar);
	put(m, card);
	const tick = async () => {
		if (!card.isConnected) return;
		if (++n > 450) return end('Abfrage nach 15 min abgebrochen.', 'warn');
		const r = await api('/api/video/jobs/' + encodeURIComponent(id), { timeout: 15000 });
		if (!card.isConnected) return;
		if (r.ok === false && !r.state) say('Abfrage fehlgeschlagen (' + errText(r) + '), neuer Versuch …', 'warn');
		else if (r.state === 'done') {
			if (typeof r.url !== 'string' || !/^\/media\/[^\s"'<>\\?#]+$/.test(r.url) || r.url.includes('..')) return end('fertig, aber ohne gültige Adresse ' + PEND, 'err');
			end('fertig.', 'ok');
			return card.appendChild(h('video', 'video', null, { controls: '', src: r.url, 'aria-label': 'Video' }));
		} else if (r.state === 'error') {
			return end('fehlgeschlagen: ' + (Array.isArray(r.log_tail) && r.log_tail.length ? clip(r.log_tail[r.log_tail.length - 1], 200) : errText(r)), 'err');
		} else {
			bar.value = Math.max(0, Math.min(1, num(r.progress, 0)));
			say(r.state === 'running' ? 'rendert ' + Math.round(bar.value * 100) + ' %' : 'wartet …');
		}
		setTimeout(tick, 2000);
	};
	tick();
}

function renderForm(a, m) {
	const id = clip(a.id || 'form'), uid = 'dkzbot-f' + (++S.seq), get = {};
	const fields = (Array.isArray(a.fields) ? a.fields : []).filter(f => f && typeof f.name === 'string' && /^[\w.-]{1,40}$/.test(f.name)).slice(0, 12);
	if (!fields.length) return ign(m, 'Formular ohne Felder');
	const form = h('form', 'card', null, { novalidate: '' }), fs = h('fieldset'), err = h('div', 'note', null, { 'data-kind': 'err', role: 'alert' });
	fs.appendChild(h('legend', null, clip(a.title || 'Rückfrage', 80)));
	fields.forEach(f => {
		const t = f.type, fid = uid + '-' + f.name, d = f.default, row = h('div', 'field');
		const ch = t === 'choice' ? (Array.isArray(f.choices) ? f.choices : []).slice(0, 30).map(String) : [];
		const lo = Math.min(num(f.min, 0), num(f.max, 100)), hi = Math.max(num(f.min, 0), num(f.max, 100));
		const inp = h(t === 'choice' ? 'select' : 'input', null, null, { id: fid, type: t === 'range' ? 'range' : t === 'bool' ? 'checkbox' : 'text', min: lo, max: hi });
		const out = t === 'range' ? h('output', null, null, { for: fid }) : null;
		ch.forEach(c => { const o = h('option', null, c); o.value = c; inp.appendChild(o); });
		if (t === 'bool') inp.checked = d === true;
		else if (d != null && (t !== 'choice' || ch.includes(String(d)))) inp.value = String(d);
		if (out) { out.textContent = inp.value; inp.addEventListener('input', () => { out.textContent = inp.value; }); }
		get[f.name] = () => {
			const v = t === 'bool' ? inp.checked : t === 'range' ? Number(inp.value) : inp.value;
			return t === 'choice' ? (ch.includes(v) ? v : undefined) : t === 'range' ? (v >= lo && v <= hi ? v : undefined) : t === 'bool' ? v : v.trim().slice(0, 200);
		};
		row.append(...(t === 'bool' ? [inp] : []), h('label', null, clip(f.label || f.name, 80), { for: fid }), ...(t === 'bool' ? [] : [inp]), ...(out ? [out] : []));
		fs.appendChild(row);
	});
	fs.append(err, h('button', 'btn', 'Absenden', { type: 'submit' }));
	form.appendChild(fs);
	form.addEventListener('submit', ev => {
		ev.preventDefault();
		if (S.busy) return;
		const values = {};
		for (const f of fields) {
			const v = get[f.name]();
			if (v === undefined) { err.textContent = 'Ungültiger Wert für «' + clip(f.label || f.name, 40) + '» — nichts gesendet.'; return; }
			values[f.name] = v;
		}
		err.textContent = '';
		fs.disabled = true;
		ask(typeof a.submit_intent === 'string' && a.submit_intent ? a.submit_intent : 'form:' + id, { form: { id, values } },
			'Angaben: ' + fields.map(f => (f.label || f.name) + ' = ' + values[f.name]).join(', '));
	});
	put(m, form);
}

const setBusy = b => {
	S.busy = send.disabled = b;
	send.textContent = b ? '…' : 'Senden';
	log.setAttribute('aria-busy', String(b));
};
async function ask(message, ctx, shown) {
	message = String(message).trim().slice(0, 2000);
	if (S.busy || !message) return;
	setBusy(true);
	const hist = S.turns.slice(-6), a = app(), wait = h('div', 'msg', 'Denkt nach …', { 'data-role': 'wait' });
	addMsg('user', shown || message);
	S.turns.push({ role: 'user', content: shown || message });
	log.appendChild(wait);
	down();
	const base = { view: a && a.currentView, theme: document.documentElement.getAttribute('data-theme') || 'master' };
	const r = await api('/api/bot/ask', { body: { message, history: hist, context: { ...base, ...(ctx || {}) } }, timeout: 120000 });
	wait.remove();
	if (!r.ok || typeof r.reply !== 'string') addMsg('error', 'Keine Antwort: ' + (r.network ? 'Server nicht erreichbar ' + PEND : errText(r)));
	else {
		const m = addMsg('assistant', r.reply, r);
		S.turns.push({ role: 'assistant', content: r.reply });
		if (typeof r.live === 'boolean') { badge.textContent = liveText(r.live, r.model); badge.setAttribute('data-live', String(r.live)); }
		await runActions(r.actions, m);
	}
	setBusy(false);
	if (S.open && !panel.contains(document.activeElement)) input.focus();
}

function setOpen(v, focus) {
	S.open = !!v;
	root.setAttribute('data-open', String(S.open));
	panel.hidden = !S.open;
	fab.setAttribute('aria-expanded', String(S.open));
	try { localStorage.setItem(KEY, S.open ? '1' : '0'); } catch (_) {}
	if (S.open) {
		if (!log.hasChildNodes()) loadHistory();
		loadStatus();
		if (focus !== false) input.focus();
	} else if (focus !== false) fab.focus();
}
function build() {
	root = h('div', null, null, { id: 'dkzbot', class: 'dkzbot', 'data-open': 'false' });
	fab = h('button', 'fab', null, { type: 'button', 'aria-expanded': 'false', 'aria-label': 'Design-Assistent öffnen (Alt+D)' });
	fab.append(h('span', 'ico', null, { 'aria-hidden': 'true' }), h('span', null, 'Assistent'));
	panel = h('section', 'panel', null, { id: 'dkzbot-panel', role: 'dialog', 'aria-label': 'Design-Assistent' });
	panel.hidden = true;
	const head = h('header', 'head'), form = h('form', 'input');
	const info = h('button', null, 'i', { type: 'button', class: 'info-btn', 'data-help': 'designbot', 'aria-label': 'Hilfe' });
	const close = h('button', 'x', '×', { type: 'button', 'aria-label': 'Schließen' });
	badge = h('span', 'tag', 'offline', { 'data-live': 'false' });
	head.append(h('strong', null, 'Design-Assistent'), badge, info, close);
	statusEl = h('div', 'status', 'Lade Status …', { role: 'status', 'data-state': 'yellow' });
	log = h('div', 'log', null, { role: 'log', 'aria-live': 'polite', 'aria-label': 'Verlauf', tabindex: '0' });
	input = h('textarea', null, null, { rows: '2', maxlength: '2000', placeholder: 'Frage … (Enter sendet)', 'aria-label': 'Nachricht' });
	send = h('button', 'btn send', 'Senden', { type: 'submit' });
	form.append(input, send);
	panel.append(head, statusEl, log, form);
	root.append(fab, panel);
	document.body.appendChild(root);

	fab.addEventListener('click', () => setOpen(true));
	close.addEventListener('click', () => setOpen(false));
	form.addEventListener('submit', e => { e.preventDefault(); const t = input.value; if (t.trim() && !S.busy) { input.value = ''; ask(t); } });
	input.addEventListener('keydown', e => { if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) { e.preventDefault(); form.requestSubmit(); } });
	document.addEventListener('keydown', e => {
		if (e.altKey && !e.ctrlKey && !e.metaKey && (e.code === 'KeyD' || (e.key || '').toLowerCase() === 'd')) { e.preventDefault(); setOpen(!S.open); }
		else if (e.key === 'Escape' && S.open && !e.defaultPrevented) setOpen(false);
	});
	let was = null;
	try { was = localStorage.getItem(KEY); } catch (_) {}
	if (was === '1' && window.innerWidth >= 640) setOpen(true, false);
}
window.DesignBot = { open: () => setOpen(true), close: () => setOpen(false), toggle: () => setOpen(!S.open) };
if (document.body) build(); else document.addEventListener('DOMContentLoaded', build);
})();
