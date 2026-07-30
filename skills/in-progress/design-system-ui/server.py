#!/usr/bin/env python3
"""
Design System UI - Zero-dependency backend (Python standard library only).

Runs with plain `python3 server.py` — no pip install required. Serves the
single-page UI, exposes /llms.txt for agents, and provides the workflow
execute / evaluate / suggest-params / patterns API endpoints with a mock
Gemma 4 12B evaluator (swap `GemmaEvaluator.infer` for a real backend).

Design goals:
  - Actually runs in a bare environment (no FastAPI/torch needed).
  - Single-page: every operation is an API call, no page navigation.
  - Agent-friendly: /llms.txt describes every action a bot can take.
"""

from __future__ import annotations

import json
import logging
import mimetypes
import os
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("design-system-ui")

ROOT = Path(__file__).resolve().parent
STATIC_FILES = {"index.html", "app.js", "styles.css", "llms.txt"}


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


# --------------------------------------------------------------------------- #
# Workflow execution                                                          #
# --------------------------------------------------------------------------- #
class WorkflowExecutor:
    """Executes a workflow node-by-node in dependency order."""

    def validate(self, wf: dict) -> list[str]:
        errors: list[str] = []
        nodes = wf.get("nodes", [])
        if not nodes:
            errors.append("Workflow must contain at least one node")
        if not any(str(n.get("type", "")).endswith("input") for n in nodes):
            errors.append("No input node found")
        if not any(str(n.get("type", "")).endswith("output") for n in nodes):
            errors.append("No output node found")
        return errors

    def _topo_order(self, wf: dict) -> list[dict]:
        """Return nodes in a stable topological order based on edges."""
        nodes = {n["id"]: n for n in wf.get("nodes", [])}
        indeg = {nid: 0 for nid in nodes}
        adj: dict[str, list[str]] = {nid: [] for nid in nodes}
        for e in wf.get("edges", []):
            src = e.get("from") or e.get("frm") or e.get("source")
            dst = e.get("to") or e.get("target")
            if src in nodes and dst in nodes:
                adj[src].append(dst)
                indeg[dst] += 1
        queue = [nid for nid, d in indeg.items() if d == 0]
        order: list[str] = []
        while queue:
            nid = queue.pop(0)
            order.append(nid)
            for nxt in adj[nid]:
                indeg[nxt] -= 1
                if indeg[nxt] == 0:
                    queue.append(nxt)
        # Append any nodes left out by cycles so nothing is silently dropped.
        for nid in nodes:
            if nid not in order:
                order.append(nid)
        return [nodes[nid] for nid in order]

    def execute(self, wf: dict) -> dict:
        wf_id = wf.get("id", "workflow")
        lines: list[str] = [
            "🚀 Workflow execution started",
            f"Workflow ID: {wf_id}",
            f"Nodes: {len(wf.get('nodes', []))}, Edges: {len(wf.get('edges', []))}",
        ]

        errors = self.validate(wf)
        if errors:
            lines += [f"❌ {e}" for e in errors]
            return {
                "status": "error",
                "output": "\n".join(lines),
                "results": {},
                "timestamp": now_iso(),
            }

        context: dict[str, dict] = {}
        for node in self._topo_order(wf):
            ntype = node.get("type", "")
            nid = node.get("id", "?")
            if ntype == "llm-node":
                model = node.get("params", {}).get("model", "gemma-4")
                temp = node.get("params", {}).get("temperature", 0.7)
                lines.append(f"🤖 {nid}: LLM ({model}, temp={temp})")
                context[nid] = {"type": "llm_output", "model": model,
                                "output": f"[mock output from {model}]"}
            elif ntype.endswith("input"):
                lines.append(f"📝 {nid}: {ntype}")
                context[nid] = {"type": "input", "value": ""}
            elif ntype.endswith("output"):
                lines.append(f"📤 {nid}: {ntype}")
                context[nid] = {"type": "output", "status": "ok"}
            elif ntype in ("transform", "filter"):
                icon = "⚡" if ntype == "transform" else "🔍"
                lines.append(f"{icon} {nid}: {ntype}")
                context[nid] = {"type": ntype, "status": "ok"}
            else:
                lines.append(f"⚠️ {nid}: unknown node type '{ntype}'")
                context[nid] = {"type": "unknown"}

        lines.append("✓ Workflow execution completed")
        return {
            "status": "success",
            "output": "\n".join(lines),
            "results": context,
            "timestamp": now_iso(),
        }


