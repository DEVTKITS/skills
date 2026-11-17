"""botapi — Plug-in für den Design-Assistenten, die Designrechner und die SVG-Bibliothek.

Routen (CONTRACT §4):
  GET  /api/bot/status · GET /api/bot/kb · POST /api/bot/ask · GET /api/bot/history ·
  POST /api/bot/clear · GET /api/design/contrast · GET /api/design/palette ·
  GET  /api/design/scale · GET /api/design/convert · GET /api/design/spacing ·
  GET  /api/design/fluid · GET/POST /api/svg/library · GET /api/svg/library/* ·
  POST /api/svg/library/delete

Antwortweg von POST /api/bot/ask (DkZ-Brainly):
  1. `control.route(message, context)` — liefert die Steuerung eine Aktion, ist das die Antwort.
  2. Wissensbasis (BM25 über `store`).
  3. Modell erreichbar (Ollama über OLLAMA_URL/GEMMA_MODEL oder `provider` über
     `providers.chat`) → formuliert auf Basis der Einträge, `live: true`.
  4. Ohne Modell: extraktive Antwort aus den besten Einträgen mit Quelle, `live: false`.
  5. Keine Treffer: mit Modell eine als solche gekennzeichnete allgemeine Antwort,
     ohne Modell eine ehrliche Absage. Nie eine erfundene Antwort.
Der Ollama-Aufruf ist bewusst hier eigenständig (urllib) — kein Import aus server.py.
"""
from __future__ import annotations

import json
import logging
import math
import os
import re
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

import designlab
import store

log = logging.getLogger("botapi")

ROOT = Path(__file__).resolve().parent.parent
KB_DIR = ROOT / "kb"
PENDING = "— ausstehend —"
MAX_MESSAGE = 4000
TOPICS_TEXT = ("Farbe, Kontrast, Typografie, Abstände, Layout, Hierarchie, Bewegung, "
               "Barrierefreiheit, Responsive Design, SVG, Icons, Datenvisualisierung, "
               "UX-Texte und das DkZ-Designsystem")


def _bad(error: str, field: str = "", status: int = 400):
    return status, {"ok": False, "error": error, **({"field": field} if field else {})}


def _pending_resp(res: dict):
    return 503, {"ok": False, "error": res.get("error", PENDING),
                 "needs": res.get("needs", "duckdb")}


def _from_store(res: dict):
    """Store-Ergebnis → Plug-in-Antwort mit passendem HTTP-Status."""
    if res.get("ok"):
        return res
    if res.get("error") == PENDING:
        return _pending_resp(res)
    status = int(res.pop("status", 400))
    return status, res


# --------------------------------------------------------------------------- #
# Wissensbasis bei Bedarf (neu) laden                                          #
# --------------------------------------------------------------------------- #
_kb_lock = threading.Lock()
_kb_state = {"sig": None, "last": None}


def _ensure_kb() -> dict | None:
    """Lädt kb/*.jsonl, wenn sich Dateinamen, Größen oder Zeitstempel geändert haben."""
    try:
        sig = tuple((p.name, p.stat().st_mtime_ns, p.stat().st_size)
                    for p in sorted(KB_DIR.glob("*.jsonl")))
    except OSError:
        sig = ()
    with _kb_lock:
        if sig != _kb_state["sig"]:
            res = store.kb_load(KB_DIR)
            if res.get("ok"):
                _kb_state["sig"] = sig
            _kb_state["last"] = res
            if res.get("errors"):
                log.warning("Wissensbasis: %d ungültige Zeilen übersprungen",
                            len(res["errors"]))
        return _kb_state["last"]


# --------------------------------------------------------------------------- #
# Modelle: eigener kleiner Ollama-Aufruf + optional providers.chat             #
# --------------------------------------------------------------------------- #
_llm_lock = threading.Lock()
_llm_cache = {"t": -1e9, "url": None, "live": False, "models": []}
PROBE_TTL = 10.0


def _ollama_url() -> str:
    return os.environ.get("OLLAMA_URL", "http://localhost:11434").rstrip("/")


def _ollama_model() -> str:
    return os.environ.get("GEMMA_MODEL", "gemma:12b")


