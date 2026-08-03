# Design System UI — Workflow Builder

Open-source, single-page workflow builder where **humans and agents** co-design
ComfyUI-style node graphs, execute them, and let a **Gemma 4 12B** evaluator
score the result and decide *iterate* or *done*. Every page exposes `/llms.txt`
so terminal agents can drive the whole thing without a browser.

> **Status: in-progress draft.** The core loop runs and is verified by an
> automated 75-check test suite (`./test.sh`) plus a Playwright tour through
> all nine surfaces. Gemma inference goes **live automatically** when an
> Ollama server is running; otherwise a deterministic heuristic evaluator
> answers with the same response schema.

## What actually works today (verified by `./test.sh`)

| Piece | State |
|-------|-------|
| Python backend (`server.py`) | ✅ Runs on **stdlib only** — `python3 server.py`, no pip install |
| Single-page UI (`index.html` + `app.js` + `styles.css`) | ✅ Served at `/`; drag-drop nodes, shift-click to connect |
| `/llms.txt` agent layer | ✅ Served on every deployment |
| Execute → evaluate → **verdict iterate/done** loop | ✅ Works; UI applies suggestions + re-executes ("Apply All") |
| Gemma 4 12B via Ollama | ✅ Auto-detected (`gemma_live: true`); heuristic fallback when absent |
| Go client (`main.go`) | ✅ Builds with stdlib only, talks to backend |
| Rust client (`client.rs`) | ✅ Builds with `rustc`, no crates, talks to backend |
| Hamburger-Menü mit 9 Oberflächen (beide Editionen) | ✅ Builder, Chat+Canvas, Skills, Provider, Stock, GitHub, Agenten, Debug, Docs |
| Info-Buttons mit wechselnden Tipps | ✅ Blinken dezent, Popover rotiert Tipps alle 4,5 s, blätterbar |
| Ampelsystem | ✅ Grün/Gelb/Rot je Provider, Summe in Kopfzeile und Menü |
| Vorlesen (TTS) | ✅ 1× Klick markiert, 2× Klick liest per Browser-Sprachausgabe vor |
| Provider (OpenRouter, NVIDIA, HuggingFace, Firecrawl, Stability, Replicate, Stock, Ollama) | ✅ 10 Anbieter, 1-Klick per Env-Key, eigene via `CUSTOM_PROVIDERS` |
| Präfix-Syntax `/` `#` `@` `!` `$` `~` | ✅ Chat übersetzt sie in echte Builder-Aktionen |
| GitHub-Integration | ✅ Repo-Status + alle Skills im Repo als `~ref` |
| Debug & Stresstest | ✅ Selbsttest + Lasttest mit p95-Latenz, in UI und API |
| Agent visibility: red monitor-edge glow + click flashes | ✅ Open protocol (`POST /api/agent/activity`), UI polls 1s |
| Playwright demo agent (`demo/agent_demo.mjs`) | ✅ Drives the real UI in Chromium, broadcasts every step, screenshots |
| Chat tab | ✅ Live via Ollama when running; offline help otherwise |
| Stock media 1-click connect | ✅ Pexels/Unsplash/Pixabay — set the env API key, done |
| Integrations manifest (`/api/integrations`) | ✅ Honest builtin / adapter-ready / unverified split |
| **Mit OpenCLAW designen** (nur OpenCLAW-Edition) | ✅ Direkt aus der Agenten-Ansicht starten: Auftrag eintippen, zusehen (roter Rand), Ergebnis lesen — `adapters/openclaw_runner.py` fährt die Schleife auch ohne installierte Shell |
| Sicherheit | ✅ Importiertes Workflow-JSON wird nie als Markup interpretiert — DOM-XSS-Regressionstest (`demo/xss_check.mjs`) |
| Test suite (`test.sh`) | ✅ 75 Prüfungen inkl. polyglotter Clients und XSS-Test — CI-ready |
| Mojo agent layer (`agents.mojo`) | 📐 Design sketch — not required to run |

## Zwei Editionen, eine Codebasis

| Edition | Start | Was drin ist |
|---|---|---|
| **Normal** | `./start.sh` | Hamburger-Menü, Kacheln, alle neun Oberflächen. Du baust, Gemma bewertet. |
| **OpenCLAW** | `./start.sh --openclaw` | Alles aus Normal **plus** „Mit OpenCLAW designen": die Agenten-Shell baut und optimiert auf Zuruf. |