# --------------------------------------------------------------------------- #
# Gemma 4 12B evaluator — live via Ollama when reachable, heuristic fallback  #
# --------------------------------------------------------------------------- #
class GemmaEvaluator:
    """
    Scores a workflow's output.

    If an Ollama server is reachable (OLLAMA_URL, default localhost:11434) the
    configured Gemma model (GEMMA_MODEL, default "gemma:12b") writes the
    qualitative critique. Otherwise a deterministic heuristic produces the
    score and feedback, so the system always works — with or without a GPU.
    """

    MODEL = os.environ.get("GEMMA_MODEL", "gemma:12b")
    OLLAMA_URL = os.environ.get("OLLAMA_URL", "http://localhost:11434")
    TIMEOUT_S = float(os.environ.get("GEMMA_TIMEOUT", "60"))

    def infer(self, prompt: str) -> str | None:
        """Call Ollama /api/generate. Returns None when unreachable/failing."""
        import urllib.error
        import urllib.request

        try:
            req = urllib.request.Request(
                f"{self.OLLAMA_URL}/api/generate",
                data=json.dumps({"model": self.MODEL, "prompt": prompt,
                                 "stream": False}).encode(),
                headers={"Content-Type": "application/json"},
            )
            with urllib.request.urlopen(req, timeout=self.TIMEOUT_S) as resp:
                return json.loads(resp.read()).get("response")
        except (urllib.error.URLError, OSError, TimeoutError,
                json.JSONDecodeError) as exc:
            log.debug("Ollama unreachable (%s) — using heuristic evaluator", exc)
            return None

    def _critique_prompt(self, wf: dict, results: dict, score: int) -> str:
        return (
            "You are a strict workflow reviewer. Given this node-graph "
            "workflow and its execution results, write a concise critique "
            "(max 120 words): what works, what is missing, one concrete "
            "improvement.\n\n"
            f"Workflow JSON:\n{json.dumps(wf)[:4000]}\n\n"
            f"Execution results:\n{json.dumps(results)[:2000]}\n\n"
            f"Heuristic score: {score}/100."
        )

    def evaluate(self, wf: dict, results: dict) -> dict:
        nodes = wf.get("nodes", [])
        edges = wf.get("edges", [])
        score = 50
        if len(nodes) >= 3:
            score += 20
        if edges:
            score += 15
        if any(n.get("type") == "llm-node" for n in nodes):
            score += 15
        score = min(100, score)

        fb = [f"Workflow Evaluation Report ({self.MODEL})",
              f"Score: {score}/100", "",
              f"Structure: {len(nodes)} nodes, {len(edges)} connections", ""]
        fb.append("✓ Contains LLM node"
                  if any(n.get("type") == "llm-node" for n in nodes)
                  else "⚠️ Consider adding an LLM node for intelligent processing")
        fb.append("✓ Nodes are connected" if edges
                  else "⚠️ Nodes are disconnected")

        suggestions = []
        if not any(n.get("type") == "llm-node" for n in nodes):
            suggestions.append({"title": "Add LLM node",
                                "description": "Add AI-powered processing",
                                "action": "ADD_NODE(type='llm-node')"})
        if len(edges) < max(0, len(nodes) - 1):
            suggestions.append({"title": "Connect more nodes",
                                "description": "Create data flow between nodes",
                                "action": "CONNECT_NODES(fromId, toId)"})

        # Live model critique when Ollama is up; heuristic text otherwise.
        critique = self.infer(self._critique_prompt(wf, results, score))
        gemma_live = critique is not None
        if gemma_live:
            fb += ["", f"— {self.MODEL} critique —", critique.strip()]

        # `verdict` drives the iterate-or-finish loop.
        verdict = "done" if score >= 85 else "iterate"
        return {"score": score, "feedback": "\n".join(fb),
                "suggestions": suggestions, "verdict": verdict,
                "model": self.MODEL, "gemma_live": gemma_live,
                "timestamp": now_iso()}


