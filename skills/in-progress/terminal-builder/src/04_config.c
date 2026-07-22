/* ------------------------------------------------------------------ */
/* Persistenz                                                          */
/* ------------------------------------------------------------------ */

static void ensure_dirs(void) {
    const char *home = getenv("HOME"); if (!home) home = ".";
    snprintf(cfgdir, sizeof cfgdir, "%s/.config", home);
    mkdir(cfgdir, 0755);
    snprintf(cfgdir, sizeof cfgdir, "%s/.config/baukasten", home);
    mkdir(cfgdir, 0755);
    char wf[600]; snprintf(wf, sizeof wf, "%s/workflows", cfgdir);
    mkdir(wf, 0755);
}

static void save_ssh(void) {
    char p[600]; snprintf(p, sizeof p, "%s/ssh.txt", cfgdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int i = 0; i < A.nssh; i++)
        fprintf(f, "%s|%s|%d\n", A.sshp[i].name, A.sshp[i].dest, A.sshp[i].port);
    fclose(f);
}

static void save_config(void) {
    char p[600]; snprintf(p, sizeof p, "%s/config", cfgdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "lang=%d\ntheme=%d\ndark=%d\ncontrast=%d\nconn=%d\nurl=%s\nkey=%s\ntts=%s\naprof=%d\nmod_rain=%d\n",
            A.lang, A.theme, A.theme != TH_HELL, A.contrast, A.conn_type,
            A.conn_url, A.conn_key, A.tts, A.aprof, A.mod_rain);
    fprintf(f, "mhs=%s\nmtoken=%s\nmroom=%s\nbridge=%s\nopencode=%s\n",
            A.matrix_hs, A.matrix_token, A.matrix_room, A.bridge_url, A.opencode_url);
    /* SERUM: URLs + Modellnamen werden gespeichert; API-Keys NICHT
     * (Anthropic/OpenAI-Key kommen aus der Umgebung, siehe *_key_get). */
    fprintf(f, "ollama=%s\nollama_model=%s\namodel=%s\noai_url=%s\noai_model=%s\n",
            A.ollama_url, A.ollama_model, A.amodel, A.oai_url, A.oai_model);
    fclose(f);
    snprintf(p, sizeof p, "%s/profiles.txt", cfgdir);
    f = fopen(p, "w");
    if (f) {
        for (int i = 0; i < A.nprof; i++) fprintf(f, "%s\n", A.prof[i]);
        fclose(f);
    }
    snprintf(p, sizeof p, "%s/skills.txt", cfgdir);
    f = fopen(p, "w");
    if (f) {
        for (int i = 0; i < A.nskill; i++)
            if (!A.sk[i].builtin)
                fprintf(f, "%s|%d|%s\n", A.sk[i].name, A.sk[i].ai, A.sk[i].desc);
        fclose(f);
    }
}

static void chomp(char *s) { size_t n = strlen(s); while (n && (s[n-1]=='\n'||s[n-1]=='\r')) s[--n]=0; }

