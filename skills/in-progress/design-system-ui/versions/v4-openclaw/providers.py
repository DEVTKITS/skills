"""
Unified provider registry — AI inference, media/stock, and scraping.

Standard library only. A provider is "connected" the moment its API key
environment variable is set; local providers (Ollama) are probed live.
Every provider reports an Ampel (traffic-light) status:

    green  — connected and reachable
    yellow — configured but unverified / degraded / needs a probe
    red    — not configured

Custom providers can be added without code: set
    CUSTOM_PROVIDERS='[{"id":"my-llm","name":"My LLM","kind":"llm",
                        "env":"MY_LLM_KEY","base_url":"https://..."}]'
"""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.parse
import urllib.request

TIMEOUT_S = float(os.environ.get("PROVIDER_TIMEOUT", "20"))

# --------------------------------------------------------------------------- #
# Provider catalogue                                                          #
# --------------------------------------------------------------------------- #
# kind: llm | image | video | stock | scrape | local
PROVIDERS: dict[str, dict] = {
    # ---- LLM / inference clouds ----
    "openrouter": {
        "name": "OpenRouter",
        "kind": "llm",
        "env": "OPENROUTER_API_KEY",
        "base_url": "https://openrouter.ai/api/v1",
        "chat_path": "/chat/completions",
        "auth": "bearer",
        "default_model": "meta-llama/llama-3.1-8b-instruct:free",
        "docs": "https://openrouter.ai/docs",
        "note": "Gateway to 200+ models incl. free tiers",
    },
    "nvidia": {
        "name": "NVIDIA NIM Cloud",
        "kind": "llm",
        "env": "NVIDIA_API_KEY",
        "base_url": "https://integrate.api.nvidia.com/v1",
        "chat_path": "/chat/completions",
        "auth": "bearer",
        "default_model": "meta/llama-3.1-8b-instruct",
        "docs": "https://build.nvidia.com",
        "note": "GPU-hosted OpenAI-compatible endpoints",
    },
    "huggingface": {
        "name": "Hugging Face",
        "kind": "llm",
        "env": "HF_TOKEN",
        "base_url": "https://router.huggingface.co/v1",
        "chat_path": "/chat/completions",
        "auth": "bearer",
        "default_model": "google/gemma-2-9b-it",
        "docs": "https://huggingface.co/docs/inference-providers",
        "note": "Inference providers router, OpenAI-compatible",
    },
    "ollama": {
        "name": "Ollama (local)",
        "kind": "local",
        "env": None,  # no key — probed live
        "base_url": os.environ.get("OLLAMA_URL", "http://localhost:11434"),
        "chat_path": "/v1/chat/completions",
        "auth": None,
        "default_model": os.environ.get("GEMMA_MODEL", "gemma:12b"),
        "docs": "https://ollama.com",
        "note": "Runs Gemma locally; powers the evaluator when up",
    },
    # ---- Scraping / research ----
    "firecrawl": {
        "name": "Firecrawl",
        "kind": "scrape",
        "env": "FIRECRAWL_API_KEY",
        "base_url": "https://api.firecrawl.dev/v1",
        "auth": "bearer",
        "docs": "https://docs.firecrawl.dev",
        "note": "URL → clean markdown for workflow inputs",
    },
    # ---- Stock media (free tiers) ----
    "pexels": {
        "name": "Pexels",
        "kind": "stock",
        "env": "PEXELS_API_KEY",
        "base_url": "https://api.pexels.com/v1",
        "auth": "raw",  # bare key in Authorization
        "media": "photos+videos",
        "docs": "https://www.pexels.com/api/",
        "note": "Free photos and videos",
    },
    "unsplash": {
        "name": "Unsplash",
        "kind": "stock",
        "env": "UNSPLASH_ACCESS_KEY",
        "base_url": "https://api.unsplash.com",
        "auth": "client-id",
        "media": "photos",
        "docs": "https://unsplash.com/developers",
        "note": "Free high-resolution photos",
    },
    "pixabay": {
        "name": "Pixabay",
        "kind": "stock",
        "env": "PIXABAY_API_KEY",
        "base_url": "https://pixabay.com/api",
        "auth": "query",  # key travels as ?key=
        "media": "photos+videos",
        "docs": "https://pixabay.com/api/docs/",
        "note": "Free photos, illustrations and videos",
    },
    # ---- Image generation ----
    "stability": {
        "name": "Stability AI",
        "kind": "image",
        "env": "STABILITY_API_KEY",
        "base_url": "https://api.stability.ai/v2beta",
        "auth": "bearer",
        "docs": "https://platform.stability.ai/docs",
        "note": "Image generation for design assets",
    },
    "replicate": {
        "name": "Replicate",
        "kind": "image",
        "env": "REPLICATE_API_TOKEN",
        "base_url": "https://api.replicate.com/v1",
        "auth": "bearer",
        "docs": "https://replicate.com/docs",
        "note": "Hosted image and video models",
    },
}


