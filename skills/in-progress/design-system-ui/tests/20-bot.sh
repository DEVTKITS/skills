# tests/20-bot.sh — Design-Assistent, Designrechner, Wissensbasis, DuckDB-Speicher.
# Track designlab-kb-store. Wird von test.sh eingelesen oder allein ausgeführt:
#   tests/run-one.sh tests/20-bot.sh 8830
# Nutzt ok / bad / say, $BASE, $PY. Eigene Hilfsserver laufen auf 8831–8833
# (Fake-Ollama, Server mit Fake-Ollama, Server mit reinem python3) und werden am Ende
# beendet. Kein `exit`, keine `trap` — die Datei wird in test.sh eingelesen.
#
# Die Prüfungen laufen in einem Python-Treiber (sauberes Aufräumen per try/finally);
# er gibt Zeilen "PASS …" / "FAIL …" / "NOTE …" aus, die hier zu ok/bad werden.

B20_TMP="$(mktemp -d)"
B20_OUT="$B20_TMP/out.txt"

cat >"$B20_TMP/driver.py" <<'B20_PY'
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

BASE, PYBIN, TMP = sys.argv[1], sys.argv[2], sys.argv[3]
ROOT = pathlib.Path.cwd()
sys.path.insert(0, str(ROOT))          # designlab, store
FAKE_PORT, MODEL_PORT, PURE_PORT = 8831, 8832, 8833
PENDING = "— ausstehend —"
ALLOWED_TOPICS = {"color", "contrast", "typography", "spacing", "layout", "hierarchy",
                  "motion", "accessibility", "responsive", "svg", "icons", "dataviz",
                  "ux-writing", "dkz"}
DKZ_SOURCE = "DkZ Design System v2 (Skill dkz-vanilla-ui-engine)"


def P(cond, label):
    print(("PASS " if cond else "FAIL ") + label, flush=True)
    return bool(cond)


def N(msg):
    print("NOTE " + msg, flush=True)