# --------------------------------------------------------------------------- #
# Agent activity feed — powers the red "AI is working" glow and click flashes #
# --------------------------------------------------------------------------- #
class ActivityFeed:
    """
    Open protocol for making agent work visible to humans.

    Any agent (Playwright script, terminal agent, Claude/GPT/DeepSeek/opencode/
    pi) POSTs what it is doing; the UI polls and renders a red monitor-edge
    glow while an agent is active plus a flash on the element being acted on.
    """

    MAX_EVENTS = 200
    ACTIVE_WINDOW_S = 6.0  # glow stays on this long after the last event

    def __init__(self) -> None:
        self.events: list[dict] = []
        self._seq = 0

    def post(self, event: dict) -> dict:
        import time
        self._seq += 1
        entry = {
            "seq": self._seq,
            "actor": str(event.get("actor", "agent"))[:64],
            "action": str(event.get("action", "work"))[:64],
            "target": str(event.get("target", ""))[:256],   # CSS selector or node id
            "detail": str(event.get("detail", ""))[:512],
            "state": event.get("state", "working"),          # working | done | error
            "monotonic": time.monotonic(),
            "timestamp": now_iso(),
        }
        self.events.append(entry)
        self.events = self.events[-self.MAX_EVENTS:]
        return {"ok": True, "seq": entry["seq"]}

    def snapshot(self, since: int = 0) -> dict:
        import time
        now = time.monotonic()
        active = any(
            e["state"] == "working" and now - e["monotonic"] < self.ACTIVE_WINDOW_S
            for e in self.events[-20:]
        )
        fresh = [{k: v for k, v in e.items() if k != "monotonic"}
                 for e in self.events if e["seq"] > since]
        return {"active": active, "events": fresh[-50:],
                "latest_seq": self._seq}


# --------------------------------------------------------------------------- #
# Chat — routes to Ollama when live, helpful fallback otherwise               #
# --------------------------------------------------------------------------- #
class ChatService:
    def reply(self, message: str, history: list[dict]) -> dict:
        prompt = ("You are the assistant inside a workflow-builder UI. "
                  "Help the user design, execute and iterate node workflows. "
                  "Be concise.\n\n")
        for turn in history[-6:]:
            prompt += f"{turn.get('role', 'user')}: {turn.get('content', '')}\n"
        prompt += f"user: {message}\nassistant:"
        answer = EVALUATOR.infer(prompt)
        if answer is not None:
            return {"reply": answer.strip(), "model": EVALUATOR.MODEL,
                    "live": True}
        return {"reply": ("(offline assistant) Ollama is not running, so I can "
                          "only give static help: load a template from the "
                          "Templates tab, connect nodes with shift+click, then "
                          "Execute → the Gemma evaluator scores it and "
                          "suggests iterations. Start `ollama serve` for a "
                          "live chat model."),
                "model": "heuristic", "live": False}


