---
name: terminal-builder
description: Build and run "baukasten" — a dependency-free C terminal builder hub (arrow/number-key TUI) that plans creation work (presentations, slides, apps, sites, dashboards, hubs, documents, graphics) fully offline and routes only AI-flagged steps through a connector WITH a return channel (Matrix client API, DkZ nanochat bridge, MCP outbox, webhook, gateway API, Discord — replies land in the nanobot). Includes an in-console AI chat (OpenCode :9900, Hermes via bridge, or active connector), terminal tools (SSH profiles, shell launcher with pager, read-only file navigator with fzf-style fuzzy filter, zsh/$SHELL launcher, fuzzy shell-history search, navi-style cheat sheets with placeholders), 4 DkZ-palette themes incl. Matrix green and Cosmo, matrix-rain intro, first-run setup, profiles, custom skills, ComfyUI workflow export, DE/EN, contrast mode, read-aloud, and a nanobot helper.
disable-model-invocation: true
---

# Terminal Builder (baukasten)

`baukasten` is a single-file C program (`baukasten.c`, libc only) — a pure
frontend that works offline-first: every planning step happens locally, and
only steps explicitly marked as AI go out through the connector the user
configured. It is **not** a clone of any hosted design builder; it is a
keyboard-driven terminal hub.

## Build & run

```sh
cd skills/in-progress/terminal-builder
make            # cc -O2 -Wall -Wextra -o baukasten baukasten.c
./baukasten
```

Optional runtime helpers (both degrade gracefully when missing):

- `curl` — only needed to POST AI steps to webhook/gateway/Discord connectors
- `espeak` / `espeak-ng` / `say` / `spd-say` — only needed for the read-aloud button

## What it does

- **Home hub** — menu on top, 8 tiles at the bottom (Präsentation, Folien,
  Apps, Sites, Dashboards, Hubs, Dokumente, Grafik). Tiles fan out into
  subtypes, then a 4-question needs check (audience, scope, style, AI share)
  so the generated plan matches what is actually needed.
- **Work-step plan** — each step references a skillbuilder template and a
  skill; both are swappable per step. Steps are tagged `local` or `AI ⚡`.
- **Skills** — 10 prebuilt skills plus user-defined ones (name, description,
  local/AI), persisted in `~/.config/baukasten/skills.txt`.
- **Profiles** — creatable/selectable, stamped into every export and send.
- **Connectors** — MCP (outbox file `outbox.jsonl`), webhook, gateway API
  (Bearer key), Discord webhook. Only AI steps use them.
- **Workflows** — every plan exports as a ComfyUI-style node graph
  (`~/.config/baukasten/workflows/*.json`), nodes = prebuilt skills, AI nodes
  flagged.
- **Freestyle** — no code: type a free description, it maps to a tile and
  starts the needs check.
- **Nanobot** — press `n` anywhere: ask about the current page, steer the
  builder ("erstelle präsentation"), or prefix `!` to send the question to
  the AI connector.
- **Accessibility** — light/dark theme, DE/EN, contrast mode (always deep
  black, gray text, neon-red headings/highlights), read-aloud button `[v]`
  on every page.

## Keys

Arrows move (also across tiles), `1–9` select, `Enter` opens, `Esc` back,
`v` read aloud, `n` nanobot, `t` theme, `k` contrast, `l` language,
`?` help, `q` quit.

## Status

Draft. Candidate next steps: incoming-message handling (poll connector
responses into the nanobot), MCP stdio transport instead of the outbox file,
per-category template packs.
