/* ------------------------------------------------------------------ */
/* Terminal-Werkzeuge: Fuzzy (fzf-Stil), Pager, Shell, Cheats (navi),  */
/* Historie, Datei-Navigator, SSH                                      */
/* ------------------------------------------------------------------ */

/* fzf-Stil: Subsequenz-Match, Groß/Klein egal */
static int fuzzy_match(const char *pat, const char *s) {
    if (!pat[0]) return 1;
    const char *p = pat;
    for (; *s && *p; s++)
        if (tolower((unsigned char)*s) == tolower((unsigned char)*p)) p++;
    return !*p;
}

/* Modaler Fuzzy-Picker: tippen filtert live, Enter wählt, Esc bricht ab.
 * Rückgabe: Index in items oder -1. */
static int fuzzy_pick(const char *title, char **items, int n) {
    char pat[80] = ""; size_t plen = 0;
    int sel = 0;
    for (;;) {
        static int idx[MAXFENT > 1000 ? MAXFENT : 1000];
        int m = 0;
        for (int i = 0; i < n && m < (int)(sizeof idx / sizeof idx[0]); i++)
            if (fuzzy_match(pat, items[i])) idx[m++] = i;
        if (sel >= m) sel = m ? m - 1 : 0;
        term_size(); apply_theme();
        olen = 0;
        o("\x1b[?2026h\x1b[H");
        ln("%s ▚▚ %s %s(%d/%d · %s)", th.head, title, th.dim, m, n,
           L("tippen = filtern, Enter = wählen, Esc = abbrechen",
             "type = filter, Enter = pick, Esc = cancel"));
        ln("%s %s%s_", th.hi, L("Suche > ", "Search > "), pat);
        ln("");
        int page = A.rows - 6; if (page < 1) page = 1;
        int top = sel - page + 1; if (top < 0) top = 0;
        for (int i = top; i < m && i < top + page; i++) {
            char clip[260];
            fitpad(clip, sizeof clip, items[idx[i]], A.cols > 6 ? A.cols - 6 : 40);
            if (i == sel) ln("  %s %s \x1b[0m%s", th.sel, clip, th.bg);
            else ln("  %s%s", th.fg, clip);
        }
        o("%s\x1b[J\x1b[0m\x1b[?2026l", th.bg);
        oflush();
        int k = readkey();
        if (k < 0 || k == K_ESC) return -1;
        if (k == K_ENTER || k == '\n') return m ? idx[sel] : -1;
        if (k == K_UP) { if (sel > 0) sel--; }
        else if (k == K_DN) { if (sel + 1 < m) sel++; }
        else if (k == K_BS || k == 8) {
            while (plen && ((unsigned char)pat[plen - 1] & 0xC0) == 0x80) plen--;
            if (plen) plen--;
            pat[plen] = 0; sel = 0;
        } else if (k >= 32 && k < 1000 && plen + 1 < sizeof pat) {
            pat[plen++] = (char)k; pat[plen] = 0; sel = 0;
        }
    }
}

static char  pager_buf[64 * 1024];
static char *pager_line[2048];
static int   pager_n, pager_top;
static char  pager_title[120];

static void pager_load(const char *title, const char *text) {
    snprintf(pager_title, sizeof pager_title, "%s", title);
    snprintf(pager_buf, sizeof pager_buf, "%s", text);
    pager_n = 0; pager_top = 0;
    char *p = pager_buf;
    while (p && pager_n < 2048) {
        pager_line[pager_n++] = p;
        char *nl = strchr(p, '\n');
        if (!nl) break;
        *nl = 0; p = nl + 1;
    }
}

