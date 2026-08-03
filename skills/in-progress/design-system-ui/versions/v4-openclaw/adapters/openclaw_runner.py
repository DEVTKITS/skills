#!/usr/bin/env python3
"""
OpenCLAW On-Demand-Runner
=========================

Schreibweise beachten: **OpenCLAW** — die Agenten-Shell. Nicht "OpenCloud",
nicht "opencloude", und nicht Anthropics Claude. Verschiedene Dinge.

Zweck
-----
OpenCLAW (und ebenso OpenHands oder OpenManus) soll NICHT dauerhaft laufen.
Dieser Runner setzt das Muster um, das die Oberfläche vorschreibt:

    aus  →  Auftrag kommt herein  →  Shell starten  →  gegen die HTTP-API
    arbeiten lassen  →  jeden Schritt über das Aktivitätsprotokoll melden
    (roter Monitorrand)  →  bei Fertigstellung herunterfahren

Er läuft ohne installiertes OpenCLAW: fehlt die Shell, führt der Runner den
Auftrag selbst gegen die API aus (Fallback-Modus) und meldet das ehrlich.
Damit ist die Integration heute nutzbar und wird automatisch echt, sobald
OPENCLAW_CMD auf eine vorhandene Shell zeigt.

Nutzung
-------
    python3 adapters/openclaw_runner.py "Baue einen RAG-Workflow und bewerte ihn"
    python3 adapters/openclaw_runner.py --task-file auftrag.txt
    python3 adapters/openclaw_runner.py --pattern rag-pipeline

Umgebung
--------
    BASE_URL       Design System UI (Standard http://localhost:8000)
    OPENCLAW_CMD   Startbefehl der Shell, z. B. "openclaw run --headless"
                   Fehlt er oder ist das Programm nicht da → Fallback-Modus.
    OPENCLAW_TIMEOUT  Sekunden bis zum harten Abbruch (Standard 300)
    MAX_ROUNDS     Iterationen bis zum Abbruch (Standard 5)

Nur Standardbibliothek.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.request

ACTOR = "openclaw"
BASE = os.environ.get("BASE_URL", "http://localhost:8000").rstrip("/")
OPENCLAW_CMD = os.environ.get("OPENCLAW_CMD", "").strip()
TIMEOUT = float(os.environ.get("OPENCLAW_TIMEOUT", "300"))
MAX_ROUNDS = int(os.environ.get("MAX_ROUNDS", "5"))


# --------------------------------------------------------------------------- #
# HTTP-Grundlagen                                                             #
# --------------------------------------------------------------------------- #
def api(path: str, payload: dict | None = None) -> dict:
    url = f"{BASE}{path}"
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(
        url, data=data, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return json.loads(resp.read() or b"{}")
    except urllib.error.HTTPError as e:
        return {"error": f"HTTP {e.code}", "body": e.read()[:300].decode(errors="replace")}
    except (urllib.error.URLError, OSError, TimeoutError, json.JSONDecodeError) as e:
        return {"error": str(e)}


def broadcast(action: str, target: str = "", detail: str = "",
              state: str = "working") -> None:
    """Pflicht: jeden Schritt melden, damit der Monitorrand rot leuchtet."""
    api("/api/agent/activity", {"actor": ACTOR, "action": action,
                                "target": target, "detail": detail,
                                "state": state})


def log(msg: str) -> None:
    print(f"[openclaw] {msg}", flush=True)


# --------------------------------------------------------------------------- #
# Shell-Erkennung                                                             #
# --------------------------------------------------------------------------- #
def shell_available() -> bool:
    """Prüft, ob die konfigurierte OpenCLAW-Shell wirklich startbar ist."""
    if not OPENCLAW_CMD:
        return False
    binary = OPENCLAW_CMD.split()[0]
    return shutil.which(binary) is not None


def run_shell(task: str) -> dict:
    """Startet OpenCLAW für genau diesen Auftrag und fährt es danach herunter."""
    briefing = (
        f"{task}\n\n"
        f"Arbeite gegen die HTTP-API unter {BASE}. Lies zuerst {BASE}/llms.txt — "
        f"dort steht die vollständige Schnittstelle. Melde JEDEN Schritt per "
        f"POST {BASE}/api/agent/activity mit actor=\"{ACTOR}\", damit der "
        f"Mensch sieht, dass eine KI arbeitet. Schließe mit state=done ab."
    )
    log(f"starte Shell: {OPENCLAW_CMD}")
    try:
        proc = subprocess.run(OPENCLAW_CMD.split(), input=briefing, text=True,
                              capture_output=True, timeout=TIMEOUT)
        return {"mode": "shell", "returncode": proc.returncode,
                "stdout": proc.stdout[-4000:], "stderr": proc.stderr[-2000:]}
    except subprocess.TimeoutExpired:
        return {"mode": "shell", "error": f"Zeitüberschreitung nach {TIMEOUT}s"}
    except (OSError, subprocess.SubprocessError) as exc:
        return {"mode": "shell", "error": str(exc)}
    finally:
        log("Shell beendet — Ressourcen freigegeben")


# --------------------------------------------------------------------------- #
# Fallback: den Auftrag selbst gegen die API fahren                           #
# --------------------------------------------------------------------------- #
def pick_pattern(task: str) -> str:
    t = task.lower()
    if "rag" in t or "retriev" in t or "wissen" in t:
        return "rag-pipeline"
    if "klassifi" in t or "classif" in t or "kategor" in t:
        return "text-classifier"
    if "agent" in t or "schritt" in t:
        return "agent-loop"
    return "eval-loop"


PATTERN_GRAPHS = {
    "text-classifier": ([("in1", "text-input"), ("llm1", "llm-node"),
                         ("out1", "text-output")],
                        [("in1", "llm1"), ("llm1", "out1")]),
    "rag-pipeline": ([("q1", "text-input"), ("ret1", "llm-node"),
                      ("gen1", "llm-node"), ("out1", "text-output")],
                     [("q1", "ret1"), ("ret1", "gen1"), ("gen1", "out1")]),
    "agent-loop": ([("task1", "text-input"), ("think1", "llm-node"),
                    ("act1", "transform"), ("obs1", "transform"),
                    ("out1", "text-output")],
                   [("task1", "think1"), ("think1", "act1"),
                    ("act1", "obs1"), ("obs1", "out1")]),
    "eval-loop": ([("in1", "text-input"), ("exec1", "llm-node"),
                   ("score1", "transform"), ("out1", "text-output")],
                  [("in1", "exec1"), ("exec1", "score1"), ("score1", "out1")]),
}


def build_workflow(pattern: str) -> dict:
    nodes_spec, edges_spec = PATTERN_GRAPHS.get(pattern,
                                                PATTERN_GRAPHS["eval-loop"])
    nodes = []
    for i, (nid, ntype) in enumerate(nodes_spec):
        params = {"model": "gemma-4", "temperature": 0.7} if ntype == "llm-node" else {}
        nodes.append({"id": nid, "type": ntype, "x": 100 + i * 180,
                      "y": 120, "params": params})
    return {"id": f"openclaw-{pattern}",
            "nodes": nodes,
            "edges": [{"from": a, "to": b} for a, b in edges_spec],
            "parameters": {}}


def apply_suggestion(wf: dict, action: str) -> bool:
    """Wendet eine maschinenlesbare Empfehlung des Evaluators an."""
    if action.startswith("ADD_NODE"):
        last = wf["nodes"][-1] if wf["nodes"] else {"x": 60, "y": 120}
        wf["nodes"].append({"id": f"llm-{len(wf['nodes'])}", "type": "llm-node",
                            "x": last.get("x", 60) + 180, "y": last.get("y", 120),
                            "params": {"model": "gemma-4", "temperature": 0.7}})
        return True
    if action.startswith("CONNECT_NODES"):
        linked = {(e["from"], e["to"]) for e in wf["edges"]}
        for a, b in zip(wf["nodes"], wf["nodes"][1:]):
            if (a["id"], b["id"]) not in linked:
                wf["edges"].append({"from": a["id"], "to": b["id"]})
                return True
    return False


def run_fallback(task: str, pattern: str | None) -> dict:
    """Fährt die Iterationsschleife selbst — funktioniert ohne OpenCLAW."""
    pattern = pattern or pick_pattern(task)
    log(f"Fallback-Modus (keine Shell gefunden) · Muster '{pattern}'")
    broadcast("plan", "", f"Muster '{pattern}' für Auftrag gewählt")

    wf = build_workflow(pattern)
    history = []

    for rnd in range(1, MAX_ROUNDS + 1):
        broadcast("execute", "#executeWorkflow", f"Runde {rnd}: ausführen")
        run = api("/api/workflow/execute", wf)
        if run.get("error") or run.get("status") != "success":
            broadcast("execute", "", f"Fehlschlag: {run.get('error') or run.get('status')}",
                      state="error")
            return {"mode": "fallback", "error": run.get("error") or run.get("status"),
                    "rounds": history}

        broadcast("evaluate", "#evalResults", f"Runde {rnd}: bewerten lassen")
        ev = api("/api/workflow/evaluate",
                 {"workflow": wf, "results": run.get("results", {})})
        score, verdict = ev.get("score"), ev.get("verdict")
        live = "live Gemma" if ev.get("gemma_live") else "Heuristik"
        log(f"Runde {rnd}: {score}/100 → {verdict} ({live})")
        history.append({"round": rnd, "score": score, "verdict": verdict,
                        "gemma_live": ev.get("gemma_live")})

        if verdict == "done":
            broadcast("session-end", "", f"fertig mit {score}/100", state="done")
            return {"mode": "fallback", "pattern": pattern, "final_score": score,
                    "verdict": "done", "rounds": history}

        applied = [s["title"] for s in ev.get("suggestions", [])
                   if apply_suggestion(wf, s.get("action", ""))]
        if not applied:
            broadcast("session-end", "",
                      f"keine anwendbaren Vorschläge mehr, Stand {score}/100",
                      state="done")
            return {"mode": "fallback", "pattern": pattern, "final_score": score,
                    "verdict": "stalled", "rounds": history}
        broadcast("iterate", "", "angewandt: " + ", ".join(applied))

    broadcast("session-end", "", f"Rundenlimit {MAX_ROUNDS} erreicht", state="done")
    return {"mode": "fallback", "pattern": pattern, "verdict": "max_rounds",
            "rounds": history}


# --------------------------------------------------------------------------- #
# Einstiegspunkt                                                              #
# --------------------------------------------------------------------------- #
def main() -> int:
    ap = argparse.ArgumentParser(description="OpenCLAW On-Demand-Runner")
    ap.add_argument("task", nargs="?", default="", help="Auftrag in Worten")
    ap.add_argument("--task-file", help="Auftrag aus Datei lesen")
    ap.add_argument("--pattern", help="Startmuster erzwingen",
                    choices=sorted(PATTERN_GRAPHS))
    args = ap.parse_args()

    task = args.task
    if args.task_file:
        task = open(args.task_file, encoding="utf-8").read().strip()
    if not task and not args.pattern:
        ap.error("Bitte einen Auftrag angeben oder --pattern wählen.")
    task = task or f"Baue und optimiere einen {args.pattern}-Workflow."

    health = api("/health")
    if health.get("error"):
        log(f"Design System UI unter {BASE} nicht erreichbar: {health['error']}")
        log("Starte sie zuerst mit ./start.sh")
        return 2

    broadcast("session-start", "", f"OpenCLAW übernimmt: {task[:120]}")
    log(f"Auftrag: {task}")

    if shell_available():
        result = run_shell(task)
        broadcast("session-end", "", "OpenCLAW-Shell beendet", state="done")
    else:
        if OPENCLAW_CMD:
            log(f"OPENCLAW_CMD gesetzt ('{OPENCLAW_CMD}'), aber Programm nicht "
                "gefunden — Fallback.")
        result = run_fallback(task, args.pattern)

    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if not result.get("error") else 1


if __name__ == "__main__":
    sys.exit(main())