def req(method, url, body=None, timeout=30):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(url, data=data, method=method,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            raw = resp.read()
            status = resp.status
    except urllib.error.HTTPError as e:
        raw, status = e.read(), e.code
    try:
        return status, json.loads(raw or b"null")
    except json.JSONDecodeError:
        return status, None


def get(_base, _path, **q):
    url = _base + _path + ("?" + urllib.parse.urlencode(q) if q else "")
    return req("GET", url)


def post(base, path, body):
    return req("POST", base + path, body)


def port_free(port):
    with socket.socket() as s:
        return s.connect_ex(("127.0.0.1", port)) != 0


def wait_health(base, secs=15):
    end = time.time() + secs
    while time.time() < end:
        try:
            if get(base, "/health")[0] == 200:
                return True
        except OSError:
            pass
        time.sleep(0.1)
    return False


def stop(proc):
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(5)


def section(fn):
    try:
        fn()
    except Exception as exc:  # noqa: BLE001 — ein Abschnitt darf die anderen nicht stoppen
        P(False, f"{fn.__name__} abgestürzt: {type(exc).__name__}: {exc}")


# --------------------------------------------------------------------------- #
def kb_schema():
    files = sorted((ROOT / "kb").glob("*.jsonl"))
    total, errors, ids, own = 0, [], set(), 0
    topics = set()
    for f in files:
        for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            total += 1
            where = f"{f.name}:{n}"
            try:
                e = json.loads(line)
            except json.JSONDecodeError:
                errors.append(f"{where} kein JSON")
                continue
            if set(e) != {"id", "topic", "title", "text", "tags", "source"}:
                errors.append(f"{where} Felder {sorted(e)}")
                continue
            if not all(isinstance(e[k], str) and e[k].strip()
                       for k in ("id", "topic", "title", "text", "source")):
                errors.append(f"{where} leeres Feld")
            if not (isinstance(e["tags"], list) and e["tags"]
                    and all(isinstance(t, str) for t in e["tags"])):
                errors.append(f"{where} tags")
            if e["id"] in ids:
                errors.append(f"{where} doppelte id {e['id']}")
            ids.add(e["id"])
            if f.name == "video.jsonl":          # gehört dem Video-Track
                continue
            own += 1
            topics.add(e["topic"])
            words = len(e["text"].split())
            if not 40 <= words <= 140:
                errors.append(f"{where} {words} Wörter")
            if e["topic"] not in ALLOWED_TOPICS:
                errors.append(f"{where} Thema {e['topic']}")
            if e["topic"] == "dkz" and e["source"] != DKZ_SOURCE:
                errors.append(f"{where} DkZ-Quelle")
    P(own >= 90, f"Wissensbasis hat ≥ 90 eigene Einträge ({own}, gesamt {total})")
    P(not errors, "alle Einträge schemagültig (Felder, 40–140 Wörter, Themen, ids)"
      + (f" — {errors[:5]}" if errors else ""))
    P(topics == ALLOWED_TOPICS, f"alle 14 Themen abgedeckt ({len(topics)})")


def status():
    s, d = get(BASE, "/api/bot/status")
    P(s == 200 and d.get("ok"), "GET /api/bot/status antwortet")
    P(d["kb"]["docs"] >= 90, f"Status meldet ≥ 90 Dokumente ({d['kb']['docs']})")
    try:
        import duckdb  # noqa: F401
        has = True
    except ImportError:
        has = False
    if has:
        P(d["kb"]["backend"] == "duckdb" and d["store"]["backend"] == "duckdb",
          "Status: Wissensbasis und Speicher laufen auf DuckDB")
    else:
        P(d["store"]["backend"] == PENDING, "Status ohne duckdb: Speicher — ausstehend —")
    P(isinstance(d["llm"]["live"], bool) and d["llm"]["model"], "Status nennt Modell und live")
    return d


RELEVANCE = [  # (Frage, erwartete Themen) — Treffer, wenn eins in den Top 3 steht
    ("Welches Kontrastverhältnis braucht Fließtext?", {"contrast"}),
    ("Wie lang sollte eine Textzeile sein?", {"typography"}),
    ("Zeilenabstand für Fließtext", {"typography"}),
    ("Wie groß müssen Tippziele sein?", {"accessibility"}),
    ("Farbharmonie komplementär oder triadisch", {"color"}),
    ("Abstände nach dem 8-Punkt-Raster", {"spacing"}),
    ("Wann nehme ich CSS Grid statt Flexbox?", {"layout"}),
    ("Wie baue ich eine klare visuelle Hierarchie?", {"hierarchy"}),
    ("Animationen für Menschen mit Schwindel reduzieren", {"motion"}),
    ("Alternativtext für Bilder schreiben", {"accessibility"}),
    ("Bilder responsiv einbinden mit srcset", {"responsive"}),
    ("SVG mit viewBox skalieren", {"svg"}),
    ("Icon-Schaltflächen beschriften", {"icons"}),
    ("Tortendiagramm oder Balkendiagramm?", {"dataviz"}),
    ("Wie formuliere ich eine Fehlermeldung?", {"ux-writing"}),
    ("Welche Themes hat DkZ?", {"dkz"}),
    ("Was besagt die 18-KB-Regel?", {"dkz"}),
    ("What contrast ratio does body text need?", {"contrast"}),
    ("minimum touch target size", {"accessibility"}),
    ("line height for body text", {"typography"}),
    ("dark mode colors", {"color"}),
    ("how to respect reduced motion", {"motion"}),
    ("make an SVG accessible", {"svg"}),
    ("should a bar chart axis start at zero", {"dataviz"}),
    ("write clear button labels", {"ux-writing"}),
    ("mobile first media queries", {"responsive"}),
    ("visual hierarchy and the primary action", {"hierarchy"}),
    ("consistent icon grid and stroke width", {"icons"}),
    ("limit the content width with max-width", {"layout"}),
    ("8pt spacing grid", {"spacing"}),
]


def relevance():
    hits = 0
    for q, want in RELEVANCE:
        s, d = get(BASE, "/api/bot/kb", q=q, limit=3)
        got = [r["topic"] for r in (d or {}).get("results", [])]
        ok = s == 200 and bool(want & set(got))
        hits += ok
        P(ok, f"Suche „{q}“ → {sorted(want)} in Top 3 {got}")
    s, d = get(BASE, "/api/bot/kb", q="Kontrast", limit=2)
    r = d["results"][0]
    P(set(r) >= {"id", "title", "topic", "score", "snippet", "source"},
      "Trefferformat id/title/topic/score/snippet/source")
    P(d["results"][0]["score"] >= d["results"][1]["score"], "Treffer nach Score sortiert")


def contrast_values():
    import designlab as dl
    cases = [("#000", "#fff", 21.0, "AAA", "AAA"), ("#767676", "#fff", 4.54, "AA", "AAA"),
             ("#777777", "#ffffff", 4.48, "fail", "AA")]
    for fg, bg, ratio, normal, large in cases:
        s, d = get(BASE, "/api/design/contrast", fg=fg, bg=bg)
        P(s == 200 and d["ratio"] == ratio and d["grade"]["normal"] == normal
          and d["grade"]["large"] == large,
          f"Kontrast {fg} auf {bg} = {ratio}:1, Fließtext {normal}, groß {large}")
    P(dl.contrast_ratio("#767676", "#fff") >= 4.5 > dl.contrast_ratio("#777777", "#fff"),
      "designlab: #767676 besteht AA, #777777 nicht (ungerundet bewertet)")
    s, d = get(BASE, "/api/design/contrast", fg="#777777", bg="#fff")
    P(d.get("suggestion", {}).get("fg") and
      dl.contrast_ratio(d["suggestion"]["fg"], "#fff") >= 4.5,
      f"Korrekturvorschlag erreicht 4,5:1 ({d.get('suggestion')})")
    s, d = get(BASE, "/api/design/contrast", fg="#808080", bg="#fff")
    P(d["ui_components"] is True and d["aa"]["normal"] is False,
      "#808080 auf Weiß: UI-Komponenten 3:1 ja, Fließtext AA nein")
    s, d = get(BASE, "/api/design/contrast", fg="nope", bg="#fff")
    P(s == 400 and d["ok"] is False and d.get("field") == "fg", "ungültige Farbe → 400 mit field")
    s, d = get(BASE, "/api/design/convert", color="hsl(0, 0%, 46.3%)")
    P(s == 200 and d["hex"] == "#767676", f"convert hsl → hex ({d.get('hex')})")
    s, d = get(BASE, "/api/design/convert", color="rgb(250, 30, 78)")
    P(d["hex"] == "#fa1e4e" and d["hsl"][0] == 346.9, "convert rgb → hex/hsl")
    s, d = get(BASE, "/api/design/palette", base="#fa1e4e", scheme="triadic")
    P(s == 200 and len(d["colors"]) == 3 and d["colors"][0]["hex"] == "#fa1e4e",
      "Palette triadisch: 3 Farben, Basis zuerst")
    hues = []
    for scheme, n in (("complementary", 2), ("analogous", 3), ("tetradic", 4), ("monochrome", 5)):
        s, d = get(BASE, "/api/design/palette", base="#3366cc", scheme=scheme)
        P(s == 200 and len(d["colors"]) == n, f"Palette {scheme}: {n} Farben")
        hues.append(d)
    comp = hues[0]["colors"]
    P(abs((comp[1]["hsl"][0] - comp[0]["hsl"][0]) % 360 - 180) < 1.5,
      "komplementär = 180° gedreht")
    s, d = get(BASE, "/api/design/palette", base="#fff", scheme="rainbow")
    P(s == 400 and d.get("field") == "scheme", "unbekanntes Schema → 400")
    s, d = get(BASE, "/api/design/scale", base=16, ratio=1.25, steps=7)
    P(s == 200 and [x["px"] for x in d["steps"]][:4] == [16, 20, 25, 31.25],
      "Typo-Skala 16 × 1,25ⁿ")
    s, d = get(BASE, "/api/design/scale", base=16, ratio=1.25, steps="x")
    P(s == 400, "Skala mit ungültigem steps → 400")
    s, d = get(BASE, "/api/design/spacing", base=8, steps=6)
    P(s == 200 and [x["px"] for x in d["steps"]] == [4, 8, 16, 24, 32, 40, 48],
      "8-Punkt-Skala 4, 8, 16 … 48")
    s, d = get(BASE, "/api/design/fluid", min=16, max=24)
    P(s == 200 and d["css"] == "clamp(1rem, 0.8333rem + 0.8333vw, 1.5rem)",
      f"fluide Schrift als clamp() ({d.get('css')})")


def ask_without_model(st):
    if st["llm"]["live"]:
        N("Ein Modell ist erreichbar — die Prüfungen ohne Modell werden übersprungen")
        return
    q = "Wie lang sollte eine Textzeile sein?"
    s, d = post(BASE, "/api/bot/ask", {"message": q})
    P(s == 200 and d["ok"] and d["live"] is False, "ask ohne Modell: ok, live:false")
    P(d["sources"] and d["sources"][0]["id"] == "typo-zeilenlaenge",
      f"ask: beste Quelle typo-zeilenlaenge ({[x['id'] for x in d['sources']]})")
    P("Quelle:" in d["reply"] and f"[{d['sources'][0]['id']}]" in d["reply"],
      "extraktive Antwort nennt Quelle und id")
    P(d["intent"] == "kb.answer" and 0 < d["confidence"] <= 1 and d["actions"] == [],
      "ask: intent kb.answer, confidence 0..1, actions []")
    kb_text = json.loads((ROOT / "kb" / "typography.jsonl").read_text(encoding="utf-8")
                         .splitlines()[0])["text"]
    P(kb_text in d["reply"], "Antwort steht wörtlich in der Wissensbasis (nichts erfunden)")
    for off in ("Wie wird das Wetter morgen in Berlin?", "Was kostet ein Auto?",
                "Who won the football world cup?", "Schreib mir ein Gedicht über Katzen"):
        s, d = post(BASE, "/api/bot/ask", {"message": off})
        P(s == 200 and d["intent"] == "out_of_scope" and d["live"] is False
          and not d["sources"] and "nur Gestaltungsfragen" in d["reply"]
          and "ollama serve" in d["reply"],
          f"Off-Topic „{off}“ → ehrliche Absage mit Hinweis aufs Modell")
    if not st.get("control"):
        s, d = post(BASE, "/api/bot/ask", {"message": "Kontrast #777777 auf #ffffff?"})
        P(d["intent"] == "design.contrast" and "4.48:1" in d["reply"],
          "ohne control.py rechnet der Assistent Kontrast selbst (4.48:1)")
    else:
        N("control.py vorhanden — Rechen-Rückfall des Assistenten nicht geprüft")
    s, d = post(BASE, "/api/bot/ask", {"message": "   "})
    P(s == 400 and d.get("field") == "message", "leere Nachricht → 400")
    s, d = post(BASE, "/api/bot/ask", {"message": "x" * 5000})
    P(s == 400, "zu lange Nachricht → 400")
    s, d = post(BASE, "/api/bot/ask", {"message": "Kontrast", "provider": "none",
                                         "history": "kaputt", "context": [1, 2]})
    P(s == 200 and d["ok"], "falsche Typen bei history/context werden toleriert")


def history():
    s, d = post(BASE, "/api/bot/clear", {})
    P(s == 200 and d["ok"], "POST /api/bot/clear")
    s, d = get(BASE, "/api/bot/history")
    P(s == 200 and d["items"] == [], "Verlauf nach dem Löschen leer")
    post(BASE, "/api/bot/ask", {"message": "Was ist ein gutes Icon-Raster?"})
    s, d = get(BASE, "/api/bot/history", limit=10)
    roles = [i["role"] for i in d["items"]]
    P(roles == ["user", "assistant"] and d["items"][0]["content"] == "Was ist ein gutes Icon-Raster?",
      "Frage und Antwort landen im Verlauf")
    P(d["items"][1]["meta"].get("sources"), "Verlauf speichert die Quellen-ids der Antwort")
    s, d = post(BASE, "/api/bot/clear", {})
    P(d.get("deleted") == 2, f"clear meldet 2 gelöschte Nachrichten ({d.get('deleted')})")
    s, d = get(BASE, "/api/bot/history")
    P(d["items"] == [], "Verlauf wieder leer")


SAFE_SVG = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><title>t</title>'
            '<rect width="10" height="10" fill="var(--accent, #fa1e4e)"/></svg>')