/* Befehl ausführen: Ausgabe in den Pager; '!' am Ende = interaktiv */
static void run_capture_to_pager(const char *cmd) {
    char work[400]; snprintf(work, sizeof work, "%s", cmd);
    size_t cl = strlen(work);
    if (cl && work[cl - 1] == '!') { work[cl - 1] = 0; run_interactive(work); return; }
    char full[512];
    snprintf(full, sizeof full, "( %s ) 2>&1", work);
    FILE *p = popen(full, "r");
    if (!p) { setmsg("%s", L("Start fehlgeschlagen.", "Launch failed.")); return; }
    static char buf[64 * 1024];
    size_t n = fread(buf, 1, sizeof buf - 32, p);
    pclose(p);
    buf[n] = 0;
    if (n >= sizeof buf - 33)
        snprintf(buf + n, 32, "%s", L("\n… gekürzt", "\n… truncated"));
    pager_load(work, buf[0] ? buf : L("(keine Ausgabe)", "(no output)"));
    push(SC_PAGER);
}

static void shell_launch(void) {
    char cmd[400] = "";
    if (!prompt_input(L("Befehl (mit ! am Ende = interaktiv) >",
                        "Command (append ! for interactive) >"), cmd, sizeof cmd)
        || !cmd[0]) return;
    run_capture_to_pager(cmd);
}

/* zsh (oder $SHELL) direkt öffnen */
static void open_shell(void) {
    const char *sh = getenv("SHELL");
    if (!sh || !sh[0])
        sh = access("/usr/bin/zsh", X_OK) == 0 ? "/usr/bin/zsh" : "/bin/bash";
    run_interactive(sh);
    setmsg(L("Shell beendet – willkommen zurück.", "Shell closed — welcome back."));
}

/* Shell-Historie (zsh + bash) fuzzy durchsuchen, Treffer ausführen */
static void history_pick(void) {
    static char buf[128 * 1024];
    static char *items[1000];
    int n = 0; size_t used = 0;
    const char *home = getenv("HOME"); if (!home) home = ".";
    const char *hf[2] = { ".zsh_history", ".bash_history" };
    for (int fi = 0; fi < 2; fi++) {
        char p[600]; snprintf(p, sizeof p, "%s/%s", home, hf[fi]);
        FILE *f = fopen(p, "r"); if (!f) continue;
        char line[512];
        while (fgets(line, sizeof line, f)) {
            chomp(line);
            char *cmd = line;
            if (line[0] == ':') { char *sc = strchr(line, ';'); if (sc) cmd = sc + 1; }
            if (!cmd[0]) continue;
            size_t l = strlen(cmd) + 1;
            if (used + l >= sizeof buf || n >= 1000) break;
            if (n && !strcmp(items[n - 1], cmd)) continue;
            memcpy(buf + used, cmd, l);
            items[n++] = buf + used;
            used += l;
        }
        fclose(f);
    }
    if (!n) { setmsg("%s", L("Keine Shell-Historie gefunden.", "No shell history found.")); return; }
    for (int i = 0; i < n / 2; i++)   /* neueste zuerst */
        { char *t = items[i]; items[i] = items[n - 1 - i]; items[n - 1 - i] = t; }
    int i = fuzzy_pick(L("Befehls-Historie (fzf-Stil)", "Command history (fzf style)"),
                       items, n);
    if (i >= 0) run_capture_to_pager(items[i]);
}

/* Cheat-Sheets (navi-Stil): beschreibung|befehl, <platzhalter> werden abgefragt */
#define MAXCHEAT 64
typedef struct { char desc[120]; char cmd[240]; } Cheat;
static Cheat cheats[MAXCHEAT];
static int ncheat;

