"""store — DuckDB-Speicher des Design-Assistenten.

Pfad: Umgebungsvariable `DKZ_DB`, Standard `data/dkz.duckdb` (Ordner wird angelegt).
Tabellen: `kb_files`, `kb_docs`, `kb_terms (term, doc_id, tf)`, `chat_log`, `svg_library`.

Regeln:
* Nur parametrisierte SQL-Abfragen — kein Nutzertext wird je in SQL eingesetzt.
* Thread-sicher: eine Verbindung, jeder Zugriff unter einer Sperre (der Server ist ein
  ThreadingHTTPServer).
* `kb_load(dir)` ist idempotent: Dateien mit unverändertem SHA-256 werden übersprungen.
* `kb_search(q, limit)`: BM25 (k1=1.2, b=0.75) als SQL über `kb_terms`; dieselbe
  Tokenisierung für Dokumente und Anfrage (`tokenize`).
* Ohne `duckdb` (oder wenn die Datei nicht zu öffnen ist): `kb_search` rechnet dieselbe
  BM25 rein in Python im Speicher (`backend: "memory"`); alle anderen Funktionen liefern
  `{"ok": False, "error": "— ausstehend —", "needs": "duckdb"}`.

Alle öffentlichen Funktionen liefern ein dict mit `ok` und werfen keine Ausnahmen für
erwartbare Fehler.
"""
from __future__ import annotations

import hashlib
import json
import logging
import math
import os
import re
import threading
import unicodedata
from datetime import datetime, timezone
from pathlib import Path

try:
    import duckdb  # type: ignore
    AVAILABLE = True
except ImportError:          # reines python3 ohne .venv
    duckdb = None
    AVAILABLE = False

log = logging.getLogger("store")

ROOT = Path(__file__).resolve().parent
PENDING = "— ausstehend —"
K1, B = 1.2, 0.75
REQUIRED = ("id", "topic", "title", "text", "tags", "source")
MAX_NAME, MAX_SVG, MAX_CONTENT = 200, 512 * 1024, 20000

_lock = threading.RLock()
_con = None
_con_path: str | None = None
_con_error: str | None = None


def _pending() -> dict:
    return {"ok": False, "error": PENDING, "needs": "duckdb",
            **({"detail": _con_error} if _con_error else {})}


def db_path() -> str:
    return os.environ.get("DKZ_DB") or str(ROOT / "data" / "dkz.duckdb")


# --------------------------------------------------------------------------- #
# Tokenisierung (deterministisch, Dokumente und Anfrage gleich)                #
# --------------------------------------------------------------------------- #
_STOP_RAW = """
der die das den dem des ein eine einer eines einem einen und oder aber auch als am an auf aus
bei bin bis bist da damit dann dass daß dein deine dem denn derer dessen dich dir doch dort du
durch er es etwa euch euer fuer für hab habe haben hat hatte ich ihr ihre ihm ihn im in ist ja
jede jeder jedes kann kannst koennen können man mein meine mich mir mit muss müssen nach nicht
nichts noch nun nur ob ohne sehr sein seine sich sie sind so soll sollte sollten sondern um uns
unser unter viel vom von vor war waren was weil welche welcher welches wenn wer werden wie wir
wird wo zu zum zur zwischen über ueber ganz gibt geht gut mehr mal schon wieso warum weshalb
wann wieviel wieviele viele vielen bitte etwas ihnen sowie beim deren denen diese dieser dieses diesem
diesen jetzt hier immer gute guter gutes guten beste besten bester richtig richtige richtigen
mache machen macht sollen sollst brauche brauchen braucht ich mich meinem meinen einem
a an and are as at be been but by can could did do does for from had has have how i if in into
is it its me my no not of on or our should so than that the their them then there these they
this to was we were what when where which who why will with would you your yours about please
tell explain much many any some get use using make makes making do does done need needs
want good better best way ways right should shall may might
"""


def _norm(text: str) -> str:
    t = text.lower()
    for a, b in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("ß", "ss")):
        t = t.replace(a, b)
    t = unicodedata.normalize("NFKD", t)
    return "".join(c for c in t if not unicodedata.combining(c))