# --------------------------------------------------------------------------- #
# Stock providers — 1-click connect via API keys in the environment           #
# --------------------------------------------------------------------------- #
class StockProviders:
    """Image/video stock search. A provider is 'connected' once its API key
    env var is set — that is the whole 1-click setup."""

    PROVIDERS = {
        "pexels": {"env": "PEXELS_API_KEY",
                   "url": "https://api.pexels.com/v1/search?query={q}&per_page={n}",
                   "auth_header": "Authorization", "kind": "photos+videos"},
        "unsplash": {"env": "UNSPLASH_ACCESS_KEY",
                     "url": "https://api.unsplash.com/search/photos?query={q}&per_page={n}",
                     "auth_header": "Authorization", "auth_prefix": "Client-ID ",
                     "kind": "photos"},
        "pixabay": {"env": "PIXABAY_API_KEY",
                    "url": "https://pixabay.com/api/?key={key}&q={q}&per_page={n}",
                    "auth_header": None, "kind": "photos+videos"},
    }

    def status(self) -> dict:
        return {name: {"connected": bool(os.environ.get(cfg["env"])),
                       "env_var": cfg["env"], "kind": cfg["kind"]}
                for name, cfg in self.PROVIDERS.items()}

    def search(self, provider: str, query: str, per_page: int = 10) -> dict:
        import urllib.error
        import urllib.parse
        import urllib.request

        cfg = self.PROVIDERS.get(provider)
        if not cfg:
            return {"error": f"unknown provider '{provider}'",
                    "available": list(self.PROVIDERS)}
        key = os.environ.get(cfg["env"], "")
        if not key:
            return {"error": f"{provider} not connected",
                    "connect": f"set env var {cfg['env']} and restart — "
                               "that is the whole 1-click setup"}

        q = urllib.parse.quote(query)
        url = cfg["url"].format(q=q, n=per_page, key=key)
        headers = {}
        if cfg.get("auth_header"):
            headers[cfg["auth_header"]] = cfg.get("auth_prefix", "") + key
        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=20) as resp:
                return {"provider": provider, "query": query,
                        "results": json.loads(resp.read())}
        except (urllib.error.URLError, OSError, TimeoutError) as exc:
            return {"error": f"{provider} request failed: {exc}"}


EXECUTOR = WorkflowExecutor()
EVALUATOR = GemmaEvaluator()
ACTIVITY = ActivityFeed()
CHAT = ChatService()
STOCK = StockProviders()

# What is genuinely built in vs. adapter-ready vs. unverified. Served at
# /api/integrations so agents and humans see honest capability status.
INTEGRATIONS = {
    "builtin": {
        "ollama-gemma": "Auto-detected local inference (OLLAMA_URL); evaluator + chat go live automatically",
        "agent-activity-protocol": "POST /api/agent/activity → red monitor-edge glow + element flash in the UI",
        "stock-providers": "Pexels / Unsplash / Pixabay via env API keys (see /api/stock/status)",
        "llms-txt": "Extra-detailed agent surface at /llms.txt",
    },
    "adapter_ready": {
        "playwright": "demo/agent_demo.mjs drives the real UI via the activity protocol (chromium preinstalled in this env)",
        "browser-use": "Any browser-use agent can drive the UI; broadcast steps via the activity protocol",
        "openmanus": "Point an OpenManus tool at the HTTP API in /llms.txt; on-demand, not resident",
        "openhands": "Same HTTP surface; run only when a task is queued (on-demand pattern documented in README)",
        "github-cli": "Design/skill import-export via git; any agent with gh/git credentials can sync the repo",
        "claude|gpt|deepseek|opencode|pi-agent": "All drive the same /llms.txt HTTP surface — no SDK needed",
    },
    "unverified_roadmap": {
        "openmontage / openstream / openclaw / odysseus / nanobot":
            "Could not verify these as existing installable projects; the "
            "activity protocol + HTTP API are the stable interfaces such "
            "tools would plug into once identified",
    },
}

PATTERNS = {
    "text-classifier": {"name": "Text Classifier",
                        "description": "Classify text into categories", "icon": "🏷️"},
    "rag-pipeline": {"name": "RAG Pipeline",
                     "description": "Retrieval-Augmented Generation", "icon": "🔍"},
    "agent-loop": {"name": "Agent Loop",
                   "description": "Think-Act-Observe agent pattern", "icon": "🤖"},
    "eval-loop": {"name": "Evaluation Loop",
                  "description": "Execute and evaluate iteratively", "icon": "📊"},
}

