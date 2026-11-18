/* SVG Studio — schema-getrieben, DOM-APIs, sanitisierter Import. window.SvgStudio = {show, load} */
(function () {
  'use strict';
  var PEND = '— ausstehend —', K = window.SvgKit, S = {}, st = { type: 'bar', vals: {}, spec: null, svg: '' };
  var root, out, themes, msg, form, tsel;

  function el(t, a, kids) {
    var e = document.createElement(t), k;
    for (k in (a || {})) { if (k === 'text') e.textContent = a[k]; else e.setAttribute(k, a[k]); }
    (kids || []).forEach(function (c) { e.appendChild(c); });
    return e;
  }
  function say(t, err) { msg.textContent = t; msg.className = 'ss-msg' + (err ? ' ss-err' : ''); }
  function api(path, body) {
    var o = body ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) } : {};
    return fetch(path, o).then(function (r) { return r.json().catch(function () { return {}; }).then(function (j) { j._s = r.status; return j; }); })
      .catch(function () { return { ok: false, error: PEND, _s: 0 }; });
  }
  function parse(f, v) {
    v = String(v).trim();
    if (v === '') return undefined;
    if (f.type === 'int' || f.type === 'num') return Number(v);
    if (f.type === 'nums') return v.split(',').map(Number);
    if (f.type === 'strs') return v.split(',').map(function (x) { return x.trim(); });
    if (f.type === 'series' || f.type === 'nodes' || f.type === 'edges') { try { return JSON.parse(v); } catch (e) { return undefined; } }
    return v;
  }
  function show0(f, v) {
    if (v === undefined || v === null) return '';
    return typeof v === 'object' ? (Array.isArray(v) && v.every(function (x) { return typeof x !== 'object'; }) ? v.join(', ') : JSON.stringify(v)) : String(v);
  }
  function field(f) {
    var cur = st.vals[f.name], id = 'ss-f-' + f.name, i, wrap = el('div'), lab = el('label', { for: id, text: f.label || f.name });
    if (f.type === 'enum') {
      i = el('select', { id: id });
      f.choices.forEach(function (c) { var o = el('option', { value: c, text: c }); if (c === cur) o.selected = true; i.appendChild(o); });
    } else if (f.type === 'bool') {
      i = el('input', { id: id, type: 'checkbox' }); i.checked = !!cur;
    } else if (f.type === 'color' || f.type === 'colorx') {
      i = el('input', { id: id, type: 'color', value: /^#[0-9a-f]{6}$/i.test(cur || '') ? cur : '#888888' });
      var clr = el('button', { type: 'button', text: 'Theme-Farbe' });
      clr.onclick = function () { delete st.vals[f.name]; i.value = '#888888'; run(); };
      i.oninput = function () { st.vals[f.name] = i.value; run(); };
      wrap.appendChild(lab); wrap.appendChild(i); wrap.appendChild(clr); return wrap;
    } else if (f.type === 'int' || f.type === 'num') {
      var step = f.step || (f.type === 'int' ? 1 : 0.05), lo = Math.max(f.min, -10000), hi = Math.min(f.max, 10000);
      i = el('input', { id: id, type: 'range', min: lo, max: hi, step: step, value: cur == null ? f.default : cur });
      var n = el('input', { type: 'number', min: f.min, max: f.max, step: step, value: i.value, 'aria-label': f.label });
      i.oninput = function () { n.value = i.value; st.vals[f.name] = Number(i.value); run(); };
      n.oninput = function () { i.value = n.value; st.vals[f.name] = Number(n.value); run(); };
      wrap.appendChild(lab); wrap.appendChild(i); wrap.appendChild(n); return wrap;
    } else if (f.type === 'series' || f.type === 'nodes' || f.type === 'edges') {
      i = el('textarea', { id: id, rows: 3 }); i.value = show0(f, cur);
    } else {
      i = el('input', { id: id, type: 'text', maxlength: f.len || 200 }); i.value = show0(f, cur);
    }
    var ev = (i.tagName === 'SELECT' || f.type === 'bool') ? 'onchange' : 'oninput';
    i[ev] = function () {
      var v = f.type === 'bool' ? i.checked : parse(f, i.value);
      if (v === undefined) delete st.vals[f.name]; else st.vals[f.name] = v;
      run();
    };
    wrap.appendChild(lab); wrap.appendChild(i); return wrap;
  }
  function build(pre) {
    var sc = K.schema[st.type], k;
    st.vals = {};
    sc.felder.forEach(function (f) { if (f.default !== undefined) st.vals[f.name] = f.default; });
    for (k in (pre || {})) st.vals[k] = pre[k];
    st.vals.type = st.type;
    while (form.firstChild) form.removeChild(form.firstChild);
    sc.felder.forEach(function (f) { form.appendChild(field(f)); });
    run();
  }
  function put(box, svg) {
    var d = new DOMParser().parseFromString(svg, 'image/svg+xml'), r = d.documentElement;
    while (box.firstChild) box.removeChild(box.firstChild);
    if (r && r.nodeName === 'svg') box.appendChild(document.importNode(r, true));
  }
  function run() {
    var spec = {}, k;
    for (k in st.vals) spec[k] = st.vals[k];
    var g = K.generate(spec);
    if (!g.ok) { say((g.error || 'Fehler') + (g.field ? ' (' + g.field + ')' : ''), true); return; }
    st.spec = g.spec; st.svg = g.svg; put(out, g.svg);
    say(g.svg.length + ' Bytes · ' + K.version);
    while (themes.firstChild) themes.removeChild(themes.firstChild);
    K.themes.forEach(function (t) {
      var s2 = {}, j; for (j in g.spec) s2[j] = g.spec[j];
      s2.theme = t; s2.colors = 'static'; s2.idprefix = 'p' + t.slice(0, 2);
      var r = K.generate(s2), fig = el('figure', {}, [el('figcaption', { text: t })]);
      if (r.ok) { var b = el('div'); put(b, r.svg); fig.appendChild(b); }
      themes.appendChild(fig);
    });
  }
  function copy() {
    if (!st.svg) return;
    (navigator.clipboard ? navigator.clipboard.writeText(st.svg) : Promise.reject()).then(function () { say('Kopiert.'); }, function () { say('Kopieren nicht erlaubt — bitte Download nutzen.', true); });
  }
  function download() {
    if (!st.svg) return;
    var u = URL.createObjectURL(new Blob([st.svg], { type: 'image/svg+xml' })), a = el('a', { href: u, download: (st.spec.type || 'svg') + '.svg' });
    document.body.appendChild(a); a.click(); document.body.removeChild(a); setTimeout(function () { URL.revokeObjectURL(u); }, 500);
  }
  function importSvg(file) {
    if (!file) return;
    file.text().then(function (t) {
      var r = K.sanitize(t);
      st.svg = r.svg; st.spec = { type: 'import' }; put(out, r.svg);
      say('Importiert, bereinigt' + (r.removed.length ? ' — entfernt: ' + r.removed.join(', ') : ' (nichts entfernt)'));
    });
  }
  function lib() {
    api('/api/svg/library').then(function (r) {
      var ul = S.ul; while (ul.firstChild) ul.removeChild(ul.firstChild);
      if (!r.ok) { ul.appendChild(el('li', { text: PEND })); return; }
      if (!r.items.length) ul.appendChild(el('li', { text: 'Noch leer.' }));
      r.items.forEach(function (it) {
        var li = el('li', { text: '#' + it.id + ' ' + it.name + ' (' + it.type + ')', tabindex: 0 });
        li.onclick = function () {
          api('/api/svg/library/' + it.id).then(function (g) {
            var item = g.item || g, svg = item.svg;
            if (!svg) { say(PEND, true); return; }
            var c = K.sanitize(svg); st.svg = c.svg; st.spec = item.spec || { type: 'import' }; put(out, c.svg); say('Geladen: ' + it.name);
          });
        };
        ul.appendChild(li);
      });
    });
  }
  function save() {
    if (!st.svg) return;
    var name = S.name.value.trim() || (st.spec.type + ' ' + new Date().toISOString().slice(0, 16));
    var body = { name: name, svg: st.svg }; if (st.spec && st.spec.type !== 'import') body.spec = st.spec;
    api('/api/svg/library', body).then(function (r) {
      if (r.ok) { say('Gespeichert.'); lib(); } else say((r.error || PEND) + (r.needs ? ' (benötigt: ' + r.needs + ')' : ''), true);
    });
  }
  function video() {
    api('/api/video/render', { svg: st.svg }).then(function (r) {
      say(r.ok ? 'Video-Auftrag angelegt.' : (r._s === 404 ? PEND + ' (Video-Rendering noch nicht gebaut)' : (r.error || PEND)), !r.ok);
    });
  }
  function btn(t, fn) { var b = el('button', { type: 'button', text: t }); b.onclick = fn; return b; }
  function init() {
    root = document.getElementById('view-svg'); if (!root) return false;
    while (root.firstChild) root.removeChild(root.firstChild);
    if (!K) { root.appendChild(el('p', { text: PEND + ' (svgkit.js fehlt)' })); return true; }
    tsel = el('select', { id: 'ss-type', 'aria-label': 'Typ' });
    Object.keys(K.schema).forEach(function (t) { tsel.appendChild(el('option', { value: t, text: K.schema[t].label || t })); });
    tsel.onchange = function () { st.type = tsel.value; build(); };
    form = el('div'); out = el('div', { class: 'ss-out', 'aria-live': 'polite' }); themes = el('div', { class: 'ss-themes' });
    msg = el('p', { class: 'ss-msg', role: 'status' });
    S.name = el('input', { type: 'text', placeholder: 'Name für die Bibliothek', maxlength: 80, 'aria-label': 'Name' });
    S.ul = el('ul', { class: 'ss-lib' });
    var file = el('input', { type: 'file', accept: '.svg,image/svg+xml', 'aria-label': 'SVG importieren' });
    file.onchange = function () { importSvg(file.files[0]); };
    var p = el('div', { class: 'ss-p' }, [el('h2', { text: 'SVG Studio' }), el('label', { for: 'ss-type', text: 'Typ' }), tsel, form]);
    var v = el('div', { class: 'ss-v' }, [out, msg,
      el('div', { class: 'row' }, [btn('Kopieren', copy), btn('Download', download), btn('Video rendern', video)]),
      el('div', { class: 'row' }, [S.name, btn('In Bibliothek', save)]),
      el('label', { text: 'SVG importieren (wird bereinigt)' }), file,
      el('h2', { text: 'Fünf Themes' }), themes,
      el('h2', { text: 'Bibliothek' }), S.ul]);
    root.appendChild(el('div', { class: 'ss' }, [p, v]));
    build(); lib(); return true;
  }
  window.SvgStudio = {
    show: function () { if (!root || !root.firstChild) init(); },
    load: function (spec) {
      if (!init()) return; if (!spec || !K.schema[spec.type]) return;
      st.type = spec.type; tsel.value = spec.type; build(spec);
    }
  };
})();
