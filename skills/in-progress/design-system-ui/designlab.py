"""designlab — Designrechner des lokalen Design-Assistenten (nur Standardbibliothek).

Zwei Ebenen:

* **Hilfsfunktionen** (`parse_color`, `contrast_ratio`, `rgb_to_hsl`, …) werfen bei
  ungültiger Eingabe `DesignError` (eine `ValueError` mit Feldname). Für Module, die
  selbst weiterrechnen wollen (Lint, Steuerung).
* **Öffentliche Rechner** (`contrast`, `convert`, `palette`, `type_scale`,
  `spacing_scale`, `fluid_type`) werfen nie: Sie liefern immer ein dict mit `ok`.
  Fehler: `{"ok": False, "error": "…", "field": "…"}`.

Farbformate: `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa` (auch ohne `#`), `rgb()`/`rgba()`
und `hsl()`/`hsla()` in Komma- und Leerzeichen-Schreibweise.

Kontrast nach WCAG 2.2 (relative Luminanz, Schwelle 0.04045). Bewertet wird mit dem
**ungerundeten** Verhältnis — 4.499:1 besteht AA nicht, auch wenn es gerundet 4.5 wäre.

CLI: `python3 designlab.py contrast '#767676' '#fff'` · `palette '#fa1e4e' triadic` ·
`scale 16 1.25 7` · `convert 'hsl(345,96%,55%)'` · `spacing 8 10` · `fluid 16 24`.
"""
from __future__ import annotations

import json
import math
import re
import sys

PENDING = "— ausstehend —"

# Schwellen aus WCAG 2.2: SC 1.4.3 (AA), SC 1.4.6 (AAA), SC 1.4.11 (Nicht-Text)
AA_NORMAL, AA_LARGE = 4.5, 3.0
AAA_NORMAL, AAA_LARGE = 7.0, 4.5
UI_COMPONENTS = 3.0

SCHEMES = {
    "complementary": [0, 180],
    "analogous": [-30, 0, 30],
    "triadic": [0, 120, 240],
    "tetradic": [0, 90, 180, 270],      # Quadrat auf dem Farbkreis
    "monochrome": None,                 # gleicher Farbton, Helligkeitsstufen
}

RATIOS = {                               # klassische Intervall-Namen für Skalen
    "minor-second": 1.067, "major-second": 1.125, "minor-third": 1.2,
    "major-third": 1.25, "perfect-fourth": 1.333, "augmented-fourth": 1.414,
    "perfect-fifth": 1.5, "golden": 1.618,
}

DKZ_SPACE = {4: "--space-xs", 8: "--space-sm", 16: "--space-md", 24: "--space-lg",
             32: "--space-xl", 48: "--space-2xl"}


class DesignError(ValueError):
    """Ungültige Eingabe. `field` nennt den betroffenen Parameter."""

    def __init__(self, message: str, field: str = ""):
        super().__init__(message)
        self.field = field


def _err(exc: Exception, field: str = "") -> dict:
    return {"ok": False, "error": str(exc),
            "field": getattr(exc, "field", "") or field}


def _num(value, field: str, lo: float, hi: float) -> float:
    try:
        x = float(value)
    except (TypeError, ValueError):
        raise DesignError(f"'{field}' muss eine Zahl sein", field) from None
    if not math.isfinite(x) or x < lo or x > hi:
        raise DesignError(f"'{field}' muss zwischen {lo:g} und {hi:g} liegen", field)
    return x


def _int(value, field: str, lo: int, hi: int) -> int:
    x = _num(value, field, lo, hi)
    if x != int(x):
        raise DesignError(f"'{field}' muss eine ganze Zahl sein", field)
    return int(x)


def _r(x: float, nd: int = 2) -> float:
    """Kaufmännisch runden (half away from zero), -0 wird 0."""
    m = 10 ** nd
    y = math.floor(abs(x) * m + 0.5) / m
    y = -y if x < 0 else y
    return 0.0 if y == 0 else y