Menü, Kacheln, Ampel, Info-Tipps, TTS, Provider, Stock, GitHub, Debug — das
ist in **beiden** Editionen identisch. Der einzige Unterschied ist der
OpenCLAW-Bereich in der Agenten-Ansicht. Die aktive Edition steht als
Kennzeichen oben rechts in der Kopfzeile.

Beide lassen sich parallel betreiben:

```bash
PORT=8000 ./start.sh &              # Normal
PORT=8001 ./start.sh --openclaw &   # OpenCLAW
```

In der Normal-Edition antworten die `/api/openclaw/*`-Endpunkte bewusst mit
404 und nennen den Umschaltbefehl — kein toter Knopf, keine Attrappe.

## Quick start (zero dependencies)

```bash
./start.sh                 # Normal-Edition, oder: python3 server.py
# UI:     http://localhost:8000/
# agents: http://localhost:8000/llms.txt
```

Drive it from the terminal in any language:

```bash
# Rust
rustc client.rs -o dsui && ./dsui execute examples/text-classifier.json

# Go
go run main.go run examples/text-classifier.json

# curl
curl -X POST http://localhost:8000/api/workflow/execute \
  -H 'Content-Type: application/json' -d @examples/text-classifier.json
```

## The agent loop (from `/llms.txt`)

1. `GET /api/patterns` — list starter templates.
2. `POST /api/workflow/execute` — run the graph.
3. `POST /api/workflow/evaluate` — Gemma scores it, returns `verdict`.
4. `verdict == "iterate"` → apply `suggestions[].action`, go to 2.
   `verdict == "done"` → report score + feedback.

## API

| Method | Path | Purpose |
|--------|------|---------|
| GET  | `/` | Single-page UI |
| GET  | `/llms.txt` | Agent instruction layer |
| GET  | `/health` | Liveness |
| GET  | `/api/patterns` | Template catalog |
| POST | `/api/workflow/execute` | Run a workflow |
| POST | `/api/workflow/evaluate` | Gemma 4 12B scoring + verdict |
| POST | `/api/workflow/suggest-params` | Suggested params for a node type |

Full request/response shapes and node types are documented in
[`llms.txt`](./llms.txt) — the same file agents read.

## Architecture

```
                 ┌────────────── single page, no navigation ──────────────┐
   Human ──────► │  index.html + app.js  (ComfyUI canvas · terminal · eval) │
   Agent ──────► │  /llms.txt  (every action as an HTTP call)               │
                 └───────────────────────────┬─────────────────────────────┘
                                              │ REST/JSON
   Rust client (client.rs) ──┐               ▼
   Go client   (main.go)    ──┼──────► server.py  (stdlib HTTP, zero deps)
   curl / any agent         ──┘          ├─ WorkflowExecutor  (topological run)
                                          └─ GemmaEvaluator    (score + verdict)
                                                     │
                                          swap infer() ─► Ollama gemma:12b / HF
```

## Going live with Gemma 4 12B (zero code changes)

Start Ollama with a Gemma model — the evaluator detects it and switches to
live critique automatically, falling back to the heuristic when unreachable:

```bash
ollama pull gemma:12b && ollama serve
python3 server.py     # evaluate responses now carry "gemma_live": true
```

Configuration via environment: `OLLAMA_URL` (default `http://localhost:11434`),
`GEMMA_MODEL` (default `gemma:12b`), `GEMMA_TIMEOUT` (default `60`).

## Testing

```bash
./test.sh                    # 75 Prüfungen: Endpunkte, Iterationsschleife, Provider,
                             # Hilfe, Debug, Repo, UI-Oberflächen, Go- und Rust-Clients
cd demo && npm install playwright
CHROMIUM_PATH=/opt/pw-browsers/chromium node ui_tour.mjs http://localhost:8000 ./shots
                             # Playwright-Rundgang durch alle 9 Oberflächen
```

## Watching the AI work (agent visibility)

While any agent operates the builder, the whole viewport gets a pulsing
**red monitor-edge glow**, an activity ticker shows `KI arbeitet: <actor> ·
<action> → <target>`, and the element being clicked **flashes red** — so a
human always sees what the machine is doing. The protocol is open HTTP
(`POST /api/agent/activity`, spec in [`llms.txt`](./llms.txt)) and works for
Playwright, browser-use, terminal agents, or any LLM.

Reference implementation with screenshots:

```bash
cd demo && npm install playwright
CHROMIUM_PATH=/path/to/chromium node agent_demo.mjs http://localhost:8000 ./shots
```

