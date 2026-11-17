#!/usr/bin/env python3
"""svgkit.py — svgspec/1 (DkZ), Python-Fassung. Regeln: svg/SPEC.md, Referenz: svgkit.js.

Nur Standardbibliothek. Schnittstelle:
    generate(spec)      -> {"ok": True, "svg", "spec"} | {"ok": False, "error", "field"}
    sanitize(svg_text)  -> {"svg", "removed": [...]}
    fmt(n)              -> Zahlenformat (ganzzahlig gerundet, kein round()/format())
CLI (CONTRACT §3): .venv/bin/python svgkit.py <spec.json>
    Erfolg: exakt das SVG nach stdout, ohne abschließenden Zeilenumbruch.
    Fehler: Exitcode 1, auf stderr eine Zeile "svgspec: <Feld>: <Meldung>", stdout leer.

Der Generator hält keinen globalen Zustand (Theme, Modus liegen in einer Instanz je Aufruf) und ist
damit threadsicher — der Server ruft ihn aus mehreren Threads gleichzeitig auf.
"""
from __future__ import annotations

import copy
import json
import math
import re
import sys
import unicodedata

VERSION = "svgspec/1"
P = 16
NA = "— ausstehend —"


# --------------------------------------------------------------------------- #
# Zahlen, Text                                                                 #
# --------------------------------------------------------------------------- #
def _isnum(v) -> bool:
    """Endliche JSON-Zahl (kein Bool, kein Text)."""
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return False
    try:
        return math.isfinite(float(v))
    except OverflowError:  # riesige Ganzzahl, in JS wäre das Infinity
        return False


def _n(x):
    """Ganzzahlige Werte als int führen (saubere JSON-Ausgabe); Rechnung bleibt f64-gleich."""
    x = float(x)
    if x.is_integer() and abs(x) < 9007199254740992.0:
        return int(x)
    return x