def _fmt(x: float, nd: int = 4) -> str:
    s = f"{_r(x, nd):.{nd}f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


# --------------------------------------------------------------------------- #
# Farben parsen und umrechnen                                                  #
# --------------------------------------------------------------------------- #
_HEX = re.compile(r"#?([0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})")
_FUNC = re.compile(r"(rgba?|hsla?)\(\s*([^()]*)\)", re.IGNORECASE)


def _channel(tok: str, field: str) -> float:
    tok = tok.strip()
    if tok.endswith("%"):
        v = _num(tok[:-1], field, 0, 100) * 2.55
    else:
        v = _num(tok, field, 0, 255)
    return v


def _alpha(tok: str, field: str) -> float:
    tok = tok.strip()
    if tok.endswith("%"):
        return _num(tok[:-1], field, 0, 100) / 100
    return _num(tok, field, 0, 1)


def _hue(tok: str, field: str) -> float:
    tok = tok.strip().lower()
    if tok.endswith("deg"):
        tok = tok[:-3]
    elif tok.endswith("turn"):
        return (_num(tok[:-4], field, -1e6, 1e6) * 360) % 360
    return _num(tok, field, -1e6, 1e6) % 360


def _split_args(body: str) -> list[str]:
    body = body.strip()
    alpha = None
    if "/" in body:                                  # moderne Schreibweise: r g b / a
        body, _, alpha = body.partition("/")
    parts = [p for p in re.split(r"[\s,]+", body.strip()) if p]
    if alpha is not None:
        parts.append(alpha.strip())
    return parts


def parse_color(value, field: str = "color") -> tuple[int, int, int, float]:
    """Farbe → (r, g, b, alpha). Wirft DesignError bei ungültiger Eingabe."""
    if not isinstance(value, str) or not value.strip():
        raise DesignError(f"'{field}' fehlt oder ist leer", field)
    s = value.strip()
    if len(s) > 64:
        raise DesignError(f"'{field}' ist zu lang", field)
    m = _HEX.fullmatch(s)
    if m:
        h = m.group(1)
        if len(h) in (3, 4):
            h = "".join(c * 2 for c in h)
        r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4))
        a = int(h[6:8], 16) / 255 if len(h) == 8 else 1.0
        return r, g, b, _r(a, 4)
    m = _FUNC.fullmatch(s)
    if not m:
        raise DesignError(
            f"'{field}': Farbformat nicht erkannt (erlaubt: #rgb, #rrggbb, rgb(), hsl())",
            field)
    kind, parts = m.group(1).lower(), _split_args(m.group(2))
    if len(parts) not in (3, 4):
        raise DesignError(f"'{field}': {kind}() braucht 3 Werte (plus optional Alpha)",
                          field)
    a = _alpha(parts[3], field) if len(parts) == 4 else 1.0
    if kind.startswith("rgb"):
        r, g, b = (_channel(p, field) for p in parts[:3])
        return round(r), round(g), round(b), _r(a, 4)
    h = _hue(parts[0], field)
    sat, lig = (p.strip() for p in parts[1:3])
    sv = _num(sat.rstrip("%"), field, 0, 100)
    lv = _num(lig.rstrip("%"), field, 0, 100)
    r, g, b = hsl_to_rgb(h, sv, lv)
    return r, g, b, _r(a, 4)


def try_parse_color(value) -> tuple[int, int, int, float] | None:
    """Wie parse_color, aber None statt Ausnahme."""
    try:
        return parse_color(value)
    except DesignError:
        return None


def rgb_to_hex(r: int, g: int, b: int) -> str:
    return "#{:02x}{:02x}{:02x}".format(*(max(0, min(255, int(c))) for c in (r, g, b)))