def library():
    _, plug = get(BASE, "/api/plugins")
    bot = next(p for p in plug["plugins"] if p["name"] == "botapi")
    has_kit = bool((bot.get("status") or {}).get("svgkit"))
    s, d = post(BASE, "/api/svg/library", {"name": "Test-Kachel", "spec": {"type": "badge"},
                                             "svg": SAFE_SVG})
    P(s == 200 and isinstance(d.get("id"), int), "Bibliothek: anlegen")
    iid = d.get("id")
    s, d = get(BASE, "/api/svg/library")
    P(s == 200 and d["backend"] == "duckdb" and any(i["id"] == iid for i in d["items"]),
      "Bibliothek: Liste enthält den Eintrag")
    item = next(i for i in d["items"] if i["id"] == iid)
    P(set(item) == {"id", "name", "created", "type", "bytes"} and item["type"] == "badge",
      "Listeneintrag hat id/name/created/type/bytes")
    s, d = get(BASE, f"/api/svg/library/{iid}")
    P(s == 200 and d["item"]["spec"] == {"type": "badge"} and "<rect" in d["item"]["svg"],
      "Bibliothek: einzeln lesen mit spec und svg")
    s, d = post(BASE, "/api/svg/library/delete", {"id": iid})
    P(s == 200 and d["ok"], "Bibliothek: löschen")
    s, d = get(BASE, f"/api/svg/library/{iid}")
    P(s == 404, "gelöschter Eintrag → 404")
    s, d = post(BASE, "/api/svg/library/delete", {"id": iid})
    P(s == 404, "doppelt löschen → 404")
    s, d = get(BASE, "/api/svg/library/abc")
    P(s == 400 and d.get("field") == "id", "ungültige id → 400")
    evil = ('<svg xmlns="http://www.w3.org/2000/svg" onload="alert(1)">'
            '<script>alert(2)</script><rect width="1" height="1"/></svg>')
    s, d = post(BASE, "/api/svg/library", {"name": "böse", "svg": evil})
    if has_kit:
        ok = s == 200
        if ok:
            _, it = get(BASE, f"/api/svg/library/{d['id']}")
            low = it["item"]["svg"].lower()
            ok = "<script" not in low and "onload" not in low
            post(BASE, "/api/svg/library/delete", {"id": d["id"]})
        P(ok or s == 400, "SVG mit script/onload wird bereinigt oder abgelehnt (svgkit)")
    else:
        P(s == 400 and d.get("field") == "svg", "SVG mit script/onload abgelehnt (ohne svgkit)")
    s, d = post(BASE, "/api/svg/library", {"name": "ext", "svg":
                '<svg xmlns="http://www.w3.org/2000/svg"><image href="http://x/y.png"/></svg>'})
    P(has_kit or s == 400, "externer href ohne svgkit abgelehnt")
    s, d = post(BASE, "/api/svg/library", {"name": "nur-spec", "spec": {"type": "badge",
                                                                       "title": "OK"}})
    if has_kit:
        P(s in (200, 400), f"nur spec mit svgkit → erzeugt oder klare Ablehnung ({s})")
        if s == 200:
            post(BASE, "/api/svg/library/delete", {"id": d["id"]})
    else:
        P(s == 503 and d.get("error") == PENDING and d.get("needs") == "svgkit",
          "nur spec ohne svgkit → 503 — ausstehend — (needs svgkit)")
    s, d = post(BASE, "/api/svg/library", {"name": "", "svg": SAFE_SVG})
    P(s == 400 and d.get("field") == "name", "Name fehlt → 400")
    s, d = post(BASE, "/api/svg/library", {"name": "x" * 300, "svg": SAFE_SVG})
    P(s == 400 and d.get("field") == "name", "Name zu lang → 400")


