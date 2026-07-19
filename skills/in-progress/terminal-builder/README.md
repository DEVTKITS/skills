# baukasten — Terminal-Builder-Hub

**DE** · Ein Terminal-Menü in C (keine Abhängigkeiten außer libc), das
Erstell-Arbeit — Präsentationen, Folien, Apps, Sites, Dashboards, Hubs,
Dokumente, Grafik — **komplett lokal ohne KI plant** und KI nur für
markierte Schritte über einen Konnektor anbindet — **mit Rückkanal**:
Antworten landen direkt im Nanobot. Konnektoren: Matrix (Client-API),
DkZ NanoChat-Bridge, MCP-Outbox, Webhook, Gateway-API, Discord.

**EN** · A C terminal menu (libc only) that plans creation work fully
offline and routes only AI-flagged steps through a connector — with a
return channel: replies land in the nanobot. Connectors: Matrix
(client API), DkZ nanochat bridge, MCP outbox, webhook, gateway API,
Discord.

## Bauen / Build

```sh
make && ./baukasten
```

Beim ersten Start läuft die **Ersteinrichtung**: Sprache → Theme (mit
Live-Vorschau in Echtfarben) → Konnektor, danach das Matrix-Rain-Intro.
Jederzeit wiederholbar unter Einstellungen.

## Bedienung / Controls

| Taste | Wirkung |
|---|---|
| `↑ ↓ ← →` | bewegen — auch durch die Kacheln unten |
| `1–9` | Eintrag oder Kachel direkt wählen |
| `Enter` / `Esc` | öffnen · zurück |
| `v` | **Vorlesen** — Seite als Sprache (espeak/say/spd-say) |
| `n` | **Nanobot** — Fragen zur Seite, Builder steuern, `!` = an KI |
| `t` | Theme: **Hell → Dunkel → Matrix → Cosmo** |
| `k` | Kontrastmodus an/aus — an = tiefschwarz, Text grau, DkZ-Neonrot `#fa1e4e` |
| `l` | Sprache Deutsch/Englisch |
| `m` | **Matrix-Regen** (cmatrix-Stil, jede Taste beendet) |
| `z` | **Terminal-Werkzeuge**: SSH, Shell, fzf, navi, Dateien |
| `i` | **KI-Chat** in der Konsole (OpenCode/Hermes/Konnektor) |
| `?` / `q` | Tasten-Hilfe · beenden |

Themes nutzen das **DkZ-Farbschema**: Matrix = Grün `#00D084` auf
Schwarz, Cosmo = Soft White `#f6f6f7` auf Blauschwarz mit Blau `#55ACEE`
und Pink `#ec4899` für KI-Marker. Jeder Frame ist in DEC-2026
Synchronized Output geklammert (kein Tearing).

## Ablauf / Flow

1. **Kachel wählen** (unten, 1–8) — jede fächert in 4 Typen auf.
2. **Bedarfs-Check**: 4 Fragen (Zielgruppe, Umfang, Stil, KI-Anteil).
3. **Arbeitsschritte**: pro Schritt Skillbuilder-Vorlage + Skill wählbar
   (`Enter` = Skill, `g` = Vorlage); Schritte sind `lokal` oder `KI ⚡`.
4. **Export**: `x` = ComfyUI-Workflow-Graph, `s` = KI-Schritte senden.

## KI-Konnektoren — mit Rückkanal

| Typ | Transport | Antwort |
|---|---|---|
| **Matrix (direkt)** | `PUT …/send/m.room.message` an den Homeserver (Token + Raum-ID, Env-Fallback `MATRIX_ACCESS_TOKEN`/`MATRIX_ROOM`) | pollt den Raum ~30 s, Antwort erscheint im Nanobot (Esc bricht ab) |
| **DkZ NanoChat-Bridge** | HTTP-POST an `http://localhost:3050` (änderbar), Payload im DkZ-Format `agent/status/befunde/naechste_aktion` | Response-Body → Nanobot |
| Webhook / Gateway / Discord | HTTP-POST via curl (Gateway mit Bearer-Key) | Response-Body → Nanobot |
| MCP | Outbox-Datei `outbox.jsonl` (Host-Prozess pollt) | — |

Einrichtung unter Verbindungen (`c`): `u` = URL/Homeserver, `y` =
Key/Token, `r` = Matrix-Raum, `b` = Bridge-URL, `p` = Test-Ping.
Alles außer KI-Schritten läuft ohne Netz.

## Terminal-Werkzeuge (`z`)

- **SSH-Profile** — `a` anlegen (`name`, `user@host`, Port), Enter
  verbindet (TUI pausiert sauber, danach zurück). Ziele sind auf
  `A–Z a–z 0–9 @ . _ -` beschränkt.
- **Shell-Launcher** — Befehl tippen, Ausgabe im scrollbaren Pager
  (↑↓, Bild↑↓, `g`/`G`); `!` am Ende = interaktiv ausführen.
- **Datei-Navigator** — read-only ab `$HOME`: Enter = rein, Esc/← =
  hoch, `/` = **Fuzzy-Filter (fzf-Stil)**, Enter auf Datei übernimmt
  den Pfad in Meldungszeile + Nanobot.
- **Shell öffnen** — startet `$SHELL` (bzw. zsh/bash) interaktiv.
- **Befehls-Historie (fzf-Stil)** — durchsucht `~/.zsh_history` (auch
  Extended-Format) und `~/.bash_history` fuzzy, Enter führt aus.
- **Cheat-Sheets (navi-Stil)** — `~/.config/baukasten/cheats.txt`
  (`beschreibung|befehl`), `<platzhalter>` werden vor dem Ausführen
  abgefragt; Startpaket wird automatisch angelegt.

## KI-Chat (`i`)

Vollwertiger Chat im Terminal — Verlauf, Blättern, Ziel-Umschaltung mit
`o`: **OpenCode** (`http://localhost:9900`, Payload
`{"agent":"baukasten","message":…}`), **Hermes** über die NanoChat-Bridge
(`{"agent":"baukasten","model":"hermes","message":…}`) oder der aktive
Konnektor (inkl. Matrix mit Antwort-Polling). Antworten erscheinen als
`KI >`-Zeilen im Verlauf.

## Skills & Profile

10 vorgebaute Skills + eigene (`a`, lokal oder KI); Profile anlegen/
aktivieren unter `p` — das aktive Profil wandert in jeden Export und
Versand.

## Freestyle — kein Code

`f`: frei beschreiben („pitch deck für investoren"), der Builder ordnet
die Beschreibung einer Kachel zu und startet den Bedarfs-Check.

## Konfiguration

Alles unter `~/.config/baukasten/`: `config` (u. a. `theme`, `mhs`,
`mtoken`, `mroom`, `bridge`, `opencode`), `profiles.txt`, `skills.txt`,
`ssh.txt`, `cheats.txt`, `workflows/`, `outbox.jsonl`.