def rgb_to_hsl(r: int, g: int, b: int) -> tuple[float, float, float]:
    """RGB (0–255) → (h 0–360, s 0–100, l 0–100), ungerundet."""
    rf, gf, bf = r / 255, g / 255, b / 255
    mx, mn = max(rf, gf, bf), min(rf, gf, bf)
    lig = (mx + mn) / 2
    d = mx - mn
    if d == 0:
        return 0.0, 0.0, lig * 100
    sat = d / (1 - abs(2 * lig - 1))
    if mx == rf:
        h = ((gf - bf) / d) % 6
    elif mx == gf:
        h = (bf - rf) / d + 2
    else:
        h = (rf - gf) / d + 4
    return (h * 60) % 360, sat * 100, lig * 100


def hsl_to_rgb(h: float, s: float, l: float) -> tuple[int, int, int]:  # noqa: E741
    """(h 0–360, s 0–100, l 0–100) → RGB (0–255)."""
    h, s, l = h % 360, max(0.0, min(100.0, s)) / 100, max(0.0, min(100.0, l)) / 100  # noqa: E741
    c = (1 - abs(2 * l - 1)) * s
    x = c * (1 - abs((h / 60) % 2 - 1))
    m = l - c / 2
    seg = int(h // 60)
    rf, gf, bf = [(c, x, 0), (x, c, 0), (0, c, x), (0, x, c), (x, 0, c), (c, 0, x)][seg % 6]
    return tuple(int(math.floor((v + m) * 255 + 0.5)) for v in (rf, gf, bf))  # type: ignore[return-value]


def _describe(r: int, g: int, b: int, a: float = 1.0) -> dict:
    h, s, l = rgb_to_hsl(r, g, b)  # noqa: E741
    hr, sr, lr = _r(h, 1), _r(s, 1), _r(l, 1)
    out = {"hex": rgb_to_hex(r, g, b), "rgb": [r, g, b], "hsl": [hr, sr, lr],
           "css": {"hex": rgb_to_hex(r, g, b),
                   "rgb": f"rgb({r}, {g}, {b})",
                   "hsl": f"hsl({_fmt(hr, 1)}, {_fmt(sr, 1)}%, {_fmt(lr, 1)}%)"}}
    if a < 1:
        out["alpha"] = a
        out["css"]["rgb"] = f"rgba({r}, {g}, {b}, {_fmt(a, 3)})"
        out["css"]["hsl"] = f"hsla({_fmt(hr, 1)}, {_fmt(sr, 1)}%, {_fmt(lr, 1)}%, {_fmt(a, 3)})"
    return out


def convert(color) -> dict:
    """HEX ↔ RGB ↔ HSL. Liefert alle Schreibweisen."""
    try:
        r, g, b, a = parse_color(color, "color")
    except DesignError as exc:
        return _err(exc)
    return {"ok": True, "input": str(color).strip(), **_describe(r, g, b, a),
            "luminance": _r(relative_luminance((r, g, b)), 4)}


# --------------------------------------------------------------------------- #
# Kontrast nach WCAG 2.2                                                       #
# --------------------------------------------------------------------------- #
def _lin(c: int) -> float:
    v = c / 255
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def relative_luminance(rgb) -> float:
    r, g, b = rgb[:3]
    return 0.2126 * _lin(r) + 0.7152 * _lin(g) + 0.0722 * _lin(b)


def _composite(fg, bg) -> tuple[int, int, int]:
    """Halbtransparente Vordergrundfarbe über deckendem Hintergrund mischen."""
    a = fg[3]
    return tuple(round(fg[i] * a + bg[i] * (1 - a)) for i in range(3))  # type: ignore[return-value]


def contrast_ratio(fg, bg) -> float:
    """Ungerundetes Kontrastverhältnis (1…21). Akzeptiert Strings oder Tupel.

    Wirft DesignError bei ungültiger Farbe oder halbtransparentem Hintergrund.
    """
    f = parse_color(fg, "fg") if isinstance(fg, str) else tuple(fg) + ((1.0,) if len(fg) == 3 else ())
    b = parse_color(bg, "bg") if isinstance(bg, str) else tuple(bg) + ((1.0,) if len(bg) == 3 else ())
    if b[3] < 1:
        raise DesignError("'bg' muss deckend sein (Alpha 1), sonst ist der Kontrast "
                          "nicht eindeutig", "bg")
    fr = _composite(f, b) if f[3] < 1 else f[:3]
    l1, l2 = relative_luminance(fr), relative_luminance(b)
    hi, lo = max(l1, l2), min(l1, l2)
    return (hi + 0.05) / (lo + 0.05)


def grade(ratio: float) -> dict:
    """Einstufung eines (ungerundeten) Verhältnisses."""
    def g(aaa: float, aa: float) -> str:
        return "AAA" if ratio >= aaa else ("AA" if ratio >= aa else "fail")
    return {
        "normal": g(AAA_NORMAL, AA_NORMAL),          # Fließtext
        "large": g(AAA_LARGE, AA_LARGE),             # ab 24 px bzw. 18.66 px fett
        "ui": "pass" if ratio >= UI_COMPONENTS else "fail",   # SC 1.4.11
    }


def suggest_fix(fg, bg, target: float = AA_NORMAL) -> str | None:
    """Nächstliegende Vordergrundfarbe (gleicher Farbton/Sättigung), die `target` erreicht."""
    try:
        f = parse_color(fg, "fg")
        b = parse_color(bg, "bg")
        if contrast_ratio(f, b) >= target:
            return rgb_to_hex(*f[:3])
    except DesignError:
        return None
    h, s, l0 = rgb_to_hsl(*f[:3])
    best = None
    for direction in (-1, 1):                         # dunkler und heller prüfen
        lo, hi = (0.0, l0) if direction < 0 else (l0, 100.0)
        edge = hsl_to_rgb(h, s, lo if direction < 0 else hi)
        if contrast_ratio(edge + (1.0,), b) < target:
            continue
        for _ in range(40):                           # Bisektion auf der Helligkeit
            mid = (lo + hi) / 2
            ok = contrast_ratio(hsl_to_rgb(h, s, mid) + (1.0,), b) >= target
            if direction < 0:
                lo, hi = (lo, mid) if not ok else (mid, hi)
            else:
                lo, hi = (lo, mid) if ok else (mid, hi)
        cand_l = lo if direction < 0 else hi
        cand = hsl_to_rgb(h, s, cand_l)
        if contrast_ratio(cand + (1.0,), b) < target:  # Rundung absichern
            cand = hsl_to_rgb(h, s, cand_l + (-0.5 if direction < 0 else 0.5))
        dist = abs(cand_l - l0)
        if best is None or dist < best[0]:
            best = (dist, cand)
    return rgb_to_hex(*best[1]) if best else None


def contrast(fg, bg) -> dict:
    """WCAG-Kontrast mit Einstufung für Fließtext, großen Text und UI-Komponenten."""
    try:
        f = parse_color(fg, "fg")
        b = parse_color(bg, "bg")
        ratio = contrast_ratio(f, b)
    except DesignError as exc:
        return _err(exc)
    gr = grade(ratio)
    out = {
        "ok": True,
        "fg": rgb_to_hex(*f[:3]), "bg": rgb_to_hex(*b[:3]),
        "ratio": _r(ratio, 2), "ratio_exact": ratio,
        "ratio_text": f"{_fmt(ratio, 2)}:1",
        "grade": gr,
        "aa": {"normal": ratio >= AA_NORMAL, "large": ratio >= AA_LARGE},
        "aaa": {"normal": ratio >= AAA_NORMAL, "large": ratio >= AAA_LARGE},
        "ui_components": ratio >= UI_COMPONENTS,
        "thresholds": {"aa_normal": AA_NORMAL, "aa_large": AA_LARGE,
                       "aaa_normal": AAA_NORMAL, "aaa_large": AAA_LARGE,
                       "ui": UI_COMPONENTS},
        "source": "WCAG 2.2, SC 1.4.3 / 1.4.6 / 1.4.11",
    }
    if f[3] < 1:
        out["fg_alpha"] = f[3]
        out["fg_composited"] = rgb_to_hex(*_composite(f, b))
    if ratio < AA_NORMAL:
        out["suggestion"] = {"target": AA_NORMAL, "fg": suggest_fix(fg, bg, AA_NORMAL)}
    return out


# --------------------------------------------------------------------------- #
# Paletten                                                                     #
# --------------------------------------------------------------------------- #
def _text_on(rgb) -> str:
    """Schwarz oder Weiß — was auf dieser Fläche den höheren Kontrast hat."""
    on_white = contrast_ratio(rgb + (1.0,), (255, 255, 255, 1.0))
    on_black = contrast_ratio(rgb + (1.0,), (0, 0, 0, 1.0))
    return "#ffffff" if on_white >= on_black else "#000000"


def palette(base, scheme: str = "complementary") -> dict:
    """Farbharmonien über HSL-Rotation (bzw. Helligkeitsstufen bei monochrome)."""
    try:
        r, g, b, _a = parse_color(base, "base")
    except DesignError as exc:
        return _err(exc)
    scheme = str(scheme or "complementary").strip().lower()
    if scheme not in SCHEMES:
        return {"ok": False, "field": "scheme",
                "error": "Unbekanntes Schema. Erlaubt: " + ", ".join(SCHEMES)}
    h, s, l = rgb_to_hsl(r, g, b)  # noqa: E741
    colors = []
    if scheme == "monochrome":
        seen = []
        for d in (-30, -15, 0, 15, 30):
            lv = max(5.0, min(95.0, l + d)) if d else l
            rgb = hsl_to_rgb(h, s, lv) if d else (r, g, b)
            hx = rgb_to_hex(*rgb)
            if hx in seen:
                continue
            seen.append(hx)
            colors.append({**_describe(*rgb), "role": "base" if d == 0 else
                           ("darker" if d < 0 else "lighter"), "offset_l": d,
                           "text_on": _text_on(rgb)})
    else:
        for d in SCHEMES[scheme]:
            rgb = (r, g, b) if d == 0 else hsl_to_rgb(h + d, s, l)
            colors.append({**_describe(*rgb), "role": "base" if d == 0 else f"{d:+d}°",
                           "rotation": d, "text_on": _text_on(rgb)})
    return {"ok": True, "base": rgb_to_hex(r, g, b), "scheme": scheme,
            "colors": colors, "method": "HSL-Rotation" if scheme != "monochrome"
            else "HSL-Helligkeitsstufen"}


# --------------------------------------------------------------------------- #
# Typografie, Abstände, fluide Schrift                                         #
# --------------------------------------------------------------------------- #
def type_scale(base=16, ratio=1.25, steps=7, below=0, root=16) -> dict:
    """Modulare Skala: size_i = base · ratio^i, für i = -below … steps-1."""
    try:
        if isinstance(ratio, str) and ratio.strip().lower() in RATIOS:
            ratio_name = ratio.strip().lower()
            ratio_v = RATIOS[ratio_name]
        else:
            ratio_name = None
            ratio_v = _num(ratio, "ratio", 1.01, 4)
        base_v = _num(base, "base", 4, 200)
        steps_v = _int(steps, "steps", 1, 20)
        below_v = _int(below, "below", 0, 6)
        root_v = _num(root, "root", 4, 64)
    except DesignError as exc:
        return _err(exc)
    out = []
    for i in range(-below_v, steps_v):
        px = base_v * ratio_v ** i
        out.append({"step": i, "px": _r(px, 2), "rem": _r(px / root_v, 4),
                    "css": f"{_fmt(px / root_v, 4)}rem"})
    return {"ok": True, "base": base_v, "ratio": ratio_v, "ratio_name": ratio_name,
            "steps": out, "root": root_v}


def spacing_scale(base=8, steps=10, root=16) -> dict:
    """8-Punkt-Abstandsskala: ein Halbschritt (base/2), dann Vielfache von base."""
    try:
        base_v = _int(base, "base", 2, 64)
        steps_v = _int(steps, "steps", 1, 32)
        root_v = _num(root, "root", 4, 64)
    except DesignError as exc:
        return _err(exc)
    values = ([base_v / 2] if base_v % 2 == 0 else []) + [base_v * i for i in range(1, steps_v + 1)]
    out = []
    for i, px in enumerate(values):
        item = {"step": i, "px": _r(px, 2), "rem": _r(px / root_v, 4)}
        if base_v == 8 and px in DKZ_SPACE:
            item["dkz_token"] = DKZ_SPACE[int(px)]
        out.append(item)
    return {"ok": True, "base": base_v, "steps": out}


def on_scale(px, base: int = 4) -> bool:
    """Liegt ein Pixelwert auf dem Raster (Vielfaches von base, 0 eingeschlossen)?"""
    try:
        v = float(px)
    except (TypeError, ValueError):
        return False
    return math.isfinite(v) and abs(v / base - round(v / base)) < 1e-9


def snap(px, base: int = 8) -> float | None:
    """Nächster Rasterwert."""
    try:
        v = float(px)
    except (TypeError, ValueError):
        return None
    return float(round(v / base) * base) if math.isfinite(v) else None


def fluid_type(min_px=16, max_px=24, min_vw=320, max_vw=1280, root=16) -> dict:
    """Fluide Schriftgröße als clamp(): linear zwischen zwei Viewport-Breiten."""
    try:
        lo = _num(min_px, "min", 4, 400)
        hi = _num(max_px, "max", 4, 400)
        vw0 = _num(min_vw, "min_vw", 100, 10000)
        vw1 = _num(max_vw, "max_vw", 100, 10000)
        root_v = _num(root, "root", 4, 64)
        if hi < lo:
            raise DesignError("'max' muss mindestens so groß wie 'min' sein", "max")
        if vw1 <= vw0:
            raise DesignError("'max_vw' muss größer als 'min_vw' sein", "max_vw")
    except DesignError as exc:
        return _err(exc)
    slope = (hi - lo) / (vw1 - vw0)                  # px je px Viewport
    intercept = lo - slope * vw0                      # px
    pref = f"{_fmt(intercept / root_v)}rem + {_fmt(slope * 100)}vw"
    css = f"clamp({_fmt(lo / root_v)}rem, {pref}, {_fmt(hi / root_v)}rem)"
    return {"ok": True, "css": css, "min_px": lo, "max_px": hi,
            "min_vw": vw0, "max_vw": vw1, "slope_vw": _r(slope * 100, 4),
            "intercept_rem": _r(intercept / root_v, 4),
            "note": "rem-Anteil im Mittelwert erhält das Zoomen (WCAG 2.2, SC 1.4.4)"}


# --------------------------------------------------------------------------- #
# CLI                                                                          #
# --------------------------------------------------------------------------- #
def _cli(argv: list[str]) -> int:
    cmds = {
        "contrast": lambda a: contrast(*a[:2]),
        "convert": lambda a: convert(a[0] if a else ""),
        "palette": lambda a: palette(a[0] if a else "", a[1] if len(a) > 1 else "complementary"),
        "scale": lambda a: type_scale(*a[:3]),
        "spacing": lambda a: spacing_scale(*a[:2]),
        "fluid": lambda a: fluid_type(*a[:4]),
    }
    if not argv or argv[0] not in cmds:
        print("Aufruf: designlab.py " + "|".join(cmds) + " …", file=sys.stderr)
        return 1
    try:
        res = cmds[argv[0]](argv[1:])
    except TypeError:
        res = {"ok": False, "error": "falsche Anzahl Argumente"}
    print(json.dumps(res, ensure_ascii=False, indent=2))
    return 0 if res.get("ok") else 1


if __name__ == "__main__":
    sys.exit(_cli(sys.argv[1:]))