static void cheats_load(void) {
    ncheat = 0;
    char p[600]; snprintf(p, sizeof p, "%s/cheats.txt", cfgdir);
    FILE *f = fopen(p, "r");
    if (!f) {
        static const char *defs[][2] = {
            {"Git: Status kurz", "git status -sb"},
            {"Git: letzte <anzahl> Commits", "git log --oneline -<anzahl>"},
            {"Platz: groesste Ordner hier", "du -sh * | sort -rh | head -15"},
            {"Suche: <muster> in Dateien", "grep -rn \"<muster>\" ."},
            {"Netz: offene Ports", "ss -tulpen"},
            {"Prozesse: Top nach RAM", "ps aux --sort=-%mem | head -15"},
        };
        f = fopen(p, "w");
        for (size_t i = 0; i < sizeof defs / sizeof defs[0] && ncheat < MAXCHEAT; i++) {
            if (f) fprintf(f, "%s|%s\n", defs[i][0], defs[i][1]);
            snprintf(cheats[ncheat].desc, sizeof cheats[0].desc, "%s", defs[i][0]);
            snprintf(cheats[ncheat].cmd, sizeof cheats[0].cmd, "%s", defs[i][1]);
            ncheat++;
        }
        if (f) fclose(f);
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, f) && ncheat < MAXCHEAT) {
        chomp(line);
        char *p1 = strchr(line, '|'); if (!p1) continue;
        *p1 = 0;
        snprintf(cheats[ncheat].desc, sizeof cheats[0].desc, "%s", line);
        snprintf(cheats[ncheat].cmd, sizeof cheats[0].cmd, "%s", p1 + 1);
        ncheat++;
    }
    fclose(f);
}

static int cheat_fill(const char *tpl, char *out, size_t cap) {
    size_t d = 0;
    for (const char *p = tpl; *p && d + 2 < cap; ) {
        if (*p == '<') {
            const char *e = strchr(p, '>');
            if (!e) { out[d++] = *p++; continue; }
            char name[60], val[120] = "";
            size_t nl = (size_t)(e - p - 1); if (nl >= sizeof name) nl = sizeof name - 1;
            memcpy(name, p + 1, nl); name[nl] = 0;
            char label[90]; snprintf(label, sizeof label, "%s >", name);
            if (!prompt_input(label, val, sizeof val)) return 0;
            d += (size_t)snprintf(out + d, cap - d, "%s", val);
            if (d >= cap) d = cap - 1;
            p = e + 1;
        } else out[d++] = *p++;
    }
    out[d] = 0;
    return 1;
}

static void cheat_run(void) {
    cheats_load();
    if (!ncheat) { setmsg("%s", L("Keine Cheats.", "No cheats.")); return; }
    static char *items[MAXCHEAT];
    static char lbl[MAXCHEAT][400];
    for (int i = 0; i < ncheat; i++) {
        snprintf(lbl[i], sizeof lbl[0], "%.118s  —  %.238s", cheats[i].desc, cheats[i].cmd);
        items[i] = lbl[i];
    }
    int i = fuzzy_pick(L("Cheat-Sheets (navi-Stil)", "Cheat sheets (navi style)"),
                       items, ncheat);
    if (i < 0) return;
    char cmd[400];
    if (cheat_fill(cheats[i].cmd, cmd, sizeof cmd) && cmd[0])
        run_capture_to_pager(cmd);
}

/* ------------------------------------------------------------------ */
/* KI-Chat in der Konsole — für OpenCode, Hermes (Bridge) & Konnektor  */
/* ------------------------------------------------------------------ */

enum { CT_KONNEKTOR, CT_OPENCODE, CT_HERMES,
       CT_OLLAMA, CT_ANTHROPIC, CT_OPENAI, NCTARGET };
static const S2 chat_target_names[NCTARGET] = {
    {"Aktiver Konnektor", "Active connector"},
    {"OpenCode", "OpenCode"},
    {"Hermes (Bridge)", "Hermes (bridge)"},
    {"SERUM: Ollama (direkt)", "SERUM: Ollama (direct)"},
    {"SERUM: Anthropic (Claude)", "SERUM: Anthropic (Claude)"},
    {"SERUM: OpenAI-kompatibel", "SERUM: OpenAI-compatible"},
};
/* Direktziele (SERUM) laufen über serum_chat(); Index dort: 0/1/2 */
static int chat_serum_target(int t) {
    return t == CT_OLLAMA ? 0 : t == CT_ANTHROPIC ? 1 : t == CT_OPENAI ? 2 : -1;
}
static int  chat_target = CT_OLLAMA;
#define CHAT_LINES 400
static char chat_ln_buf[CHAT_LINES][160];
static int  chat_n, chat_scroll;   /* scroll = Zeilen vom Ende entfernt */