PARAM_SUGGESTIONS = {
    "llm-node": {"model": "gemma-4", "temperature": 0.7, "maxTokens": 1000},
    "text-input": {"placeholder": "Enter text here"},
    "transform": {"script": "default_transform"},
    "filter": {"condition": "true"},
}


# --------------------------------------------------------------------------- #
# HTTP handler                                                                #
# --------------------------------------------------------------------------- #
class Handler(BaseHTTPRequestHandler):
    server_version = "DesignSystemUI/1.0"

    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.body_written = True
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, obj, status: int = 200) -> None:
        self._send(status, json.dumps(obj).encode(), "application/json")

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length", 0) or 0)
        if not length:
            return {}
        try:
            return json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            return {}

    def _serve_static(self, name: str) -> None:
        if name == "" or name == "/":
            name = "index.html"
        name = name.lstrip("/")
        if name not in STATIC_FILES:
            self._json({"error": "not found"}, 404)
            return
        path = ROOT / name
        if not path.exists():
            self._json({"error": f"{name} not found on disk"}, 404)
            return
        ctype = mimetypes.guess_type(str(path))[0] or "application/octet-stream"
        if name == "llms.txt":
            ctype = "text/plain; charset=utf-8"
        self._send(200, path.read_bytes(), ctype)

    def do_OPTIONS(self) -> None:  # noqa: N802
        self._send(204, b"", "text/plain")

    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            self._serve_static("index.html")
        elif path == "/llms.txt" or path == "/api/llms.txt":
            self._serve_static("llms.txt")
        elif path == "/api/patterns":
            self._json({"patterns": PATTERNS})
        elif path == "/api/agent/activity":
            from urllib.parse import parse_qs
            qs = parse_qs(urlparse(self.path).query)
            since = int(qs.get("since", ["0"])[0] or 0)
            self._json(ACTIVITY.snapshot(since))
        elif path == "/api/integrations":
            self._json({"integrations": INTEGRATIONS})
        elif path == "/api/stock/status":
            self._json({"providers": STOCK.status()})
        elif path == "/health":
            self._json({"status": "healthy", "service": "design-system-ui",
                        "timestamp": now_iso()})
        elif path.lstrip("/") in STATIC_FILES:
            self._serve_static(path)
        else:
            self._json({"error": "not found", "path": path}, 404)

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        payload = self._read_json()
        if path == "/api/workflow/execute":
            self._json(EXECUTOR.execute(payload))
        elif path == "/api/workflow/evaluate":
            self._json(EVALUATOR.evaluate(payload.get("workflow", {}),
                                          payload.get("results", {})))
        elif path == "/api/workflow/suggest-params":
            self._json(PARAM_SUGGESTIONS.get(payload.get("nodeType", ""), {}))
        elif path == "/api/agent/activity":
            self._json(ACTIVITY.post(payload))
        elif path == "/api/chat":
            self._json(CHAT.reply(payload.get("message", ""),
                                  payload.get("history", [])))
        elif path == "/api/stock/search":
            self._json(STOCK.search(payload.get("provider", ""),
                                    payload.get("query", ""),
                                    int(payload.get("per_page", 10))))
        else:
            self._json({"error": "not found", "path": path}, 404)

    def log_message(self, fmt: str, *args) -> None:
        log.info("%s - %s", self.address_string(), fmt % args)


def main() -> None:
    port = int(os.environ.get("PORT", "8000"))
    host = os.environ.get("HOST", "0.0.0.0")
    httpd = ThreadingHTTPServer((host, port), Handler)
    log.info("Design System UI serving on http://%s:%d", host, port)
    log.info("Open http://localhost:%d  |  agents: http://localhost:%d/llms.txt",
             port, port)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        log.info("shutting down")
        httpd.shutdown()


if __name__ == "__main__":
    main()
