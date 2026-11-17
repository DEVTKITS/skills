# Plan — DkZ Design-Paket (v7)

*Ein DkZ-Projekt.* Überarbeitete Fassung nach Durchsicht und Abgleich mit
[Open Design](https://github.com/nexu-io/open-design) (Apache-2.0).

## 1. Ziel

Ein lokal laufendes Paket, in dem man **gestaltet, Bilder und Seiten erzeugt, Videos
rendert, Entwürfe prüfen lässt und Fragen stellt** — auf dem DkZ-Designsystem:

- **SVG-Generator**, fest eingebaut, in vier Sprachen Byte für Byte identisch
- **Vorlagen und Artefakte** (Landingpage, Dashboard, Mobile-App, Deck …) mit Sandbox-Vorschau
- **Design-Systeme** im portablen `DESIGN.md`-Format, DkZ als Standard
- **Lint und 5-Dimensionen-Kritik** — messbar, ohne erfundene Punktzahlen
- **HyperFrames**: aus HTML und SVG werden MP4-Videos
- **Intelligente Steuerung**: Sätze werden zu Aktionen, fehlende Angaben werden als Formular erfragt
- **Lokaler, fest integrierter Design-Assistent** auf jeder Oberfläche
- **Coding-Agenten** (Claude Code, opencode, pi, OpenClaw …) per MCP und Laufzeit-Register

## 2. Was die Durchsicht am alten Plan gefunden hat

| Befund | Änderung |
|---|---|
| Der erste Bau-Lauf scheiterte vollständig am Kontingent; alle 15 Agenten brachen sofort ab | Wellen mit Abhängigkeiten statt Massenstart; ein ausfallender Track stoppt nur seine Nachfolger, die Prüfung läuft trotzdem |
| `svgkit.js` sollte neun Typen **und** den Sanitizer unter 18 KB halten — kaum machbar | Sanitizer als eigenes Modul `svgkit-safe.js`; beide halten die Grenze |
| Der Test-Lader las `tests/run-one.sh` selbst als Test ein | behoben (`tests/[0-9]*.sh`) |
| Kein Schutz gegen Anfragen an interne Adressen über eigene Provider-URLs (SSRF) | SSRF-Prüfung in `providers.py` |
| Die erste Anforderung — „die UI befragt den User für den optimalen Workflow" — war nicht abgedeckt | Rückfragen als Formular (`form`-Aktion) |
| „Für opencode und pi" war nur über HTTP gelöst | MCP-Server über stdio und ein Laufzeit-Register für Coding-Agenten |
| Der ursprüngliche Wunsch war ein Claude-Design-Nachbau | Claude-Design-ZIP-Import, Vorlagen, Design-Systeme, Kritik — die Kernfunktionen von Open Design |
| Kein Abschlussschritt für Hilfe, Doku, Bilder, Version | eigener Integrationsschritt nach der Prüfung (§11) |

## 3. Aus Open Design übernommen — und was nicht

| Open Design | Hier |
|---|---|
| `DESIGN.md`-Designsysteme | **übernommen** — Format, Import per Git/Upload, DkZ als `DESIGN.md`. Fremde Markenpakete werden nicht mitgeliefert, sind aber importierbar |
| Design-Vorlagen mit Gerätrahmen | **übernommen** — neun Vorlagen, Desktop/Tablet/Telefon |
| Sandbox-Vorschau (`srcdoc`-iframe) | **übernommen** — ohne `allow-same-origin` |
| Export HTML · ZIP · Markdown · PDF (Druck) · MP4 | **übernommen** |
| Artefakt-Lint + 5-Dimensionen-Kritik | **übernommen** — und an die Gemma-Bewertungsschleife gekoppelt |
| Claude-Design-ZIP-Import | **übernommen** — mit Zip-Slip- und Zip-Bomben-Schutz |
| Agent-Laufzeiten (27 CLIs) | **übernommen** als Register mit Erkennung; Start nur auf Zuruf |
| MCP-Server | **übernommen** — stdio, Standardbibliothek |
| BYOK-Proxy mit SSRF-Schutz | **übernommen** — Provider gab es schon, der Schutz fehlte |
| Rückfrage-Formular | **übernommen** |
| Next.js, React, Electron | **nicht** — DkZ verbietet Frameworks |
| Cloud-Konto, Marktplatz, Telemetrie | **nicht** — das Paket ist lokal |
| 151 Marken-Designsysteme mitliefern | **nicht** — Verwechslungsgefahr; Import ist möglich |

## 4. Wer macht was

| Technik | Aufgabe | Beleg |
|---|---|---|
| **Vanilla-JS** | Oberfläche, kanonischer SVG-Generator, Dock, SVG Studio, Artefakt- und System-Ansicht | Playwright, keine Konsolenfehler |
| **CSS3** | DkZ-Tokens, fünf Themes, alle Oberflächen | Rundgang in allen fünf Themes |
| **Python** | Server, Plug-ins, Steuerung, Assistent, Designrechner, Vorlagen, Lint, MCP | `test.sh` |
| **DuckDB** | Wissensbasis (BM25 per SQL), Verlauf, SVG-Bibliothek, Artefakte | echte Abfragen |
| **Rust** | `dsui-svg`: vollständige Zweitfassung des Generators | Byte-Parität |
| **C** | `svgkit.c`: Teilmenge im C-Server | Byte-Parität auf der Teilmenge |
| **Bun** | DkZ-Prüfer, Größenbudget, Generator-Tests, Bündeln | `bun test`, `bun run lint` |
| **Node 22** | nur für HyperFrames | echtes MP4, mit `ffprobe` geprüft |

## 5. Architektur

```
 Browser (Vanilla-JS + CSS3, DkZ-Tokens, 5 Themes)
 ├─ SVG Studio · Vorlagen & Artefakte · Design-Systeme · Builder · Chat · …
 └─ Design-Assistent-Dock (jede Oberfläche, Alt+D) → führt actions[] aus
        │ HTTP/JSON
 server.py (Python, Standardbibliothek) ── Plug-in-Mechanismus
 ├─ svgapi       → svgkit.py   (+ Rust-CLI, C-Server)
 ├─ botapi       → designlab · store (DuckDB) · kb/*.jsonl
 ├─ controlapi   → control.py  (Absicht → Aktion oder Rückfrage-Formular)
 ├─ systemsapi   → designsystems.py (DESIGN.md)
 ├─ artifactsapi → artifacts.py (Vorlagen, Export, Claude-Design-Import)
 ├─ critiqueapi  → critique.py (Lint, 5 Dimensionen)
 ├─ runtimesapi  → runtimes.py (Coding-Agenten)
 └─ videoapi     → video/ (HyperFrames)
 mcp_server.py (stdio) ── dieselben Fähigkeiten für Claude Code, opencode, pi, …
```

## 6. SVG-Generator

Neun Typen (`bar line donut pattern badge icon monogram gradient graph`), Farben aus den
DkZ-Tokens, zwei Modi (`vars` folgt dem Seiten-Theme, `static` ist eigenständig). Kern ist
die **Byte-Parität** über JS, Python, Rust und (Teilmenge) C, geprüft gegen
Referenzdateien. Importierte SVGs laufen durch eine Allowlist.

## 7. Vorlagen, Design-Systeme, Kritik

- **Vorlagen** erzeugen ohne Modell ein vollständiges Artefakt aus Feldern; mit Modell
  dürfen Texte vorgeschlagen werden. Vorschau im Sandbox-iframe mit Gerätrahmen.
- **Design-Systeme**: `DESIGN.md` wird gelesen, die Tokens fließen in Vorlagen und
  Assistent. Fremdes Markdown ist Fremdtext — CSS-Werte nur über eine Allowlist.
- **Kritik**: Lint misst (Kontrast, Token-Treue, `alt`, Überschriften, Tippziele …). Die
  fünf Dimensionen werden **ohne Modell nur dort bewertet, wo der Lint Belege liefert** —
  sonst `— ausstehend —`. Mit Modell kommen die übrigen dazu, jeweils mit Belegen.
- **Kopplung**: Die bisherige Bewertungsschleife (execute → evaluate → iterate) kann die
  Kritik als zusätzliche Hürde nutzen.

## 8. Steuerung und Assistent

Regelbasiert, deutsch und englisch, zehn Absichten. Fehlen Angaben, kommt ein
**Formular** statt eines Ratens. Ein Modell darf beim Verstehen helfen, kann aber keine
Absicht erfinden. Der Assistent antwortet aus der Wissensbasis mit Quelle, rechnet
Rechenbares (WCAG, Paletten, Skalen) und sagt ehrlich, ob ein Modell geantwortet hat.

## 9. Coding-Agenten

Das Laufzeit-Register erkennt installierte CLIs und startet sie nur auf Zuruf in einem
eigenen Projektordner. Der **MCP-Server** gibt Claude Code, opencode, pi und anderen die
Werkzeuge direkt — samt Einbau-Anleitung, soweit belegbar.

## 10. DkZ-Konformität

Tokens statt Hex, `esc()`/DOM-APIs, fünf Themes, kein Framework, mobile-first,
`— ausstehend —` statt erfundener Daten. **Bekannte Abweichung:** `app.js` verletzt die
18-KB-Regel; neue Module halten sie ein, der Bun-Prüfer zeigt die Altlast an. Das
Zerlegen von `app.js` ist ein eigener Schritt.

**Skill → Workflow → Agent (DkZ R104):** Skills `dkz-svg-generator`, `dkz-design-bot`,
`dkz-video-render`; der Bau-Workflow in Wellen; ein wiederkehrender Prüf-Agent
`agents/design-qa.md` (Parität, Rundgang, Lint, Budget).

## 11. Ablauf

| Welle | Tracks | hängt ab von |
|---|---|---|
| **1** | `svgkit-js` · `designlab-kb-store` · `designbot-ui` · `video-hyperframes` · `dkz-theme` · `designsystems` | — |
| **2** | `svgkit-py` · `svgkit-rs` · `svgkit-c` · `svg-studio-ui` · `bun-toolchain` | `svgkit-js` |
| **2** | `templates-artifacts` | `designsystems`, `svgkit-js` |
| **3** | `control-router` | `svgkit-py`, `designlab-kb-store` |
| **3** | `critique-lint` | `svgkit-py`, `designlab-kb-store` |
| **3** | `agent-runtimes-mcp` | `svgkit-py`, `designlab-kb-store`, `templates-artifacts` |
| **Prüfung** | Sicherheit · DkZ · Funktion/Parität · Ehrlichkeit — ändern nichts | alle |
| **Integration** | Hilfetexte, `llms.txt`, README, Demo-Bilder, Version **v7**, Commit | Prüfung |

Verträge, Dateibesitz und Ports: [`docs/CONTRACT.md`](docs/CONTRACT.md).

## 12. Ehrliche Grenzen

- Ohne Modell beantwortet der Assistent nur Gestaltungsfragen aus der Wissensbasis.
  **Ollama ist hier nicht installiert** — der Modellweg wird gegen einen Fake getestet.
- Ein ChatGPT-Abo genügt nicht für den GPT-Weg; die API wird separat abgerechnet.
- Die C-Fassung des Generators deckt eine Teilmenge ab.
- Von den Coding-Agent-CLIs ist hier nur `claude` installiert; opencode, pi, Codex und
  OpenClaw fehlen. Erkennung und Start werden gegen Attrappen getestet; der MCP-Server
  lässt sich mit `claude` real einbinden und prüfen.
- Die Kritik bewertet ohne Modell nur, was messbar ist.
- Alles wurde in einer Umgebung gebaut und geprüft.

## 13. Status

*— wird nach dem Bau mit gemessenen Ergebnissen gefüllt —*