static void chat_add(const char *who, const char *text) {
    char pfx[24]; snprintf(pfx, sizeof pfx, "%s", who);
    size_t len = strlen(text), pos = 0;
    int first = 1;
    while (pos < len || first) {
        size_t n = len - pos; if (n > 110) n = 110;
        while (n > 1 && pos + n < len
               && ((unsigned char)text[pos + n] & 0xC0) == 0x80) n--;
        if (chat_n >= CHAT_LINES) {
            memmove(chat_ln_buf[0], chat_ln_buf[1],
                    sizeof chat_ln_buf - sizeof chat_ln_buf[0]);
            chat_n = CHAT_LINES - 1;
        }
        snprintf(chat_ln_buf[chat_n], sizeof chat_ln_buf[0], "%s %.*s",
                 first ? pfx : "      ", (int)n, text + pos);
        chat_n++;
        pos += n; first = 0;
        if (!n) break;
    }
    chat_scroll = 0;
}

static void chat_send(void) {
    char q[400] = "";
    if (!prompt_input(L("Nachricht >", "Message >"), q, sizeof q) || !q[0]) return;
    chat_add(L("Du  >", "You >"), q);
    last_reply[0] = 0;
    int serum = chat_serum_target(chat_target);
    if (serum >= 0) {
        char out[4096];
        setmsg(L("SERUM %s denkt …", "SERUM %s thinking …"),
               A.lang ? chat_target_names[chat_target].en : chat_target_names[chat_target].de);
        render();
        long t0 = (long)time(NULL);
        long code = serum_chat(serum, q, out, sizeof out);
        long secs = (long)time(NULL) - t0;
        sanitize_text(out);
        if (out[0]) snprintf(last_reply, sizeof last_reply, "%.2020s  · %lds", out, secs);
        else if (code <= 0)
            setmsg("%s", L("Ziel nicht erreichbar – Healthcheck (Verbindungen h).",
                           "Target unreachable — healthcheck (Connections h)."));
        else
            setmsg(L("Fehler HTTP %ld", "Error HTTP %ld"), code);
    } else if (chat_target == CT_KONNEKTOR) {
        send_to_connector(q);
        A.nano_open = 0;  /* Chat zeigt die Antwort selbst */
    } else {
        const char *url = chat_target == CT_OPENCODE ? A.opencode_url : A.bridge_url;
        char eq[600], body[900], resp[2048];
        json_escape(eq, sizeof eq, q);
        if (chat_target == CT_HERMES)
            snprintf(body, sizeof body,
                     "{\"agent\":\"baukasten\",\"model\":\"hermes\",\"message\":\"%s\"}", eq);
        else
            snprintf(body, sizeof body,
                     "{\"agent\":\"baukasten\",\"message\":\"%s\"}", eq);
        long code = curl_http("POST", url, NULL, body, resp, sizeof resp, 20);
        if (code / 100 == 2) {
            sanitize_text(resp);
            snprintf(last_reply, sizeof last_reply, "%s",
                     resp[0] ? resp : L("(leere Antwort)", "(empty reply)"));
        } else if (code <= 0)
            setmsg(L("%s auf %s nicht erreichbar – läuft der Dienst?",
                     "%s at %s unreachable — is the service running?"),
                   A.lang ? chat_target_names[chat_target].en
                          : chat_target_names[chat_target].de, url);
        else
            setmsg(L("Fehler HTTP %ld von %s", "Error HTTP %ld from %s"), code, url);
    }
    if (last_reply[0])
        chat_add(L("KI  >", "AI  >"), last_reply);
    else
        chat_add("  ·", L("(keine Antwort erhalten)", "(no reply received)"));
}

