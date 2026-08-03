# C-Gerüst

Ein eigenständiger HTTP-Server in reinem C99 (POSIX-Sockets, pthreads) —
keine Bibliotheken außer der C-Standardbibliothek.

```bash
cc -O2 -std=c99 -pthread -o dsui-c server.c
PORT=8080 ./dsui-c
```

Für Umgebungen ohne Python. Liefert die Oberfläche aus und bedient die
Kernendpunkte:

| Endpunkt | |
|---|---|
| `GET /` `/app.js` `/styles.css` `/llms.txt` | die Single-Page-Oberfläche |
| `GET /health` | Antwort trägt `"engine":"c"` |
| `GET /api/edition` `/api/patterns` | Edition und Template-Katalog |
| `POST /api/workflow/execute` | topologische Ausführung mit Validierung |
| `POST /api/workflow/evaluate` | Bewertung mit Urteil `iterate`/`done` |
| `GET`/`POST` `/api/agent/activity` | Aktivitäts-Feed (roter Monitorrand) |

Die Ergebnisse sind mit `server.py` abgeglichen: gleiche Punktzahl, gleiches
Urteil, gleiche Knotenzahl, gleiche Fehlerbehandlung. Der Testlauf in
`../test.sh` prüft das bei jedem Durchlauf.

Provider-Aufrufe, Chat, Agent-zu-Agent, OpenCLAW, Hilfe- und Debugsystem
bleiben bewusst in `server.py` — der C-Teil ist das schlanke Gerüst, nicht
eine zweite Vollimplementierung.

## Grenzen

Der JSON-Leser ist bewusst minimal: er liest flache String-Felder und die
`nodes`/`edges`-Listen aus dem in `llms.txt` dokumentierten Schema. Für
beliebiges JSON ist er nicht gedacht. Eingaben sind längenbegrenzt
(`MAX_REQ`, `MAX_NODES`, `MAX_EDGES`), Ausgaben werden JSON-escaped.
