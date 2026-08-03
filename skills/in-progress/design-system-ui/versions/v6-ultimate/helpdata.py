"""
Help, guide and pattern registry.

Every interactive surface has a help topic: a title, a short guide, a list of
rotating tips (the info button cycles through them while pulsing), and where
useful a set of patterns/syntax/prefixes. Served at /api/help so that agents
read exactly the same guidance humans see in the popups.
"""

from __future__ import annotations

# --------------------------------------------------------------------------- #
# Syntax prefixes — a shared shorthand for humans, bots and the chat box      #
# --------------------------------------------------------------------------- #
PREFIXES = {
    "/": {"name": "command", "example": "/execute",
          "desc": "Run a builder command directly (execute, validate, clear, iterate)."},
    "@": {"name": "node reference", "example": "@llm1",
          "desc": "Reference a node by id in chat or a parameter value."},
    "#": {"name": "pattern / template", "example": "#rag-pipeline",
          "desc": "Load a workflow template by its pattern id."},
    "!": {"name": "provider call", "example": "!firecrawl https://example.com",
          "desc": "Invoke a connected provider inline (firecrawl, pexels, openrouter)."},
    "$": {"name": "variable", "example": "$INPUT",
          "desc": "Workflow variable resolved at execution time."},
    "~": {"name": "skill", "example": "~design-system-ui",
          "desc": "Reference a skill from the repo to insert into a workflow."},
}

