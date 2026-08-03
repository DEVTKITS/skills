/* ------------------------------------------------------------------ */
/* i18n                                                                */
/* ------------------------------------------------------------------ */

static const char *L(const char *de, const char *en) { return A.lang ? en : de; }

static const S2 cat_names[NCAT] = {
    {"Präsentation", "Presentation"}, {"Folien", "Slides"},
    {"Apps", "Apps"},                 {"Sites", "Sites"},
    {"Dashboards", "Dashboards"},     {"Hubs", "Hubs"},
    {"Dokumente", "Documents"},       {"Grafik", "Graphics"},
};

static const S2 sub_names[NCAT][NSUB] = {
    { {"Pitch-Deck","Pitch deck"}, {"Schulung","Training"},
      {"Konferenz-Talk","Conference talk"}, {"Produkt-Demo","Product demo"} },
    { {"Einzelfolie","Single slide"}, {"Foliensatz","Slide set"},
      {"Vorlagen-Set","Template set"}, {"Infografik-Folie","Infographic slide"} },
    { {"CLI-Tool","CLI tool"}, {"Desktop-App","Desktop app"},
      {"Mobile-App","Mobile app"}, {"Mini-Tool","Mini tool"} },
    { {"Landingpage","Landing page"}, {"Portfolio","Portfolio"},
      {"Blog","Blog"}, {"Shop","Shop"} },
    { {"KPI-Board","KPI board"}, {"Monitoring","Monitoring"},
      {"Analytics","Analytics"}, {"Admin-Panel","Admin panel"} },
    { {"Wissens-Hub","Knowledge hub"}, {"Team-Hub","Team hub"},
      {"Projekt-Hub","Project hub"}, {"Doku-Hub","Docs hub"} },
    { {"Bericht","Report"}, {"Handbuch","Manual"},
      {"Whitepaper","Whitepaper"}, {"Brief","Letter"} },
    { {"Logo","Logo"}, {"Diagramm","Diagram"},
      {"Social-Post","Social post"}, {"Illustration","Illustration"} },
};

static const S2 q_names[NQ] = {
    {"Zielgruppe?", "Audience?"},
    {"Umfang?", "Scope?"},
    {"Stil?", "Style?"},
    {"KI-Anteil?", "AI share?"},
};

static const S2 q_opts[NQ][NOPT] = {
    { {"Intern","Internal"}, {"Kunden","Clients"}, {"Öffentlich","Public"} },
    { {"Klein","Small"}, {"Mittel","Medium"}, {"Groß","Large"} },
    { {"Nüchtern","Sober"}, {"Modern","Modern"}, {"Verspielt","Playful"} },
    { {"Keine KI","No AI"}, {"Text-KI","Text AI"}, {"Text+Bild-KI","Text+image AI"} },
};

static const S2 tmpl_names[4] = {
    {"Skillbuilder: Minimal", "Skillbuilder: Minimal"},
    {"Skillbuilder: Standard", "Skillbuilder: Standard"},
    {"Skillbuilder: Ausführlich", "Skillbuilder: Extended"},
    {"Eigene Vorlage", "Custom template"},
};

static const S2 conn_names[NCONN] = {
    {"Kein Konnektor", "No connector"},
    {"MCP (Outbox-Datei)", "MCP (outbox file)"},
    {"Webhook (HTTP POST)", "Webhook (HTTP POST)"},
    {"Gateway-API (POST + Key)", "Gateway API (POST + key)"},
    {"Discord (Webhook)", "Discord (webhook)"},
    {"Matrix (Client-Server-API)", "Matrix (client-server API)"},
    {"DkZ NanoChat-Bridge (HTTP)", "DkZ nanochat bridge (HTTP)"},
};

static const S2 theme_names[NTHEME] = {
    {"Hell", "Light"}, {"Dunkel", "Dark"}, {"Matrix", "Matrix"}, {"Cosmo", "Cosmo"},
};

/* eingebaute Skills: Index-Konstanten für die Planerzeugung */
enum { SK_OUTLINE, SK_COPY, SK_LAYOUT, SK_ASSET, SK_IMGGEN,
       SK_DIAGRAM, SK_PDF, SK_HTML, SK_QA, SK_TRANS, SK_BUILTIN_N };

static const SkillDef builtin_skills[SK_BUILTIN_N] = {
    {"gliederung",    "Struktur & Outline erzeugen",          1, 1},
    {"texter",        "Copywriting / Textentwurf",            1, 1},
    {"layout",        "Layout-Raster & Typografie (lokal)",   0, 1},
    {"asset-sammler", "Vorhandene Assets einsammeln (lokal)", 0, 1},
    {"bildgenerator", "Bilder per KI erzeugen",               1, 1},
    {"diagramm",      "Diagramme aus Daten (lokal)",          0, 1},
    {"export-pdf",    "Export als PDF (lokal)",               0, 1},
    {"export-html",   "Export als HTML (lokal)",              0, 1},
    {"qa-check",      "Qualitäts- & Konsistenz-Check (lokal)",0, 1},
    {"uebersetzer",   "Übersetzung DE/EN",                    1, 1},
};

/* Hauptmenü auf dem Home-Screen (über den Kacheln) */
#define NMENU 8
static const S2 menu_names[NMENU] = {
    {"Workflows (ComfyUI-Export)", "Workflows (ComfyUI export)"},
    {"Skills verwalten", "Manage skills"},
    {"Profile", "Profiles"},
    {"Verbindungen (MCP/Matrix/Bridge/Webhook/Discord)",
     "Connections (MCP/Matrix/bridge/webhook/Discord)"},
    {"Einstellungen", "Settings"},
    {"Freestyle (kein Code, freie Eingabe)", "Freestyle (no code, free input)"},
    {"Terminal-Werkzeuge (SSH/Shell/fzf/navi/Dateien)",
     "Terminal tools (SSH/shell/fzf/navi/files)"},
    {"KI-Chat (OpenCode/Hermes/Konnektor)", "AI chat (OpenCode/Hermes/connector)"},
};
static const char menu_keys[NMENU] = { 'w', 's', 'p', 'c', 'e', 'f', 'z', 'i' };

