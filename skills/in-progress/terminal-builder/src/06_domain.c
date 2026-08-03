/* ------------------------------------------------------------------ */
/* Planerzeugung & Workflow-Export                                     */
/* ------------------------------------------------------------------ */

static void add_step(const char *de, const char *en, int skill, int ai) {
    if (A.nst >= MAXSTEP) return;
    Step *s = &A.st[A.nst++];
    s->label.de = de; s->label.en = en;
    s->skill = skill; s->ai = ai; s->tmpl = 1;
}

static void gen_plan(void) {
    A.nst = 0;
    int ki = A.ans[3];             /* 0 keine, 1 Text, 2 Text+Bild */
    int export_skill = (A.cat >= 2 && A.cat <= 5) ? SK_HTML : SK_PDF;

    add_step("Briefing & Ziele festlegen", "Define briefing & goals", -1, 0);
    add_step("Struktur & Gliederung", "Structure & outline", SK_OUTLINE, ki >= 1);
    add_step("Inhalte & Texte", "Content & copy", SK_COPY, ki >= 1);
    add_step("Design & Layout", "Design & layout", SK_LAYOUT, 0);
    if (A.ans[1] >= 1) {           /* ab mittlerem Umfang eigener Asset-Schritt */
        if (ki == 2) add_step("Bilder & Assets (KI)", "Images & assets (AI)", SK_IMGGEN, 1);
        else add_step("Bilder & Assets", "Images & assets", SK_ASSET, 0);
    }
    if (A.cat == 4 || A.cat == 7)  /* Dashboards & Grafik: Diagramme */
        add_step("Diagramme & Daten", "Diagrams & data", SK_DIAGRAM, 0);
    if (A.ans[0] == 2 && ki >= 1)  /* öffentlich + KI: Übersetzung */
        add_step("Übersetzung DE/EN", "Translation DE/EN", SK_TRANS, 1);
    add_step("Qualitäts-Check", "Quality check", SK_QA, 0);
    add_step("Export & Übergabe", "Export & handover", export_skill, 0);
}

static int export_workflow(char *outpath, size_t cap) {
    char name[120];
    snprintf(name, sizeof name, "%s-%s-%ld.json",
             cat_names[A.cat].en, sub_names[A.cat][A.sub].en, (long)time(NULL));
    for (char *p = name; *p; p++) {
        if (*p == ' ') *p = '_';
        *p = (char)tolower((unsigned char)*p);
    }
    snprintf(outpath, cap, "%s/workflows/%s", cfgdir, name);
    FILE *f = fopen(outpath, "w");
    if (!f) return 0;
    fprintf(f, "{\n  \"app\": \"baukasten\",\n  \"format\": \"comfyui-graph\",\n");
    fprintf(f, "  \"project\": {\"category\": \"%s\", \"type\": \"%s\", \"profile\": \"%s\",\n",
            cat_names[A.cat].en, sub_names[A.cat][A.sub].en, A.prof[A.aprof]);
    fprintf(f, "    \"audience\": \"%s\", \"scope\": \"%s\", \"style\": \"%s\", \"ai_share\": \"%s\"},\n",
            q_opts[0][A.ans[0]].en, q_opts[1][A.ans[1]].en,
            q_opts[2][A.ans[2]].en, q_opts[3][A.ans[3]].en);
    fprintf(f, "  \"nodes\": {\n");
    for (int i = 0; i < A.nst; i++) {
        Step *s = &A.st[i];
        const char *skname = s->skill >= 0 ? A.sk[s->skill].name : "manual";
        fprintf(f, "    \"%d\": {\"class_type\": \"skill/%s\", \"_meta\": "
                   "{\"title\": \"%s\", \"template\": \"%s\", \"ai\": %s}",
                i + 1, skname, s->label.en, tmpl_names[s->tmpl].en,
                s->ai ? "true" : "false");
        if (i > 0)
            fprintf(f, ", \"inputs\": {\"prev\": [\"%d\", 0]}", i);
        fprintf(f, "}%s\n", i + 1 < A.nst ? "," : "");
    }
    fprintf(f, "  }\n}\n");
    fclose(f);
    return 1;
}