typedef struct { char name[256]; long size; int isdir; } FEnt;
static FEnt fent[MAXFENT];
static int  nfent;
static char fdir[512], fstart[512];

static int fent_cmp(const void *a, const void *b) {
    const FEnt *x = a, *y = b;
    if (x->isdir != y->isdir) return y->isdir - x->isdir;
    return strcasecmp(x->name, y->name);
}

static void files_load(const char *dir) {
    nfent = 0;
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && nfent < MAXFENT) {
        if (e->d_name[0] == '.') continue;
        FEnt *fe = &fent[nfent];
        snprintf(fe->name, sizeof fe->name, "%s", e->d_name);
        char p[900]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p, &st) == 0) { fe->isdir = S_ISDIR(st.st_mode); fe->size = (long)st.st_size; }
        else { fe->isdir = 0; fe->size = -1; }
        nfent++;
    }
    closedir(d);
    qsort(fent, (size_t)nfent, sizeof fent[0], fent_cmp);
}

static void fmt_size(long sz, char *out, size_t cap) {
    if (sz < 0) snprintf(out, cap, "?");
    else if (sz < 1024) snprintf(out, cap, "%ldB", sz);
    else if (sz < 1024L * 1024) snprintf(out, cap, "%.1fK", (double)sz / 1024.0);
    else if (sz < 1024L * 1024 * 1024) snprintf(out, cap, "%.1fM", (double)sz / 1048576.0);
    else snprintf(out, cap, "%.1fG", (double)sz / 1073741824.0);
}

static int ssh_dest_ok(const char *d) {
    if (!d[0]) return 0;
    for (; *d; d++)
        if (!isalnum((unsigned char)*d) && !strchr("@._-", *d)) return 0;
    return 1;
}

static void ssh_connect(int i) {
    if (i < 0 || i >= A.nssh) return;
    SshProf *s = &A.sshp[i];
    if (!ssh_dest_ok(s->dest)) {
        setmsg("%s", L("Ungültiges SSH-Ziel (nur Buchstaben/Zahlen @ . _ -).",
                       "Invalid SSH target (letters/digits @ . _ - only)."));
        return;
    }
    char cmd[200];
    snprintf(cmd, sizeof cmd, "ssh -p %d '%s'", s->port, s->dest);
    run_interactive(cmd);
    setmsg(L("SSH-Sitzung \"%s\" beendet.", "SSH session \"%s\" ended."), s->name);
}


/* ------------------------------------------------------------------ */
/* Skill-Import: Repo-URL -> git clone + SKILL.md registrieren + FLOWS */
/* YouTube-URL -> Anfrage ans aktive SERUM-Ziel                        */
/* ------------------------------------------------------------------ */

static int git_url_ok(const char *u) {
    if (strncmp(u, "https://", 8)) return 0;
    for (const char *p = u; *p; p++)
        if (!isalnum((unsigned char)*p) && !strchr(":/._~-", *p)) return 0;
    return 1;
}

