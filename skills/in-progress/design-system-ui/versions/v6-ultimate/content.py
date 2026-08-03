"""
Content-Studio — aus Playwright-Läufen Berichte und Podcasts machen.

Ein DkZ-Projekt.

Playwright und die Agenten dieser Oberfläche hinterlassen eine Spur: jeden
Klick, jede Ausführung, jede Bewertung im Aktivitäts-Feed. Dieses Modul macht
daraus etwas Vorzeigbares — einen gegliederten Bericht oder ein gesprochenes
Podcast-Skript — und liefert fertige Einbettungsschnipsel für Seiten, Designs
und Videos (auch Video-im-Video).

Damit wird nachvollziehbar, was eine KI am Rechner getan hat, ohne dass
jemand ein Rohprotokoll lesen muss.

Quelle der Inhalte:
  - der Aktivitäts-Feed eines Laufs (Schritte, Ziele, Zustände)
  - optional angereichert durch ein Modell (Ollama, OpenAI/GPT, OpenRouter,
    NVIDIA, Hugging Face). Ohne Modell entsteht ein deterministischer
    Bericht direkt aus den Ereignissen — nichts wird erfunden.

Nur Standardbibliothek.
"""

from __future__ import annotations

import json
import re
from collections import Counter

PROJECT = "DkZ"

# --------------------------------------------------------------------------- #
# Vorlagen für die Erzählweise eines Laufs                                     #
# --------------------------------------------------------------------------- #
# Kein Thema-Preset mehr: der Inhalt kommt aus dem, was tatsächlich passiert
# ist. Die Vorlagen bestimmen nur Gliederung und Ton.
FORMATS: dict[str, dict] = {
    "run-report": {
        "title": "Laufbericht",
        "summary": ("Was der Agent Schritt für Schritt getan hat, was dabei "
                    "herauskam und wo es hakte."),
        "sections": ["Überblick", "Ablauf", "Ergebnisse", "Auffälligkeiten",
                     "Fazit"],
    },
    "test-summary": {
        "title": "Testzusammenfassung",
        "summary": ("Verdichtete Sicht auf einen Playwright-Durchlauf: "
                    "Umfang, Ergebnis, offene Punkte."),
        "sections": ["Umfang", "Ergebnis", "Offene Punkte"],
    },
    "changelog": {
        "title": "Änderungsnotiz",
        "summary": "Was sich durch diesen Lauf geändert hat, in Prosa.",
        "sections": ["Was neu ist", "Was geprüft wurde", "Was noch offen ist"],
    },
    "walkthrough": {
        "title": "Durchgang zum Mitlesen",
        "summary": ("Der Lauf als erzählte Führung — geeignet als Podcast "
                    "oder Begleittext zu einem Bildschirmvideo."),
        "sections": ["Einstieg", "Was wir sehen", "Was passiert",
                     "Was das bedeutet"],
    },
}


def list_formats() -> dict:
    return {"project": PROJECT,
            "formats": {k: {"title": v["title"], "summary": v["summary"],
                            "sections": v["sections"]}
                        for k, v in FORMATS.items()}}