STOPWORDS = frozenset(_norm(w) for w in _STOP_RAW.split())
_TOKEN = re.compile(r"\d+(?:\.\d+)+|[a-z0-9]+")
_SUFFIXES = ("ationen", "ations", "ungen", "heiten", "keiten", "ischen", "lichen", "ation",
             "ierung", "ings", "ung", "heit", "keit", "isch", "lich", "ing", "ern", "en",
             "er", "es", "ed", "ly", "e", "s", "n")


_REWRITE = (("ibility", "ibl"), ("ability", "abl"), ("ible", "ibl"), ("able", "abl"))


def stem(tok: str) -> str:
    """Einfache Suffix-Stammbildung (ein Durchgang, längstes Suffix, Stamm ≥ 4).
    Vorab: accessibility/accessible → accessibl, usability/usable → usabl."""
    if tok.isdigit() or "." in tok:
        return tok
    for suf, rep in _REWRITE:
        if tok.endswith(suf) and len(tok) - len(suf) >= 2:
            return tok[: -len(suf)] + rep
    for suf in _SUFFIXES:
        if tok.endswith(suf) and len(tok) - len(suf) >= 4:
            return tok[: -len(suf)]
    return tok


def tokenize(text: str) -> list[str]:
    """Kleinschreibung, Umlaute → ae/oe/ue/ss, Stoppwörter raus, Stammbildung."""
    if not isinstance(text, str):
        return []
    out = []
    for tok in _TOKEN.findall(_norm(text)):
        if len(tok) < 2 or tok in STOPWORDS:
            continue
        out.append(stem(tok))
    return out


def _doc_tokens(doc: dict) -> list[str]:
    """Titel und Tags zählen doppelt (einfache Feldgewichtung), dann Thema und Text."""
    title = tokenize(doc["title"])
    tags = tokenize(" ".join(doc["tags"]))
    return title * 2 + tags * 2 + tokenize(doc["topic"]) + tokenize(doc["text"])


def _tf(tokens: list[str]) -> dict[str, int]:
    tf: dict[str, int] = {}
    for t in tokens:
        tf[t] = tf.get(t, 0) + 1
    return tf


# --------------------------------------------------------------------------- #
# Wissensbasis-Dateien lesen und prüfen                                        #
# --------------------------------------------------------------------------- #
def validate_entry(obj) -> str | None:
    """Fehlermeldung oder None, wenn der Eintrag dem Schema entspricht."""
    if not isinstance(obj, dict):
        return "kein JSON-Objekt"
    for k in REQUIRED:
        if k not in obj:
            return f"Feld '{k}' fehlt"
    for k in ("id", "topic", "title", "text", "source"):
        if not isinstance(obj[k], str) or not obj[k].strip():
            return f"Feld '{k}' muss ein nicht leerer Text sein"
    if not isinstance(obj["tags"], list) or not all(isinstance(t, str) for t in obj["tags"]):
        return "Feld 'tags' muss eine Liste von Texten sein"
    if not re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,80}", obj["id"]):
        return "Feld 'id' hat ungültige Zeichen"
    return None


def _read_kb_file(path: Path) -> tuple[list[dict], list[str]]:
    docs, errors = [], []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as exc:
        return [], [f"{path.name}: nicht lesbar ({exc})"]
    for n, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as exc:
            errors.append(f"{path.name}:{n}: kein gültiges JSON ({exc.msg})")
            continue
        err = validate_entry(obj)
        if err:
            errors.append(f"{path.name}:{n}: {err}")
            continue
        docs.append({k: obj[k] for k in REQUIRED})
    return docs, errors


def _kb_files(directory) -> list[Path]:
    d = Path(directory)
    return sorted(p for p in d.glob("*.jsonl") if p.is_file()) if d.is_dir() else []


# Ändert sich die Tokenisierung oder die Feldgewichtung, ändert sich diese Signatur —
# dann gelten alle Dateien als verändert und der Index wird neu aufgebaut.
_INDEX_SIG = hashlib.sha256(repr((_STOP_RAW, _SUFFIXES, _REWRITE, _TOKEN.pattern,
                                  "fields:title2,tags2,topic1,text1")).encode()).hexdigest()


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes() + _INDEX_SIG.encode()).hexdigest()


