/* DkZ-Designsystem — Theme-Schalter, esc(), Token-Zugriff (Vanilla-JS, < 18 KB).
   Stellt window.DkZ bereit: { esc, setTheme, theme, THEMES, saved, token, rgb,
   readable, loadFonts, bar }. Verträge: docs/CONTRACT.md §1.
   Andere Module fallen ohne DkZ auf document.documentElement.setAttribute(
   'data-theme', name) zurück. */
(function () {
    'use strict';

    var KEY = 'dkz-theme';
    var FONT_ID = 'dkz-fonts';
    var THEMES = ['master', 'light', 'contrast', 'control', 'content'];
    // Symbol, Kurzname, ausführlicher Name (Tooltip)
    var META = {
        master:   ['🔴', 'Master',   'Master Neonrot'],
        light:    ['⚪', 'Hell',     'Hell'],
        contrast: ['🟢', 'Kontrast', 'Kontrast (nur Schwarz und Neongrün)'],
        control:  ['🟠', 'Control',  'Control Room'],
        content:  ['🟣', 'Content',  'Content']
    };
    // Google-Fonts-Adresse der DkZ-Schriften. Wird NUR auf Wunsch geladen
    // (Einstellung dkz_fonts), weil die Anfrage das Gerät verlässt.
    var FONTS_URL = 'https://fonts.googleapis.com/css2' +
        '?family=Inter:wght@400;500;600;700' +
        '&family=JetBrains+Mono:wght@400;500' +
        '&family=Archivo:wdth,wght@62..125,100..900' +
        '&family=Syne:wght@400..800' +
        '&family=Space+Mono:wght@400;700&display=swap';

    var root = document.documentElement;

    /* Fremdtext für innerHTML entschärfen (auch für Attributwerte). */
    function esc(str) {
        return String(str == null ? '' : str)
            .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
    }

    function theme() {
        var t = root.getAttribute('data-theme');
        return THEMES.indexOf(t) >= 0 ? t : 'master';
    }

    function saved() {
        try {
            var v = localStorage.getItem(KEY);
            return THEMES.indexOf(v) >= 0 ? v : null;
        } catch (e) { return null; }
    }

    function mark(name) {
        var btns = document.querySelectorAll('.theme-btn[data-theme]');
        for (var i = 0; i < btns.length; i++) {
            var on = btns[i].getAttribute('data-theme') === name;
            btns[i].classList.toggle('active', on);
            btns[i].setAttribute('aria-pressed', on ? 'true' : 'false');
        }
    }

    /* Theme setzen, merken, Leiste und Zuhörer benachrichtigen.
       Unbekannte Namen ergeben das Standard-Theme master. */
    function apply(name, store) {
        if (THEMES.indexOf(name) < 0) name = 'master';
        var changed = root.getAttribute('data-theme') !== name;
        root.setAttribute('data-theme', name);
        if (store) {
            try { localStorage.setItem(KEY, name); } catch (e) { /* privat/gesperrt */ }
        }
        mark(name);
        if (changed) {
            try {
                window.dispatchEvent(new CustomEvent('dkz-themechange', { detail: { theme: name } }));
            } catch (e) { /* alte Browser */ }
        }
        return name;
    }

    function setTheme(name) { return apply(name, true); }

    /* Token zur Laufzeit lesen (z. B. fürs Canvas): DkZ.token('--accent'). */
    function token(name, fallback) {
        var v = '';
        try { v = getComputedStyle(root).getPropertyValue(name).trim(); } catch (e) { /* */ }
        return v || fallback || '';
    }

    /* Beliebige CSS-Farbe → [r,g,b] (0–255) oder null. Versteht rgb(), hsl-Ergebnisse
       und color(srgb …) der berechneten Werte. */
    function rgb(css) {
        var probe = document.createElement('span');
        probe.style.color = css;
        if (!probe.style.color) return null;
        probe.style.display = 'none';
        document.body.appendChild(probe);
        var c = getComputedStyle(probe).color;
        probe.remove();
        var m = /^rgba?\(\s*([\d.]+)[ ,]+([\d.]+)[ ,]+([\d.]+)/.exec(c);
        if (m) return [+m[1], +m[2], +m[3]];
        m = /^color\(srgb\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)/.exec(c);
        return m ? [m[1] * 255, m[2] * 255, m[3] * 255] : null;
    }

    function lum(c) {
        var l = c.map(function (v) {
            v /= 255;
            return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
        });
        return 0.2126 * l[0] + 0.7152 * l[1] + 0.0722 * l[2];
    }

    function ratio(a, b) {
        var x = lum(a), y = lum(b);
        return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
    }

    /* Lesbare Schriftfarbe auf einer Fläche: wählt aus den Tokens --bg und --text
       diejenige mit dem größeren WCAG-Kontrast. */
    function readable(bgCss) {
        var bg = rgb(bgCss), a = rgb(token('--bg', '#000')), b = rgb(token('--text', '#fff'));
        if (!bg || !a || !b) return token('--text');
        return ratio(bg, a) >= ratio(bg, b) ? token('--bg') : token('--text');
    }

    /* DkZ-Schriften auf Wunsch laden (an) oder wieder entfernen (aus). */
    function loadFonts(on) {
        var el = document.getElementById(FONT_ID);
        if (on === false) { if (el) el.remove(); return false; }
        if (el) return true;
        el = document.createElement('link');
        el.id = FONT_ID;
        el.rel = 'stylesheet';
        el.href = FONTS_URL;
        document.head.appendChild(el);
        return true;
    }

    /* Theme-Leiste: bindet vorhandene Knöpfe; ist keine da, baut sie die fünf
       Knöpfe in #themeBar (oder ans Ende der Kopfzeile). */
    function bar() {
        var host = document.getElementById('themeBar');
        if (!host) {
            var head = document.querySelector('.header-controls, header');
            if (!head) return null;
            host = document.createElement('div');
            host.id = 'themeBar';
            host.className = 'theme-bar';
            head.appendChild(host);
        }
        if (!host.querySelector('.theme-btn')) {
            THEMES.forEach(function (t) {
                var b = document.createElement('button');
                b.type = 'button';
                b.className = 'theme-btn';
                b.setAttribute('data-theme', t);
                b.title = META[t][2];
                b.setAttribute('aria-label', 'Theme ' + META[t][2]);
                var i = document.createElement('span');
                i.className = 'theme-ico';
                i.textContent = META[t][0];
                var n = document.createElement('span');
                n.className = 'theme-name';
                n.textContent = META[t][1];
                b.appendChild(i);
                b.appendChild(n);
                host.appendChild(b);
            });
        }
        return host;
    }

    document.addEventListener('click', function (e) {
        var b = e.target.closest && e.target.closest('.theme-btn[data-theme]');
        if (b) setTheme(b.getAttribute('data-theme'));
    });

    window.DkZ = {
        esc: esc, setTheme: setTheme, theme: theme, THEMES: THEMES,
        saved: saved, token: token, rgb: rgb, readable: readable,
        loadFonts: loadFonts, bar: bar, META: META, FONTS_URL: FONTS_URL
    };

    // Start: gespeicherte Wahl, sonst das im <html>-Tag stehende Theme.
    bar();
    apply(saved() || theme(), false);   // nur anwenden, nicht als Wahl merken
})();