INJECT = ["'; DROP TABLE kb_docs; --", "x') OR 1=1; DELETE FROM svg_library; --",
          "\" OR \"\"=\"; DROP TABLE chat_log; --", "1; UPDATE kb_docs SET text='x'"]


def injection():
    _, st0 = get(BASE, "/api/bot/status")
    for q in INJECT:
        s, d = get(BASE, "/api/bot/kb", q=q)
        P(s == 200 and d["ok"], f"Injektion in q harmlos: {q[:24]}")
    post(BASE, "/api/bot/clear", {})
    ids = []
    for name in INJECT:
        s, d = post(BASE, "/api/svg/library", {"name": name, "svg": SAFE_SVG})
        P(s == 200, f"Injektion in name harmlos: {name[:24]}")
        if s == 200:
            _, it = get(BASE, f"/api/svg/library/{d['id']}")
            P(it["item"]["name"] == name, "Name wörtlich gespeichert")
            ids.append(d["id"])
    for msg in INJECT:
        s, d = post(BASE, "/api/bot/ask", {"message": msg})
        P(s == 200 and d["ok"], f"Injektion in message harmlos: {msg[:24]}")
    _, h = get(BASE, "/api/bot/history", limit=50)
    P([i["content"] for i in h["items"] if i["role"] == "user"] == INJECT,
      "Nachrichten wörtlich im Verlauf, Tabelle chat_log intakt")
    _, st1 = get(BASE, "/api/bot/status")
    P(st1["kb"]["docs"] == st0["kb"]["docs"], "kb_docs unverändert nach Injektionsversuchen")
    _, lst = get(BASE, "/api/svg/library")
    P(all(any(i["id"] == x for i in lst["items"]) for x in ids), "svg_library intakt")
    for x in ids:
        post(BASE, "/api/svg/library/delete", {"id": x})
    s, d = post(BASE, "/api/svg/library/delete", {"id": "1 OR 1=1"})
    P(s == 400, "Injektion in id → 400")
    post(BASE, "/api/bot/clear", {})