# --------------------------------------------------------------------------- #
# Speicher-Fallback: dieselbe BM25 in reinem Python                            #
# --------------------------------------------------------------------------- #
class _MemoryIndex:
    def __init__(self) -> None:
        self.files: dict[str, str] = {}          # name -> sha
        self.docs: dict[str, dict] = {}          # id -> doc (+ file, len)
        self.terms: dict[str, dict[str, int]] = {}   # term -> {doc_id: tf}

    def remove_file(self, name: str) -> None:
        gone = [i for i, d in self.docs.items() if d["file"] == name]
        for i in gone:
            del self.docs[i]
        for term in list(self.terms):
            posting = self.terms[term]
            for i in gone:
                posting.pop(i, None)
            if not posting:
                del self.terms[term]
        self.files.pop(name, None)

    def add_doc(self, doc: dict, name: str) -> None:
        toks = _doc_tokens(doc)
        self.docs[doc["id"]] = {**doc, "file": name, "len": len(toks)}
        for term, tf in _tf(toks).items():
            self.terms.setdefault(term, {})[doc["id"]] = tf

    def search(self, qterms: list[str], limit: int) -> list[dict]:
        n = len(self.docs)
        if not n or not qterms:
            return []
        avgdl = sum(d["len"] for d in self.docs.values()) / n
        scores: dict[str, float] = {}
        matched: dict[str, int] = {}
        for term in qterms:
            posting = self.terms.get(term)
            if not posting:
                continue
            df = len(posting)
            idf = math.log(1 + (n - df + 0.5) / (df + 0.5))
            for doc_id, tf in posting.items():
                dl = self.docs[doc_id]["len"]
                scores[doc_id] = scores.get(doc_id, 0.0) + idf * (tf * (K1 + 1)) / (
                    tf + K1 * (1 - B + B * dl / avgdl))
                matched[doc_id] = matched.get(doc_id, 0) + 1
        ranked = sorted(scores, key=lambda i: (-scores[i], i))[:limit]
        return [{**{k: self.docs[i][k] for k in ("id", "title", "topic", "text", "source")},
                 "tags": self.docs[i]["tags"], "score": scores[i], "matched": matched[i]}
                for i in ranked]


_mem = _MemoryIndex()


# --------------------------------------------------------------------------- #
# DuckDB-Verbindung                                                            #
# --------------------------------------------------------------------------- #
_SCHEMA = [
    "CREATE SEQUENCE IF NOT EXISTS chat_seq START 1",
    "CREATE SEQUENCE IF NOT EXISTS svg_seq START 1",
    """CREATE TABLE IF NOT EXISTS kb_files (
         name VARCHAR PRIMARY KEY, sha256 VARCHAR NOT NULL, docs INTEGER,
         loaded_at TIMESTAMP)""",
    """CREATE TABLE IF NOT EXISTS kb_docs (
         id VARCHAR PRIMARY KEY, topic VARCHAR, title VARCHAR, text VARCHAR,
         tags VARCHAR, source VARCHAR, file VARCHAR, len INTEGER)""",
    "CREATE TABLE IF NOT EXISTS kb_terms (term VARCHAR, doc_id VARCHAR, tf INTEGER)",
    """CREATE TABLE IF NOT EXISTS chat_log (
         id BIGINT PRIMARY KEY DEFAULT nextval('chat_seq'), ts TIMESTAMP,
         role VARCHAR, content VARCHAR, meta VARCHAR)""",
    """CREATE TABLE IF NOT EXISTS svg_library (
         id BIGINT PRIMARY KEY DEFAULT nextval('svg_seq'), name VARCHAR, created TIMESTAMP,
         type VARCHAR, spec VARCHAR, svg VARCHAR, bytes INTEGER)""",
]


def _conn():
    """Offene Verbindung oder None (dann ist DuckDB nicht nutzbar). Unter _lock aufrufen."""
    global _con, _con_path, _con_error
    if not AVAILABLE:
        return None
    path = db_path()
    if _con is not None and _con_path == path:
        return _con
    if _con is not None:                       # DKZ_DB hat gewechselt (Tests)
        try:
            _con.close()
        except Exception:  # noqa: BLE001
            pass
        _con = None
    try:
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        con = duckdb.connect(path)
        for stmt in _SCHEMA:
            con.execute(stmt)
    except Exception as exc:  # noqa: BLE001 — z. B. Datei von anderem Prozess gesperrt
        _con_error = f"{type(exc).__name__}: {exc}"[:300]
        log.warning("DuckDB nicht nutzbar (%s) — Wissensbasis im Speicher", _con_error)
        return None
    _con, _con_path, _con_error = con, path, None
    return _con


