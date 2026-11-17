# Vertrag für alle Tracks

Diese Datei ist verbindlich. Sie gilt für jeden Agenten, der am DkZ-Design-Paket
mitbaut. **Lies sie vollständig, bevor du etwas schreibst.**

Projekt: `skills/in-progress/design-system-ui/` · Ein DkZ-Projekt.

---

## 0. Grundregeln

1. **Nur eigene Dateien schreiben.** Siehe Besitztabelle in §6. Wer eine fremde
   Datei ändern müsste, ändert sie *nicht*, sondern meldet es im Ergebnis unter
   `needs_from_others`.
2. **Kein `git commit`, kein `git push`, kein `git add`.** Das macht der Orchestrator.
3. **Nichts erfinden.** Fehlende Daten werden als `— ausstehend —` ausgewiesen,
   nie aufgefüllt (DkZ-Brainly-Regel). Keine erfundenen Zahlen, Zitate, Versionen,
   Benchmarks. Was du nicht prüfen konntest, schreibst du als „nicht geprüft".
4. **Ehrlich berichten.** Wenn etwas nicht läuft, steht das im Ergebnis. Ein
   fehlschlagender Test wird nicht abgeschwächt, damit er grün wird.
5. **Sprache:** Oberflächentexte und Kommentare Deutsch (wie im Rest des
   Repos), Bezeichner Englisch.
6. **Eigene DuckDB-Datei pro Testserver.** DuckDB erlaubt nur *einen* schreibenden
   Prozess pro Datei. `tests/run-one.sh` und `test.sh` setzen dafür `DKZ_DB` auf eine
   Wegwerf-Datei. Wer einen Server selbst startet, setzt `DKZ_DB` ebenfalls
   (Standard im Betrieb: `data/dkz.duckdb`; `store.py` liest die Variable).
6b. **Eigener Portbereich.** Starte Testserver nur auf deinem Port (§7) — mehrere
   Tracks laufen gleichzeitig. Beende jeden Prozess, den du startest.
7. **Werkzeuge:** Python `.venv/bin/python` (3.11, Standardbibliothek + `duckdb`
   2.0), Node 22, Bun 1.4, `rustc`/`cargo` 1.97, `gcc` 13, `ffmpeg`, Chromium unter
   `/opt/pw-browsers/chromium`. Keine weiteren Python-Pakete. Rust und C ohne
   externe Bibliotheken.
8. **Playwright** liegt unter `demo/` (`cd demo && npm install playwright`, falls
   `node_modules` fehlt). Chromium: `CHROMIUM_PATH=/opt/pw-browsers/chromium`.
9. **Kein Framework** (React/Vue/Angular/Svelte …) — nur Vanilla-JS und CSS3.

## 1. DkZ-Designsystem (verbindlich — aus dem Skill `dkz-vanilla-ui-engine`)

**Eiserne Regeln:**

- `esc()` bei **jedem** Nutzer- oder Fremdtext, bevor er per `innerHTML` landet.
  Bevorzugt gar kein `innerHTML` mit Fremdtext: `textContent`, `createElement`,
  `replaceChildren`.
- **CSS-Variablen nutzen — kein hartkodiertes Hex in Styles.** (Ausnahme:
  Fallback-Werte innerhalb von `var(--x, #hex)` und die Token-Definitionen
  selbst in `dkz.css`.)
- Kein React/Vue/Angular — Vanilla-JS, **18-KB-Regel**: ein ausgeliefertes
  JS-Modul bleibt unter 18 KB (roh, unminifiziert). Neue Module halten das ein.
  *(Auslegung: „18 KB pro Modul". Der Bestand `app.js` verletzt das — das ist
  bekannt, dokumentiert und wird nicht in diesem Durchgang behoben.)*
- Mobile-first, mindestens 320 px Breite, 16 px Seitenabstand.
- Fehlende Daten: Platzhalter `— ausstehend —`.

**Tokens (verbindlich):**

