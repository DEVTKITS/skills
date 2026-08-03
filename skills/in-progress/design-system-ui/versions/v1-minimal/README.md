# Design System UI — Workflow Builder

Open-source, single-page workflow builder where **humans and agents** co-design
ComfyUI-style node graphs, execute them, and let a **Gemma 4 12B** evaluator
score the result and decide *iterate* or *done*. Every page exposes `/llms.txt`
so terminal agents can drive the whole thing without a browser.

> **Status: in-progress draft.** The core loop runs and is verified end-to-end.
> The Gemma 4 12B evaluator is currently **mocked** — swap one method to go live.

## What actually works today (verified)

| Piece | State |
|-------|-------|
| Python backend (`server.py`) | ✅ Runs on **stdlib only** — `python3 server.py`, no pip install |
| Single-page UI (`index.html` + `app.js` + `styles.css`) | ✅ Served at `/` |
| `/llms.txt` agent layer | ✅ Served on every deployment |
| Execute → evaluate → **verdict iterate/done** loop | ✅ Works (mock Gemma score) |
| Go client (`main.go`) | ✅ Builds with stdlib only, talks to backend |
| Rust client (`client.rs`) | ✅ Builds with `rustc`, no crates, talks to backend |
| Mojo agent layer (`agents.mojo`) | 📐 Design sketch — not required to run |
| Real Gemma 4 12B inference | ⏳ Mocked; wire to Ollama/HF to enable |

## Quick start (zero dependencies)

```bash
./start.sh                 # or: python3 server.py
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

## Going live with Gemma 4 12B

The evaluator is one method away from real. In `server.py`,
`GemmaEvaluator.infer(prompt)` returns a mock string. Point it at Ollama:

```bash
ollama pull gemma:12b && ollama serve
```

Then have `infer` POST to `http://localhost:11434/api/generate`
(`{"model":"gemma:12b","prompt":prompt,"stream":false}`) and read `.response`.
No other change is required — scoring, verdict, and the iterate loop already
consume the result.

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
├── start.sh            launcher
├── main.go             Go client / desktop bridge (stdlib only)
├── client.rs           Rust terminal client (stdlib only)
├── agents.mojo         Mojo agent-layer sketch
├── requirements.txt    optional deps (only for real Gemma via HF)
├── go.mod              Go module (no external requires)
└── examples/
    └── text-classifier.json   sample workflow
```

## Roadmap

- [x] Zero-dependency runnable backend + verified execute/evaluate/iterate loop
- [x] stdlib Go + Rust clients that talk to the backend
- [x] `/llms.txt` served on every deployment
- [ ] Real Gemma 4 12B via Ollama (wire `GemmaEvaluator.infer`)
- [ ] Live terminal streaming (Server-Sent Events / WebSocket)
- [ ] ComfyUI JSON import/export round-trip
- [ ] Go/Gio desktop + mobile shell embedding the UI
- [ ] Video / OpenMontage UI-UX template pack