def concurrency():
    post(BASE, "/api/bot/clear", {})
    marker = f"PAR-{os.getpid()}"
    jobs = []
    for i in range(20):
        jobs.append(("ask", {"message": f"Wie viel Zeilenabstand braucht Fließtext? {marker}-{i}"}))
        jobs.append(("kb", {"q": ["Kontrast", "spacing grid", "SVG viewBox", "Farbe"][i % 4]}))
    for i in range(10):
        jobs.append(("lib", {"name": f"{marker}-{i}", "svg": SAFE_SVG}))
        jobs.append(("calc", {"fg": "#767676", "bg": "#fff"}))

    def run(job):
        kind, body = job
        if kind == "ask":
            return post(BASE, "/api/bot/ask", body)
        if kind == "kb":
            return get(BASE, "/api/bot/kb", **body)
        if kind == "lib":
            return post(BASE, "/api/svg/library", body)
        return get(BASE, "/api/design/contrast", **body)

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=12) as ex:
        res = list(ex.map(run, jobs))
    bad = [r for r in res if r[0] != 200 or not (r[1] or {}).get("ok")]
    P(not bad, f"{len(jobs)} parallele Anfragen (12 Threads) alle ok in {time.time()-t0:.1f}s"
      + (f" — Fehler: {bad[:2]}" if bad else ""))
    _, h = get(BASE, "/api/bot/history", limit=500)
    mine = [i for i in h["items"] if marker in i["content"] and i["role"] == "user"]
    P(len(mine) == 20 and len(h["items"]) == 40,
      f"Verlauf vollständig: 20 Fragen + 20 Antworten ({len(mine)}/{len(h['items'])})")
    _, lst = get(BASE, "/api/svg/library", limit=1000)
    lib = [i for i in lst["items"] if i["name"].startswith(marker)]
    P(len(lib) == 10 and len({i["id"] for i in lib}) == 10, "10 parallele Speicherungen, eindeutige ids")
    for i in lib:
        post(BASE, "/api/svg/library/delete", {"id": i["id"]})
    post(BASE, "/api/bot/clear", {})