```css
--accent:#fa1e4e; --accent-hover:#ff3366;
--bg:#060608; --bg-card:#0e0e10; --bg-elevated:#141418;
--text:#f6f6f7; --text-muted:rgba(246,246,247,0.55);
--green:#00ff88; --yellow:#ffb800; --red:#ff3b5c; --blue:#55ACEE;
--pink:#ec4899; --amber:#f5a623; --cyan:#3fe0d0; --lila:#9d7bff;
--font-main:'Inter',system-ui,-apple-system,sans-serif;
--font-mono:'JetBrains Mono','IBM Plex Mono',monospace;
--font-size-xs:.75rem; --font-size-sm:.875rem; --font-size-base:1rem;
--font-size-lg:1.25rem; --font-size-xl:1.5rem; --font-size-2xl:2rem;
--space-xs:4px; --space-sm:8px; --space-md:16px; --space-lg:24px;
--space-xl:32px; --space-2xl:48px;
--border:1px solid rgba(255,255,255,0.08);
--border-accent:1px solid rgba(250,30,78,0.3);
--glass:rgba(14,14,16,0.85); --glass-blur:blur(12px);
--radius:12px; --radius-sm:8px; --radius-pill:50px;
--shadow:0 4px 24px rgba(0,0,0,0.4); --shadow-glow:0 0 20px rgba(250,30,78,0.15);
--transition:all .2s ease;
```

**Fünf Themes** über `data-theme` auf `<html>` (Standard `master`):

| Theme | Kern |
|---|---|
| `master` | dunkel, Akzent Neonrot, Glas-Karten, feine Borders |
| `light` | `--bg:#f6f6f7; --text:#0e0e10; --bg-card:#ffffff`; Border `1px solid rgba(0,0,0,.08)`; Akzent bleibt |
| `contrast` | `--bg:#000; --text:#00ff88; --accent:#00ff88`; **nur Schwarz und Neongrün**; alle Borders `1px solid #00ff88` |
| `control` | Font `'Archivo Expanded'`, HUD-Borders, geometrische clip-paths, Scanlines; Border `1px solid rgba(250,30,78,.3)` |
| `content` | Fonts `'Syne'` + `'Space Mono'`, Pill-Tags (`border-radius:50px`), weichere Formen, Lila als Zweitfarbe |

Theme-Wechsel merkt sich `localStorage['dkz-theme']` (in `try/catch`).

`dkz.js` stellt global bereit: `window.DkZ = { esc(str), setTheme(name), theme(), THEMES }`.
Andere Module nutzen das und fallen ohne `DkZ` auf
`document.documentElement.setAttribute('data-theme', name)` zurück.

## 2. Plug-in-Mechanismus (im Server bereits vorhanden und getestet)

Eine Fähigkeit liefert eine Datei `plugins/<name>.py`. Erlaubte Namen: `svgapi`,
`botapi`, `controlapi`, `videoapi`. Fehlt die Datei, meldet `/api/plugins`
`— ausstehend —`.

```python
ROUTES = {
    "GET /api/x":      fn,   # exakt
    "POST /api/y":     fn,
    "GET /api/z/*":    fn,   # Präfix; ctx["rest"] enthält den Rest des Pfads
}
def STATUS(): return {...}   # optional, erscheint unter /api/plugins
```

`fn(ctx)` bekommt `{"payload": dict, "query": dict, "path", "rest", "port", "edition"}`
und liefert `dict|list` (200), `(status, obj)` oder `(status, bytes, content_type)`.
Eine geworfene Ausnahme wird zu `500 {"ok":false,"error":...}` — fang erwartbare
Fehler trotzdem selbst ab und antworte mit `400`/`404` und einer klaren Meldung.

Plug-ins importieren Geschwistermodule über das Wurzelverzeichnis
(`import svgkit`, `import store`, `import designlab`). Optionale Abhängigkeiten
(`duckdb`, ein noch nicht vorhandenes Geschwistermodul) werden mit
`try/except ImportError` abgefangen; die Route antwortet dann mit
`503 {"ok":false,"error":"— ausstehend —","needs":"duckdb"}`.

