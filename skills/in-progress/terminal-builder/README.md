# baukasten — Terminal-Builder-Hub

**DE** · Ein Terminal-Menü in C (keine Abhängigkeiten außer libc), das
Erstell-Arbeit — Präsentationen, Folien, Apps, Sites, Dashboards, Hubs,
Dokumente, Grafik — **komplett lokal ohne KI plant** und KI nur für
markierte Schritte über einen konfigurierbaren Konnektor anbindet:
MCP-Outbox, Webhook, Gateway-API oder Discord. Reines Frontend,
offline-first.

**EN** · A C terminal menu (libc only) that plans creation work fully
offline and routes only AI-flagged steps through a configurable connector
(MCP outbox, webhook, gateway API, Discord). Pure frontend, offline-first.

## Bauen / Build

```sh
make && ./baukasten
```

## Bedienung / Controls

| Taste | Wirkung |
|---|---|
| `↑ ↓ ← →` | bewegen — auch durch die Kacheln unten |
| `1–9` | Eintrag oder Kachel direkt wählen |
| `Enter` | öffnen / bestätigen |
| `Esc` | zurück |
| `v` | **Vorlesen** — Seite als Sprache (espeak/say/spd-say) |
| `n` | **Nanobot** — Fragen zur Seite, Builder steuern, `!` = an KI |
| `t` | Theme hell/dunkel |
| `k` | Kontrastmodus an/aus — an = tiefschwarz, Text grau, Neonrot |
| `l` | Sprache Deutsch/Englisch |
| `?` | Tasten-Hilfe |
| `q` | beenden |

## Ablauf / Flow

1. **Kachel wählen** (unten, 1–8): Präsentation, Folien, Apps, Sites,
   Dashboards, Hubs, Dokumente, Grafik — jede Kachel fächert nach innen
   in 4 Typen auf.
2. **Bedarfs-Check**: 4 Fragen (Zielgruppe, Umfang, Stil, KI-Anteil), damit
   genau evaluiert wird, was für das beste Ergebnis gebraucht wird.
3. **Arbeitsschritte**: Jeder Schritt verweist auf eine
   Skillbuilder-Vorlage und einen Skill — beides pro Schritt auswählbar
   (`Enter` = Skill tauschen, `g` = Vorlage). Schritte sind `lokal` oder
   `KI ⚡` markiert.
4. **Export**: `x` schreibt den Plan als ComfyUI-Workflow-Graph
   (`~/.config/baukasten/workflows/*.json`) — Knoten = vorgebaute Skills,
   KI-Knoten markiert. `s` schickt die KI-Schritte an den Konnektor.

## Skills & Profile

- 10 vorgebaute Skills (gliederung, texter, layout, bildgenerator, …),
  eigene Skills mit `a` anlegbar (lokal oder KI), Löschung mit `d`.
- Profile anlegen/aktivieren unter `p` — das aktive Profil wandert in
  jeden Export und jeden Versand.

## Konnektoren (nur für KI-Schritte)

| Typ | Transport |
|---|---|
| MCP | Outbox-Datei `~/.config/baukasten/outbox.jsonl` (ein Host-Prozess pollt sie) |
| Webhook | `curl` HTTP-POST, JSON-Payload |
| Gateway-API | HTTP-POST + `Authorization: Bearer <key>` |
| Discord | Webhook-POST mit `{"content": …}` |

Alles andere läuft ohne Netz und ohne KI auf dem eigenen Rechner.

## Freestyle — kein Code

`f` öffnen, frei beschreiben („pitch deck für investoren"), der Builder
ordnet die Beschreibung einer Kachel zu und startet den Bedarfs-Check.

## Konfiguration

Alles liegt unter `~/.config/baukasten/`: `config`, `profiles.txt`,
`skills.txt`, `workflows/`, `outbox.jsonl`.