def store_direct():
    """BM25 in DuckDB und reine Python-BM25 rechnen gleich; kb_load ist idempotent."""
    try:
        import duckdb  # noqa: F401
    except ImportError:
        N("duckdb fehlt im Test-Python — Vergleich DuckDB/Speicher übersprungen")
        return
    os.environ["DKZ_DB"] = os.path.join(TMP, "direct.duckdb")
    import store
    r1 = store.kb_load(ROOT / "kb")
    r2 = store.kb_load(ROOT / "kb")
    P(r1["ok"] and r1["loaded"] and not r1["errors"], f"kb_load lädt {r1['docs']} Dokumente")
    P(r2["loaded"] == [] and len(r2["skipped"]) == len(r1["loaded"]),
      "kb_load ist idempotent (zweiter Lauf überspringt alle Dateien)")
    store.kb_memory_load(ROOT / "kb")
    same = 0
    for q, _ in RELEVANCE:
        a = store.kb_search(q, 5)["results"]
        b = store.kb_search(q, 5, force_memory=True)["results"]
        same += [(x["id"], round(x["score"], 6)) for x in a] == \
                [(x["id"], round(x["score"], 6)) for x in b]
    P(same == len(RELEVANCE), f"BM25 per SQL = BM25 in Python ({same}/{len(RELEVANCE)} Anfragen)")
    P(store.tokenize("Größe Schriftgrößen") == ["groess", "schriftgroess"]
      and store.tokenize("the colors of the Farben") == ["color", "farb"],
      "Tokenisierung: Umlaute, Stoppwörter, Stammbildung")
    # BM25 von Hand für einen Term gegenrechnen
    import math
    con = store._conn()
    n, avgdl = con.execute("SELECT COUNT(*), AVG(len) FROM kb_docs").fetchone()
    term = "viewbox"
    rows = con.execute("SELECT doc_id, tf FROM kb_terms WHERE term = ?", [term]).fetchall()
    lens = dict(con.execute("SELECT id, len FROM kb_docs").fetchall())
    idf = math.log(1 + (n - len(rows) + 0.5) / (len(rows) + 0.5))
    manual = sorted(((idf * tf * 2.2 / (tf + 1.2 * (0.25 + 0.75 * lens[d] / avgdl)), d)
                     for d, tf in rows), reverse=True)
    got = store.kb_search("viewBox", 50)["results"]
    P(abs(got[0]["score"] - manual[0][0]) < 1e-9 and got[0]["id"] == manual[0][1],
      f"BM25-Score gegengerechnet (k1=1.2, b=0.75): {got[0]['id']} {got[0]['score']:.4f}")


FAKE = r'''
import json, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
port, out = int(sys.argv[1]), sys.argv[2]
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _j(self, obj, code=200):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path == "/api/tags":
            self._j({"models": [{"name": "fake-gemma"}]})
        else:
            self._j({"error": "not found"}, 404)
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(n) or b"{}")
        with open(out, "a", encoding="utf-8") as fh:
            fh.write(json.dumps({"path": self.path, "body": body}) + "\n")
        if self.path == "/api/generate":
            self._j({"model": body.get("model"), "response": "FAKE-OLLAMA-ANTWORT", "done": True})
        elif self.path == "/v1/chat/completions":
            self._j({"model": "fake-gpt", "choices": [{"message": {"role": "assistant",
                     "content": "FAKE-OPENAI-ANTWORT"}}]})
        else:
            self._j({"error": "not found"}, 404)
ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
'''