def _custom_providers() -> dict[str, dict]:
    """User-defined providers from the CUSTOM_PROVIDERS env var (JSON list)."""
    raw = os.environ.get("CUSTOM_PROVIDERS", "").strip()
    if not raw:
        return {}
    try:
        entries = json.loads(raw)
    except json.JSONDecodeError:
        return {}
    out: dict[str, dict] = {}
    for e in entries if isinstance(entries, list) else []:
        pid = str(e.get("id", "")).strip()
        if not pid:
            continue
        out[pid] = {
            "name": e.get("name", pid),
            "kind": e.get("kind", "llm"),
            "env": e.get("env"),
            "base_url": e.get("base_url", ""),
            "chat_path": e.get("chat_path", "/chat/completions"),
            "auth": e.get("auth", "bearer"),
            "default_model": e.get("default_model", ""),
            "docs": e.get("docs", ""),
            "note": "User-defined via CUSTOM_PROVIDERS",
            "custom": True,
        }
    return out


def all_providers() -> dict[str, dict]:
    merged = dict(PROVIDERS)
    merged.update(_custom_providers())
    return merged


# --------------------------------------------------------------------------- #
# Ampel status                                                                #
# --------------------------------------------------------------------------- #
def _probe_ollama(base_url: str) -> bool:
    try:
        with urllib.request.urlopen(f"{base_url}/api/tags", timeout=2) as r:
            return r.status == 200
    except (urllib.error.URLError, OSError, TimeoutError):
        return False


def status(probe_local: bool = True) -> dict:
    """Ampel status for every provider: green / yellow / red."""
    out: dict[str, dict] = {}
    for pid, cfg in all_providers().items():
        env_var = cfg.get("env")
        if env_var is None:  # local, probe it
            up = _probe_ollama(cfg["base_url"]) if probe_local else False
            ampel, state = ("green", "reachable") if up else ("red", "not running")
        elif os.environ.get(env_var):
            ampel, state = "green", "key configured"
        else:
            ampel, state = "red", "no key"
        out[pid] = {
            "id": pid,
            "name": cfg["name"],
            "kind": cfg["kind"],
            "ampel": ampel,
            "state": state,
            "env_var": env_var,
            "connect_hint": (f"set {env_var} and restart — that is the whole "
                             "1-click setup") if env_var else
                            "start `ollama serve` locally",
            "docs": cfg.get("docs", ""),
            "note": cfg.get("note", ""),
            "media": cfg.get("media"),
            "custom": cfg.get("custom", False),
        }
    return out


def summary() -> dict:
    st = status()
    green = sum(1 for p in st.values() if p["ampel"] == "green")
    return {"total": len(st), "connected": green,
            "ampel": "green" if green else "red",
            "by_kind": {k: [p["id"] for p in st.values() if p["kind"] == k]
                        for k in sorted({p["kind"] for p in st.values()})}}


# --------------------------------------------------------------------------- #
# Calls                                                                       #
# --------------------------------------------------------------------------- #
def _auth_headers(cfg: dict, key: str) -> dict:
    auth = cfg.get("auth")
    if auth == "bearer":
        return {"Authorization": f"Bearer {key}"}
    if auth == "raw":
        return {"Authorization": key}
    if auth == "client-id":
        return {"Authorization": f"Client-ID {key}"}
    return {}