def fmt(n) -> str:
    """Einzige Zahlenregel (SPEC §2): k = floor(|n|*100 + 0.5), alles Weitere ganzzahlig."""
    n = float(n)
    if not math.isfinite(n):
        return "0"
    k = math.floor(abs(n) * 100 + 0.5)
    if k == 0:
        return "0"
    f = k % 100
    s = ("-" if n < 0 else "") + str(math.floor(k / 100))
    if f:
        s += "." + ("0" + str(f) if f < 10 else str(f) if f % 10 else str(f // 10))
    return s


def _short(v) -> str:
    if abs(v) >= 1e6:
        return fmt(v / 1e6) + "M"
    if abs(v) >= 1e4:
        return fmt(v / 1e3) + "k"
    return fmt(v)


_XE = {"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&apos;"}
_ESC_RE = re.compile(r"[&<>\"']")


def _esc(s) -> str:
    return _ESC_RE.sub(lambda m: _XE[m.group(0)], str(s))


_CLEAN_RE = re.compile("[\x00-\x1f\x7f￾￿\ud800-\udfff]")


def _clean(s: str, n: int) -> str:
    return _CLEAN_RE.sub("", s)[: int(n)]


def _trunc(s: str, n) -> str:
    if len(s) <= n:
        return s
    if n < 1:
        return ""
    return s[: int(n) - 1] + "…"


def _fit(px, fs) -> int:
    return math.floor(px / (fs * 0.6))


def _num(v) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _E(tag, attrs=None, content=None) -> str:
    s = "<" + tag
    if attrs:
        for k, v in attrs.items():
            if v is None or v is False:
                continue
            s += " " + k + '="' + (fmt(v) if _num(v) else _esc(v)) + '"'
    return s + ("/>" if content is None else ">" + content + "</" + tag + ">")


def _D(*a) -> str:
    out, prev = [], False
    for v in a:
        if isinstance(v, str):
            out.append(v)
            prev = False
        else:
            out.append((" " if prev else "") + fmt(v))
            prev = True
    return "".join(out)


# --------------------------------------------------------------------------- #
# Themes                                                                       #
# --------------------------------------------------------------------------- #
TOK = "bg card elevated text muted accent green yellow red blue pink amber cyan lila".split()
_CSSV = {"card": "bg-card", "elevated": "bg-elevated", "muted": "text-muted"}
_BASE = "060608 0e0e10 141418 f6f6f7 8e8e8f fa1e4e 00ff88 ffb800 ff3b5c 55acee ec4899 f5a623 3fe0d0 9d7bff".split()
_FONT = "Inter,system-ui,-apple-system,sans-serif"
_SER = "accent cyan lila amber green blue pink yellow"


def _th(hexes, ser, radius, font, line, lineop):
    return {"hex": {t: "#" + hexes[i] for i, t in enumerate(TOK)}, "series": ser.split(), "radius": radius,
            "font": font, "line": line, "lineop": lineop}


THEMES = {
    "master": _th(_BASE, _SER, 12, _FONT, "text", 0.08),
    "light": _th("f6f6f7 ffffff f0f0f2 0e0e10 6a6a6b".split() + _BASE[5:],
                 "accent blue lila pink amber cyan green yellow", 12, _FONT, "text", 0.08),
    "contrast": _th(["000000" if i < 3 else "00ff88" for i in range(len(TOK))], "accent", 12, _FONT, "text", 1),
    "control": _th(_BASE, _SER, 0, "Archivo Expanded," + _FONT, "accent", 0.3),
    "content": _th(_BASE, "accent lila cyan pink amber green blue yellow", 20, "Syne," + _FONT, "text", 0.08),
}

ICONS = {
    "check": "M5 12.5L9.5 17L19 7.5", "cross": "M6 6L18 18M18 6L6 18", "play": "M8 5V19L19 12Z",
    "pause": "M8 5V19M16 5V19", "arrow-right": "M4 12H20M14 6L20 12L14 18", "arrow-left": "M20 12H4M10 6L4 12L10 18",
    "star": "M12 3L14.4 9.3L21 9.6L15.8 13.7L17.6 20.2L12 16.5L6.4 20.2L8.2 13.7L3 9.6L9.6 9.3Z",
    "heart": "M12 20C12 20 4 15.5 4 9.5A4 4 0 0 1 12 7.5A4 4 0 0 1 20 9.5C20 15.5 12 20 12 20Z",
    "menu": "M4 6H20M4 12H20M4 18H20", "plus": "M12 5V19M5 12H19", "minus": "M5 12H19",
    "info": "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM12 11V17M12 7.5V8",
    "search": "M4 10.5A6.5 6.5 0 1 0 17 10.5A6.5 6.5 0 1 0 4 10.5ZM15.5 15.5L20 20",
    "download": "M12 4V15M7 10L12 15L17 10M5 20H19",
}


# --------------------------------------------------------------------------- #
# Schema (gleiche Kurzschrift wie svgkit.js: name|typ|min/Auswahl|max|default|label|len)  #
# --------------------------------------------------------------------------- #
def _WH(w, h, a):
    return f"width|int|{a}|2400|{w}|Breite;height|int|{120 if a > 99 else a}|2400|{h}|Höhe;"


def _COL(n, d, label):
    return f"{n}|color|||{d}|{label};"


def _B(n, d, label):
    return f"{n}|bool|||{d}|{label};"


_HD = _B("heading", 1, "Titel zeigen")
_FA = _COL("color", "accent", "Farbe")


def _LAB(n):
    return f"labels|strs|||[]|Beschriftungen|{n};"


_TY = {
    "bar": ["Balkendiagramm", _WH(480, 280, 160) + "data|nums|-1e9|1e9|[12,19,7,15,22,10]|Werte|64;" + _LAB(64) + _FA
            + _B("values", 1, "Werte zeigen") + _B("grid", 1, "Raster") + _HD],
    "line": ["Liniendiagramm", _WH(480, 280, 160) + "data|series|-1e9|1e9|[[4,9,6,12,10,15,13]]|Reihen|200;" + _LAB(200)
             + _FA + _B("area", 1, "Fläche") + _B("dots", 1, "Punkte") + _B("grid", 1, "Raster") + _HD],
    "donut": ["Ringdiagramm", _WH(360, 240, 160) + "data|nums|0|1e9|[45,30,25]|Werte|12;" + _LAB(12)
              + "thickness|num|0.1|0.9|0.34|Ringstärke;center|str||||Mitte|16;" + _B("legend", 1, "Legende") + _HD],
    "pattern": ["Muster", _WH(240, 240, 16) + "kind|enum|grid/dots/stripes/waves/confetti||grid|Art;"
                "size|int|4|200|24|Zellgröße;stroke|num|0.5|12|1.5|Linienstärke;angle|int|-90|90|45|Winkel;"
                "density|int|1|400|60|Dichte;opacity|num|0.05|1|0.6|Deckkraft;" + _FA + _B("bg", 1, "Karte")],
    "badge": ["Badge", "height|int|16|64|24|Höhe;label|str|||DkZ|Beschriftung|32;value|str|||aktiv|Wert|32;" + _FA],
    "icon": ["Symbol", "name|enum|" + "/".join(ICONS) + "||check|Symbol;size|int|12|512|48|Größe;" + _FA
             + "stroke|num|1|4|2|Linienstärke;" + _B("bg", 0, "Karte")],
    "monogram": ["Monogramm", "text|str|||DZ|Zeichen|3;size|int|24|512|96|Größe;shape|enum|circle/square/hex||circle|Form;"
                 + _FA + _COL("color2", "lila", "Zweitfarbe")],
    "gradient": ["Verlauf", _WH(480, 240, 16) + "kind|enum|linear/radial||linear|Art;" + _COL("from", "accent", "Von")
                 + _COL("to", "lila", "Bis") + "via|colorx||||Über;angle|int|0|360|45|Winkel;cx|num|0|100|50|Mitte x;"
                 "cy|num|0|100|50|Mitte y;text|str||||Text|60;"],
    "graph": ["Workflow", _WH(0, 0, 0) + 'nodes|nodes|||[{"id":"A","type":"input"},{"id":"B","type":"model"},'
              '{"id":"C","type":"output"}]|Knoten|60;edges|edges|||[{"from":"A","to":"B"},{"from":"B","to":"C"}]|Kanten|120;'
              + _HD],
}
_COMMON = ("theme|enum|" + "/".join(THEMES) + "||master|Theme;colors|enum|vars/static||vars|Farbmodus;"
           "idprefix|str|||sk|ID-Präfix|32;seed|int|0|4294967295|1|Seed;")
_UNIT = {"width": "px", "height": "px", "size": "px", "stroke": "px", "angle": "°", "cx": "%", "cy": "%"}


def _build_schema():
    out = {}
    for t, (label, rest) in _TY.items():
        felder = []
        for d in (_COMMON + "title|str|||" + label + "|Titel|120;" + rest).split(";"):
            if not d:
                continue
            parts = d.split("|") + [None] * 7
            name, typ, a, b, v, lab, ln = parts[:7]
            f = {"name": name, "type": typ, "label": lab or name}
            if typ == "enum":
                f["choices"] = a.split("/")
            if a and typ != "enum":
                f["min"] = _n(float(a))
                f["max"] = _n(float(b))
            if typ in ("int", "num"):
                f["step"] = 1 if typ == "int" else 0.01
                if name in _UNIT:
                    f["unit"] = _UNIT[name]
            if "step" in f:
                f["default"] = _n(float(v))
            elif typ == "bool":
                f["default"] = v == "1"
            elif re.match(r"[\[{]", v):
                f["default"] = json.loads(v)
            else:
                f["default"] = v
            if ln:
                f["len"] = int(ln)
            felder.append(f)
        out[t] = {"label": label, "felder": felder}
    return out


SCHEMA = _build_schema()
THEME_NAMES = list(THEMES)


def tokens(name):
    return dict((THEMES.get(name) or THEMES["master"])["hex"])


# --------------------------------------------------------------------------- #
# Normalisierung                                                               #
# --------------------------------------------------------------------------- #
class _Bad(Exception):
    def __init__(self, field, error):
        super().__init__(error)
        self.field, self.error = field, error


_HEX_RE = re.compile(r"#(?:[0-9a-fA-F]{3}){1,2}")
_IDP_RE = re.compile(r"[A-Za-z][A-Za-z0-9_-]*")


def _val(f, v):
    k, name = f["type"], f["name"]

    def no(m="ungültiger Wert"):
        raise _Bad(name, m)

    def num(e):
        if not _isnum(e):
            no("Zahl erwartet")
        return _n(min(max(float(e), f["min"]), f["max"]))

    def string(e, n):
        if e is None:
            return ""
        if not isinstance(e, str):
            no("Text erwartet")
        return _clean(e, n)

    if v is None:
        return copy.deepcopy(f["default"])
    if k == "int":
        return _n(math.floor(num(v)))
    if k == "num":
        return num(v)
    if k == "bool":
        return v if isinstance(v, bool) else no("true/false erwartet")
    if k == "enum":
        return v if (isinstance(v, str) and v in f["choices"]) else no("erlaubt: " + ", ".join(f["choices"]))
    if k == "str":
        return string(v, f["len"])
    if k[0] == "c":
        if not isinstance(v, str) or not (v in TOK or _HEX_RE.fullmatch(v) or (k == "colorx" and not v)):
            no("Token oder #rgb/#rrggbb")
        v = v.lower()
        return re.sub(r"^#(.)(.)(.)$", r"#\1\1\2\2\3\3", v)
    if not isinstance(v, list):
        no("Liste erwartet")
    v = v[: f["len"]]
    if k == "nums":
        return [num(e) for e in v]
    if k == "series":
        if v and isinstance(v[0], list):
            return [[num(e) for e in r[:200]] if isinstance(r, list) else no() for r in v[:4]]
        return [[num(e) for e in v]] if v else []
    if k == "strs":
        return [_clean(e, 40) if isinstance(e, str) else no("Text erwartet") for e in v]
    o = []
    for e in v:
        if not isinstance(e, dict):
            no()
        if k == "edges":
            o.append({"from": string(e.get("from"), 32), "to": string(e.get("to"), 32)})
            continue
        r = {"id": string(e.get("id"), 32), "type": string(e.get("type"), 24), "label": string(e.get("label"), 40)}
        if not r["id"] or any(q["id"] == r["id"] for q in o):
            no("id fehlt oder doppelt")
        if e.get("x") is not None or e.get("y") is not None:
            if not _isnum(e.get("x")) or not _isnum(e.get("y")):
                no("x/y: Zahlen")
            r["x"] = _n(min(max(float(e["x"]), 0), 4000))
            r["y"] = _n(min(max(float(e["y"]), 0), 4000))
        o.append(r)
    return o


def _norm(x):
    if not isinstance(x, dict):
        raise _Bad("spec", "Objekt erwartet")
    t = x.get("type")
    if not isinstance(t, str) or t not in _TY:
        raise _Bad("type", "unbekannter Typ")
    o = {"type": t}
    for f in SCHEMA[t]["felder"]:
        o[f["name"]] = _val(f, x.get(f["name"]))
    if "nodes" in o and x.get("nodes") is not None and x.get("edges") is None:
        o["edges"] = []
    if not _IDP_RE.fullmatch(o["idprefix"]):
        raise _Bad("idprefix", "[A-Za-z][A-Za-z0-9_-]*")
    if not o["title"]:
        o["title"] = _TY[t][0]
    if "edges" in o:
        ids = {n["id"] for n in o["nodes"]}
        for e in o["edges"]:
            if e["from"] not in ids or e["to"] not in ids:
                raise _Bad("edges", "unbekannter Knoten")
    return o


# --------------------------------------------------------------------------- #
# Generator (eine Instanz je Aufruf — kein gemeinsamer Zustand)                #
# --------------------------------------------------------------------------- #
class _Gen:
    def __init__(self, s):
        self.s = s
        self.T = THEMES[s["theme"]]
        self.V = s["colors"] == "vars"
        self.K = s["theme"] == "contrast"

    # -- Farben --------------------------------------------------------------
    def hexof(self, t):
        if self.K:
            return "#000000" if t in TOK[:3] else "#00ff88"
        return t if t[0] == "#" else self.T["hex"][t]

    def C(self, t):
        if self.V and t[0] != "#":
            return "var(--" + _CSSV.get(t, t) + ", " + self.hexof(t) + ")"
        return self.hexof(t)

    def ink(self, t):
        if self.K:
            return "#000000"
        n = int(self.hexof(t)[1:], 16)
        return "#060608" if (n >> 16) * 299 + (n >> 8 & 255) * 587 + (n & 255) * 114 >= 128000 else "#ffffff"

    def op(self, x):
        return None if self.K else x

    def SC(self, i):
        ser = self.T["series"]
        return ser[i % len(ser)]

    def CM(self):
        return self.C("muted")

    def CT(self):
        return self.C("text")

    def LN(self):
        return {"stroke": self.C(self.T["line"]), "stroke-opacity": self.op(self.T["lineop"])}

    # -- Bausteine -----------------------------------------------------------
    @staticmethod
    def RC(x, y, width, height, rx, a=None):
        d = {"x": x, "y": y, "width": width, "height": height, "rx": rx}
        if a:
            d.update(a)
        return _E("rect", d)

    @staticmethod
    def CI(cx, cy, r, a=None):
        d = {"cx": cx, "cy": cy, "r": r}
        if a:
            d.update(a)
        return _E("circle", d)

    @staticmethod
    def PA(d, a=None):
        o = {"d": d}
        if a:
            o.update(a)
        return _E("path", o)

    def fr(self, w, h, rx, a):
        return self.RC(0.5, 0.5, w - 1, h - 1, rx, a)

    def card(self, w, h):
        T = self.T
        o = [self.fr(w, h, T["radius"], {"fill": self.C("card"), **self.LN()})]
        if not T["radius"]:
            o.append(self.PA(_D("M", 1, 11, "V", 1, "H", 11, "M", w - 11, 1, "H", w - 1, "V", 11, "M", w - 1, h - 11, "V",
                                h - 1, "H", w - 11, "M", 11, h - 1, "H", 1, "V", h - 11),
                             {"fill": "none", "stroke": self.C("accent"), "stroke-width": 2}))
        return o

    @staticmethod
    def txt_(x, y, fs, fill, s, a=None, w=None, ex=None):
        d = {"x": x, "y": y, "font-size": fs, "font-weight": w, "text-anchor": a, "fill": fill}
        if ex:
            d.update(ex)
        return _E("text", d, _esc(s))

    def txt(self, *a, **k):
        return self.txt_(*a, **k)

    def head(self, w):
        s = self.s
        return [self.txt(P, 29, 13, self.CT(), _trunc(s["title"], _fit(w - 32, 13)), None, 600)] if s["heading"] else []

    def base(self, w, h):
        return self.card(w, h) + self.head(w)

    def none(self, x, y, fs=None):
        return self.txt(x, y, fs or 13, self.CM(), NA, "middle")

    # -- Achsen --------------------------------------------------------------
    @staticmethod
    def ticks(lo, hi):
        raw = (hi - lo) / 4
        mag = 1
        while mag * 10 <= raw:
            mag *= 10
        while mag > raw:
            mag /= 10
        r = raw / mag
        st = mag * (1 if r <= 1 else 2 if r <= 2 else 5 if r <= 5 else 10)
        lo = math.floor(lo / st) * st
        hi = math.ceil(hi / st) * st
        return lo, hi, st, math.floor((hi - lo) / st + 0.5)

    def axes(self, w, h, lo, hi, o):
        s = self.s
        if hi - lo < 0.01:
            lo -= 1
            hi += 1
        lo, hi, st, nt = self.ticks(lo, hi)
        y0 = P + (26 if s["heading"] else 0) + 8
        y1 = h - P - (20 if len(s["labels"]) else 0)
        x1 = w - P
        tl = [_short(lo + i * st) for i in range(nt + 1)]
        x0 = P + (max([len(t) for t in tl] + [0]) * 6.6 + 8 if s["grid"] else 0)

        def Y(v):
            return y0 + (hi - v) * (y1 - y0) / (hi - lo)

        if s["grid"]:
            for i in range(nt + 1):
                y = Y(lo + i * st)
                o.append(_E("line", {"x1": x0, "y1": y, "x2": x1, "y2": y, **self.LN(),
                                     "stroke-dasharray": "2 4" if self.K else None}))
                o.append(self.txt(x0 - 8, y + 4, 11, self.CM(), tl[i], "end"))
        return x0, x1, y0, y1, Y

    # -- bar -----------------------------------------------------------------
    def r_bar(self, w, h):
        s = self.s
        d = s["data"]
        n = len(d)
        o = self.base(w, h)
        if not n:
            return o + [self.none(w / 2, h / 2 + 4)]
        x0, x1, _, y1, Y = self.axes(w, h, min(0, *d), max(0, *d), o)
        sl = (x1 - x0) / n
        bw = min(sl * 0.64, 64)
        c = self.C(s["color"])
        for i, v in enumerate(d):
            cx = x0 + i * sl + sl / 2
            yv, yz, lb = Y(v), Y(0), _short(v)
            o.append(self.RC(cx - bw / 2, min(yv, yz), bw, abs(yv - yz), min(self.T["radius"] / 3, bw / 2), {"fill": c}))
            if s["values"] and len(lb) * 6.6 <= sl:
                o.append(self.txt(cx, min(yv + 14, y1 - 4) if v < 0 else yv - 6, 11, self.CT(), lb, "middle"))
            if i < len(s["labels"]):
                o.append(self.txt(cx, h - P - 4, 11, self.CM(), _trunc(s["labels"][i], _fit(sl - 4, 11)), "middle"))
        return o

    # -- line ----------------------------------------------------------------
    def r_line(self, w, h):
        s = self.s
        S = s["data"]
        o = self.base(w, h)
        allv = [v for r in S for v in r]
        m = max([0] + [len(r) for r in S])
        if not m:
            return o + [self.none(w / 2, h / 2 + 4)]
        x0, x1, _, y1, Y = self.axes(w, h, min(allv), max(allv), o)

        def X(j):
            return (x0 + x1) / 2 if m == 1 else x0 + j * (x1 - x0) / (m - 1)

        ls = [_trunc(lb, 12) for lb in s["labels"][:m]]
        lw = max([len(t) for t in ls] + [0]) * 6.6 + 8
        ev = 1
        while m > 1 and ev * (x1 - x0) / (m - 1) < lw:
            ev += 1
        for j, lb in enumerate(ls):
            if j % ev == 0:
                o.append(self.txt(X(j), h - P - 4, 11, self.CM(), lb,
                                  "start" if m > 1 and j == 0 else "end" if m > 1 and j == m - 1 else "middle"))
        for i, r in enumerate(S):
            if not r:
                continue
            c = self.C(self.SC(i) if i else s["color"])
            p = "".join(_D("L" if j else "M", X(j), Y(v)) for j, v in enumerate(r))
            if s["area"] and len(S) == 1 and not self.K:
                o.append(self.PA(p + _D("L", X(len(r) - 1), y1, "L", X(0), y1, "Z"), {"fill": c, "fill-opacity": 0.16}))
            o.append(self.PA(p, {"fill": "none", "stroke": c, "stroke-width": 2, "stroke-linejoin": "round",
                                 "stroke-linecap": "round", "stroke-dasharray": "6 4" if self.K and i % 2 else None}))
            if s["dots"] and m <= 40:
                for j, v in enumerate(r):
                    o.append(self.CI(X(j), Y(v), 3, {"fill": self.C("card"), "stroke": c, "stroke-width": 2}))
        return o

    # -- donut ---------------------------------------------------------------
    def r_donut(self, w, h):
        s, K = self.s, self.K
        d = s["data"]
        n = len(d)
        o = self.base(w, h)
        tot = 0
        for v in d:  # links nach rechts (kein sum(): dessen Summation ist version-abhängig)
            tot = tot + v
        top = P + (26 if s["heading"] else 0)
        ah = h - top - P
        aw = (w - 2 * P) * 0.5 if s["legend"] else w - 2 * P
        r = min(ah, aw) / 2
        cx = P + aw / 2 if s["legend"] else w / 2
        cy = top + ah / 2
        ri = r * (1 - s["thickness"])

        def ring(q):
            return _D("M", cx, cy - q, "A", q, q, 0, 1, 1, cx, cy + q, "A", q, q, 0, 1, 1, cx, cy - q, "Z")

        def pt(q, a):
            return _D(cx + q * math.cos(a), cy + q * math.sin(a))

        def col(i):
            return ("bg" if i % 2 else "accent") if K else self.SC(i)

        if not (tot > 0):
            return o + [self.PA(ring(r) + ring(ri), {"fill": self.CM(), "fill-opacity": self.op(0.25),
                                                      "fill-rule": "evenodd"}),
                        self.none(cx, cy + 4, min(13, ri / 4.5))]
        cum = 0
        fs = min(max(math.floor(ri * 0.45), 10), 36)
        k = min(n, math.floor(ah / 20)) if s["legend"] else 0
        lx = P + aw + 16
        ly = cy - k * 10
        pi = math.pi
        for i, v in enumerate(d):
            if v <= 0:
                continue
            a0 = -pi / 2 + 2 * pi * cum / tot
            a1 = -pi / 2 + 2 * pi * (cum + v) / tot
            L = 1 if a1 - a0 > pi else 0
            full = v >= tot
            cum = cum + v
            dd = (ring(r) + ring(ri)) if full else (
                "M" + pt(r, a0) + _D("A", r, r, 0, L, 1) + " " + pt(r, a1) + "L" + pt(ri, a1)
                + _D("A", ri, ri, 0, L, 0) + " " + pt(ri, a0) + "Z")
            o.append(self.PA(dd, {"fill": self.C(col(i)), "fill-rule": "evenodd" if full else None,
                                  "stroke": self.C("accent" if K else "card"), "stroke-width": 1 if K else 2}))
        o.append(self.txt(cx, cy + fs * 0.35, fs, self.CT(), _trunc(s["center"] or _short(tot), _fit(ri * 2 - 8, fs)),
                          "middle", 700))
        for i in range(k):
            y = ly + i * 20
            o.append(self.RC(lx, y + 5, 10, 10, 2, {"fill": self.C(col(i)), "stroke": self.C("accent") if K else None}))
            o.append(self.txt(lx + 16, y + 14, 12, self.CT(),
                              _trunc(s["labels"][i] if i < len(s["labels"]) else "Teil " + str(i + 1),
                                     _fit(w - P - lx - 68, 12))))
            o.append(self.txt(w - P, y + 14, 12, self.CM(), fmt(math.floor(d[i] * 1000 / tot + 0.5) / 10) + "%", "end"))
        return o

    # -- pattern -------------------------------------------------------------
    def r_pattern(self, w, h):
        s, T = self.s, self.T
        z = s["size"]
        c = self.C(s["color"])
        id_ = s["idprefix"]
        o = self.card(w, h) if s["bg"] else []
        state = [s["seed"]]

        def rnd():
            state[0] = (state[0] * 1664525 + 1013904223) % 4294967296
            return state[0] / 4294967296

        if s["kind"] == "confetti":
            g = []
            for _ in range(s["density"]):
                x = rnd() * w
                y = rnd() * h
                k = math.floor(rnd() * 2)
                f = self.C(T["series"][math.floor(rnd() * len(T["series"]))])
                q = z * 0.3 + rnd() * z * 0.4
                tr = "rotate(" + _D(math.floor(rnd() * 360), x, y) + ")"
                g.append(self.RC(x - q / 2, y - q * 0.2, q, q * 0.4, 1, {"fill": f, "transform": tr}) if k
                         else self.CI(x, y, q / 2, {"fill": f}))
            return o + [_E("defs", {}, _E("clipPath", {"id": id_ + "-clip"}, self.RC(None, None, w, h, T["radius"]))),
                        _E("g", {"clip-path": "url(#" + id_ + "-clip)", "opacity": self.op(s["opacity"])}, "".join(g))]
        tw = z * 2 if s["kind"] == "waves" else z
        m = z / 2
        ln = {"fill": "none", "stroke": c, "stroke-width": s["stroke"]}
        kind = s["kind"]
        if kind == "grid":
            b = self.PA(_D("M", z, 0, "L", 0, 0, "L", 0, z), ln)
        elif kind == "dots":
            b = self.CI(m, m, s["stroke"], {"fill": c})
        elif kind == "stripes":
            b = self.RC(None, None, s["stroke"], z, None, {"fill": c})
        else:
            b = self.PA(_D("M", 0, m, "Q", m, 0, z, m, "T", tw, m), ln)
        return o + [_E("defs", {}, _E("pattern", {"id": id_ + "-pat", "width": tw, "height": z,
                                                  "patternUnits": "userSpaceOnUse",
                                                  "patternTransform": ("rotate(" + fmt(s["angle"]) + ")")
                                                  if kind == "stripes" else None}, b)),
                    self.RC(None, None, w, h, T["radius"], {"fill": "url(#" + id_ + "-pat)",
                                                             "opacity": self.op(s["opacity"])})]

    # -- badge ---------------------------------------------------------------
    def r_badge(self):
        s, T = self.s, self.T
        h = s["height"]
        fs = h * 0.46
        cw = fs * 0.6
        nl = len(s["label"])
        lw = nl * cw + h * 0.8 if nl else 0
        vw = len(s["value"]) * cw + h * 0.8
        w = lw + vw
        r = h / 2 if T["radius"] == 20 else min(T["radius"], h) / 2
        c = self.C(s["color"])
        y = h / 2 + fs * 0.35
        o = [self.fr(w, h, r, {"fill": self.C("elevated"), **self.LN()}), self.RC(lw, None, vw, h, r, {"fill": c})]
        if lw and r:
            o.append(self.RC(lw, None, min(r, vw), h, None, {"fill": c}))
        if nl:
            o.append(self.txt(lw / 2, y, fs, self.CT(), s["label"], "middle", 500))
        o.append(self.txt(lw + vw / 2, y, fs, self.ink(s["color"]), s["value"], "middle", 700))
        return (w, h, 0, o)

    # -- icon ----------------------------------------------------------------
    def r_icon(self):
        s = self.s
        o = []
        if s["bg"]:
            o.append(self.RC(-4, -4, 32, 32, self.T["radius"] / 2,
                             {"fill": self.C("card"), **self.LN(), "stroke-width": 0.5}))
        o.append(self.PA(ICONS[s["name"]], {"fill": "none", "stroke": self.C(s["color"]), "stroke-width": s["stroke"],
                                            "stroke-linejoin": "round", "stroke-linecap": "round"}))
        return (s["size"], s["size"], "-4 -4 32 32" if s["bg"] else "0 0 24 24", o)

    # -- monogram ------------------------------------------------------------
    def r_monogram(self):
        s, K = self.s, self.K
        z = s["size"]
        c = z / 2
        q = c - 1
        k = 0.8660254037844386
        n = len(s["text"])
        fs = z * (0.5 if n < 2 else 0.42 if n < 3 else 0.34)
        id_ = s["idprefix"] + "-grad"
        fill = self.C("accent") if K else "url(#" + id_ + ")"
        o = []
        if not K:
            o.append(_E("defs", {}, _E("linearGradient", {"id": id_, "x1": 0, "y1": 0, "x2": 1, "y2": 1},
                                       _E("stop", {"offset": 0, "stop-color": self.C(s["color"])})
                                       + _E("stop", {"offset": 1, "stop-color": self.C(s["color2"])}))))
        if s["shape"] == "circle":
            o.append(self.CI(c, c, q, {"fill": fill}))
        elif s["shape"] == "square":
            o.append(self.RC(1, 1, z - 2, z - 2, z * 0.22 if self.T["radius"] else 0, {"fill": fill}))
        else:
            o.append(self.PA(_D("M", c, 1, "L", c + q * k, c - q / 2, "L", c + q * k, c + q / 2, "L", c, z - 1,
                                "L", c - q * k, c + q / 2, "L", c - q * k, c - q / 2, "Z"), {"fill": fill}))
        if n:
            o.append(self.txt(c, c + fs * 0.35, fs, self.ink(s["color"]), s["text"], "middle", 700))
        return (z, z, 0, o)

    # -- gradient ------------------------------------------------------------
    def r_gradient(self, w, h):
        s, K = self.s, self.K
        id_ = s["idprefix"] + "-grad"
        a = s["angle"] * math.pi / 180
        cs = math.cos(a) * 50
        sn = math.sin(a) * 50
        st = []

        def stop(offset, t):
            st.append(_E("stop", {"offset": offset, "stop-color": self.C(t)}))

        def pc(v):
            return fmt(v) + "%"

        if K:
            stop(0, "accent")
            stop(0.5, "accent")
            stop(0.5, "bg")
            stop(1, "bg")
        else:
            stop(0, s["from"])
            if s["via"]:
                stop(0.5, s["via"])
            stop(1, s["to"])
        sts = "".join(st)
        if s["kind"] == "linear":
            grad = _E("linearGradient", {"id": id_, "x1": pc(50 - cs), "y1": pc(50 - sn), "x2": pc(50 + cs),
                                         "y2": pc(50 + sn)}, sts)
        else:
            grad = _E("radialGradient", {"id": id_, "cx": pc(s["cx"]), "cy": pc(s["cy"]), "r": "75%"}, sts)
        o = [_E("defs", {}, grad),
             self.fr(w, h, self.T["radius"], {"fill": "url(#" + id_ + ")", "stroke": self.C("accent") if K else None})]
        tx = _trunc(s["text"], _fit(w - 48, 12))
        if tx:
            fs = min(max(h * 0.16, 12), 72, (w - 48) / (len(tx) * 0.6))
            o.append(self.txt(w / 2, h / 2 + fs * 0.35, fs, self.C("accent") if K else self.ink(s["from"]), tx, "middle",
                              700, {"stroke": self.C("bg"), "stroke-width": 3, "paint-order": "stroke"} if K else None))
        return o

    # -- graph ---------------------------------------------------------------
    def r_graph(self):
        s, T = self.s, self.T
        N = s["nodes"]
        nn = len(N)
        nw, nh, gx, gy = 136, 48, 56, 20
        top = P + (28 if s["heading"] else 0)
        id_ = s["idprefix"] + "-arrow"
        first = {}
        for i, n in enumerate(N):
            first.setdefault(n["id"], i)

        def ix(k):
            return first.get(k, -1)

        pos = []
        if not nn:
            cw, ch = 320, top + 80
        elif all(n.get("x") is not None for n in N):
            pos = [[P + n["x"], top + n["y"]] for n in N]
            cw = P + max(n["x"] for n in N) + nw + P
            ch = top + max(n["y"] for n in N) + nh + P
        else:
            rk = [0] * nn
            go, it = 1, 0
            while go:
                cond = it < nn
                it += 1
                if not cond:
                    break
                go = 0
                for e in s["edges"]:
                    a, b = ix(e["from"]), ix(e["to"])
                    if a != b and rk[b] < rk[a] + 1 and rk[a] + 1 < nn:
                        rk[b] = rk[a] + 1
                        go = 1
            cnt = [0] * (max(rk) + 1)
            idx = []
            for r in rk:
                idx.append(cnt[r])
                cnt[r] += 1
            mc = max(cnt)
            hh = mc * nh + (mc - 1) * gy
            pos = [[P + r * (nw + gx), top + (hh - (cnt[r] * nh + (cnt[r] - 1) * gy)) / 2 + idx[i] * (nh + gy)]
                   for i, r in enumerate(rk)]
            cw = 2 * P + len(cnt) * nw + (len(cnt) - 1) * gx
            ch = top + hh + P
        if any(e["from"] != e["to"] and pos[ix(e["to"])][0] <= pos[ix(e["from"])][0] + nw for e in s["edges"]):
            ch += 28
        o = self.card(cw, ch) + self.head(cw)
        if not nn:
            o.append(self.none(160, top + 36))
        else:
            o.append(_E("defs", {}, _E("marker", {"id": id_, "viewBox": "0 0 10 10", "refX": 9, "refY": 5,
                                                  "markerWidth": 7, "markerHeight": 7, "orient": "auto-start-reverse"},
                                       self.PA("M0 0L10 5L0 10Z", {"fill": self.CM()}))))
            for e in s["edges"]:
                ax, ay = pos[ix(e["from"])]
                bx, by = pos[ix(e["to"])]
                bk = bx <= ax + nw
                sx = ax + nw / 2 if bk else ax + nw
                sy = ay + (nh if bk else nh / 2)
                tx = bx + nw / 2 if bk else bx
                ty = by + (nh if bk else nh / 2)
                dx = 0 if bk else max(24, (tx - sx) / 2)
                dy = 40 if bk else 0
                if e["from"] != e["to"]:
                    o.append(self.PA(_D("M", sx, sy, "C", sx + dx, sy + dy, tx - dx, ty + dy, tx, ty),
                                     {"fill": "none", "stroke": self.CM(), "stroke-width": 1.5,
                                      "stroke-dasharray": "4 4" if bk else None, "marker-end": "url(#" + id_ + ")"}))
            for i, n in enumerate(N):
                x, y = pos[i]
                c = self.C(self.SC(sum(ord(ch_) for ch_ in n.get("type", ""))))
                o.append(self.RC(x, y, nw, nh, min(T["radius"], 10), {"fill": self.C("elevated"), "stroke": c,
                                                                      "stroke-opacity": self.op(0.45)}))
                o.append(self.RC(x + 6, y + 10, 3, nh - 20, 1.5, {"fill": c}))
                o.append(self.txt(x + 16, y + 21, 12, self.CT(), _trunc(n.get("label") or n["id"], _fit(nw - 22, 12)),
                                  None, 600))
                if n.get("type"):
                    o.append(self.txt(x + 16, y + 36, 10, self.CM(), _trunc(n["type"], _fit(nw - 22, 10))))
        return (s["width"] or cw, s["height"] or ch, _D(0, 0, cw, ch), o)

    def render(self):
        s = self.s
        t = s["type"]
        if t == "bar":
            r = self.r_bar(s["width"], s["height"])
        elif t == "line":
            r = self.r_line(s["width"], s["height"])
        elif t == "donut":
            r = self.r_donut(s["width"], s["height"])
        elif t == "pattern":
            r = self.r_pattern(s["width"], s["height"])
        elif t == "gradient":
            r = self.r_gradient(s["width"], s["height"])
        elif t == "badge":
            r = self.r_badge()
        elif t == "icon":
            r = self.r_icon()
        elif t == "monogram":
            r = self.r_monogram()
        else:
            r = self.r_graph()
        if isinstance(r, tuple):
            w, h, vb, body = r
        else:
            w, h, vb, body = s["width"], s["height"], 0, r
        ti = _esc(s["title"])
        font = "var(--font-main, " + self.T["font"] + ")" if self.V else self.T["font"]
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="' + fmt(w) + '" height="' + fmt(h) + '" viewBox="'
                + (vb or _D(0, 0, w, h)) + '" role="img" aria-label="' + ti + '" font-family="' + font + '">\n<title>'
                + ti + "</title>\n" + "\n".join(body) + "\n</svg>")


def generate(spec) -> dict:
    """spec (Python-Objekt aus JSON) -> {"ok":True,"svg","spec"} oder {"ok":False,"error","field"}."""
    try:
        s = _norm(spec)
    except _Bad as e:
        return {"ok": False, "error": e.error, "field": e.field}
    return {"ok": True, "spec": s, "svg": _Gen(s).render()}


# --------------------------------------------------------------------------- #
# Sanitizer (Allowlist, SPEC §11) — Zeichenketten-Tokenizer, ohne externe Pakete #
# --------------------------------------------------------------------------- #
_NS = "http://www.w3.org/2000/svg"
_MAX, _MAXN, _MAXD = 1_000_000, 5000, 64
_EL = frozenset("svg g defs title desc path rect circle ellipse line polyline polygon text tspan textPath linearGradient "
                "radialGradient stop pattern clipPath mask marker symbol use".split())
_TXT = frozenset("text tspan textPath title desc".split())
_AT = frozenset(
    "id class style transform opacity role lang xml:space aria-label aria-hidden aria-labelledby aria-describedby tabindex "
    "focusable x y x1 y1 x2 y2 cx cy r rx ry fx fy dx dy width height d points viewBox preserveAspectRatio version xmlns "
    "fill fill-opacity fill-rule clip-rule stroke stroke-width stroke-opacity stroke-linecap stroke-linejoin "
    "stroke-miterlimit stroke-dasharray stroke-dashoffset vector-effect paint-order font-family font-size font-weight "
    "font-style text-anchor dominant-baseline letter-spacing text-decoration offset stop-color stop-opacity gradientUnits "
    "gradientTransform spreadMethod patternUnits patternContentUnits patternTransform clipPathUnits maskUnits "
    "maskContentUnits clip-path mask marker-start marker-mid marker-end markerWidth markerHeight markerUnits refX refY "
    "orient startOffset href xlink:href".split())
_ENT = {"amp": "&", "lt": "<", "gt": ">", "quot": '"', "apos": "'"}
_BADC = re.compile("[\x00-\x08\x0b\x0c\x0e-\x1f\x7f￾￿\ud800-\udfff]")
_ENT_RE = re.compile(r"&(#x[0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]*);")
_HREF_RE = re.compile(r"#[A-Za-z0-9_.:-]+")
_URL_GOOD = re.compile(r"url\((['\"]?)#[a-z0-9_.:-]+\1\)")
# Unsichtbares/Leerraum, das zur Verschleierung dient — nur für die ERKENNUNG entfernt (strenger, nie lockerer)
_INVIS = re.compile("[\\s\x00-\x1f\x7f-\x9f­͏؜ᅟᅠ᠎​-‏ -‮⁠-⁯"
                    "ㅤ︀-️﻿ﾠ]")
_BAD_VAL = re.compile(r"(?:java|vb|live)script:|data:|expression\(|@import|behavior:|-moz-binding|&#|\\|"
                      r"image-set\(|image\(|src\(|cross-fade\(|element\(|paint\(|@namespace|@font-face")
_SELF = re.compile(r"<(?:script|foreignObject|iframe|object|embed|style|image|a|animate|set)[\s>/]|javascript:", re.I)
_SELF_ON = re.compile(r"\son[a-z]+\s*=", re.I)
_NAME_RE = re.compile(r"(/?)([^\s/>\"'=<]+)")
_SPEC_RE = re.compile(r"[>\"']")
_ATT_RE = re.compile(r"([^\s=/\"'<>]+)(?:\s*=\s*(?:\"([^\"]*)\"|'([^']*)'|([^\s\"'>]+)))?")
_RAW_SKIP = {"script", "style", "foreignobject"}


def _dec(s, rm):
    """Entities genau einmal auflösen; unbekannte benannte Entities (z. B. aus einer DOCTYPE) werden leer + gemeldet."""
    def one(m):
        e = m.group(1)
        if e[0] == "#":
            digits = e[2:] if e[1] in "xX" else e[1:]
            digits = digits.lstrip("0")
            c = int(digits, 16 if e[1] in "xX" else 10) if 0 < len(digits) <= 8 else 0
            if 0 < c < 0x110000:
                return chr(c)
            rm("entity:" + e)
            return ""
        if e in _ENT:
            return _ENT[e]
        rm("entity:" + e)
        return ""
    return _ENT_RE.sub(one, s)


def _ok_val(n, v, rm):
    """Wertprüfung je Attribut; None = Attribut verwerfen."""
    if _BADC.search(v):
        rm("attr:" + n + " (Steuerzeichen)")
        return None
    if n in ("href", "xlink:href"):
        if _HREF_RE.fullmatch(v.strip()):
            return v.strip()
        rm("attr:" + n + " (nicht lokal)")
        return None
    k = _INVIS.sub("", unicodedata.normalize("NFKC", v)).lower()
    if n != "aria-label" and _BAD_VAL.search(k):
        rm("attr:" + n + " (Schema)")
        return None
    if n == "style" and ("<" in k or ">" in k):
        rm("attr:style")
        return None
    # url(…) nur als url(#id) und stets geschlossen: CSS beendet eine offene url( am Wertende gültig
    # ("fill:url(http://x" lädt extern), deshalb zählen wir alle "url(" gegen die wohlgeformten Treffer.
    if k.count("url(") != len(_URL_GOOD.findall(k)):
        rm("attr:" + n + " (url)")
        return None
    return v


def _run(svg):
    removed, out = [], []

    def rm(m):
        if m not in removed:
            removed.append(m)

    st = {"n": 0, "root": False, "closed": False, "skip": 0, "work": 0}
    stack: list = []

    def open_(name, attrs, self_close):
        if st["closed"] or (not st["root"] and name != "svg"):
            rm("element:" + name)
            return False
        if name not in _EL:
            rm("element:" + name)
            return False
        st["n"] += 1
        if st["n"] > _MAXN or len(stack) >= _MAXD:
            rm("limit")
            return False
        s = "<" + name
        seen = set()
        if name == "svg" and not st["root"]:
            s += ' xmlns="' + _NS + '"'
        for k, v in attrs:
            if k == "xmlns":
                if v != _NS:
                    rm("attr:xmlns")
                continue
            if k == "xmlns:xlink":
                continue
            if k[:6] == "xmlns:" or k[:2].lower() == "on":
                rm("attr:" + k)
                continue
            if k not in _AT:
                rm("attr:" + k)
                continue
            v = _ok_val(k, v, rm)
            if v is None:
                continue
            o = "href" if k == "xlink:href" else k
            if o in seen:
                continue
            seen.add(o)
            s += " " + o + '="' + _esc(v) + '"'
        if name == "use" and "href" not in seen:
            rm("element:use (ohne lokalen Verweis)")
            return False
        st["root"] = True
        out.append(s + ("/>" if self_close else ">"))
        if not self_close:
            stack.append(name)
        elif not stack:
            st["closed"] = True
        return True

    def close():
        if stack:
            out.append("</" + stack.pop() + ">")
            if not stack:
                st["closed"] = True

    def text(t):
        if stack and stack[-1] in _TXT and t:
            if _BADC.search(t):
                t = _BADC.sub("", t)
            out.append(_esc(t))

    if len(svg) > _MAX:
        return {"svg": "", "removed": ["zu groß (>1 MB)"]}
    i, L = 0, len(svg)
    budget = 8 * L + 100_000
    while i < L:
        lt = svg.find("<", i)
        if lt < 0:
            if not st["skip"]:
                text(_dec(svg[i:], rm))
            break
        if lt > i and not st["skip"]:
            text(_dec(svg[i:lt], rm))
        i = lt
        if svg.startswith("<!--", i):
            e = svg.find("-->", i + 4)
            rm("comment")
            i = L if e < 0 else e + 3
            continue
        if svg.startswith("<![CDATA[", i):
            e = svg.find("]]>", i)
            if not st["skip"]:
                text(svg[i + 9: L if e < 0 else e])
            i = L if e < 0 else e + 3
            continue
        if svg.startswith("<?", i):
            e = svg.find("?>", i)
            rm("processing-instruction")
            i = L if e < 0 else e + 2
            continue
        if svg.startswith("<!", i):
            rm("doctype")
            d, e = 0, i
            while e < L:
                m = re.compile(r"[\[\]>]").search(svg, e)
                if not m:
                    e = L
                    break
                e = m.start()
                c = svg[e]
                if c == "[":
                    d += 1
                elif c == "]":
                    d -= 1
                elif d <= 0:
                    break
                e += 1
            i = e + 1
            continue
        # <tag …> mit Anführungszeichen-bewusster Suche nach dem Ende; Rechenbudget gegen quadratische Eingaben
        nm_m = _NAME_RE.match(svg, i + 1)
        end = -1
        if nm_m:
            j = nm_m.end()
            while True:
                m = _SPEC_RE.search(svg, j)
                if not m:
                    break
                if m.group() == ">":
                    end = m.start()
                    break
                q = svg.find(m.group(), m.end())
                if q < 0:
                    break
                j = q + 1
            st["work"] += (end if end >= 0 else L) - i
            if st["work"] > budget:
                rm("zu aufwändig")
                return {"svg": "", "removed": removed}
        if end < 0:
            rm("markup (defekt)")
            i += 1
            continue
        cl, nm = nm_m.group(1), nm_m.group(2)
        rest = svg[nm_m.end(): end]
        i = end + 1
        self_close = not cl and bool(re.search(r"/\s*$", rest))
        if cl:
            if st["skip"]:
                st["skip"] -= 1
                continue
            if nm in stack:
                k = len(stack) - 1 - stack[::-1].index(nm)
                while len(stack) > k:
                    close()
            continue
        if st["skip"]:
            if not self_close:
                st["skip"] += 1
            continue
        if self_close:
            rest = re.sub(r"/\s*$", "", rest)
        at = []
        for a in _ATT_RE.finditer(rest):
            v = a.group(2) if a.group(2) is not None else a.group(3) if a.group(3) is not None \
                else a.group(4) if a.group(4) is not None else ""
            at.append((a.group(1), _dec(v, rm)))
        if not open_(nm, at, self_close) and not self_close:
            if nm.lower() in _RAW_SKIP:  # Inhalt (Code, CSS) bis zum passenden Ende überspringen
                t = re.compile(r"</" + re.escape(nm) + r"\s*>", re.I).search(svg, i)
                i = t.end() if t else L
            else:
                st["skip"] = 1
    while stack:  # nie ein offenes Element zurücklassen
        close()
    if not st["root"]:
        return {"svg": "", "removed": removed + ["kein svg-Element"]}
    return {"svg": "".join(out), "removed": removed}


def sanitize(svg_text) -> dict:
    """Importiertes/eingefügtes SVG -> {"svg": bereinigt (aus erlaubten Tokens neu geschrieben), "removed": [...]}."""
    if isinstance(svg_text, (bytes, bytearray)):
        svg_text = bytes(svg_text).decode("utf-8", "replace")
    if not isinstance(svg_text, str):
        svg_text = "" if svg_text is None else str(svg_text)
    r = _run(svg_text)
    # Selbstprüfung (fail-closed): Textinhalte und aria-label ausgenommen
    probe = re.sub(r'aria-label="[^"]*"', "", re.sub(r">[^<]*<", "><", r["svg"]))
    if _SELF.search(probe) or _SELF_ON.search(re.sub(r'="[^"]*"', '=""', probe)):
        return {"svg": "", "removed": r["removed"] + ["selbstprüfung"]}
    return r


# --------------------------------------------------------------------------- #
# CLI                                                                          #
# --------------------------------------------------------------------------- #
def _nope(c):
    raise ValueError("ungültiges JSON: " + c)


def main(argv) -> int:
    try:
        if len(argv) < 2:
            raise ValueError("Aufruf: svgkit.py <spec.json>")
        with open(argv[1], "rb") as fh:
            raw = fh.read().decode("utf-8", "replace")
        r = generate(json.loads(raw, parse_int=float, parse_constant=_nope))
    except Exception as e:  # noqa: BLE001 — Datei fehlt, JSON kaputt, zu tief verschachtelt …
        r = {"ok": False, "field": "spec", "error": " ".join(str(e).split()) or type(e).__name__}
    if r["ok"]:
        sys.stdout.buffer.write(r["svg"].encode("utf-8"))
        sys.stdout.buffer.flush()
        return 0
    sys.stderr.write("svgspec: " + r["field"] + ": " + r["error"] + "\n")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
