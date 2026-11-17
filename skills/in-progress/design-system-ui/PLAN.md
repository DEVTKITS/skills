# Plan — DkZ Design-Paket

*Ein DkZ-Projekt.* Stand der Planung: Grundlage für Version **v7**.

## 1. Ziel

Ein einziges, lokal laufendes Paket, in dem man **gestaltet, Bilder erzeugt, Videos
rendert und Fragen stellt** — mit dem DkZ-Designsystem als durchgehender Basis:

- ein **SVG-Generator**, fest eingebaut, in vier Sprachen identisch umgesetzt
- **HyperFrames** (HeyGen): aus HTML und SVG werden MP4-Videos
- eine **intelligente Steuerung**: Sätze wie „Mach ein Donut-Diagramm mit 40, 30, 30"
  oder „Wechsle ins Kontrast-Theme" werden zu Aktionen
- ein **lokaler, fest integrierter Design-Assistent** auf jeder Oberfläche — für
  alle Fragen, hauptsächlich zu Gestaltung

## 2. Wer macht was

| Technik | Aufgabe | Wie es belegt wird |
|---|---|---|
| **Vanilla-JS** | Oberfläche, kanonischer SVG-Generator (`svgkit.js`), Assistent-Dock, SVG Studio | Playwright-Rundgang, keine Konsolenfehler |
| **CSS3** | DkZ-Tokens, fünf Themes, alle Oberflächen | Rundgang in allen fünf Themes |
| **Python** | Server, Plug-ins, Steuerung, Assistent, Designrechner, SVG-Fassung | `test.sh` |
| **DuckDB** | Wissensbasis (Volltext per SQL), Chatverlauf, SVG-Bibliothek | echte Abfragen, nicht nur Ablage |
| **Rust** | `dsui-svg`: vollständige Zweitfassung des Generators | Byte-Parität gegen `svg/golden/` |
| **C** | `svgkit.c`: Teilmenge im C-Server, ohne Bibliotheken | Byte-Parität auf der Teilmenge |
| **Bun** | Werkzeugkette: DkZ-Prüfer, Größenbudget, Generator-Tests | `bun test`, `bun run lint` |
| **Node 22** | läuft nur für HyperFrames | echtes MP4, mit `ffprobe` geprüft |

Jede Sprache hat eine **echte, prüfbare Aufgabe** — keine bloße Namensnennung.

## 3. Architektur

```
 Browser (Vanilla-JS + CSS3, DkZ-Tokens, 5 Themes)
 ├─ SVG Studio ── svgkit.js (kanonisch)      ├─ Design-Assistent-Dock (jede Oberfläche)
 └─ Builder · Chat · Studio · Provider · …   └─ führt actions[] aus
        │ HTTP/JSON
 server.py (Python, Standardbibliothek) ── Plug-in-Mechanismus
 ├─ plugins/svgapi.py     → svgkit.py (+ Rust-CLI, C-Server als Gegenprobe)
 ├─ plugins/botapi.py     → designlab.py · store.py (DuckDB) · kb/*.jsonl
 ├─ plugins/controlapi.py → control.py  (Absicht erkennen → Aktion)
 └─ plugins/videoapi.py   → video/ (HyperFrames, Node 22, FFmpeg, Chromium)
```

Jede Fähigkeit ist ein **Plug-in**: eine eigene Datei, die ihre Routen selbst mitbringt.
`server.py` bleibt klein, und mehrere Arbeiten laufen parallel, ohne sich zu stören.

## 4. SVG-Generator

Neun Typen: `bar`, `line`, `donut`, `pattern`, `badge`, `icon`, `monogram`,
`gradient`, `graph` (Workflow als Diagramm). Die Farben kommen aus den
DkZ-Tokens und können zwei Wege gehen: **`vars`** folgt live dem Seiten-Theme,
**`static`** erzeugt eine eigenständige Datei.

Der Kern ist die **Parität**: dieselbe Beschreibung ergibt in JS, Python, Rust und
(Teilmenge) C **dasselbe Byte-für-Byte**. Das wird bei jedem Testlauf gegen
Referenzdateien geprüft. Importierte SVGs laufen durch eine Allowlist
(`sanitize`), weil SVG sonst Skripte einschleusen kann.

## 5. Intelligente Steuerung

Regelbasiert und deterministisch, Deutsch und Englisch. Ein Modell darf beim
Verstehen helfen, muss aber erklären, was es verstanden hat, und kann keine
Absicht erfinden. Zehn Absichten: SVG erzeugen, Theme wechseln, Einstellung setzen,
Template laden, Kommando ausführen, Oberfläche öffnen, Video rendern, Kontrast
berechnen, Palette bilden, Typo-Skala bilden.