def ollama_live() -> dict:
    """Ist Ollama erreichbar? Kurzes Zeitlimit, Ergebnis 10 Sekunden zwischengespeichert."""
    url = _ollama_url()
    with _llm_lock:
        now = time.monotonic()
        if _llm_cache["url"] == url and now - _llm_cache["t"] < PROBE_TTL:
            return dict(_llm_cache)
    live, models = False, []
    try:
        timeout = float(os.environ.get("BOT_PROBE_TIMEOUT", "1.0"))
        with urllib.request.urlopen(f"{url}/api/tags", timeout=timeout) as resp:
            if resp.status == 200:
                data = json.loads(resp.read(200_000) or b"{}")
                models = [str(m.get("name", "")) for m in data.get("models", [])
                          if isinstance(m, dict)][:50]
                live = True
    except (urllib.error.URLError, OSError, ValueError, TimeoutError):
        live = False
    with _llm_lock:
        _llm_cache.update({"t": time.monotonic(), "url": url, "live": live,
                           "models": models})
        return dict(_llm_cache)


def ollama_generate(prompt: str) -> str | None:
    try:
        timeout = float(os.environ.get("BOT_LLM_TIMEOUT", "60"))
        req = urllib.request.Request(
            f"{_ollama_url()}/api/generate",
            data=json.dumps({"model": _ollama_model(), "prompt": prompt, "stream": False,
                             "options": {"temperature": 0.2}}).encode(),
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            text = json.loads(resp.read(2_000_000) or b"{}").get("response")
    except (urllib.error.URLError, OSError, ValueError, TimeoutError) as exc:
        log.info("Ollama-Antwort fehlgeschlagen: %s", exc)
        with _llm_lock:
            _llm_cache["t"] = -1e9                 # beim nächsten Mal neu prüfen
        return None
    return text.strip() if isinstance(text, str) and text.strip() else None


def _pick_model(provider):
    """→ (fn(prompt) -> (text|None, error|None), Modellname) oder (None, Grund)."""
    prov = (provider or "").strip().lower() if isinstance(provider, str) else ""
    if prov in ("none", "offline", "kb"):
        return None, "kein Modell gewünscht"
    if prov in ("", "auto", "ollama"):
        if ollama_live()["live"]:
            return (lambda p: (ollama_generate(p), "Ollama antwortet nicht")), _ollama_model()
        return None, "Ollama nicht erreichbar"
    try:
        import providers  # Wurzelmodul, kein Zirkelbezug zu server.py
    except ImportError:
        return None, "providers.py fehlt"

    def call(prompt):
        try:
            res = providers.chat(provider, prompt, [])
        except Exception as exc:  # noqa: BLE001
            return None, f"{type(exc).__name__}: {exc}"
        if res.get("ok") and isinstance(res.get("reply"), str) and res["reply"].strip():
            call.model = res.get("model") or provider
            return res["reply"].strip(), None
        return None, " ".join(str(x) for x in (res.get("error"), res.get("connect")) if x)
    call.model = provider
    return call, provider


# --------------------------------------------------------------------------- #
# Steuerung (control.py) und Rechen-Rückfall                                   #
# --------------------------------------------------------------------------- #
def _control():
    try:
        import control  # noqa: PLC0415 — optionales Geschwistermodul
    except ImportError:
        return None
    return control if callable(getattr(control, "route", None)) else None


def _describe_actions(actions: list) -> str:
    parts = []
    for a in actions[:5]:
        if isinstance(a, dict):
            t = str(a.get("type", "?"))
            detail = a.get("name") or a.get("view") or a.get("explain") or ""
            parts.append(f"{t} {detail}".strip())
    return "Aktion: " + "; ".join(parts) if parts else "Aktion erkannt."


_HEXRE = re.compile(r"(?<![\w#])#?(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?![\w])"
                    r"|(?:rgb|hsl)a?\([^()]{1,60}\)", re.IGNORECASE)


def _calc_fallback(message: str) -> dict | None:
    """Nur wenn control.py fehlt: Kontrast zweier Farben direkt ausrechnen."""
    if not re.search(r"kontrast|contrast", message, re.IGNORECASE):
        return None
    colors = [c for c in _HEXRE.findall(message)
              if c.startswith(("#", "rgb", "hsl", "RGB", "HSL")) and designlab.try_parse_color(c)]
    if len(colors) < 2:
        return None
    res = designlab.contrast(colors[0], colors[1])
    if not res.get("ok"):
        return None
    g = res["grade"]
    word = {"AAA": "AAA", "AA": "AA", "fail": "nicht bestanden"}
    reply = (f"Kontrast {res['fg']} auf {res['bg']}: {res['ratio_text']}. "
             f"Fließtext: {word[g['normal']]}, großer Text: {word[g['large']]}, "
             f"UI-Komponenten (3:1): {'bestanden' if g['ui'] == 'pass' else 'nicht bestanden'}."
             + (f" Vorschlag für 4,5:1: {res['suggestion']['fg']}."
                if res.get("suggestion", {}).get("fg") else "")
             + " Quelle: WCAG 2.2, SC 1.4.3 / 1.4.6 / 1.4.11.")
    return {"ok": True, "reply": reply, "sources": [], "intent": "design.contrast",
            "actions": [], "live": False, "model": "designlab", "confidence": 1.0,
            "result": res}


# --------------------------------------------------------------------------- #
# Antworten                                                                    #
# --------------------------------------------------------------------------- #
def _snippet(text: str, n: int = 220) -> str:
    if len(text) <= n:
        return text
    cut = text[:n].rsplit(" ", 1)[0]
    return cut + " …"


def _confidence(score: float, coverage: float) -> float:
    """Heuristik für die Verankerung in der Wissensbasis (0..1), keine Wahrscheinlichkeit."""
    return round(max(0.0, min(1.0, coverage)) * (1 - math.exp(-score / 6)), 2)


def _history_text(history) -> str:
    lines = []
    for turn in (history or [])[-6:]:
        if isinstance(turn, dict) and isinstance(turn.get("content"), str):
            role = "Nutzer" if turn.get("role") == "user" else "Assistent"
            lines.append(f"{role}: {turn['content'][:500]}")
    return "\n".join(lines)


def build_prompt(message: str, hits: list[dict], history=None) -> str:
    entries = "\n\n".join(f"[{h['id']}] {h['title']} (Quelle: {h['source']})\n{h['text']}"
                          for h in hits)
    hist = _history_text(history)
    return ("Du bist der Design-Assistent im DkZ-Design-Paket. Beantworte die Frage "
            "ausschließlich auf Grundlage der folgenden Wissensbasis-Einträge. Nenne die "
            "ids der verwendeten Einträge in eckigen Klammern, z. B. [" + hits[0]["id"] + "]. "
            "Wenn die Einträge die Frage nicht beantworten, sag das offen. Erfinde keine "
            "Zahlen, Studien oder Zitate. Antworte knapp in der Sprache der Frage.\n\n"
            f"Einträge:\n{entries}\n\n"
            + (f"Bisheriger Verlauf:\n{hist}\n\n" if hist else "")
            + f"Frage: {message}\nAntwort:")


def build_general_prompt(message: str, history=None) -> str:
    hist = _history_text(history)
    return ("Du bist ein hilfsbereiter Assistent. Die lokale Design-Wissensbasis enthält zu "
            "dieser Frage nichts. Antworte knapp und ehrlich; wenn du etwas nicht sicher "
            "weißt, sag das. Erfinde keine Zahlen, Quellen oder Zitate.\n\n"
            + (f"Bisheriger Verlauf:\n{hist}\n\n" if hist else "")
            + f"Frage: {message}\nAntwort:")


def _extractive(hits: list[dict], note: str = "") -> str:
    top = hits[0]
    lines = [f"{top['title']}: {top['text']}", f"Quelle: {top['source']} [{top['id']}]"]
    if len(hits) > 1:
        lines.append("Siehe auch: " + "; ".join(f"{h['title']} [{h['id']}]" for h in hits[1:3]))
    if note:
        lines.append(note)
    return "\n".join(lines)


def _refusal(reason: str = "") -> str:
    return ("Dazu habe ich in der lokalen Wissensbasis nichts gefunden. Ohne verbundenes "
            f"Modell beantworte ich nur Gestaltungsfragen ({TOPICS_TEXT}) und erfinde keine "
            "Antwort. Ein Modell verbindest du so: Ollama starten (ollama serve, ein Modell "
            "mit ollama pull laden, OLLAMA_URL und GEMMA_MODEL setzen) oder in der "
            "Oberfläche einen Cloud-Anbieter wählen und dessen API-Schlüssel als "
            "Umgebungsvariable setzen."
            + (f" (Hinweis: {reason})" if reason else ""))


def answer(message: str, history=None, context=None, provider=None) -> dict:
    """Kern von POST /api/bot/ask (ohne Speichern)."""
    context = context if isinstance(context, dict) else {}

    # 1. Steuerung zuerst
    ctl = _control()
    if ctl is not None:
        try:
            r = ctl.route(message, context)
        except Exception as exc:  # noqa: BLE001 — Steuerung darf den Assistenten nicht stoppen
            log.warning("control.route fehlgeschlagen: %s", exc)
            r = None
        if isinstance(r, dict) and (r.get("actions") or
                                    (isinstance(r.get("reply"), str) and r["reply"].strip())):
            actions = [a for a in (r.get("actions") or []) if isinstance(a, dict)]
            src = r.get("source", "rules")
            try:
                conf = max(0.0, min(1.0, float(r.get("confidence", 1.0))))
            except (TypeError, ValueError):
                conf = 1.0
            return {"ok": True,
                    "reply": r.get("reply") or _describe_actions(actions),
                    "sources": r.get("sources") if isinstance(r.get("sources"), list) else [],
                    "intent": str(r.get("intent", "control")), "actions": actions,
                    "params": r.get("params") if isinstance(r.get("params"), dict) else {},
                    "live": src == "llm", "model": r.get("model") or src,
                    "confidence": conf, "via": "control"}
    else:
        calc = _calc_fallback(message)
        if calc:
            return calc

    # 2. Wissensbasis
    _ensure_kb()
    res = store.kb_search(message, 5)
    qterms = res.get("terms", [])
    hits = []
    for r in res.get("results", []):
        rel = store.kb_relevance(r, qterms, res.get("unknown", []))
        if rel["relevant"]:
            hits.append({**r, **rel})
    if hits:
        top = hits[0]["score"]
        hits = [h for h in hits if h["score"] >= 0.5 * top][:3]
    sources = [{"id": h["id"], "title": h["title"], "score": round(h["score"], 4),
                "source": h["source"]} for h in hits]

    # 3. Modell?
    fn, model = _pick_model(provider)
    base = {"ok": True, "sources": sources, "actions": [], "kb_backend": res.get("backend")}
    if hits:
        conf = _confidence(hits[0]["score"], hits[0]["coverage"])
        if fn is not None:
            text, err = fn(build_prompt(message, hits, history))
            if text:
                return {**base, "reply": text, "intent": "kb.answer", "live": True,
                        "model": getattr(fn, "model", model), "confidence": conf,
                        "grounded": True}
            note = f"(Modell nicht verfügbar: {err} — Antwort direkt aus der Wissensbasis.)"
        else:
            note = ""
        # 4. extraktiv
        return {**base, "reply": _extractive(hits, note), "intent": "kb.answer",
                "live": False, "model": "extractive", "confidence": conf, "grounded": True}

    # 5. keine Treffer
    if fn is not None:
        text, err = fn(build_general_prompt(message, history))
        if text:
            return {**base, "reply": "Allgemeine Antwort (nicht aus der Wissensbasis): " + text,
                    "intent": "general", "live": True, "model": getattr(fn, "model", model),
                    "confidence": 0.0, "grounded": False}
        return {**base, "reply": _refusal(f"Modell nicht verfügbar: {err}"),
                "intent": "out_of_scope", "live": False, "model": "none",
                "confidence": 0.0, "grounded": False}
    reason = model if provider and str(provider).lower() not in ("", "auto", "ollama") else ""
    return {**base, "reply": _refusal(reason), "intent": "out_of_scope", "live": False,
            "model": "none", "confidence": 0.0, "grounded": False}


# --------------------------------------------------------------------------- #
# Routen: Assistent                                                            #
# --------------------------------------------------------------------------- #
def r_status(ctx):
    kb = _ensure_kb() or {}
    st = store.status()
    llm = ollama_live()
    return {"ok": True,
            "kb": {**st["kb"], "errors": len(kb.get("errors", []))},
            "llm": {"live": llm["live"], "model": _ollama_model(), "provider": "ollama",
                    "url": _ollama_url(),
                    "model_installed": (_ollama_model() in llm["models"]) if llm["live"] else None},
            "store": st["store"],
            "control": _control() is not None}


def r_kb(ctx):
    q = ctx["query"].get("q", "")
    if not isinstance(q, str) or not q.strip():
        return _bad("q fehlt", "q")
    if len(q) > 500:
        return _bad("q ist zu lang (max. 500 Zeichen)", "q")
    try:
        limit = max(1, min(20, int(ctx["query"].get("limit", 5))))
    except (TypeError, ValueError):
        return _bad("limit muss eine ganze Zahl sein", "limit")
    _ensure_kb()
    res = store.kb_search(q, limit)
    return {"ok": True, "backend": res.get("backend"), "terms": res.get("terms", []),
            "results": [{"id": r["id"], "title": r["title"], "topic": r["topic"],
                         "score": round(r["score"], 4), "snippet": _snippet(r["text"]),
                         "source": r["source"]} for r in res.get("results", [])]}


def r_ask(ctx):
    p = ctx.get("payload") or {}
    if not isinstance(p, dict):
        return _bad("JSON-Objekt erwartet")
    message = p.get("message")
    if not isinstance(message, str) or not message.strip():
        return _bad("message fehlt", "message")
    if len(message) > MAX_MESSAGE:
        return _bad(f"message ist zu lang (max. {MAX_MESSAGE} Zeichen)", "message")
    history = p.get("history") if isinstance(p.get("history"), list) else []
    provider = p.get("provider") if isinstance(p.get("provider"), str) else None
    message = message.strip()
    resp = answer(message, history, p.get("context"), provider)
    meta = {"intent": resp.get("intent"), "live": resp.get("live"),
            "model": resp.get("model"),
            "sources": [s.get("id") for s in resp.get("sources", []) if isinstance(s, dict)]}
    u = store.chat_add("user", message, {"provider": provider} if provider else {})
    a = store.chat_add("assistant", str(resp.get("reply", "")), meta)
    resp["stored"] = bool(u.get("ok") and a.get("ok"))
    return resp


def r_history(ctx):
    return _from_store(store.chat_history(ctx["query"].get("limit", 50)))


def r_clear(ctx):
    return _from_store(store.chat_clear())


# --------------------------------------------------------------------------- #
# Routen: Designrechner                                                        #
# --------------------------------------------------------------------------- #
def _calc(res: dict):
    return res if res.get("ok") else (400, res)


def r_contrast(ctx):
    q = ctx["query"]
    return _calc(designlab.contrast(q.get("fg", ""), q.get("bg", "")))


def r_palette(ctx):
    q = ctx["query"]
    return _calc(designlab.palette(q.get("base", ""), q.get("scheme", "complementary")))


def r_scale(ctx):
    q = ctx["query"]
    return _calc(designlab.type_scale(q.get("base", 16), q.get("ratio", 1.25),
                                      q.get("steps", 7), q.get("below", 0)))


def r_convert(ctx):
    return _calc(designlab.convert(ctx["query"].get("color", "")))


def r_spacing(ctx):
    q = ctx["query"]
    return _calc(designlab.spacing_scale(q.get("base", 8), q.get("steps", 10)))


def r_fluid(ctx):
    q = ctx["query"]
    return _calc(designlab.fluid_type(q.get("min", 16), q.get("max", 24),
                                      q.get("min_vw", 320), q.get("max_vw", 1280)))


# --------------------------------------------------------------------------- #
# Routen: SVG-Bibliothek                                                       #
# --------------------------------------------------------------------------- #
def _svgkit():
    try:
        import svgkit  # noqa: PLC0415 — Geschwistermodul des Tracks svgkit-py
    except ImportError:
        return None
    return svgkit


def _unwrap_svg(res):
    if isinstance(res, str):
        return res, None
    if isinstance(res, tuple) and res and isinstance(res[0], str):
        return res[0], None
    if isinstance(res, dict):
        if isinstance(res.get("svg"), str) and res.get("ok", True):
            return res["svg"], None
        return None, str(res.get("error") or "svgkit lieferte kein SVG")
    return None, "svgkit lieferte kein SVG"


def _generate(spec):
    kit = _svgkit()
    fn = None
    for name in ("generate", "render", "build"):
        if kit is not None and callable(getattr(kit, name, None)):
            fn = getattr(kit, name)
            break
    if fn is None:
        return None, (503, {"ok": False, "error": PENDING, "needs": "svgkit"})
    try:
        svg, err = _unwrap_svg(fn(spec))
    except (ValueError, TypeError, KeyError) as exc:
        return None, _bad(f"spec ungültig: {exc}", "spec")
    if err:
        return None, _bad(err, "spec")
    return svg, None


_SVG_START = re.compile(r"\s*(<\?xml[^>]{0,200}\?>\s*)?<svg[\s>]", re.IGNORECASE)
_SVG_BAD = re.compile(
    r"<\s*/?\s*(script|foreignobject|iframe|object|embed|handler|!entity|!doctype)"
    r"|\son[a-z]+\s*="
    r"|javascript\s*:|vbscript\s*:|data\s*:\s*text/html"
    r"|<\?xml-stylesheet"
    r"|\bhref\s*=\s*[\"']?\s*(?!#)", re.IGNORECASE)


def _clean_svg(svg: str):
    """Mit svgkit: bereinigen. Ohne svgkit: nur streng prüfen und ggf. ablehnen."""
    kit = _svgkit()
    if kit is not None and callable(getattr(kit, "sanitize", None)):
        try:
            out, err = _unwrap_svg(kit.sanitize(svg))
        except (ValueError, TypeError) as exc:
            return None, _bad(f"svg ungültig: {exc}", "svg")
        if err or not out:
            return None, _bad(err or "svg ungültig", "svg")
        return out, None
    if not _SVG_START.match(svg) or _SVG_BAD.search(svg):
        return None, _bad("svg enthält nicht erlaubte Inhalte (ohne svgkit wird nur "
                          "geprüft, nicht bereinigt: kein script, keine on*-Attribute, "
                          "keine externen Verweise)", "svg")
    return svg, None


def r_lib_list(ctx):
    return _from_store(store.svg_list(ctx["query"].get("limit", 200)))


def r_lib_get(ctx):
    rest = (ctx.get("rest") or "").strip("/")
    return _from_store(store.svg_get(rest))


def r_lib_save(ctx):
    p = ctx.get("payload") or {}
    if not isinstance(p, dict):
        return _bad("JSON-Objekt erwartet")
    name, spec, svg = p.get("name"), p.get("spec"), p.get("svg")
    if not isinstance(name, str) or not name.strip():
        return _bad("name fehlt", "name")
    if spec is not None and not isinstance(spec, dict):
        return _bad("spec muss ein Objekt sein", "spec")
    if svg is not None and not isinstance(svg, str):
        return _bad("svg muss Text sein", "svg")
    if not store.AVAILABLE:
        return _pending_resp({})
    if svg and svg.strip():
        if len(svg.encode()) > store.MAX_SVG:
            return _bad("svg ist zu groß (max. 512 KB)", "svg")
        svg, err = _clean_svg(svg)
    else:
        if not spec:
            return _bad("spec oder svg fehlt", "spec")
        svg, err = _generate(spec)
    if err:
        return err
    return _from_store(store.svg_save(name, spec or {}, svg))


def r_lib_delete(ctx):
    p = ctx.get("payload") or {}
    return _from_store(store.svg_delete(p.get("id") if isinstance(p, dict) else None))


ROUTES = {
    "GET /api/bot/status": r_status,
    "GET /api/bot/kb": r_kb,
    "POST /api/bot/ask": r_ask,
    "GET /api/bot/history": r_history,
    "POST /api/bot/clear": r_clear,
    "GET /api/design/contrast": r_contrast,
    "GET /api/design/palette": r_palette,
    "GET /api/design/scale": r_scale,
    "GET /api/design/convert": r_convert,
    "GET /api/design/spacing": r_spacing,
    "GET /api/design/fluid": r_fluid,
    "GET /api/svg/library": r_lib_list,
    "GET /api/svg/library/*": r_lib_get,
    "POST /api/svg/library": r_lib_save,
    "POST /api/svg/library/delete": r_lib_delete,
}


def STATUS():  # noqa: N802 — Name vom Plug-in-Vertrag vorgegeben
    return {"duckdb": store.AVAILABLE, "db": store.db_path(),
            "kb_dir": str(KB_DIR), "control": _control() is not None,
            "svgkit": _svgkit() is not None}