def backend() -> str:
    with _lock:
        return "duckdb" if _conn() is not None else "memory"


def status() -> dict:
    with _lock:
        con = _conn()
        if con is None:
            return {"ok": True, "store": {"backend": PENDING, "path": db_path(),
                                          "needs": "duckdb",
                                          **({"detail": _con_error} if _con_error else {})},
                    "kb": {"docs": len(_mem.docs), "backend": "memory",
                           "files": len(_mem.files)}}
        docs = con.execute("SELECT COUNT(*) FROM kb_docs").fetchone()[0]
        files = con.execute("SELECT COUNT(*) FROM kb_files").fetchone()[0]
        chats = con.execute("SELECT COUNT(*) FROM chat_log").fetchone()[0]
        svgs = con.execute("SELECT COUNT(*) FROM svg_library").fetchone()[0]
        return {"ok": True, "store": {"backend": "duckdb", "path": db_path(),
                                      "chat_messages": chats, "svg_items": svgs},
                "kb": {"docs": docs, "backend": "duckdb", "files": files}}


def _now() -> datetime:
    return datetime.now(timezone.utc).replace(tzinfo=None)


def _iso(ts) -> str | None:
    return ts.isoformat(timespec="seconds") + "Z" if isinstance(ts, datetime) else ts


# --------------------------------------------------------------------------- #
# Wissensbasis                                                                 #
# --------------------------------------------------------------------------- #
def kb_load(directory=None) -> dict:
    """Lädt kb/*.jsonl. Unveränderte Dateien (gleicher SHA-256) werden übersprungen,
    entfernte Dateien aus dem Index genommen. Ungültige Zeilen landen in `errors`."""
    directory = Path(directory) if directory else ROOT / "kb"
    files = _kb_files(directory)
    loaded, skipped, errors = [], [], []
    with _lock:
        con = _conn()
        if con is None:
            names = {p.name for p in files}
            for name in [n for n in _mem.files if n not in names]:
                _mem.remove_file(name)
            for p in files:
                sha = _sha(p)
                if _mem.files.get(p.name) == sha:
                    skipped.append(p.name)
                    continue
                docs, errs = _read_kb_file(p)
                errors += errs
                _mem.remove_file(p.name)
                for d in docs:
                    if d["id"] in _mem.docs:
                        errors.append(f"{p.name}: doppelte id '{d['id']}' übersprungen")
                        continue
                    _mem.add_doc(d, p.name)
                _mem.files[p.name] = sha
                loaded.append(p.name)
            return {"ok": True, "backend": "memory", "loaded": loaded, "skipped": skipped,
                    "errors": errors, "docs": len(_mem.docs)}

        known = dict(con.execute("SELECT name, sha256 FROM kb_files").fetchall())
        names = {p.name for p in files}
        con.execute("BEGIN TRANSACTION")
        try:
            for name in [n for n in known if n not in names]:
                _drop_file(con, name)
            for p in files:
                sha = _sha(p)
                if known.get(p.name) == sha:
                    skipped.append(p.name)
                    continue
                docs, errs = _read_kb_file(p)
                errors += errs
                _drop_file(con, p.name)
                count = 0
                for d in docs:
                    if con.execute("SELECT 1 FROM kb_docs WHERE id = ?", [d["id"]]).fetchone():
                        errors.append(f"{p.name}: doppelte id '{d['id']}' übersprungen")
                        continue
                    toks = _doc_tokens(d)
                    con.execute(
                        "INSERT INTO kb_docs (id, topic, title, text, tags, source, file, len)"
                        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                        [d["id"], d["topic"], d["title"], d["text"],
                         json.dumps(d["tags"], ensure_ascii=False), d["source"], p.name,
                         len(toks)])
                    tf = _tf(toks)
                    if tf:                        # ein Statement je Dokument, Listen als Parameter
                        con.execute(
                            "INSERT INTO kb_terms (term, doc_id, tf) "
                            "SELECT unnest(?::VARCHAR[]), ?, unnest(?::INTEGER[])",
                            [list(tf), d["id"], list(tf.values())])
                    count += 1
                con.execute("INSERT INTO kb_files (name, sha256, docs, loaded_at)"
                            " VALUES (?, ?, ?, ?)", [p.name, sha, count, _now()])
                loaded.append(p.name)
            con.execute("COMMIT")
        except Exception as exc:  # noqa: BLE001
            con.execute("ROLLBACK")
            return {"ok": False, "error": f"Laden fehlgeschlagen: {exc}"[:300]}
        docs = con.execute("SELECT COUNT(*) FROM kb_docs").fetchone()[0]
    return {"ok": True, "backend": "duckdb", "loaded": loaded, "skipped": skipped,
            "errors": errors, "docs": docs}


