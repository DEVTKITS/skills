"""
Skill-Import aus Git — eigene Skills in Workflows einbinden.

Nimmt eine GitHub-/GitLab-URL (Repo, Blob oder Raw), holt die SKILL.md und
liest Name, Beschreibung und Aufrufbarkeit heraus. Importierte Skills lassen
sich als `~name` in Chat und Parametern referenzieren.

Absichtlich nur lesend: es wird nichts geklont, nichts ausgeführt, nichts
installiert. Der Import legt eine Beschreibung ab, kein Programm.

Nur Standardbibliothek.
"""

from __future__ import annotations

import json
import re
import urllib.error
import urllib.request
from pathlib import Path

STORE = Path(__file__).resolve().parent / "imported-skills.json"
TIMEOUT = 20
MAX_BYTES = 512 * 1024


def _raw_candidates(url: str) -> list[str]:
    """Aus einer beliebigen Git-URL mögliche Raw-Adressen der SKILL.md bauen."""
    u = url.strip().rstrip("/")
    out: list[str] = []

    # Bereits eine Raw-URL
    if "raw.githubusercontent.com" in u or "/-/raw/" in u:
        return [u] if u.endswith(".md") else [f"{u}/SKILL.md"]

    # github.com/owner/repo/blob/branch/pfad/SKILL.md
    m = re.match(r"https?://github\.com/([^/]+)/([^/]+)/blob/([^/]+)/(.+)", u)
    if m:
        owner, repo, branch, path = m.groups()
        return [f"https://raw.githubusercontent.com/{owner}/{repo}/{branch}/{path}"]

    # github.com/owner/repo/tree/branch/pfad
    m = re.match(r"https?://github\.com/([^/]+)/([^/]+)/tree/([^/]+)/(.+)", u)
    if m:
        owner, repo, branch, path = m.groups()
        return [f"https://raw.githubusercontent.com/{owner}/{repo}/{branch}/{path}/SKILL.md"]

    # github.com/owner/repo  → Standardzweige durchprobieren
    m = re.match(r"https?://github\.com/([^/]+)/([^/]+?)(?:\.git)?$", u)
    if m:
        owner, repo = m.groups()
        for branch in ("main", "master"):
            out.append(f"https://raw.githubusercontent.com/{owner}/{repo}/{branch}/SKILL.md")
        return out

    # GitLab blob
    m = re.match(r"https?://gitlab\.com/(.+)/-/blob/([^/]+)/(.+)", u)
    if m:
        proj, branch, path = m.groups()
        return [f"https://gitlab.com/{proj}/-/raw/{branch}/{path}"]

    return [u if u.endswith(".md") else f"{u}/SKILL.md"]


def _fetch(url: str) -> tuple[str | None, str | None]:
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "design-system-ui"})
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            return resp.read(MAX_BYTES).decode("utf-8", errors="replace"), None
    except urllib.error.HTTPError as e:
        return None, f"HTTP {e.code}"
    except (urllib.error.URLError, OSError, TimeoutError) as e:
        return None, str(e)


def parse_skill_md(text: str) -> dict:
    """Frontmatter und erste Überschrift auslesen."""
    meta: dict = {}
    body = text
    if text.startswith("---"):
        end = text.find("\n---", 3)
        if end > 0:
            for line in text[3:end].splitlines():
                if ":" in line:
                    k, v = line.split(":", 1)
                    meta[k.strip()] = v.strip()
            body = text[end + 4:]

    heading = ""
    for line in body.splitlines():
        if line.startswith("# "):
            heading = line[2:].strip()
            break

    return {
        "name": meta.get("name") or heading or "unbenannt",
        "description": meta.get("description", "").strip(),
        "user_invoked": str(meta.get("disable-model-invocation", "")).lower() == "true",
        "heading": heading,
        "excerpt": "\n".join(
            l for l in body.splitlines() if l.strip())[:600],
    }


def load_imported() -> dict:
    if STORE.is_file():
        try:
            return json.loads(STORE.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            pass
    return {"skills": []}


def _persist(data: dict) -> None:
    try:
        STORE.write_text(json.dumps(data, ensure_ascii=False, indent=2),
                         encoding="utf-8")
    except OSError:
        pass


def import_skill(url: str) -> dict:
    if not url or not url.strip():
        return {"ok": False, "error": "Bitte eine Git-URL angeben"}
    if not re.match(r"^https?://", url.strip()):
        return {"ok": False, "error": "Nur http(s)-URLs werden geholt"}

    tried: list[str] = []
    for candidate in _raw_candidates(url):
        tried.append(candidate)
        text, err = _fetch(candidate)
        if text is None:
            continue
        parsed = parse_skill_md(text)
        entry = {**parsed, "source": url.strip(), "raw_url": candidate,
                 "ref": f"~{parsed['name']}"}

        data = load_imported()
        data["skills"] = [s for s in data["skills"]
                          if s.get("raw_url") != candidate]
        data["skills"].append(entry)
        _persist(data)
        return {"ok": True, "skill": entry, "count": len(data["skills"])}

    return {"ok": False,
            "error": "Keine SKILL.md gefunden",
            "tried": tried,
            "hint": ("Direkte Raw-URL zur SKILL.md angeben, wenn das Repo "
                     "einen anderen Aufbau hat.")}


def remove_skill(ref: str) -> dict:
    data = load_imported()
    before = len(data["skills"])
    data["skills"] = [s for s in data["skills"]
                      if s.get("ref") != ref and s.get("name") != ref.lstrip("~")]
    _persist(data)
    return {"ok": True, "removed": before - len(data["skills"]),
            "count": len(data["skills"])}
