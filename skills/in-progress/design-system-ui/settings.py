"""
Benutzerdefinierte Oberfläche — stufenlos einstellbar, serverseitig gesichert.

Jede Einstellung hat Typ, Grenzen und Standardwert. Die Oberfläche baut ihre
Regler daraus auf, statt sie doppelt zu pflegen; Agenten lesen dasselbe
Schema über /api/settings/schema und können gezielt setzen.

Gesichert wird in settings.json neben dem Server. Nur Standardbibliothek.
"""

from __future__ import annotations

import json
from pathlib import Path

STORE = Path(__file__).resolve().parent / "settings.json"

# name -> (typ, standard, min, max, schritt, einheit, gruppe, beschreibung)
SCHEMA: dict[str, dict] = {
    # ---- Farbe ----
    "accent_hue": {"type": "range", "default": 199, "min": 0, "max": 360,
                   "step": 1, "unit": "°", "group": "Farbe",
                   "label": "Akzentfarbe (Farbton)"},
    "accent_saturation": {"type": "range", "default": 89, "min": 0, "max": 100,
                          "step": 1, "unit": "%", "group": "Farbe",
                          "label": "Sättigung"},
    "accent_lightness": {"type": "range", "default": 48, "min": 20, "max": 80,
                         "step": 1, "unit": "%", "group": "Farbe",
                         "label": "Helligkeit"},
    "theme": {"type": "choice", "default": "master",
              "choices": ["master", "light", "contrast", "control", "content"],
              "group": "Farbe", "label": "Erscheinungsbild (DkZ-Theme)"},
    "contrast": {"type": "range", "default": 100, "min": 80, "max": 140,
                 "step": 1, "unit": "%", "group": "Farbe",
                 "label": "Kontrast"},

    # ---- Form ----
    "radius": {"type": "range", "default": 8, "min": 0, "max": 28,
               "step": 1, "unit": "px", "group": "Form",
               "label": "Ecken-Rundung"},
    "density": {"type": "range", "default": 100, "min": 70, "max": 140,
                "step": 1, "unit": "%", "group": "Form",
                "label": "Dichte (Abstände)"},
    "border_width": {"type": "range", "default": 1, "min": 0, "max": 4,
                     "step": 1, "unit": "px", "group": "Form",
                     "label": "Rahmenstärke"},

    # ---- Schrift ----
    "font_size": {"type": "range", "default": 14, "min": 11, "max": 22,
                  "step": 1, "unit": "px", "group": "Schrift",
                  "label": "Grundschriftgröße"},
    "line_height": {"type": "range", "default": 150, "min": 120, "max": 200,
                    "step": 5, "unit": "%", "group": "Schrift",
                    "label": "Zeilenabstand"},
    "font_family": {"type": "choice", "default": "system",
                    "choices": ["system", "serif", "mono"], "group": "Schrift",
                    "label": "Schriftart"},

    # ---- Verhalten ----
    "tip_interval": {"type": "range", "default": 4.5, "min": 2, "max": 20,
                     "step": 0.5, "unit": "s", "group": "Verhalten",
                     "label": "Tippwechsel-Intervall"},
    "activity_poll": {"type": "range", "default": 1.0, "min": 0.3, "max": 10,
                      "step": 0.1, "unit": "s", "group": "Verhalten",
                      "label": "Aktivitäts-Abfrage"},
    "glow_intensity": {"type": "range", "default": 100, "min": 0, "max": 200,
                       "step": 5, "unit": "%", "group": "Verhalten",
                       "label": "Stärke des roten Monitorrands"},
    "animations": {"type": "bool", "default": True, "group": "Verhalten",
                   "label": "Animationen"},
    "tts_enabled": {"type": "bool", "default": True, "group": "Verhalten",
                    "label": "Vorlesen bei Doppelklick"},
    "info_blink": {"type": "bool", "default": True, "group": "Verhalten",
                   "label": "Info-Buttons blinken"},
    "dkz_fonts": {"type": "bool", "default": False, "group": "Schrift",
                  "label": "DkZ-Schriften laden (Google Fonts, sendet Anfragen nach außen)"},

    # ---- Arbeitsfläche ----
    "canvas_grid": {"type": "bool", "default": True, "group": "Arbeitsfläche",
                    "label": "Raster auf dem Canvas"},
    "grid_size": {"type": "range", "default": 20, "min": 8, "max": 80,
                  "step": 2, "unit": "px", "group": "Arbeitsfläche",
                  "label": "Rasterweite"},
    "snap_to_grid": {"type": "bool", "default": False, "group": "Arbeitsfläche",
                     "label": "Am Raster einrasten"},
    "node_width": {"type": "range", "default": 100, "min": 70, "max": 220,
                   "step": 5, "unit": "px", "group": "Arbeitsfläche",
                   "label": "Node-Breite"},
    "start_view": {"type": "choice", "default": "builder",
                   "choices": ["builder", "chat", "studio", "skills",
                               "providers", "stock", "github", "agents",
                               "debug", "settings", "docs"],
                   "group": "Arbeitsfläche", "label": "Startoberfläche"},
}


def defaults() -> dict:
    return {k: v["default"] for k, v in SCHEMA.items()}


def _clamp(spec: dict, value):
    """Wert auf den erlaubten Bereich begrenzen statt ihn zu verwerfen."""
    t = spec["type"]
    if t == "range":
        try:
            v = float(value)
        except (TypeError, ValueError):
            return spec["default"]
        v = max(spec["min"], min(spec["max"], v))
        return int(v) if float(spec["step"]).is_integer() else round(v, 2)
    if t == "bool":
        if isinstance(value, str):
            return value.lower() in ("1", "true", "ja", "on", "yes")
        return bool(value)
    if t == "choice":
        return value if value in spec["choices"] else spec["default"]
    return spec["default"]


def load() -> dict:
    merged = defaults()
    if STORE.is_file():
        try:
            stored = json.loads(STORE.read_text(encoding="utf-8"))
            for k, v in stored.items():
                if k in SCHEMA:
                    merged[k] = _clamp(SCHEMA[k], v)
        except (OSError, json.JSONDecodeError):
            pass
    return merged


def save(patch: dict) -> dict:
    current = load()
    applied, ignored = {}, []
    for k, v in (patch or {}).items():
        if k in SCHEMA:
            current[k] = _clamp(SCHEMA[k], v)
            applied[k] = current[k]
        else:
            ignored.append(k)
    try:
        STORE.write_text(json.dumps(current, ensure_ascii=False, indent=2),
                         encoding="utf-8")
    except OSError as exc:
        return {"ok": False, "error": f"konnte nicht sichern: {exc}",
                "settings": current}
    return {"ok": True, "applied": applied, "ignored": ignored,
            "settings": current}


def reset() -> dict:
    try:
        if STORE.is_file():
            STORE.unlink()
    except OSError:
        pass
    return {"ok": True, "settings": defaults()}


def schema() -> dict:
    groups: dict[str, list] = {}
    for name, spec in SCHEMA.items():
        groups.setdefault(spec["group"], []).append({"name": name, **spec})
    return {"groups": groups, "count": len(SCHEMA)}