def _drop_file(con, name: str) -> None:
    con.execute("DELETE FROM kb_terms WHERE doc_id IN (SELECT id FROM kb_docs WHERE file = ?)",
                [name])
    con.execute("DELETE FROM kb_docs WHERE file = ?", [name])
    con.execute("DELETE FROM kb_files WHERE name = ?", [name])


_BM25_SQL = """
WITH stats AS (
    SELECT COUNT(*)::DOUBLE AS n, AVG(len)::DOUBLE AS avgdl FROM kb_docs
),
qt AS (
    SELECT DISTINCT unnest(?::VARCHAR[]) AS term
),
df AS (
    SELECT t.term, COUNT(*)::DOUBLE AS df
    FROM kb_terms t JOIN qt ON t.term = qt.term
    GROUP BY t.term
)
SELECT d.id, d.title, d.topic, d.text, d.source, d.tags,
       SUM( ln(1 + (s.n - df.df + 0.5) / (df.df + 0.5))
            * (t.tf * (? + 1))
            / (t.tf + ? * (1 - ? + ? * d.len / s.avgdl)) ) AS score,
       COUNT(*) AS matched
FROM kb_terms t
JOIN df ON t.term = df.term
JOIN kb_docs d ON d.id = t.doc_id
CROSS JOIN stats s
GROUP BY d.id, d.title, d.topic, d.text, d.source, d.tags
ORDER BY score DESC, d.id ASC
LIMIT ?
"""


def _split_compounds(qterms: list[str], known) -> list[str]:
    """Anfrageseitige Zerlegung deutscher Komposita, die selbst nicht im Index stehen:
    "farbpalette" → "farb" + "palett", wenn beide Teile im Index vorkommen.
    `known(terms) -> set` prüft, welche Terme es gibt. Deterministisch (frühester Schnitt
    mit dem längsten rechten Teil)."""
    present = known(qterms)
    out: list[str] = []
    for t in qterms:
        if t in present or len(t) < 8 or not t.isalpha():
            out.append(t)
            continue
        cands = []
        for i in range(4, len(t) - 3):
            left, right = t[:i], t[i:]
            lefts = {stem(left), left}
            if left.endswith("s") and len(left) > 4:          # Fugen-s, z. B. "lesbarkeits|regel"
                lefts |= {left[:-1], stem(left[:-1])}
            cands.append((sorted(lefts), sorted({right, stem(right)})))
        flat = sorted({x for ls, rs in cands for x in ls + rs})
        have = known(flat)
        split = None
        for ls, rs in cands:
            lhit = next((x for x in ls if x in have), None)
            rhit = next((x for x in rs if x in have), None)
            if lhit and rhit:
                split = [lhit, rhit]
                break
        out.extend(split or [t])
    return list(dict.fromkeys(out))


def _known_mem(terms) -> set:
    return {t for t in terms if t in _mem.terms}


def _known_db(con):
    def known(terms) -> set:
        if not terms:
            return set()
        rows = con.execute("SELECT DISTINCT term FROM kb_terms WHERE term IN "
                           "(SELECT unnest(?::VARCHAR[]))", [list(terms)]).fetchall()
        return {r[0] for r in rows}
    return known