Statische Dateien dürfen nur ausgeliefert werden, wenn sie in `STATIC_FILES`
stehen (`server.py`). Bereits freigegeben: `dkz.css dkz.js svgkit.js svgstudio.js
svgstudio.css designbot.js designbot.css`. Mediendateien liegen in `data/media/`
und werden unter `/media/<name>` ausgeliefert (Range-fähig).

## 3. SVG-Generator („svgspec/1")

Eine Spezifikation (JSON) beschreibt ein Bild; **mehrere Implementierungen müssen
dasselbe Byte-für-Byte erzeugen.** Die kanonische Referenz ist `svgkit.js`; die
Datei `svg/SPEC.md` (vom Track `svgkit-js` geschrieben) ist danach die genaue
Schnittstelle für alle Portierungen.

**Typen:** `bar`, `line`, `donut`, `pattern`, `badge`, `icon`, `monogram`,
`gradient`, `graph` (Workflow-Knoten/Kanten als Diagramm).

**Gemeinsame Felder:** `type` (Pflicht), `theme` (`master|light|contrast|control|content`,
Standard `master`), `colors` (`"vars"` = `var(--accent, #fa1e4e)` mit Fallback-Hex,
folgt dem Seiten-Theme; `"static"` = reines Hex, eigenständige Datei; Standard
`"vars"`), `width`/`height` (begrenzt, mit Typ-Standard), `title` (wird `<title>` +
`aria-label`), `idprefix`, `seed`.

**Determinismus (nicht verhandelbar, sonst bricht die Parität):**

