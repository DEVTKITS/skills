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


EXECUTOR = WorkflowExecutor()
EVALUATOR = GemmaEvaluator()

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