static void skill_import(void) {
    char url[300] = "";
    if (!prompt_input(L("Repo- oder YouTube-URL >", "Repo or YouTube URL >"), url, sizeof url)
        || !url[0]) return;

    if (strstr(url, "youtube.com") || strstr(url, "youtu.be")) {
        int serum = chat_serum_target(chat_target);
        char q[400];
        snprintf(q, sizeof q,
                 L("Extrahiere aus diesem Video die nutzbaren Skills und einen Schritt-Workflow: %s",
                   "Extract usable skills and a step workflow from this video: %s"), url);
        if (serum >= 0) {
            char out[4096];
            setmsg("%s", L("Frage SERUM zu YouTube …", "Asking SERUM about YouTube …")); render();
            long code = serum_chat(serum, q, out, sizeof out);
            sanitize_text(out);
            if (out[0]) { pager_load("YouTube → Skills", out); push(SC_PAGER); }
            else setmsg(L("Keine Antwort (HTTP %ld) – Ziel/Healthcheck prüfen.",
                          "No reply (HTTP %ld) — check target/healthcheck."), code);
        } else send_to_connector(q);
        return;
    }

    if (!git_url_ok(url)) {
        setmsg("%s", L("Nur https://-Git-URLs (Buchstaben/Zahlen : / . _ ~ -).",
                       "https:// git URLs only (letters/digits : / . _ ~ -)."));
        return;
    }
    /* Paketname aus letztem Pfadsegment */
    char name[80]; const char *slash = strrchr(url, '/');
    snprintf(name, sizeof name, "%s", slash ? slash + 1 : "pack");
    char *dot = strstr(name, ".git"); if (dot) *dot = 0;
    for (char *p = name; *p; p++) if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') *p = '_';
    if (!name[0]) snprintf(name, sizeof name, "pack");

    char dir[700], cmd[2400];
    snprintf(dir, sizeof dir, "%s/skillpacks/%s", cfgdir, name);
    char mk[760]; snprintf(mk, sizeof mk, "%s/skillpacks", cfgdir); mkdir(mk, 0755);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'; git clone --depth 1 '%s' '%s' 2>&1", dir, url, dir);
    setmsg("%s", L("Klone Repo …", "Cloning repo …")); render();
    if (system(cmd) != 0) { setmsg("%s", L("git clone fehlgeschlagen (Netz/URL prüfen).",
                                            "git clone failed (check network/URL).")); return; }

    /* SKILL.md-Dateien finden und als Skills registrieren + FLOWS.md schreiben */
    char find[900];
    snprintf(find, sizeof find, "cd '%s' && find . -iname 'SKILL.md' 2>/dev/null", dir);
    FILE *p = popen(find, "r");
    int added = 0;
    char fl[900]; snprintf(fl, sizeof fl, "%s/FLOWS.md", dir);
    FILE *flows = fopen(fl, "w");
    if (flows) fprintf(flows, "# FLOWS — %s\n\nImportierte Skills (baukasten/SERUM):\n\n", name);
    if (p) {
        char line[600];
        while (fgets(line, sizeof line, p) && A.nskill < MAXSKILL) {
            chomp(line);
            /* Skill-Name = Ordnername über SKILL.md */
            char *sl = strrchr(line, '/'); if (sl) *sl = 0;
            const char *base = strrchr(line, '/'); base = base ? base + 1 : line;
            if (!base[0] || !strcmp(base, ".")) base = name;
            SkillDef *s = &A.sk[A.nskill++];
            snprintf(s->name, sizeof s->name, "%.47s", base);
            snprintf(s->desc, sizeof s->desc, "%.44s/%.44s", name, base);
            s->ai = 0; s->builtin = 0;
            if (flows) fprintf(flows, "- **%s** — `%s/%s`\n", base, name, base);
            added++;
        }
        pclose(p);
    }
    if (flows) {
        fprintf(flows, "\n## Workflow\n1. Skill in einem Plan-Schritt wählen (Enter).\n"
                       "2. KI-Schritte über den aktiven Konnektor/das SERUM-Ziel senden.\n"
                       "3. Ergebnis im Nanobot/Chat prüfen.\n");
        fclose(flows);
    }
    save_config();
    if (added) setmsg(L("%d Skills aus \"%s\" importiert (FLOWS.md erzeugt).",
                        "Imported %d skills from \"%s\" (FLOWS.md written)."), added, name);
    else setmsg(L("Kein SKILL.md in \"%s\" gefunden.", "No SKILL.md found in \"%s\"."), name);
}