def model_path():
    for p in (FAKE_PORT, MODEL_PORT):
        if not P(port_free(p), f"Port {p} frei für Hilfsserver"):
            return
    fake_py = os.path.join(TMP, "fake_ollama.py")
    prompts = os.path.join(TMP, "prompts.jsonl")
    pathlib.Path(fake_py).write_text(FAKE, encoding="utf-8")
    fake = srv = None
    try:
        fake = subprocess.Popen([sys.executable, fake_py, str(FAKE_PORT), prompts],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        env = dict(os.environ, PORT=str(MODEL_PORT), DKZ_DB=os.path.join(TMP, "model.duckdb"),
                   OLLAMA_URL=f"http://127.0.0.1:{FAKE_PORT}", GEMMA_MODEL="fake-gemma",
                   ALLOW_LOCAL_PROVIDERS="1",
                   CUSTOM_PROVIDERS=json.dumps([{"id": "fake-openai", "kind": "llm",
                                                 "base_url": f"http://127.0.0.1:{FAKE_PORT}/v1",
                                                 "default_model": "fake-gpt"}]))
        log = open(os.path.join(TMP, "model-server.log"), "w")
        srv = subprocess.Popen([PYBIN, "server.py"], cwd=ROOT, env=env, stdout=log,
                               stderr=subprocess.STDOUT)
        base = f"http://127.0.0.1:{MODEL_PORT}"
        if not P(wait_health(base), "Server mit Fake-Ollama startet"):
            return
        _, st = get(base, "/api/bot/status")
        P(st["llm"]["live"] is True and st["llm"]["model"] == "fake-gemma",
          "Status: Modell live (Fake-Ollama, /api/tags)")
        s, d = post(base, "/api/bot/ask", {"message": "Wie lang sollte eine Textzeile sein?"})
        P(s == 200 and d["live"] is True and d["model"] == "fake-gemma"
          and d["reply"] == "FAKE-OLLAMA-ANTWORT", "Modellpfad: live:true, Antwort vom Modell")
        sent = [json.loads(x) for x in pathlib.Path(prompts).read_text().splitlines()]
        gen = [x for x in sent if x["path"] == "/api/generate"]
        prompt = gen[-1]["body"]["prompt"] if gen else ""
        ids = [x["id"] for x in d.get("sources", [])]
        P(ids and all(f"[{i}]" in prompt for i in ids),
          f"Prompt enthält die gefundenen Einträge mit ids {ids}")
        P("Zeilenlänge für Fließtext" in prompt and "45 bis 75 Zeichen" in prompt,
          "Prompt enthält Titel und Text des besten Eintrags")
        P("ausschließlich auf Grundlage" in prompt and "Erfinde keine" in prompt,
          "Prompt verpflichtet das Modell auf die Einträge")
        P(gen and gen[-1]["body"]["model"] == "fake-gemma" and gen[-1]["body"]["stream"] is False,
          "Ollama-Aufruf mit GEMMA_MODEL, ohne Streaming")
        s, d = post(base, "/api/bot/ask", {"message": "Wie wird das Wetter morgen in Berlin?"})
        P(d["intent"] == "general" and d["live"] is True
          and d["reply"].startswith("Allgemeine Antwort (nicht aus der Wissensbasis)"),
          "Off-Topic mit Modell: als allgemeine Antwort gekennzeichnet")
        s, d = post(base, "/api/bot/ask", {"message": "Wie lang sollte eine Textzeile sein?",
                                            "provider": "fake-openai"})
        if d.get("live"):
            P(d["reply"] == "FAKE-OPENAI-ANTWORT", "provider über providers.chat: live, Antwort vom Anbieter")
        else:
            P("Modell nicht verfügbar" in d["reply"] and d["sources"],
              "provider abgelehnt (z. B. SSRF-Prüfung) → ehrlicher Rückfall auf die Wissensbasis")
            N(f"provider-Pfad nicht live: {d['reply'][-160:]}")
        s, d = post(base, "/api/bot/ask", {"message": "Kontrast", "provider": "none"})
        P(d["live"] is False, "provider none → bewusst ohne Modell")
        _, h = get(base, "/api/bot/history", limit=50)
        P(any(i["meta"].get("live") is True for i in h["items"]),
          "Verlauf vermerkt live-Antworten")
        stop(fake)
        fake = None
        s, d = post(base, "/api/bot/ask", {"message": "Wie lang sollte eine Textzeile sein?"})
        P(s == 200 and d["live"] is False and "Modell nicht verfügbar" in d["reply"]
          and "[typo-zeilenlaenge]" in d["reply"],
          "Modell fällt aus → extraktiver Rückfall mit Hinweis, live:false")
    finally:
        stop(srv)
        stop(fake)


def pure_python3():
    py3 = shutil.which("python3")
    if not py3:
        N("python3 nicht gefunden — Start ohne .venv nicht geprüft")
        return
    if subprocess.run([py3, "-c", "import duckdb"], capture_output=True).returncode == 0:
        N("python3 hat duckdb — Start ohne duckdb nicht prüfbar")
        return
    if not P(port_free(PURE_PORT), f"Port {PURE_PORT} frei"):
        return
    env = dict(os.environ, PORT=str(PURE_PORT), DKZ_DB=os.path.join(TMP, "pure.duckdb"),
               OLLAMA_URL="http://127.0.0.1:9")
    log = open(os.path.join(TMP, "pure-server.log"), "w")
    srv = subprocess.Popen([py3, "server.py"], cwd=ROOT, env=env, stdout=log,
                           stderr=subprocess.STDOUT)
    base = f"http://127.0.0.1:{PURE_PORT}"
    try:
        if not P(wait_health(base), "Server startet mit reinem python3 (ohne .venv/duckdb)"):
            return
        _, plug = get(base, "/api/plugins")
        bot = next(p for p in plug["plugins"] if p["name"] == "botapi")
        P(bot["loaded"] and bot["status"]["duckdb"] is False, "botapi geladen, duckdb: false")
        s, d = get(base, "/api/bot/status")
        P(s == 200 and d["store"]["backend"] == PENDING and d["kb"]["backend"] == "memory"
          and d["kb"]["docs"] >= 90, "Status: Speicher — ausstehend —, Wissensbasis im Speicher")
        s, d = get(base, "/api/bot/kb", q="Kontrast Fließtext")
        P(s == 200 and d["backend"] == "memory" and d["results"][0]["topic"] == "contrast",
          "Suche ohne duckdb über Python-BM25")
        s, d = post(base, "/api/bot/ask", {"message": "Wie lang sollte eine Textzeile sein?"})
        P(s == 200 and d["ok"] and d["stored"] is False and d["sources"],
          "ask ohne duckdb antwortet (Verlauf nicht gespeichert: stored:false)")
        for method, path, body in (("GET", "/api/bot/history", None),
                                   ("POST", "/api/bot/clear", {}),
                                   ("GET", "/api/svg/library", None),
                                   ("GET", "/api/svg/library/1", None),
                                   ("POST", "/api/svg/library", {"name": "a", "svg": SAFE_SVG}),
                                   ("POST", "/api/svg/library/delete", {"id": 1})):
            s, d = req(method, base + path, body)
            P(s == 503 and d == {"ok": False, "error": PENDING, "needs": "duckdb"},
              f"{method} {path} ohne duckdb → 503 — ausstehend —")
        s, d = get(base, "/api/design/contrast", fg="#000", bg="#fff")
        P(d["ratio"] == 21.0, "Designrechner läuft ohne duckdb")
    finally:
        stop(srv)
        log.close()


st = None
for fn in (kb_schema, relevance, contrast_values, history, library, injection,
           concurrency, store_direct, model_path, pure_python3):
    if fn is relevance:
        try:
            st = status()
        except Exception as exc:  # noqa: BLE001
            P(False, f"status abgestürzt: {exc}")
            st = {"llm": {"live": False}}
        section(lambda: ask_without_model(st))
    section(fn)
B20_PY

say "  (Design-Assistent: Treiber läuft, Hilfsserver auf 8831–8833)"
"$PY" "$B20_TMP/driver.py" "$BASE" "$PY" "$B20_TMP" >"$B20_OUT" 2>&1
B20_RC=$?
while IFS= read -r B20_LINE; do
  case "$B20_LINE" in
    "PASS "*) ok "${B20_LINE#PASS }" ;;
    "FAIL "*) bad "${B20_LINE#FAIL }" ;;
    "NOTE "*) say "  - ${B20_LINE#NOTE }" ;;
    *) say "    $B20_LINE" ;;
  esac
done <"$B20_OUT"
[ "$B20_RC" -eq 0 ] || bad "Python-Treiber von 20-bot endete mit Code $B20_RC"
rm -rf "$B20_TMP"
unset B20_TMP B20_OUT B20_RC B20_LINE
