"""svgapi — Plug-in für den SVG-Generator (CONTRACT §4, Abschnitt „SVG").

Routen:
  GET  /api/svg/types     Inhalt von svg/types.json (Felder, Grenzen, Standardwerte, Themes, Tokens) + engine
  POST /api/svg/generate  {spec}  -> {"ok":true,"svg","bytes","spec":<normalisiert>,"engine":"py"} | 400 {error, field}
  POST /api/svg/sanitize  {svg}   -> {"ok":true,"svg","removed":[…]}

Die Bibliotheks-Routen (/api/svg/library …) gehören dem Plug-in botapi.

Grenzen (alle Zahlen sind Obergrenzen, keine Schätzungen):
  * Eingabe > 256 KB (UTF-8) -> 413.  Hinweis: server.py liest den Anfragetext vollständig, bevor ein Plug-in
    läuft; dieses Plug-in kann die Größe nur an der geparsten Nutzlast messen, nicht den Empfang abbrechen.
  * Antwort > 1 MiB (generate) bzw. > 2 MiB (sanitize) -> 413 (nie ein abgeschnittenes SVG ausliefern).
Der Generator hält keinen gemeinsamen Zustand; gleichzeitige Aufrufe aus den Server-Threads sind unabhängig.
"""
from __future__ import annotations

import json
import os

import svgkit

ENGINE = "py"
MAX_IN = 256 * 1024
MAX_OUT_GENERATE = 1024 * 1024
MAX_OUT_SANITIZE = 2 * 1024 * 1024
_TYPES_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "svg", "types.json")
_cache: dict = {"mtime": None, "data": None}


def _err(status: int, error: str, field: str = ""):
    return status, {"ok": False, "error": error, "field": field}


def _size(obj) -> int:
    """UTF-8-Größe der Nutzlast als JSON (so groß war sie mindestens auf dem Draht)."""
    return len(json.dumps(obj, ensure_ascii=False).encode("utf-8", "surrogatepass"))


def _types():
    try:
        mtime = os.path.getmtime(_TYPES_FILE)
        if _cache["mtime"] != mtime:
            with open(_TYPES_FILE, encoding="utf-8") as fh:
                _cache["data"], _cache["mtime"] = json.load(fh), mtime
        data = dict(_cache["data"])
    except (OSError, ValueError):
        # svg/types.json fehlt oder ist unlesbar: dieselben Daten aus der eingebauten Beschreibung
        data = {"ok": True, "spec_version": svgkit.VERSION, "types": svgkit.SCHEMA, "themes": svgkit.THEME_NAMES,
                "modes": ["vars", "static"], "tokens": {t: svgkit.tokens(t) for t in svgkit.THEME_NAMES}}
    data["engine"] = ENGINE
    return data


def types(ctx):
    return _types()


def generate(ctx):
    payload = ctx.get("payload")
    if not isinstance(payload, dict):
        return _err(400, "Anfrage muss ein JSON-Objekt {spec} sein", "spec")
    if "spec" in payload:
        spec = payload["spec"]
    elif "type" in payload:
        spec = payload  # bequem: die Spezifikation selbst als Anfragetext
    else:
        return _err(400, "Feld spec fehlt (oder der Anfragetext ist kein gültiges JSON)", "spec")
    if _size(spec) > MAX_IN:
        return _err(413, "Eingabe zu groß (> 256 KB)", "spec")
    r = svgkit.generate(spec)
    if not r["ok"]:
        return _err(400, r["error"], r["field"])
    n = len(r["svg"].encode("utf-8"))
    if n > MAX_OUT_GENERATE:
        return _err(413, "Ergebnis zu groß (> 1 MiB)", "spec")
    return {"ok": True, "svg": r["svg"], "bytes": n, "spec": r["spec"], "engine": ENGINE}


def sanitize(ctx):
    payload = ctx.get("payload")
    if not isinstance(payload, dict) or not isinstance(payload.get("svg"), str):
        return _err(400, "Feld svg (Text) erwartet", "svg")
    svg = payload["svg"]
    if len(svg.encode("utf-8", "surrogatepass")) > MAX_IN:
        return _err(413, "Eingabe zu groß (> 256 KB)", "svg")
    r = svgkit.sanitize(svg)
    if len(r["svg"].encode("utf-8")) > MAX_OUT_SANITIZE:
        return _err(413, "Ergebnis zu groß (> 2 MiB)", "svg")
    return {"ok": True, "svg": r["svg"], "removed": r["removed"], "engine": ENGINE}


ROUTES = {
    "GET /api/svg/types": types,
    "POST /api/svg/generate": generate,
    "POST /api/svg/sanitize": sanitize,
}


def STATUS():  # noqa: N802 — Name vom Plug-in-Vertrag vorgegeben
    return {"engine": ENGINE, "spec_version": svgkit.VERSION, "types": list(svgkit.SCHEMA),
            "themes": svgkit.THEME_NAMES, "max_input_bytes": MAX_IN}