/* Workflows-Ordner einlesen */
static int list_workflows(char names[][256], int max) {
    char p[600]; snprintf(p, sizeof p, "%s/workflows", cfgdir);
    DIR *d = opendir(p); if (!d) return 0;
    int n = 0; struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.') continue;
        snprintf(names[n], 256, "%s", e->d_name);
        n++;
    }
    closedir(d);
    return n;
}

/* ------------------------------------------------------------------ */
/* Nanobot                                                             */
/* ------------------------------------------------------------------ */

static void nano_add(const char *prefix, const char *text) {
    if (A.nhn >= 8) {
        memmove(A.nh[0], A.nh[1], sizeof A.nh - sizeof A.nh[0]);
        A.nhn = 7;
    }
    snprintf(A.nh[A.nhn++], sizeof A.nh[0], "%s %s", prefix, text);
}

/* lange Antworten (KI-Rückkanal) in lesbare Zeilen brechen */
static void nano_add_wrapped(const char *prefix, const char *text, size_t maxtotal) {
    size_t len = strlen(text), pos = 0;
    int truncated = len > maxtotal;
    if (truncated) len = maxtotal;
    int first = 1;
    while (pos < len) {
        size_t n = len - pos; if (n > 88) n = 88;
        while (n > 1 && ((unsigned char)text[pos + n] & 0xC0) == 0x80) n--;
        char chunk[92];
        memcpy(chunk, text + pos, n); chunk[n] = 0;
        nano_add(first ? prefix : " ", chunk);
        first = 0; pos += n;
    }
    if (truncated) nano_add(" ", "…");
}

static int contains(const char *hay, const char *needle) {
    return strstr(hay, needle) != NULL;
}

