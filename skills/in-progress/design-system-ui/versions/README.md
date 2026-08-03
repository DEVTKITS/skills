# Versionen

Jede Version ist **eigenständig lauffähig**. Ordner kopieren genügt — es gibt
keine Verweise nach draußen, keine gemeinsamen Dateien, keine Installation.

```bash
cd versions/v3-vollbild
python3 server.py          # läuft sofort, nur Standardbibliothek
./test.sh                  # Selbsttest dieser Version
```

| Version | Was sie kann |
|---|---|
| **v1-minimal** | Erste wirklich lauffähige Fassung. Backend nur mit Standardbibliothek, Node-Canvas, `execute` und `evaluate`. |
| **v2-agenten** | Agenten-Sichtbarkeit: roter Monitorrand, Aktivitäts-Ticker, aufblinkende Elemente. Playwright-Demo, Chat, Stock-Anbindung. |
| **v3-vollbild** | Vollintegration: Hamburger-Menü, neun Oberflächen, Info-Buttons mit Wechseltipps, Ampelsystem, Vorlesen, Provider, Debug. Inklusive DOM-XSS-Fix. |
| **v4-openclaw** | „Mit OpenCLAW designen": die Agenten-Shell baut und optimiert den Workflow auf Zuruf, direkt aus der Oberfläche. |
| **v5-editionen** | Zwei Editionen aus einer Codebasis: Normal und OpenCLAW. |
| **v6-ultimate** | Alles zusammen: Content Studio (Playwright-Läufe als Bericht oder Podcast, einbettbar bis Video-im-Video), stufenlos einstellbare Oberfläche (22 Optionen), Skill-Import aus Git, C-Gerüst, Nanobot mit Agent-zu-Agent, OpenAI/GPT als Provider. |

Der jeweils aktuelle Stand liegt eine Ebene höher im Skill-Ordner und ist
immer die neueste Version.

## Eine neue Version einfrieren

```bash
./save-version.sh v6-name "Kurzbeschreibung, was neu ist"
```

Das kopiert den aktuellen Stand nach `versions/v6-name/` und legt eine
`VERSION.md` mit Datum und Commit dazu.

## Warum eigenständig statt Verweise

Eine archivierte Version, die auf gemeinsame Dateien zeigt, ändert sich mit —
und ist dann keine Archivierung mehr, sondern eine Illusion. Deshalb bringt
jede Version ihr komplettes Backend, ihre Oberfläche und ihre Tests mit. Der
Preis sind ein paar Kopien; der Gewinn ist, dass jede Version in einem Jahr
noch genau das tut, was sie am Tag des Einfrierens getan hat.