Dieselbe Schnittstelle (`/api/control/route`) können **Terminal-Agenten** per
`llms.txt` nutzen — der Mensch sieht dabei den roten Monitorrand.

## 6. Design-Assistent

Fest eingebauter Dock, `Alt+D`, auf jeder Oberfläche. Antworten über Gestaltung
kommen aus einer **Wissensbasis in DuckDB** und nennen ihre Quelle. Rechenbares
(Kontrast nach WCAG, Paletten, Typo-Skalen) wird **berechnet**, nicht geraten.

- **Mit Modell** (Ollama, OpenAI/GPT, OpenRouter, NVIDIA, Hugging Face): das Modell
  formuliert auf Basis der gefundenen Einträge. Anzeige: „live".
- **Ohne Modell**: extraktive Antwort direkt aus den Einträgen. Anzeige: „lokal".
- **Fragen außerhalb der Gestaltung**: nur mit Modell. Ohne eines sagt der
  Assistent ehrlich, was er lokal kann — er erfindet nichts.

## 7. HyperFrames

Belegt vorhanden: npm `hyperframes` (HeyGen, Apache-2.0, Node ≥ 22; Headless-Chrome
und FFmpeg). Aus einem SVG oder einem Lauf-Bericht wird eine HTML-Komposition und
daraus ein MP4. Rendern läuft asynchron im Hintergrund; ist etwas nicht
einsatzbereit, sagt die Schnittstelle genau, was fehlt — es gibt kein
Platzhalter-Video.

## 8. DkZ-Konformität

Eingehalten: Tokens statt Hex, `esc()`/`textContent`, fünf Themes, kein Framework,
mobile-first, `— ausstehend —` statt erfundener Daten.

**Bekannte Abweichung:** die DkZ-18-KB-Regel (Auslegung: je JS-Modul). Der Bestand
`app.js` ist deutlich größer. Neue Module halten die Grenze ein, der Bun-Prüfer
**zeigt die Abweichung an**, statt sie zu verschweigen. Das Aufteilen von
`app.js` ist als eigener Schritt geplant (Phase 3), weil es ein riskanter Umbau ist,
der ein Sicherheitsnetz aus Tests braucht.

## 9. Skill → Workflow → Agent (DkZ R104)

| Stufe | Hier |
|---|---|
| **Skill** | `dkz-svg-generator`, `dkz-design-bot`, `dkz-video-render` — je ein `SKILL.md` |
| **Workflow** | Bau-Workflow in Wellen: Referenz → Portierungen → Oberfläche → Prüfung |
| **Agent** | `agents/design-qa.md` — der wiederkehrende Prüfer (Parität, Rundgang, DkZ-Lint, Budget) |

## 10. Phasen

| Phase | Inhalt | Stand |
|---|---|---|
| 0 | Plug-in-Mechanismus, Vertrag, Python-Umgebung mit DuckDB, Test-Schiene | **fertig** |
| 1 | SVG-Generator in JS · Python · Rust · C, Parität | siehe Status |
| 2 | Assistent, Wissensbasis, Steuerung, DuckDB-Speicher | siehe Status |
| 3 | DkZ-Theme-Schicht, SVG Studio, Dock | siehe Status |
| 4 | HyperFrames-Anbindung | siehe Status |
| 5 | Bun-Werkzeugkette, DkZ-Prüfer | siehe Status |
| 6 | Unabhängige Prüfung (Sicherheit, DkZ, Funktion, Ehrlichkeit) | siehe Status |
| später | `app.js` in ≤18-KB-Module zerlegen · C-Teilmenge erweitern · A2A-Adresse für den Assistenten · Wissensbasis erweitern | offen |

## 11. Ehrliche Grenzen

- Der Assistent ist **lokal nur so gut wie seine Wissensbasis**. Sie ist
  kuratiert, aber begrenzt; für freie Konversation braucht er ein Modell.
  Ollama ist hier nicht installiert — der Modellweg ist dadurch gebaut und gegen
  Attrappen getestet, **nicht gegen ein echtes Modell**.
- Ein ChatGPT-Abo allein genügt nicht für den GPT-Weg: die API wird separat
  abgerechnet.
- Die C-Fassung des SVG-Generators deckt eine **Teilmenge** ab.
- Videos rendern langsam; die Dauer hängt von CPU und Länge ab.
- Dieser Plan wurde in einer Umgebung gebaut und geprüft; nichts davon wurde auf
  anderer Hardware, anderen Betriebssystemen oder mit anderen Browsern gemessen.

## 12. Status

*— wird nach dem Bau mit gemessenen Ergebnissen gefüllt —*