# --------------------------------------------------------------------------- #
# Lauf auswerten                                                              #
# --------------------------------------------------------------------------- #
def _slug(text: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s or "lauf"


def summarize_run(events: list[dict]) -> dict:
    """
    Verdichtet einen Aktivitäts-Feed zu Kennzahlen und lesbaren Schritten.

    Erfindet nichts: was nicht im Feed steht, taucht auch im Bericht nicht auf.
    """
    events = [e for e in events if isinstance(e, dict)]
    actors = Counter(str(e.get("actor", "?")) for e in events)
    actions = Counter(str(e.get("action", "?")) for e in events)
    errors = [e for e in events if e.get("state") == "error"]
    targets = [str(e.get("target", "")) for e in events if e.get("target")]

    steps = []
    for e in events:
        action = str(e.get("action", "work"))
        if action in ("session-start", "session-end"):
            continue
        steps.append({
            "actor": str(e.get("actor", "?")),
            "action": action,
            "target": str(e.get("target", "")),
            "detail": str(e.get("detail", "")),
            "state": str(e.get("state", "working")),
            "time": str(e.get("timestamp", ""))[11:19],
        })

    started = next((e.get("timestamp") for e in events
                    if e.get("action") == "session-start"), None)
    ended = next((e.get("timestamp") for e in reversed(events)
                  if e.get("action") == "session-end"), None)

    return {
        "event_count": len(events),
        "step_count": len(steps),
        "actors": dict(actors),
        "actions": dict(actions),
        "error_count": len(errors),
        "errors": [{"detail": e.get("detail", ""), "target": e.get("target", "")}
                   for e in errors][:10],
        "distinct_targets": len(set(targets)),
        "started": started,
        "ended": ended,
        "steps": steps,
    }


def _facts_from_run(run: dict) -> list[str]:
    """Belastbare Sätze, die sich direkt aus dem Lauf ergeben."""
    facts = []
    if run["actors"]:
        who = ", ".join(f"{a} ({n} Schritte)" for a, n in run["actors"].items())
        facts.append(f"Beteiligt waren: {who}.")
    facts.append(f"Der Lauf umfasst {run['step_count']} Schritte "
                 f"auf {run['distinct_targets']} verschiedenen Zielen.")
    if run["actions"]:
        top = ", ".join(f"{a} ×{n}" for a, n in
                        sorted(run["actions"].items(), key=lambda x: -x[1])[:5])
        facts.append(f"Häufigste Aktionen: {top}.")
    if run["error_count"]:
        facts.append(f"{run['error_count']} Schritt(e) meldeten einen Fehler.")
    else:
        facts.append("Kein Schritt meldete einen Fehler.")
    if run["started"]:
        facts.append(f"Start: {run['started']}.")
    if run["ended"]:
        facts.append(f"Ende: {run['ended']}.")
    return facts


# --------------------------------------------------------------------------- #
# Erzeugung                                                                   #
# --------------------------------------------------------------------------- #
def _prompt(fmt: dict, run: dict, style: str, kind: str,
            hosts: list[str] | None) -> str:
    facts = "\n".join(f"- {f}" for f in _facts_from_run(run))
    steps = "\n".join(
        f"- {s['time']} {s['actor']} {s['action']} {s['target']} {s['detail']}".rstrip()
        for s in run["steps"][:60]) or "- (keine Schritte aufgezeichnet)"
    who = f"Sprecher: {', '.join(hosts)}.\n" if hosts else ""
    art = ("ein Podcast-Skript mit Sprechtext, ohne Regieanweisungen"
           if kind == "podcast" else "einen gegliederten Bericht")
    return (
        f"Schreibe auf Deutsch {art} über einen automatisierten "
        f"Browser-Durchlauf (Playwright). Format: {fmt['title']}. "
        f"Ton: {style}.\n{who}"
        f"Gliederung: {', '.join(fmt['sections'])}.\n\n"
        f"Kennzahlen des Laufs:\n{facts}\n\n"
        f"Aufgezeichnete Schritte:\n{steps}\n\n"
        "Halte dich strikt an diese Aufzeichnung. Erfinde keine Schritte, "
        "keine Zahlen und keine Ergebnisse, die oben nicht stehen."
    )


def _split_blocks(text: str) -> list[tuple[str, str]]:
    blocks: list[tuple[str, str]] = []
    title, buf = "Abschnitt", []
    for line in text.splitlines():
        st = line.strip()
        heading = st.startswith("#") or (st.endswith(":") and 0 < len(st) < 80)
        if heading:
            if buf:
                blocks.append((title, "\n".join(buf).strip()))
                buf = []
            title = st.lstrip("#").rstrip(":").strip() or "Abschnitt"
        elif st:
            buf.append(st)
    if buf:
        blocks.append((title, "\n".join(buf).strip()))
    return blocks or [("Abschnitt", text.strip())]


def _fallback_sections(fmt: dict, run: dict) -> list[dict]:
    """Bericht direkt aus den Ereignissen — ohne Modell, ohne Erfindung."""
    facts = _facts_from_run(run)
    out = [{"heading": "Überblick", "text": " ".join(facts)}]

    ablauf = "\n".join(
        f"• {s['time']}  {s['actor']} — {s['action']}"
        f"{' → ' + s['target'] if s['target'] else ''}"
        f"{'  (' + s['detail'] + ')' if s['detail'] else ''}"
        for s in run["steps"][:40])
    out.append({"heading": "Ablauf", "text": ablauf or "Keine Schritte aufgezeichnet."})

    if run["error_count"]:
        out.append({"heading": "Auffälligkeiten",
                    "text": "\n".join(f"• {e['detail'] or e['target']}"
                                      for e in run["errors"])})
    else:
        out.append({"heading": "Auffälligkeiten",
                    "text": "Keine Fehler im Protokoll."})

    out.append({"heading": "Fazit",
                "text": (f"{run['step_count']} Schritte, "
                         f"{run['error_count']} Fehler. "
                         + ("Lauf ohne Beanstandung." if not run["error_count"]
                            else "Fehler prüfen, bevor der Lauf als Beleg dient."))})
    return out


def make_report(run_events: list[dict], fmt_key: str, style: str, infer) -> dict:
    fmt = FORMATS.get(fmt_key, FORMATS["run-report"])
    run = summarize_run(run_events)

    raw = infer(_prompt(fmt, run, style, "report", None))
    live = raw is not None
    sections = ([{"heading": t, "text": x} for t, x in _split_blocks(raw)]
                if live else _fallback_sections(fmt, run))

    return {
        "kind": "report", "project": PROJECT,
        "format": fmt_key, "format_title": fmt["title"],
        "title": f"{fmt['title']} — Playwright-Lauf",
        "slug": _slug(f"{fmt_key}-lauf"),
        "style": style, "sections": sections,
        "run": {k: v for k, v in run.items() if k != "steps"},
        "word_count": sum(len(s["text"].split()) for s in sections),
        "live": live,
        "note": ("Vom angebundenen Modell formuliert, auf Basis des "
                 "Aktivitätsprotokolls." if live else
                 "Direkt aus dem Aktivitätsprotokoll erzeugt — kein Modell "
                 "verbunden, nichts hinzugedichtet."),
    }


def make_podcast(run_events: list[dict], fmt_key: str, minutes: int,
                 style: str, hosts: list[str], infer) -> dict:
    fmt = FORMATS.get(fmt_key, FORMATS["walkthrough"])
    run = summarize_run(run_events)
    minutes = max(1, min(60, int(minutes)))
    hosts = [h for h in hosts if h] or ["Host"]

    raw = infer(_prompt(fmt, run, style, "podcast", hosts))
    live = raw is not None

    if live:
        blocks = _split_blocks(raw)
        segments = [{"title": t, "speaker": hosts[i % len(hosts)], "text": x}
                    for i, (t, x) in enumerate(blocks)]
    else:
        base = _fallback_sections(fmt, run)
        segments = [{"title": "Intro", "speaker": hosts[0],
                     "text": (f"Wir gehen einen automatisierten Durchlauf "
                              f"durch — {run['step_count']} Schritte, "
                              f"aufgezeichnet von Playwright.")}]
        segments += [{"title": s["heading"], "speaker": hosts[i % len(hosts)],
                      "text": s["text"]} for i, s in enumerate(base)]
        segments.append({"title": "Outro", "speaker": hosts[-1],
                         "text": "Das war der Durchlauf. Danke fürs Zuhören."})

    words = sum(len(s["text"].split()) for s in segments)
    return {
        "kind": "podcast", "project": PROJECT,
        "format": fmt_key, "format_title": fmt["title"],
        "title": f"{fmt['title']} — Playwright-Lauf",
        "slug": _slug(f"{fmt_key}-podcast"),
        "style": style, "hosts": hosts,
        "target_minutes": minutes,
        "estimated_minutes": round(words / 130, 1),  # ~130 Wörter/Minute
        "word_count": words, "segments": segments,
        "run": {k: v for k, v in run.items() if k != "steps"},
        "live": live,
        "note": ("Vom angebundenen Modell gesprochen formuliert." if live else
                 "Direkt aus dem Aktivitätsprotokoll — kein Modell verbunden."),
    }


# --------------------------------------------------------------------------- #
# Einbetten in Seiten, Designs und Videos                                     #
# --------------------------------------------------------------------------- #
def _esc(s: str) -> str:
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


def embed(piece: dict, form: str = "article", media: dict | None = None) -> dict:
    """
    Baut einen fertigen Einbettungsschnipsel.

    article  gegliederter Textblock für eine Seite
    player   Audio-Player mit Kapitelmarken je Abschnitt
    pip      Video-im-Video: Bildschirmaufnahme plus kleiner Einspieler
    json     Rohdaten zur eigenen Weiterverarbeitung
    """
    media = media or {}
    title = _esc(piece.get("title", "Beitrag"))
    slug = piece.get("slug", "lauf")

    if form == "json":
        return {"form": "json", "mime": "application/json",
                "code": json.dumps(piece, ensure_ascii=False, indent=2)}

    parts = piece.get("segments") or piece.get("sections") or []

    if form == "player":
        audio = _esc(media.get("audio_url", f"/media/{slug}.mp3"))
        chapters = "\n".join(
            f'      <li><button data-t="{i * 60}">'
            f'{_esc(p.get("title") or p.get("heading", ""))}</button></li>'
            for i, p in enumerate(parts))
        code = (f'<figure class="dsui-podcast" data-project="{PROJECT}" '
                f'data-slug="{_esc(slug)}">\n'
                f'  <figcaption>{title}</figcaption>\n'
                f'  <audio controls preload="metadata" src="{audio}"></audio>\n'
                f'  <nav class="dsui-chapters"><ol>\n{chapters}\n  </ol></nav>\n'
                f'</figure>')
        return {"form": "player", "mime": "text/html", "code": code}

    if form == "pip":
        main = _esc(media.get("video_url", f"/media/{slug}.mp4"))
        inset = _esc(media.get("inset_url", f"/media/{slug}-einspieler.mp4"))
        pos = media.get("inset_position", "bottom-right")
        size = int(media.get("inset_percent", 28))
        corner = {"bottom-right": "bottom:4%;right:4%",
                  "bottom-left": "bottom:4%;left:4%",
                  "top-right": "top:4%;right:4%",
                  "top-left": "top:4%;left:4%"}.get(pos, "bottom:4%;right:4%")
        code = (f'<div class="dsui-pip" data-project="{PROJECT}" '
                f'style="position:relative;max-width:100%">\n'
                f'  <video src="{main}" controls playsinline '
                f'style="width:100%;display:block;border-radius:12px"></video>\n'
                f'  <video src="{inset}" muted autoplay loop playsinline '
                f'style="position:absolute;{corner};width:{size}%;'
                f'border-radius:10px;box-shadow:0 6px 20px rgba(0,0,0,.45)"></video>\n'
                f'</div>')
        return {"form": "pip", "mime": "text/html", "code": code,
                "inset_position": pos, "inset_percent": size}

    body = "\n".join(
        f'  <section>\n    <h3>{_esc(p.get("title") or p.get("heading", ""))}</h3>\n'
        f'    <p>{_esc(p.get("text", ""))}</p>\n  </section>'
        for p in parts)
    code = (f'<article class="dsui-beitrag" data-project="{PROJECT}" '
            f'data-slug="{_esc(slug)}">\n  <h2>{title}</h2>\n{body}\n</article>')
    return {"form": "article", "mime": "text/html", "code": code}