static void nano_answer(const char *q) {
    char lo[200]; size_t i;
    for (i = 0; q[i] && i + 1 < sizeof lo; i++) lo[i] = (char)tolower((unsigned char)q[i]);
    lo[i] = 0;

    /* '!' = ausdrücklich an die KI (Konnektor) schicken */
    if (q[0] == '!') { send_to_connector(q + 1); nano_add("→", A.msg); return; }

    /* Builder direkt steuern: "erstelle/baue/create ..." */
    if (contains(lo, "erstell") || contains(lo, "baue") ||
        contains(lo, "create") || contains(lo, "build")) {
        static const char *keys[NCAT] = {
            "präsentation|presentation|pitch", "folie|slide", "app",
            "site|web|page", "dashboard|kpi", "hub", "dokument|doc|report|bericht",
            "grafik|logo|bild|image" };
        for (int c = 0; c < NCAT; c++) {
            char tmp[80]; snprintf(tmp, sizeof tmp, "%s", keys[c]);
            for (char *t = strtok(tmp, "|"); t; t = strtok(NULL, "|"))
                if (contains(lo, t)) {
                    A.cat = c; depth = 0; push(SC_SUB);
                    nano_add("🤖", L("Ok – Kategorie geöffnet. Typ wählen (1–4).",
                                     "Ok — category opened. Pick a type (1–4)."));
                    return;
                }
        }
    }
    if (contains(lo, "kontrast") || contains(lo, "contrast")) {
        A.contrast = !A.contrast; apply_theme(); save_config();
        nano_add("🤖", A.contrast
                 ? L("Kontrastmodus AN: schwarz, grauer Text, neonrote Akzente.",
                     "Contrast mode ON: black, gray text, neon-red accents.")
                 : L("Kontrastmodus AUS.", "Contrast mode OFF."));
        return;
    }
    if (contains(lo, "sprache") || contains(lo, "language") || contains(lo, "english")
        || contains(lo, "deutsch")) {
        A.lang = !A.lang; save_config();
        nano_add("🤖", L("Sprache umgeschaltet: Deutsch.", "Language switched: English."));
        return;
    }
    if (contains(lo, "taste") || contains(lo, "key") || contains(lo, "hilfe")
        || contains(lo, "help")) {
        nano_add("🤖", L("Pfeile bewegen, 1–9 wählen, Enter öffnet, Esc zurück, "
                         "v liest vor, t Theme, k Kontrast, l Sprache, q beendet.",
                         "Arrows move, 1–9 select, Enter opens, Esc back, "
                         "v reads aloud, t theme, k contrast, l language, q quits."));
        return;
    }
    if (contains(lo, "workflow") || contains(lo, "comfy")) {
        nano_add("🤖", L("Jeder Plan lässt sich mit x als ComfyUI-Graph exportieren; "
                         "Knoten = vorgebaute Skills, KI-Knoten sind markiert.",
                         "Every plan exports (x) as a ComfyUI graph; "
                         "nodes = prebuilt skills, AI nodes are flagged."));
        return;
    }
    if (contains(lo, "skill")) {
        nano_add("🤖", L("Unter Skills (s) siehst du vorgebaute und eigene Skills; "
                         "a legt neue an, jeder Skill ist lokal oder KI.",
                         "Under Skills (s) you see prebuilt and custom skills; "
                         "a adds new ones, each skill is local or AI."));
        return;
    }
    if (contains(lo, "profil") || contains(lo, "profile")) {
        nano_add("🤖", L("Profile (p): anlegen mit a, aktivieren mit Enter – "
                         "das aktive Profil wandert in jeden Export und Versand.",
                         "Profiles (p): add with a, activate with Enter — "
                         "the active profile is stamped on exports and sends."));
        return;
    }
    if (contains(lo, "matrix") && !contains(lo, "regen") && !contains(lo, "rain")) {
        nano_add("🤖", L("Matrix läuft direkt über die Client-API (Verbindungen: u=Homeserver, "
                         "y=Token, r=Raum – oder Env MATRIX_ACCESS_TOKEN/MATRIX_ROOM). "
                         "Antworten aus dem Raum landen hier im Nanobot. Taste m = Matrix-Regen.",
                         "Matrix talks straight to the client API (Connections: u=homeserver, "
                         "y=token, r=room — or env MATRIX_ACCESS_TOKEN/MATRIX_ROOM). "
                         "Replies from the room land here in the nanobot. Key m = matrix rain."));
        return;
    }
    if (contains(lo, "ssh") || contains(lo, "shell") || contains(lo, "datei")
        || contains(lo, "file") || contains(lo, "navigator")) {
        nano_add("🤖", L("Terminal-Werkzeuge unter z: SSH-Profile (Enter verbindet), "
                         "Shell-Launcher (Ausgabe im Pager, ! am Ende = interaktiv) und "
                         "Datei-Navigator (Enter = rein, Esc = hoch).",
                         "Terminal tools under z: SSH profiles (Enter connects), "
                         "shell launcher (output in pager, append ! for interactive) and "
                         "file navigator (Enter = in, Esc = up)."));
        return;
    }
    if (contains(lo, "git") || contains(lo, "repo") || contains(lo, "commit")) {
        nano_add("🤖", L("Git-Repo-Visualizer: Werkzeuge z → 8. Wählt ein lokales Repo, "
                         "zeigt Branch/HEAD, Autor- und Datei-Endungs-Balken; 2=Graph, "
                         "3=Branches, 4=Dateien, 5=Commits, c = Claude/SERUM zum Repo fragen.",
                         "Git repo visualizer: tools z → 8. Picks a local repo, shows "
                         "branch/HEAD, author and file-extension bars; 2=graph, 3=branches, "
                         "4=files, 5=commits, c = ask Claude/SERUM about the repo."));
        return;
    }
    if (contains(lo, "verbind") || contains(lo, "connect") || contains(lo, "mcp")
        || contains(lo, "discord") || contains(lo, "webhook") || contains(lo, "gateway")) {
        nano_add("🤖", L("Verbindungen (c): MCP-Outbox, Webhook, Gateway-API oder Discord. "
                         "Nur KI-Schritte nutzen den Konnektor – alles andere bleibt lokal.",
                         "Connections (c): MCP outbox, webhook, gateway API or Discord. "
                         "Only AI steps use the connector — everything else stays local."));
        return;
    }

    /* Standard: die aktuelle Seite erklären */
    switch (cur()->sc) {
        case SC_HOME:
            nano_add("🤖", L("Das ist der Start-Hub: oben das Menü, unten die Kacheln "
                             "(1–8). Eine Kachel öffnen und der Bedarfs-Check fächert "
                             "die Auswahl weiter auf.",
                             "This is the home hub: menu on top, tiles below (1–8). "
                             "Open a tile and the needs check fans the choice out further."));
            break;
        case SC_EVAL:
            nano_add("🤖", L("Der Bedarfs-Check stellt 4 kurze Fragen, damit der Plan "
                             "genau das Nötige enthält – auch den KI-Anteil wählst du hier.",
                             "The needs check asks 4 short questions so the plan holds "
                             "exactly what's needed — you pick the AI share here too."));
            break;
        case SC_PLAN:
            nano_add("🤖", L("Jeder Schritt zeigt Skill + Skillbuilder-Vorlage. Enter "
                             "tauscht den Skill, t die Vorlage, x exportiert den "
                             "ComfyUI-Workflow, s schickt KI-Schritte an den Konnektor.",
                             "Each step shows skill + skillbuilder template. Enter swaps "
                             "the skill, t the template, x exports the ComfyUI workflow, "
                             "s sends AI steps to the connector."));
            break;
        case SC_FREESTYLE:
            nano_add("🤖", L("Freestyle: einfach hinschreiben, was entstehen soll – "
                             "kein Code nötig, ich sortiere es in die passende Kachel.",
                             "Freestyle: just write what you want to make — no code "
                             "needed, I sort it into the right tile."));
            break;
        default:
            nano_add("🤖", L("Frag mich zu dieser Seite, oder steuere den Builder: "
                             "z. B. \"erstelle präsentation\". Mit ! vorne geht die "
                             "Frage an die KI über den Konnektor.",
                             "Ask me about this page, or steer the builder: e.g. "
                             "\"create presentation\". Prefix ! to send the question "
                             "to the AI via the connector."));
    }
}