static int load_config(void) {
    int had = 0, saw_theme = 0, legacy_dark = 1;
    A.lang = 0; A.theme = TH_MATRIX; A.contrast = 0; A.mod_rain = 1; A.conn_type = CONN_NONE;
    A.conn_url[0] = A.conn_key[0] = A.tts[0] = 0;
    A.matrix_hs[0] = A.matrix_token[0] = A.matrix_room[0] = A.matrix_me[0] = 0;
    snprintf(A.bridge_url, sizeof A.bridge_url, "http://localhost:3050");
    snprintf(A.opencode_url, sizeof A.opencode_url, "http://localhost:9900");
    snprintf(A.ollama_url, sizeof A.ollama_url, "http://localhost:11434");
    snprintf(A.ollama_model, sizeof A.ollama_model, "qwen2.5:7b");
    snprintf(A.amodel, sizeof A.amodel, "claude-haiku-4-5-20251001");
    snprintf(A.oai_url, sizeof A.oai_url, "http://localhost:8080");
    snprintf(A.oai_model, sizeof A.oai_model, "gpt-4o-mini");
    A.oai_key[0] = 0;
    /* Cosmo/Odysseus-Presets als Startprofile (Bundle greift bei Aktivierung) */
    { static const char *pp[] = { "standard", "desktop", "vps", "agent", "builder", "monitor" };
      A.nprof = 6;
      for (int i = 0; i < 6; i++) snprintf(A.prof[i], sizeof A.prof[0], "%s", pp[i]); }
    A.aprof = 0;
    A.nskill = SK_BUILTIN_N;
    memcpy(A.sk, builtin_skills, sizeof builtin_skills);
    A.nssh = 0;

    char p[600], line[512];
    snprintf(p, sizeof p, "%s/config", cfgdir);
    FILE *f = fopen(p, "r");
    if (f) {
        had = 1;
        while (fgets(line, sizeof line, f)) {
            chomp(line);
            char *eq = strchr(line, '='); if (!eq) continue;
            *eq = 0; const char *v = eq + 1;
            if (!strcmp(line, "lang")) A.lang = atoi(v) ? 1 : 0;
            else if (!strcmp(line, "theme")) { A.theme = atoi(v); saw_theme = 1;
                if (A.theme < 0 || A.theme >= NTHEME) A.theme = TH_MATRIX; }
            else if (!strcmp(line, "dark")) legacy_dark = atoi(v) ? 1 : 0;
            else if (!strcmp(line, "contrast")) A.contrast = atoi(v) ? 1 : 0;
            else if (!strcmp(line, "conn")) { A.conn_type = atoi(v);
                if (A.conn_type < 0 || A.conn_type >= NCONN) A.conn_type = 0; }
            else if (!strcmp(line, "url")) snprintf(A.conn_url, sizeof A.conn_url, "%s", v);
            else if (!strcmp(line, "key")) snprintf(A.conn_key, sizeof A.conn_key, "%s", v);
            else if (!strcmp(line, "tts")) snprintf(A.tts, sizeof A.tts, "%s", v);
            else if (!strcmp(line, "aprof")) A.aprof = atoi(v);
            else if (!strcmp(line, "mod_rain")) A.mod_rain = atoi(v) ? 1 : 0;
            else if (!strcmp(line, "mhs")) snprintf(A.matrix_hs, sizeof A.matrix_hs, "%s", v);
            else if (!strcmp(line, "mtoken")) snprintf(A.matrix_token, sizeof A.matrix_token, "%s", v);
            else if (!strcmp(line, "mroom")) snprintf(A.matrix_room, sizeof A.matrix_room, "%s", v);
            else if (!strcmp(line, "bridge")) snprintf(A.bridge_url, sizeof A.bridge_url, "%s", v);
            else if (!strcmp(line, "opencode")) snprintf(A.opencode_url, sizeof A.opencode_url, "%s", v);
            else if (!strcmp(line, "ollama")) snprintf(A.ollama_url, sizeof A.ollama_url, "%s", v);
            else if (!strcmp(line, "ollama_model")) snprintf(A.ollama_model, sizeof A.ollama_model, "%s", v);
            else if (!strcmp(line, "amodel")) snprintf(A.amodel, sizeof A.amodel, "%s", v);
            else if (!strcmp(line, "oai_url")) snprintf(A.oai_url, sizeof A.oai_url, "%s", v);
            else if (!strcmp(line, "oai_model")) snprintf(A.oai_model, sizeof A.oai_model, "%s", v);
        }
        fclose(f);
        if (!saw_theme) A.theme = legacy_dark ? TH_DUNKEL : TH_HELL;
    }
    snprintf(p, sizeof p, "%s/ssh.txt", cfgdir);
    f = fopen(p, "r");
    if (f) {
        while (fgets(line, sizeof line, f) && A.nssh < MAXSSH) {
            chomp(line);
            char *p1 = strchr(line, '|'); if (!p1) continue;
            *p1 = 0; char *p2 = strchr(p1 + 1, '|'); if (!p2) continue;
            *p2 = 0;
            SshProf *s = &A.sshp[A.nssh];
            snprintf(s->name, sizeof s->name, "%s", line);
            snprintf(s->dest, sizeof s->dest, "%s", p1 + 1);
            s->port = atoi(p2 + 1); if (s->port <= 0 || s->port > 65535) s->port = 22;
            A.nssh++;
        }
        fclose(f);
    }
    snprintf(p, sizeof p, "%s/profiles.txt", cfgdir);
    f = fopen(p, "r");
    if (f) {
        A.nprof = 0;
        while (fgets(line, sizeof line, f) && A.nprof < MAXPROF) {
            chomp(line);
            if (line[0]) snprintf(A.prof[A.nprof++], sizeof A.prof[0], "%s", line);
        }
        if (!A.nprof) { snprintf(A.prof[0], sizeof A.prof[0], "standard"); A.nprof = 1; }
        fclose(f);
    }
    if (A.aprof < 0 || A.aprof >= A.nprof) A.aprof = 0;
    snprintf(p, sizeof p, "%s/skills.txt", cfgdir);
    f = fopen(p, "r");
    if (f) {
        while (fgets(line, sizeof line, f) && A.nskill < MAXSKILL) {
            chomp(line);
            char *p1 = strchr(line, '|'); if (!p1) continue;
            *p1 = 0; char *p2 = strchr(p1 + 1, '|'); if (!p2) continue;
            *p2 = 0;
            SkillDef *s = &A.sk[A.nskill];
            snprintf(s->name, sizeof s->name, "%s", line);
            s->ai = atoi(p1 + 1) ? 1 : 0;
            snprintf(s->desc, sizeof s->desc, "%s", p2 + 1);
            s->builtin = 0;
            A.nskill++;
        }
        fclose(f);
    }
    return had;
}

