---
name: design-system-ui
description: Draft — single-page web UI where humans and agents co-design ComfyUI-style workflows, execute them, and let a Gemma 4 12B evaluator score and iterate. Open-source, agent-navigable via llms.txt.
disable-model-invocation: true
---

# Design System UI + Workflow Builder

Interactive web UI for designing, executing, and evaluating AI workflows with integrated ComfyUI, terminal execution, and local model evaluation — all without leaving the page.

> **Zwei Editionen:** `./start.sh` startet die **Normal**-Edition
> (Hamburger-Menü, Kacheln, neun Oberflächen), `./start.sh --openclaw` die
> **OpenCLAW**-Edition (dasselbe plus Agenten-Shell). Beide parallel
> betreibbar.
>
> **Status: in-progress draft.** The Python backend runs with zero dependencies
> (stdlib only) and the execute/evaluate/iterate loop works end-to-end with a
> **mock** Gemma 4 12B. The Go and Rust clients build against stdlib only. Mojo
> agent layer is a design sketch (Mojo not required to run the system).

## Purpose

Single-page application where users and agents collaborate to:
1. **Design workflows** via ComfyUI node graph
2. **Configure parameters** through intelligent prompting
3. **Execute workflows** in isolated terminals
4. **Evaluate results** with Gemma 4 12B local model
5. **Iterate** until workflow is optimized

## Features

- **ComfyUI Integration**: Drag-and-drop node-based workflow builder
- **In-Page Terminal**: Execute workflows without context switching
- **Live Evaluation**: Real-time feedback from Gemma 4 12B
- **Agent-Readable**: llms.txt surface for AI-driven operation
- **Pattern Templates**: Pre-built workflow patterns for common tasks
- **Cross-Platform**: Go desktop/mobile apps + web UI

## Technical Stack

- **Frontend**: Vanilla HTML5, CSS3+, Web Components
- **Backend**: Python (FastAPI/Flask) - Linux server
- **Agents**: Mojo (workflow definitions, agent logic)
- **Desktop/Mobile**: Go (Electron-like)
- **Model Eval**: Gemma 4 12B (local inference)
- **Orchestration**: ComfyUI

## Architecture

```
Web UI (single page, all operations in-context)
├─ ComfyUI Canvas (left panel)
├─ Config Panel (right panel - agent interaction)
├─ Terminal Output (bottom - execution logs)
├─ Evaluation Results (overlay - model feedback)
└─ llms.txt (surface layer - agent navigation)

Backend (Python server)
├─ Workflow executor
├─ Terminal session manager
├─ ComfyUI bridge
├─ Model inference (Gemma 4 12B)
└─ Iteration loop controller

Agents (Mojo layer)
├─ Workflow analyzers
├─ Optimization recommenders
├─ Parameter suggester
└─ Result evaluators
```

## Usage

### For Users
1. Design workflow with ComfyUI node editor
2. System prompts for parameter optimization
3. Execute workflow from UI button
4. View live terminal output
5. Review evaluation feedback
6. Iterate or finalize

### For Agents
1. Read `llms.txt` for system state and capabilities
2. Interact via config panel (click, input, suggest)
3. Evaluate results and recommend changes
4. Operate without page navigation
5. Self-describe actions via typed outputs

## API Endpoints

```
POST /api/workflow/create        - Create new workflow
POST /api/workflow/execute       - Execute workflow
GET  /api/workflow/status/:id    - Get execution status
POST /api/workflow/evaluate      - Request model evaluation
GET  /api/llms.txt               - Agent instruction layer
GET  /api/patterns               - Workflow pattern templates
POST /api/iterate                - Loop back for optimization
```

## llms.txt Structure

Every page serves llms.txt with:
- Current workflow state (JSON)
- Available actions (clickable endpoints)
- System capabilities (models, tools)
- Agent instruction manifesto
- Navigation guide (no page leaving needed)

## Example Flow

```
User/Agent → Design workflow (ComfyUI) 
          → Invoke execution 
          → See terminal output (in-page)
          → Get Gemma 4 12B evaluation
          → Agent suggests improvements via llms.txt
          → Click suggested changes
          → Re-execute 
          → Repeat or finalize
```

## Development Phases

**Phase 1** (MVP): Web UI + ComfyUI integration + Python executor
**Phase 2**: Gemma 4 12B evaluation loop + iteration controller  
**Phase 3**: Mojo agent layer + pattern templates
**Phase 4**: Go desktop/mobile apps

---

**User-invoked draft** — lives in `in-progress/` until it graduates to a stable bucket.