/* ------------------------------------------------------------------ */
/* Vorlese-Text der aktuellen Seite                                    */
/* ------------------------------------------------------------------ */

static void cat_add(char *b, size_t cap, const char *s) {
    size_t l = strlen(b);
    snprintf(b + l, cap - l, "%s", s);
}

static void build_speech(char *b, size_t cap) {
    b[0] = 0;
    cat_add(b, cap, screen_name()); cat_add(b, cap, ". ");
    switch (cur()->sc) {
        case SC_HOME:
            cat_add(b, cap, L("Kacheln: ", "Tiles: "));
            for (int i = 0; i < NCAT; i++) {
                cat_add(b, cap, A.lang ? cat_names[i].en : cat_names[i].de);
                cat_add(b, cap, ". ");
            }
            break;
        case SC_SUB:
            for (int i = 0; i < NSUB; i++) {
                cat_add(b, cap, A.lang ? sub_names[A.cat][i].en : sub_names[A.cat][i].de);
                cat_add(b, cap, ". ");
            }
            break;
        case SC_EVAL:
            cat_add(b, cap, A.lang ? q_names[A.qidx].en : q_names[A.qidx].de);
            cat_add(b, cap, " ");
            for (int i = 0; i < NOPT; i++) {
                cat_add(b, cap, A.lang ? q_opts[A.qidx][i].en : q_opts[A.qidx][i].de);
                cat_add(b, cap, ". ");
            }
            break;
        case SC_PLAN:
            for (int i = 0; i < A.nst; i++) {
                cat_add(b, cap, A.lang ? A.st[i].label.en : A.st[i].label.de);
                cat_add(b, cap, A.st[i].ai ? L(" mit KI. ", " with AI. ") : L(" lokal. ", " local. "));
            }
            break;
        default:
            if (A.msg[0]) cat_add(b, cap, A.msg);
    }
}