def kb_search(q, limit=5, force_memory: bool = False) -> dict:
    """BM25-Suche. Liefert {"ok","results":[{id,title,topic,text,source,tags,score,matched}],
    "terms":[…],"backend"}."""
    try:
        limit = max(1, min(50, int(limit)))
    except (TypeError, ValueError):
        limit = 5
    qterms = list(dict.fromkeys(tokenize(q if isinstance(q, str) else "")))[:32]
    with _lock:
        con = None if force_memory else _conn()
        if con is None:
            qterms = _split_compounds(qterms, _known_mem)
            res = _mem.search(qterms, limit)
            unknown = [t for t in qterms if t not in _known_mem(qterms)]
            return {"ok": True, "results": res, "terms": qterms, "unknown": unknown,
                    "backend": "memory"}
        if not qterms:
            return {"ok": True, "results": [], "terms": [], "unknown": [], "backend": "duckdb"}
        known = _known_db(con)
        qterms = _split_compounds(qterms, known)
        present = known(qterms)
        unknown = [t for t in qterms if t not in present]
        rows = con.execute(_BM25_SQL, [qterms, K1, K1, B, B, limit]).fetchall()
    results = []
    for r in rows:
        try:
            tags = json.loads(r[5]) if r[5] else []
        except json.JSONDecodeError:
            tags = []
        results.append({"id": r[0], "title": r[1], "topic": r[2], "text": r[3],
                        "source": r[4], "tags": tags, "score": float(r[6]),
                        "matched": int(r[7])})
    return {"ok": True, "results": results, "terms": qterms, "unknown": unknown,
            "backend": "duckdb"}


def kb_relevance(result: dict, qterms: list[str], unknown=()) -> dict:
    """Wie gut passt ein Treffer zur Anfrage? Grundlage für "Treffer oder ehrliche Absage".

    * `coverage` — Anteil der Anfrageterme, die im Treffer vorkommen
    * `strong`   — mindestens ein Anfrageterm steht im Titel oder in den Tags
    * relevant   — strong UND (≥ 2 Terme getroffen ODER Abdeckung ≥ 0,5, wobei kein
      Anfrageterm der ganzen Wissensbasis unbekannt sein darf: "Was kostet ein Auto?"
      trifft sonst über ein einzelnes Allerweltswort).
    """
    n = max(1, len(qterms))
    matched = int(result.get("matched", 0))
    head = set(tokenize(result.get("title", "")) + tokenize(" ".join(result.get("tags", []))))
    strong = any(t in head for t in qterms)
    coverage = matched / n
    relevant = strong and (matched >= 2 or (coverage >= 0.5 and not unknown))
    return {"coverage": round(coverage, 4), "strong": strong, "matched": matched,
            "relevant": relevant}


def kb_memory_load(directory=None) -> dict:
    """Lädt die Wissensbasis zusätzlich in den Speicher-Index (für Vergleiche/Tests)."""
    directory = Path(directory) if directory else ROOT / "kb"
    with _lock:
        _mem.__init__()
        errors = []
        for p in _kb_files(directory):
            docs, errs = _read_kb_file(p)
            errors += errs
            for d in docs:
                if d["id"] not in _mem.docs:
                    _mem.add_doc(d, p.name)
            _mem.files[p.name] = _sha(p)
        return {"ok": True, "docs": len(_mem.docs), "errors": errors}


def kb_count() -> int:
    with _lock:
        con = _conn()
        if con is None:
            return len(_mem.docs)
        return int(con.execute("SELECT COUNT(*) FROM kb_docs").fetchone()[0])


# --------------------------------------------------------------------------- #
# Chat-Verlauf                                                                 #
# --------------------------------------------------------------------------- #
def chat_add(role: str, content: str, meta: dict | None = None) -> dict:
    if role not in ("user", "assistant", "system"):
        return {"ok": False, "error": "role muss user, assistant oder system sein",
                "field": "role"}
    if not isinstance(content, str):
        return {"ok": False, "error": "content muss Text sein", "field": "content"}
    with _lock:
        con = _conn()
        if con is None:
            return _pending()
        row = con.execute(
            "INSERT INTO chat_log (ts, role, content, meta) VALUES (?, ?, ?, ?) RETURNING id",
            [_now(), role, content[:MAX_CONTENT],
             json.dumps(meta or {}, ensure_ascii=False)]).fetchone()
    return {"ok": True, "id": int(row[0])}


def chat_history(limit=50) -> dict:
    """Die letzten `limit` Nachrichten, älteste zuerst."""
    try:
        limit = max(1, min(500, int(limit)))
    except (TypeError, ValueError):
        limit = 50
    with _lock:
        con = _conn()
        if con is None:
            return _pending()
        rows = con.execute(
            "SELECT id, ts, role, content, meta FROM "
            "(SELECT * FROM chat_log ORDER BY id DESC LIMIT ?) ORDER BY id ASC",
            [limit]).fetchall()
    items = []
    for r in rows:
        try:
            meta = json.loads(r[4]) if r[4] else {}
        except json.JSONDecodeError:
            meta = {}
        items.append({"id": int(r[0]), "ts": _iso(r[1]), "role": r[2], "content": r[3],
                      "meta": meta})
    return {"ok": True, "items": items, "backend": "duckdb"}