/* ------------------------------------------------------------------ */
/* Navigation                                                          */
/* ------------------------------------------------------------------ */

static Frame *cur(void) { return &stk[depth]; }

static void push(int sc) {
    if (depth + 1 < (int)(sizeof stk / sizeof stk[0])) {
        depth++; stk[depth].sc = sc; stk[depth].sel = 0;
    }
}

static void popscr(void) { if (depth > 0) depth--; }

static void setmsg(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(A.msg, sizeof A.msg, fmt, ap);
    va_end(ap);
}

static const char *screen_name(void) {
    switch (cur()->sc) {
        case SC_HOME: return L("Start-Hub", "Home hub");
        case SC_SUB: return A.lang ? cat_names[A.cat].en : cat_names[A.cat].de;
        case SC_EVAL: return L("Bedarfs-Check", "Needs check");
        case SC_PLAN: return L("Arbeitsschritte", "Work steps");
        case SC_PICK: return L("Skill wählen", "Pick skill");
        case SC_WORKFLOWS: return L("Workflows", "Workflows");
        case SC_SKILLS: return L("Skills", "Skills");
        case SC_PROFILES: return L("Profile", "Profiles");
        case SC_CONNECT: return L("Verbindungen", "Connections");
        case SC_SETTINGS: return L("Einstellungen", "Settings");
        case SC_FREESTYLE: return L("Freestyle", "Freestyle");
        case SC_HELP: return L("Tasten-Hilfe", "Key help");
        case SC_TOOLS: return L("Terminal-Werkzeuge", "Terminal tools");
        case SC_SSH: return L("SSH-Profile", "SSH profiles");
        case SC_PAGER: return L("Ausgabe", "Output");
        case SC_FILES: return L("Datei-Navigator", "File navigator");
        case SC_CHAT: return L("KI-Chat", "AI chat");
        case SC_MONITOR: return L("Dienst-Monitor", "Service monitor");
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Texteingabe (unten am Bildschirm)                                   */
/* ------------------------------------------------------------------ */

static int prompt_input(const char *label, char *buf, size_t cap) {
    size_t len = strlen(buf);
    for (;;) {
        o("\x1b[%d;1H%s%s \x1b[7m %s \x1b[27m%s %s%s_\x1b[K", A.rows, th.bg, th.hi,
          label, th.fg, th.bg, buf);
        oflush();
        int k = readkey();
        if (k == K_ESC) return 0;
        if (k == K_ENTER || k == '\n') return 1;
        if (k == K_BS || k == 8) {
            /* ein UTF-8-Zeichen löschen */
            while (len && ((unsigned char)buf[len - 1] & 0xC0) == 0x80) len--;
            if (len) len--;
            buf[len] = 0;
        } else if (k >= 32 && k != K_BS && k < 1000 && len + 1 < cap) {
            buf[len++] = (char)k; buf[len] = 0;
        }
    }
}