def _request(url: str, headers: dict, payload: dict | None = None) -> dict:
    data = json.dumps(payload).encode() if payload is not None else None
    headers = {"Content-Type": "application/json", **headers}
    try:
        req = urllib.request.Request(url, data=data, headers=headers)
        with urllib.request.urlopen(req, timeout=TIMEOUT_S) as resp:
            return {"ok": True, "data": json.loads(resp.read() or b"{}")}
    except urllib.error.HTTPError as e:
        return {"ok": False, "error": f"HTTP {e.code}",
                "body": e.read()[:500].decode(errors="replace")}
    except (urllib.error.URLError, OSError, TimeoutError, json.JSONDecodeError) as e:
        return {"ok": False, "error": str(e)}


def chat(provider: str, message: str, history: list[dict] | None = None,
         model: str | None = None) -> dict:
    """OpenAI-compatible chat against any configured LLM provider."""
    cfg = all_providers().get(provider)
    if not cfg:
        return {"ok": False, "error": f"unknown provider '{provider}'"}
    if cfg["kind"] not in ("llm", "local"):
        return {"ok": False, "error": f"'{provider}' is not a chat provider"}

    key = os.environ.get(cfg["env"], "") if cfg.get("env") else ""
    if cfg.get("env") and not key:
        return {"ok": False, "error": f"{provider} not connected",
                "connect": f"set {cfg['env']}"}

    messages = [{"role": t.get("role", "user"), "content": t.get("content", "")}
                for t in (history or [])[-8:]]
    messages.append({"role": "user", "content": message})

    url = cfg["base_url"].rstrip("/") + cfg.get("chat_path", "/chat/completions")
    res = _request(url, _auth_headers(cfg, key),
                   {"model": model or cfg.get("default_model"),
                    "messages": messages, "stream": False})
    if not res["ok"]:
        return res
    try:
        reply = res["data"]["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError):
        return {"ok": False, "error": "unexpected response shape",
                "raw": str(res["data"])[:500]}
    return {"ok": True, "reply": reply, "provider": provider,
            "model": model or cfg.get("default_model")}


def scrape(url: str, formats: list[str] | None = None) -> dict:
    """Firecrawl: turn a URL into clean markdown for workflow input."""
    cfg = all_providers()["firecrawl"]
    key = os.environ.get(cfg["env"], "")
    if not key:
        return {"ok": False, "error": "firecrawl not connected",
                "connect": f"set {cfg['env']}"}
    return _request(cfg["base_url"] + "/scrape", _auth_headers(cfg, key),
                    {"url": url, "formats": formats or ["markdown"]})


def stock_search(provider: str, query: str, per_page: int = 12,
                 media: str = "photos") -> dict:
    """Search a stock provider for photos or videos."""
    cfg = all_providers().get(provider)
    if not cfg or cfg["kind"] != "stock":
        return {"ok": False, "error": f"'{provider}' is not a stock provider",
                "available": [p for p, c in all_providers().items()
                              if c["kind"] == "stock"]}
    key = os.environ.get(cfg["env"], "")
    if not key:
        return {"ok": False, "error": f"{provider} not connected",
                "connect": f"set {cfg['env']} and restart — 1-click setup"}

    q = urllib.parse.quote(query)
    if provider == "pexels":
        seg = "videos/search" if media == "videos" else "search"
        base = "https://api.pexels.com/videos" if media == "videos" else cfg["base_url"]
        url = f"{base}/{seg.split('/')[-1]}?query={q}&per_page={per_page}"
    elif provider == "unsplash":
        url = f"{cfg['base_url']}/search/photos?query={q}&per_page={per_page}"
    elif provider == "pixabay":
        endpoint = "videos/" if media == "videos" else ""
        url = f"https://pixabay.com/api/{endpoint}?key={key}&q={q}&per_page={per_page}"
    else:
        return {"ok": False, "error": "unsupported stock provider"}

    headers = {} if cfg["auth"] == "query" else _auth_headers(cfg, key)
    res = _request(url, headers)
    if res["ok"]:
        res["provider"] = provider
        res["query"] = query
    return res