def chat_clear() -> dict:
    with _lock:
        con = _conn()
        if con is None:
            return _pending()
        n = con.execute("SELECT COUNT(*) FROM chat_log").fetchone()[0]
        con.execute("DELETE FROM chat_log")
    return {"ok": True, "deleted": int(n)}


# --------------------------------------------------------------------------- #
# SVG-Bibliothek                                                               #
# --------------------------------------------------------------------------- #
def svg_save(name: str, spec, svg: str, type_: str | None = None) -> dict:
    if not isinstance(name, str) or not name.strip():
        return {"ok": False, "error": "name fehlt", "field": "name"}
    if len(name) > MAX_NAME:
        return {"ok": False, "error": f"name ist länger als {MAX_NAME} Zeichen",
                "field": "name"}
    if not isinstance(svg, str) or not svg.strip():
        return {"ok": False, "error": "svg fehlt", "field": "svg"}
    if len(svg.encode()) > MAX_SVG:
        return {"ok": False, "error": "svg ist zu groß (max. 512 KB)", "field": "svg"}
    try:
        spec_json = json.dumps(spec if spec is not None else {}, ensure_ascii=False)
    except (TypeError, ValueError):
        return {"ok": False, "error": "spec ist kein JSON", "field": "spec"}
    if type_ is None and isinstance(spec, dict):
        type_ = str(spec.get("type", ""))[:40] or None
    with _lock:
        con = _conn()
        if con is None:
            return _pending()
        row = con.execute(
            "INSERT INTO svg_library (name, created, type, spec, svg, bytes)"
            " VALUES (?, ?, ?, ?, ?, ?) RETURNING id",
            [name.strip(), _now(), type_, spec_json, svg, len(svg.encode())]).fetchone()
    return {"ok": True, "id": int(row[0])}


def svg_list(limit=200) -> dict:
    try:
        limit = max(1, min(1000, int(limit)))
    except (TypeError, ValueError):
        limit = 200
    with _lock:
        con = _conn()
        if con is None:
            return _pending()
        rows = con.execute("SELECT id, name, created, type, bytes FROM svg_library"
                           " ORDER BY id DESC LIMIT ?", [limit]).fetchall()
    return {"ok": True, "backend": "duckdb",
            "items": [{"id": int(r[0]), "name": r[1], "created": _iso(r[2]), "type": r[3],
                       "bytes": r[4]} for r in rows]}


def _as_id(value) -> int | None:
    try:
        v = int(str(value).strip())
    except (TypeError, ValueError):
        return None
    return v if 0 < v < 2 ** 62 else None


def svg_get(item_id) -> dict:
    iid = _as_id(item_id)
    if iid is None:
        return {"ok": False, "error": "id muss eine positive ganze Zahl sein", "field": "id"}
    with _lock:
        con = _conn()
        if con is None:
            return _pending()
        r = con.execute("SELECT id, name, created, type, bytes, spec, svg FROM svg_library"
                        " WHERE id = ?", [iid]).fetchone()
    if not r:
        return {"ok": False, "error": "nicht gefunden", "field": "id", "status": 404}
    try:
        spec = json.loads(r[5]) if r[5] else {}
    except json.JSONDecodeError:
        spec = {}
    return {"ok": True, "item": {"id": int(r[0]), "name": r[1], "created": _iso(r[2]),
                                 "type": r[3], "bytes": r[4], "spec": spec, "svg": r[6]}}


def svg_delete(item_id) -> dict:
    iid = _as_id(item_id)
    if iid is None:
        return {"ok": False, "error": "id muss eine positive ganze Zahl sein", "field": "id"}
    with _lock:
        con = _conn()
        if con is None:
            return _pending()
        r = con.execute("DELETE FROM svg_library WHERE id = ? RETURNING id", [iid]).fetchone()
    if not r:
        return {"ok": False, "error": "nicht gefunden", "field": "id", "status": 404}
    return {"ok": True, "deleted": int(r[0])}