## On-demand agents — OpenCLAW, OpenHands, OpenManus, browser-use

> **Schreibweise: OpenCLAW** — die Agenten-Shell. Nicht „OpenCloud", nicht
> „opencloude", und nicht Anthropics Claude. Verschiedene Dinge.

Schwere Agenten-Frameworks sollen nicht dauerhaft mitlaufen. Das unterstützte
Muster: **aus lassen** → Auftrag kommt herein → Framework starten → gegen
diese HTTP-Oberfläche arbeiten lassen (es braucht nur `llms.txt`) → jeden
Schritt über das Aktivitätsprotokoll melden → bei `state=done` herunterfahren.

In der Oberfläche: **Menü → Agenten & Sichtbarkeit → „Mit OpenCLAW designen"**.
Auftrag eintippen, starten — der Monitorrand leuchtet rot, das Aktivitäts-
protokoll zeigt jeden Schritt, am Ende steht das Urteil.

Auf der Kommandozeile:

```bash
python3 adapters/openclaw_runner.py "Baue einen RAG-Workflow und optimiere ihn"
python3 adapters/openclaw_runner.py --pattern eval-loop
```

Er meldet `session-start`, wählt anhand des Auftragstexts ein Muster, führt
aus, lässt bewerten, wendet die Empfehlungen an und wiederholt, bis
`verdict=done` — währenddessen leuchtet der Monitorrand durchgehend rot.

| Variable | Bedeutung |
|---|---|
| `OPENCLAW_CMD` | Startbefehl der Shell, z. B. `openclaw run --headless`. **Fehlt sie, fährt der Runner die Schleife selbst** — sofort nutzbar, wird automatisch echt, sobald die Shell da ist. |
| `OPENCLAW_TIMEOUT` | Harter Abbruch in Sekunden (Standard 300) |
| `MAX_ROUNDS` | Iterationsgrenze (Standard 5) |
| `BASE_URL` | Ziel-UI (Standard `http://localhost:8000`) |

Für **OpenHands** oder **OpenManus** genügt es, `OPENCLAW_CMD` auf deren
Binary zu zeigen — der Runner ist frameworkneutral.

`GET /api/integrations` meldet ehrlich, was eingebaut ist, was adapter-ready
ist und welche Namen sich nicht als existierende Projekte verifizieren ließen
(openmontage, openstream, odysseus, nanobot).

## Files

```
design-system-ui/
├── SKILL.md            skill definition (user-invoked draft)
├── README.md           this file
├── llms.txt            agent instruction layer (served at /llms.txt)
├── index.html          single-page UI
├── styles.css          responsive, light/dark aware
├── app.js              frontend logic (canvas, terminal, eval)
├── server.py           zero-dependency backend (execute + evaluate + iterate)
├── providers.py        Provider-Registry mit Ampelstatus (10 Anbieter + custom)
├── helpdata.py         Hilfethemen, wechselnde Tipps, Präfix-Syntax
├── adapters/
│   └── openclaw_runner.py  OpenCLAW On-Demand-Runner (frameworkneutral)
├── start.sh            launcher
├── test.sh             smoke-test suite (54 checks, CI-ready)
├── main.go             Go client / desktop bridge (stdlib only)
├── client.rs           Rust terminal client (stdlib only)
├── agents.mojo         Mojo agent-layer sketch
├── requirements.txt    optional deps (only for real Gemma via HF)
├── go.mod              Go module (no external requires)
├── demo/
│   ├── agent_demo.mjs        Playwright-Agent, der die UI sichtbar bedient
│   └── ui_tour.mjs           Regressionsrundgang durch alle Oberflächen
└── examples/
    └── text-classifier.json  Beispiel-Workflow
```

## Roadmap

- [x] Zero-dependency runnable backend + verified execute/evaluate/iterate loop
- [x] stdlib Go + Rust clients that talk to the backend
- [x] `/llms.txt` served on every deployment
- [x] Real Gemma 4 12B via Ollama with automatic heuristic fallback
- [x] Automated smoke-test suite (`test.sh`, 75 checks inkl. DOM-XSS-Regression)
- [x] UI iterate loop: real suggestions, Apply All → auto re-execute
- [ ] Live terminal streaming (Server-Sent Events / WebSocket)
- [ ] ComfyUI JSON import/export round-trip
- [ ] Go/Gio desktop + mobile shell embedding the UI
- [ ] Video / OpenMontage UI-UX template pack