- Zahlen werden über **eine** Formatregel ausgegeben: auf 2 Nachkommastellen
  gerundet (Betrag mal 100, plus 0,5, abgerundet — „half away from zero"), ohne
  überflüssige Nullen, `-0` wird `0`. Keine Sprachformatierung (`toFixed`,
  `%g`, `repr`) verwenden, die je Sprache anders rundet.
- Zufall nur über einen ganzzahligen Generator (z. B. LCG
  `s = (s * 1664525 + 1013904223) mod 2^32`), identisch in allen Sprachen.
- XML-Escaping für `& < > " '` in allen Texten und Attributen.
- Reihenfolge von Attributen und Elementen ist festgelegt und wird in `SPEC.md`
  beschrieben. Keine Zeitstempel, keine Zufalls-IDs.
- Farben nur über Token oder validierte Literale (`#rgb`, `#rrggbb`); alles
  andere wird verworfen. Das Theme `contrast` kennt nur Schwarz und `#00ff88`.

**Sicherheit:** Generierte SVGs enthalten nie `<script>`, `on*`-Attribute,
`javascript:`-URLs oder `<foreignObject>`. Dazu gibt es `sanitize(svgString)`
(Allowlist von Elementen/Attributen) für importierte oder eingefügte SVGs —
in JS (`DOMParser`) **und** Python (`html.parser`/`xml`, ohne externe Pakete).

**CLI-Verträge der Fassungen** (schreiben exakt das SVG nach stdout, ohne abschließenden
Zeilenumbruch; Fehler: Exitcode 1, Meldung auf stderr):
`node svgkit.js <spec.json>` · `.venv/bin/python svgkit.py <spec.json>` ·
`rust/target/release/dsui-svg <spec.json>` · `c/dsui-svg <spec.json>`
(Bauen: `cargo build --release --manifest-path rust/Cargo.toml`, `make -C c dsui-svg`).

**Parität:** `svg/fixtures/<name>.json` sind Eingaben, `svg/golden/<name>.svg` die
erwarteten Ausgaben (erzeugt von `svgkit.js`). Jede Implementierung wird mit
`svg/run_parity.sh <impl>` geprüft. Welche Fixtures eine Implementierung abdeckt,
steht in `svg/fixtures/<name>.json` unter `"_impl": ["js","py","rs","c"]`. Die
C-Fassung deckt eine dokumentierte Teilmenge ab; JS, Python und Rust alle Typen.

## 4. API-Verträge

Alle Antworten sind JSON mit `ok`. Fehler: `{"ok":false,"error":"…","field":"…"}`.

### SVG (`plugins/svgapi.py`)

| Route | Zweck |
|---|---|
| `GET /api/svg/types` | `{"spec_version":"svgspec/1","types":{…Felder, Grenzen, Standardwerte…},"themes":[…],"tokens":{…}}` |
| `POST /api/svg/generate` | `{spec}` → `{"ok":true,"svg":"<svg…>","bytes":n,"spec":<normalisiert>,"engine":"py"}`; ungültig → `400` |
| `POST /api/svg/sanitize` | `{svg}` → `{"ok":true,"svg":"…","removed":[…]}` |
| `GET /api/svg/library` | `{"ok":true,"items":[{"id","name","created","type","bytes"}],"backend":"duckdb"}` |
| `GET /api/svg/library/*` | `rest` = id → `{"ok":true,"item":{…,"spec","svg"}}` oder 404 |
| `POST /api/svg/library` | `{name,spec,svg?}` → `{"ok":true,"id":n}` (erzeugt `svg` aus `spec`, wenn leer) |
| `POST /api/svg/library/delete` | `{id}` |

Die Bibliotheks-Routen gehören dem Track `designlab-kb-store`, die übrigen dem
Track `svgkit-py`. Beide liegen in **getrennten Plug-in-Dateien**:
`plugins/svgapi.py` (types/generate/sanitize) und `plugins/botapi.py`
(Bibliothek, Wissensbasis, Chat, Designrechner).

### Design-Assistent (`plugins/botapi.py`)

| Route | Zweck |
|---|---|
| `GET /api/bot/status` | `{"ok":true,"kb":{"docs":n,"backend":"duckdb"\|"memory"},"llm":{"live":bool,"model":"…"},"store":{"backend":"duckdb"\|"— ausstehend —","path":"…"}}` |
| `GET /api/bot/kb?q=…&limit=5` | `{"ok":true,"results":[{"id","title","topic","score","snippet","source"}]}` |
| `POST /api/bot/ask` | `{message, history?, context?, provider?}` → `{"ok":true,"reply","sources":[{"id","title","score"}],"intent","actions":[…],"live":bool,"model":"…","confidence":0..1}` |
| `GET /api/bot/history?limit=50` | gespeicherter Verlauf (DuckDB) |
| `POST /api/bot/clear` | Verlauf löschen |
| `GET /api/design/contrast?fg=#hex&bg=#hex` | WCAG-Kontrastverhältnis, Note für Fließtext/großen Text, AA/AAA |
| `GET /api/design/palette?base=#hex&scheme=…` | Paletten (`complementary`, `analogous`, `triadic`, `tetradic`, `monochrome`) |
| `GET /api/design/scale?base=16&ratio=1.25&steps=7` | Typografie-Skala |
| `GET /api/design/convert?color=…` | HEX ↔ RGB ↔ HSL |

**Aktionen** (`actions[]`), die die Oberfläche ausführt — jeweils ein Objekt mit `type`:

```
{"type":"svg","spec":{…},"svg":"<svg…>","explain":"…"}
{"type":"theme","name":"contrast"}
{"type":"setting","name":"accent_hue","value":280}
{"type":"pattern","name":"rag-pipeline"}
{"type":"command","name":"execute"}
{"type":"open_view","view":"svg"}
{"type":"video_request","source":{"spec":{…}},"preset":"svg-reveal"}   # die Oberfläche startet den Auftrag
{"type":"video","job_id":"…"}                                          # läuft bereits
```

**Antwortregeln des Assistenten (DkZ-Brainly):** Antworten über Gestaltung kommen
aus der Wissensbasis und nennen ihre Quelle. Läuft ein Modell (Ollama, OpenAI,
OpenRouter …), formuliert es auf Basis der gefundenen Einträge und `live` ist
`true`. Ohne Modell antwortet der Assistent **extraktiv** aus den Einträgen
(`live:false`). Fragen außerhalb der Gestaltung beantwortet er **nur**, wenn ein
Modell verbunden ist; sonst sagt er ehrlich, dass er lokal nur Gestaltungsfragen
beantwortet und wie man ein Modell verbindet. Er erfindet nie eine Antwort.

### Intelligente Steuerung (`plugins/controlapi.py`, Modul `control.py`)

| Route | Zweck |
|---|---|
| `GET /api/control/intents` | Liste aller Absichten mit Beispielsätzen |
| `POST /api/control/route` | `{message, context?}` → `{"ok":true,"intent":"…","confidence":0..1,"params":{…},"actions":[…],"reply":"…"\|null,"source":"rules"\|"llm"}` |

`control.route(message: str, context: dict) -> dict | None` — liefert `None`,
wenn die Nachricht keine Steueraktion ist (dann antwortet der Assistent aus der
Wissensbasis). `botapi` ruft `control.route` zuerst auf.

Absichten: `svg.generate`, `theme.set`, `setting.set`, `pattern.load`,
`command.run`, `view.open`, `video.render`, `design.contrast`, `design.palette`,
`design.scale`. Deutsch **und** Englisch. Regelbasiert und deterministisch; ein
Modell darf optional beim Verstehen helfen (`source:"llm"`), muss aber immer
erklären, was es verstanden hat (`explain`), und darf keine Absicht außerhalb der
Liste erfinden.

### Video (`plugins/videoapi.py`, Ordner `video/`)

HyperFrames (HeyGen, Apache-2.0, npm `hyperframes`): HTML schreiben, MP4 rendern;
Node ≥ 22, FFmpeg, Headless-Chrome.

| Route | Zweck |
|---|---|
| `GET /api/video/status` | `{"ok":true,"hyperframes":{"installed":bool,"version":"…"},"node":"…","ffmpeg":bool,"chrome":bool,"ready":bool,"hint":"…"}` |
| `POST /api/video/render` | `{source:{svg?|spec?|report?}, preset, duration?, fps?, width?, height?}` → `{"ok":true,"job_id":"…"}` (asynchron) |
| `GET /api/video/jobs/*` | `rest` = job_id → `{"state":"queued\|running\|done\|error","progress":0..1,"log_tail":[…],"url":"/media/<name>.mp4","bytes":n,"duration":s}` |

Rendern dauert; es läuft im Hintergrund wie der OpenCLAW-Runner. Ergebnisse
landen in `data/media/`. Ist HyperFrames nicht einsatzbereit, antwortet
`/api/video/render` mit `503` und dem genauen Grund — kein Platzhalter-Video.

## 5. Oberfläche

- **SVG Studio** (`#view-svg`, `svgstudio.js` + `svgstudio.css`): Formular aus
  `SvgKit.schema` (stufenlose Regler), Live-Vorschau, **Theme-Vorschau in allen fünf
  DkZ-Themes nebeneinander**, Export (kopieren, `.svg` laden), In-Bibliothek-speichern,
  Einbett-Schnipsel, „Als Video rendern". Einstieg über `window.SvgStudio.show(app)`.
- **Design-Assistent** (`designbot.js` + `designbot.css`): ein **fest eingebauter
  Dock** unten rechts auf **jeder** Oberfläche (eingeklappt als Knopf, ausgeklappt
  als Chat). Baut sich selbst in `document.body`. Tastenkürzel `Alt+D`. Führt
  `actions[]` der Antwort aus. Zeigt Quellen und ob ein Modell antwortet
  (`live`). Verlauf aus `/api/bot/history`.
- **DkZ-Theme** (`dkz.css` + `dkz.js`): Token-Schicht, die fünf Themes, Theme-Leiste
  in der Kopfzeile, `esc()`. `styles.css` bekommt die DkZ-Tokens als Quelle; die
  bisherigen `--color-*`-Variablen bleiben als Aliasse bestehen, damit nichts
  bricht.
- Alles nutzt ausschließlich DkZ-Tokens (§1) und funktioniert in **allen fünf**
  Themes.

Jedes neue Bedienelement hat einen Info-Button (`data-help`) — Hilfetexte stehen
in `helpdata.py` (Besitz: Orchestrator; schlage Texte im Ergebnis unter
`help_topics` vor).

## 6. Dateibesitz

| Track | Besitzt (nur hier schreiben) |
|---|---|
| `svgkit-js` | `svgkit.js`, `svg/**` (`SPEC.md`, `types.json`, `fixtures/`, `fixtures-invalid/`, `golden/`, `make_golden.mjs`, `make_types.mjs`, `run_parity.sh`), `tests/10-svgkit-js.sh` |
| `svgkit-py` | `svgkit.py`, `plugins/svgapi.py`, `tests/11-svgkit-py.sh` |
| `svgkit-rs` | `rust/**` (Crate `dsui-svg`), `tests/12-svgkit-rs.sh` |
| `svgkit-c` | `c/svgkit*.c`, `c/svgkit.h`, `c/Makefile`, in `c/server.c` **nur** die zusätzlichen Endpunkte `POST /api/svg/generate` und `GET /api/svg/types`, `tests/13-svgkit-c.sh` |
| `designlab-kb-store` | `designlab.py`, `store.py`, `kb/*.jsonl` (außer `kb/video.jsonl`), `plugins/botapi.py`, `tests/20-bot.sh` |
| `control-router` | `control.py`, `plugins/controlapi.py`, `tests/21-control.sh` |
| `designbot-ui` | `designbot.js`, `designbot.css`, `tests/22-designbot-ui.mjs` |
| `svg-studio-ui` | `svgstudio.js`, `svgstudio.css`, `tests/14-svg-studio-ui.mjs` |
| `video-hyperframes` | `video/**`, `plugins/videoapi.py`, `kb/video.jsonl`, `tests/30-video.sh` |
| `dkz-theme` | `dkz.css`, `dkz.js`, `styles.css`, die Kopfzeile und das `<html>`-Tag in `index.html`, in `app.js` **nur** Theme-/Farb-Code (`applySettings`, hartkodierte Farben in `redrawCanvas` u. ä. auf Tokens umstellen), in `settings.py` **nur** die Theme-Auswahl (die fünf DkZ-Themes statt `auto/hell/dunkel`) und ein Schalter `dkz_fonts`, `tests/50-dkz-theme.mjs` |
| `bun-toolchain` | `bun/**` (inkl. `bun/baseline.json`), `package.json`, `tests/40-bun.sh` |

Gemeinsam und nur vom Orchestrator geändert: `server.py`, `helpdata.py`, `test.sh`,
`llms.txt`, `README.md`, `PLAN.md`, `docs/**`, der Rest von `app.js` und `index.html`.

Ein Test darf `.sh` (nutzt `tests/run-one.sh`) oder `.mjs` (Playwright) sein.

## 7. Portbereiche für eigene Testserver

| Track | Ports |
|---|---|
| svgkit-js / svgkit-py | 8810–8819 |
| svgkit-rs / svgkit-c | 8820–8829 |
| designlab-kb-store | 8830–8839 |
| control-router | 8840–8849 |
| designbot-ui / svg-studio-ui | 8850–8869 |
| video-hyperframes | 8870–8879 |
| dkz-theme / bun-toolchain | 8880–8889 |
| Prüfer (Review) | 8900–8949 |

## 8. Ergebnisformat jedes Tracks

Am Ende gibst du ein JSON-Objekt zurück mit: `track`, `status`
(`done|partial|blocked`), `files` (geschrieben), `tests` (`{passed, failed, how_run}`),
`honest_limits` (was **nicht** geht oder nicht geprüft wurde), `needs_from_others`,
`help_topics` (Vorschläge für `helpdata.py`), `notes`.