# --------------------------------------------------------------------------- #
# Help topics — keyed by the data-help attribute on each info button          #
# --------------------------------------------------------------------------- #
TOPICS: dict[str, dict] = {
    "canvas": {
        "title": "Workflow Canvas",
        "guide": ("Der Arbeitsbereich für den Node-Graph. Ziehe Nodes aus dem "
                  "Nodes-Tab hierher, verbinde sie und führe den Workflow aus."),
        "tips": [
            "Klick wählt einen Node aus — Shift+Klick auf einen zweiten verbindet beide.",
            "Lade ein Template aus dem Templates-Tab als Startpunkt statt bei null zu beginnen.",
            "Jeder Workflow braucht mindestens einen Input- und einen Output-Node.",
            "Zoom mit + / − oben rechts, ⊡ setzt die Ansicht zurück.",
            "Import JSON lädt einen ComfyUI-kompatiblen Graphen von der Festplatte.",
        ],
        "patterns": ["#text-classifier", "#rag-pipeline", "#agent-loop", "#eval-loop"],
    },
    "nodes": {
        "title": "Node-Bibliothek",
        "guide": ("Bausteine des Workflows. Input-Nodes liefern Daten, "
                  "Processing-Nodes transformieren sie, Output-Nodes geben sie aus."),
        "tips": [
            "Nodes per Drag & Drop auf den Canvas ziehen.",
            "llm-node ist das Herz: Modell, Temperatur und Prompt einstellbar.",
            "transform und filter brauchen kein Modell — sie laufen deterministisch.",
            "Die Ausführungsreihenfolge ergibt sich topologisch aus den Kanten.",
            "Unbekannte Node-Typen brechen nicht ab, sie werden im Terminal gemeldet.",
        ],
        "patterns": ["@node-id", "$INPUT"],
    },
    "params": {
        "title": "Parameter",
        "guide": ("Feineinstellung des ausgewählten Nodes. „AI Suggest“ füllt "
                  "sinnvolle Defaults je Node-Typ."),
        "tips": [
            "Temperature 0.2–0.4 für Klassifikation, 0.7–0.9 für kreative Ausgaben.",
            "maxTokens begrenzt Kosten und Laufzeit — starte klein.",
            "Ein Node ohne Auswahl zeigt hier nichts: erst links im Canvas klicken.",
            "AI Suggest Parameters fragt den Server nach empfohlenen Werten.",
            "Parameter werden sofort im Workflow-JSON gespeichert.",
        ],
    },
    "patterns": {
        "title": "Templates & Patterns",
        "guide": ("Fertige Workflow-Muster. Laden, anpassen, ausführen — der "
                  "schnellste Weg zu einem lauffähigen Graphen."),
        "tips": [
            "Text Classifier: Input → LLM → Kategorie. Ideal für Triage.",
            "RAG Pipeline: Query → Retrieve → Generate. Für Wissensfragen.",
            "Agent Loop: Think → Act → Observe. Für mehrstufige Aufgaben.",
            "Eval Loop: Execute → Score → Iterate. Selbstoptimierend.",
            "In der Chat-Box lädt #rag-pipeline dasselbe Template per Text.",
        ],
        "patterns": ["#text-classifier", "#rag-pipeline", "#agent-loop", "#eval-loop"],
    },
    "chat": {
        "title": "Chat-Assistent",
        "guide": ("Sprich mit dem Builder. Der Chat routet zum gewählten "
                  "Provider — lokal via Ollama oder Cloud via OpenRouter, "
                  "NVIDIA, Hugging Face."),
        "tips": [
            "Präfixe sparen Tippen: /execute, #rag-pipeline, @llm1, !firecrawl <url>.",
            "Ohne konfigurierten Provider antwortet die Offline-Hilfe — gleiche Struktur.",
            "Der Provider-Wechsel oben gilt sofort für die nächste Nachricht.",
            "Die letzten Nachrichten gehen als Kontext mit, ältere werden gekürzt.",
            "Fragen wie „warum nur 50 Punkte?“ beantwortet der Assistent aus dem Verdict.",
        ],
        "patterns": list(PREFIXES.keys()),
    },
    "execution": {
        "title": "Ausführung",
        "guide": ("Validieren prüft die Struktur, Execute läuft den Graphen "
                  "topologisch ab und schreibt alles ins Terminal."),
        "tips": [
            "Validate vor Execute spart Fehlläufe — es nennt fehlende Nodes.",
            "Das Terminal zeigt jeden Node mit Typ und Parametern.",
            "Nach der Ausführung bewertet Gemma automatisch das Ergebnis.",
            "Rote Zeilen sind Fehler, gelbe Warnungen, grüne Erfolge.",
            "Der Statusbadge oben wechselt Ready → Running → Complete.",
        ],
        "patterns": ["/execute", "/validate"],
    },
    "evaluation": {
        "title": "Bewertung & Iteration",
        "guide": ("Gemma bewertet den Lauf von 0–100 und entscheidet: "
                  "iterate (nochmal eine Runde) oder done (fertig)."),
        "tips": [
            "Score ≥ 85 ⇒ verdict done, darunter ⇒ iterate mit Vorschlägen.",
            "„Apply All“ wendet alle Vorschläge an und führt automatisch neu aus.",
            "gemma_live: true heißt echtes Modell, false heißt Heuristik-Fallback.",
            "Starte `ollama serve` für echte Modellkritik statt Heuristik.",
            "Jeder Vorschlag nennt seine Aktion, z. B. ADD_NODE oder CONNECT_NODES.",
        ],
        "patterns": ["/iterate"],
    },
    "providers": {
        "title": "Provider & API-Verbindungen",
        "guide": ("Alle Anbieter an einem Ort: LLM-Clouds, Stock-Datenbanken, "
                  "Scraping. Ampel zeigt den Status, ein Klick erklärt die "
                  "Verbindung."),
        "tips": [
            "Grün = verbunden, Rot = kein Key. Key setzen, Server neu starten, fertig.",
            "OpenRouter bündelt 200+ Modelle inklusive kostenloser Varianten.",
            "NVIDIA NIM und Hugging Face sind OpenAI-kompatibel — gleicher Aufruf.",
            "Stock: Pexels und Pixabay liefern Fotos UND Videos, Unsplash Fotos.",
            "Eigene Anbieter via CUSTOM_PROVIDERS als JSON-Liste ergänzen.",
        ],
        "patterns": ["!openrouter", "!firecrawl", "!pexels"],
    },
    "stock": {
        "title": "Stock-Medien",
        "guide": ("Freie Bild- und Videodatenbanken direkt im Builder "
                  "durchsuchen und als Node-Input übernehmen."),
        "tips": [
            "Ein Provider gilt als verbunden, sobald sein API-Key gesetzt ist.",
            "PEXELS_API_KEY, UNSPLASH_ACCESS_KEY, PIXABAY_API_KEY — mehr braucht es nicht.",
            "Suchergebnisse kommen unverändert vom Anbieter zurück.",
            "Videos: bei Pexels und Pixabay media=videos wählen.",
            "In der Chat-Box: !pexels studio background sucht direkt.",
        ],
        "patterns": ["!pexels <query>", "!unsplash <query>", "!pixabay <query>"],
    },
    "github": {
        "title": "GitHub-Integration",
        "guide": ("Skills und Designs aus Repositories in Workflows ziehen — "
                  "und den eigenen Stand versionieren."),
        "tips": [
            "Status zeigt Branch, letzten Commit und offene Änderungen.",
            "Skills aus dem Repo lassen sich als Workflow-Node einfügen.",
            "~skill-name referenziert einen Skill in Chat und Parametern.",
            "Ohne Schreibrechte bleibt alles lokal — nichts geht ungefragt raus.",
            "Ein Bundle-Export sichert den Branch auch ohne Push-Recht.",
        ],
        "patterns": ["~<skill-name>"],
    },
    "agent": {
        "title": "Agenten-Sichtbarkeit",
        "guide": ("Wenn eine KI die Oberfläche bedient, leuchtet der "
                  "Monitorrand rot, ein Ticker nennt jede Aktion und das "
                  "geklickte Element blinkt auf."),
        "tips": [
            "Der rote Rand bedeutet: gerade arbeitet eine KI am Rechner.",
            "Der Ticker zeigt actor · action → target in Echtzeit.",
            "Jeder Agent meldet sich über POST /api/agent/activity.",
            "demo/agent_demo.mjs führt das komplett mit Playwright vor.",
            "Nach state=done verblasst der Rand automatisch.",
        ],
        "patterns": ["POST /api/agent/activity"],
    },
    "openclaw": {
        "title": "Mit OpenCLAW designen",
        "guide": ("OpenCLAW ist die Agenten-Shell, die den Workflow für dich "
                  "baut und optimiert. Auftrag beschreiben, starten, zusehen — "
                  "der Monitorrand leuchtet, solange sie arbeitet."),
        "tips": [
            "Schreibweise: OpenCLAW. Nicht „OpenCloud“, nicht „opencloude“, nicht Claude.",
            "Ohne installierte Shell fährt der Runner die Schleife selbst — sofort nutzbar.",
            "OPENCLAW_CMD auf die Shell zeigen lassen, dann übernimmt sie echt.",
            "Dasselbe funktioniert mit OpenHands oder OpenManus — nur die Variable ändern.",
            "Die Shell läuft nur während des Auftrags und wird danach beendet.",
        ],
        "patterns": ["POST /api/openclaw/run", "GET /api/openclaw/status"],
    },
    "studio": {
        "title": "Content Studio",
        "guide": ("Aus einem Playwright-Lauf einen Bericht oder ein "
                  "Podcast-Skript machen und als fertigen Block in Seiten, "
                  "Designs oder Videos einbetten. Ein DkZ-Projekt."),
        "tips": [
            "Der Inhalt kommt aus dem Aktivitätsprotokoll — nichts wird hinzugedichtet.",
            "Vier Formate: Laufbericht, Testzusammenfassung, Änderungsnotiz, Durchgang.",
            "Ohne verbundenes Modell entsteht der Bericht direkt aus den Ereignissen.",
            "„Video im Video“ legt einen Einspieler über die Bildschirmaufnahme — Position und Größe stufenlos.",
            "Erst einen Agenten laufen lassen, dann erzeugen: leeres Protokoll ergibt leeren Bericht.",
        ],
        "patterns": ["POST /api/content/report", "POST /api/content/embed"],
    },
    "settings": {
        "title": "Einstellungen",
        "guide": ("Die Oberfläche stufenlos anpassen: Farbe, Form, Schrift, "
                  "Verhalten und Arbeitsfläche. Änderungen wirken sofort und "
                  "werden gesichert."),
        "tips": [
            "Alle Regler wirken sofort — kein Speichern, kein Neuladen nötig.",
            "Der Farbton bestimmt die Akzentfarbe der gesamten Oberfläche.",
            "Dichte verkleinert oder vergrößert alle Abstände auf einmal.",
            "Animationen und blinkende Info-Buttons lassen sich ganz abschalten.",
            "„Zurücksetzen“ stellt alle 22 Einstellungen auf die Standardwerte.",
        ],
        "patterns": ["GET /api/settings/schema", "POST /api/settings"],
    },
    "debug": {
        "title": "Debug & Stresstest",
        "guide": ("Selbsttest prüft alle Endpunkte, der Stresstest fährt viele "
                  "Ausführungen parallel und misst Latenzen."),
        "tips": [
            "Selftest läuft in Sekunden und nennt jeden fehlerhaften Endpunkt.",
            "Stresstest: Anzahl der Läufe wählen, Ergebnis zeigt min/avg/max in ms.",
            "Fehlerhafte Läufe erscheinen separat mit Ursache.",
            "Nutze den Stresstest vor dem Ausrollen einer neuen Node-Art.",
            "./test.sh im Terminal prüft zusätzlich Go- und Rust-Clients.",
        ],
        "patterns": ["/selftest", "/stress <n>"],
    },
    "tts": {
        "title": "Vorlesen (TTS)",
        "guide": ("Einmal klicken markiert einen Textblock, zweimal klicken "
                  "liest ihn mit der Browser-Sprachausgabe vor."),
        "tips": [
            "Ein Klick markiert — der Block bekommt einen farbigen Rahmen.",
            "Doppelklick startet das Vorlesen, erneuter Doppelklick stoppt.",
            "Die Sprache folgt dem Text; Deutsch wird automatisch erkannt.",
            "Über das Menü lässt sich Vorlesen komplett abschalten.",
            "Funktioniert offline — die Stimme kommt vom Browser, nicht aus der Cloud.",
        ],
    },
    "menu": {
        "title": "Hauptmenü",
        "guide": ("Das Hamburger-Menü öffnet alle Oberflächen: Builder, Chat, "
                  "Skills, Provider, Stock, GitHub, Agenten, Debug und Docs."),
        "tips": [
            "Jede Oberfläche bleibt auf derselben Seite — kein Navigieren nötig.",
            "Die Ampel im Menü zeigt sofort, wie viele Provider verbunden sind.",
            "Tastatur: Esc schließt das Menü, Klick auf den Hintergrund ebenso.",
            "Agenten erreichen jede Oberfläche direkt über die HTTP-API.",
            "Docs öffnet die llms.txt — dieselbe Anleitung, die Bots lesen.",
        ],
    },
    "skills": {
        "title": "Skill- & Workflow-Builder",
        "guide": ("Aus einem funktionierenden Workflow wird ein wiederverwendbarer "
                  "Skill: Name, Beschreibung, Trigger — fertig zum Export."),
        "tips": [
            "Erst den Workflow zum Laufen bringen, dann als Skill sichern.",
            "Ein guter Skill-Name ist ein Verb plus Objekt, z. B. classify-tickets.",
            "Die Beschreibung entscheidet, wann ein Agent den Skill wählt.",
            "Export erzeugt SKILL.md plus workflow.json.",
            "~skill-name fügt einen bestehenden Skill in den Graphen ein.",
        ],
        "patterns": ["~<skill-name>"],
    },
}


def topic(key: str) -> dict:
    t = TOPICS.get(key)
    if not t:
        return {"error": f"unknown help topic '{key}'",
                "available": sorted(TOPICS)}
    return {"key": key, **t}


def all_help() -> dict:
    return {"topics": TOPICS, "prefixes": PREFIXES,
            "count": len(TOPICS)}
