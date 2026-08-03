/* ------------------------------------------------------------------ */
/* Git-Repo-Visualizer — lokale Repos ansehen & analysieren, verzahnt  */
/* mit SERUM (Claude/Ollama) und den Terminal-Chat-Tools. Offline-first;*/
/* GitHub-API-Anreicherung ist optionaler Best-Effort.                 */
/* ------------------------------------------------------------------ */

static char grepo[512];               /* gewähltes Repo (absoluter Pfad) */
static char gbranch[96], ghead[24], gremote[256];
static int  gcommits, gbranches, gtags, gdirty;
static Tally gauth[8]; static int ngauth;
static Tally gext[8];  static int ngext;
static char ggh_info[240];            /* GitHub: Sterne/Beschreibung (optional) */

/* Pfad muss ein existierendes Verzeichnis ohne ' sein (Shell-Härtung). */
static int gpath_ok(const char *p) {
    if (!p[0] || strchr(p, '\'')) return 0;
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* `git -C '<repo>' <args>` einfangen; out ggf. leer bei Fehler. */
static void git_cap(const char *repo, const char *args, char *out, size_t cap) {
    if (out && cap) out[0] = 0;
    if (!gpath_ok(repo)) return;
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "git -C '%s' %s 2>/dev/null", repo, args);
    FILE *p = popen(cmd, "r");
    if (!p) return;
    size_t n = out && cap ? fread(out, 1, cap - 1, p) : 0;
    pclose(p);
    if (out && cap) { out[n] = 0; chomp(out); }
}

static int git_int(const char *repo, const char *args) {
    char b[64]; git_cap(repo, args, b, sizeof b);
    return b[0] ? atoi(b) : 0;
}

static void tally_add(Tally *t, int *n, int max, const char *name, int add) {
    for (int i = 0; i < *n; i++)
        if (!strcmp(t[i].name, name)) { t[i].n += add; return; }
    if (*n < max) { snprintf(t[*n].name, 64, "%s", name); t[*n].n = add; (*n)++; }
}

static int tally_cmp(const void *a, const void *b) {
    return ((const Tally*)b)->n - ((const Tally*)a)->n;
}

/* GitHub-API-Anreicherung (nur github.com, Token optional aus GITHUB_TOKEN). */
static void gitviz_github(void) {
    ggh_info[0] = 0;
    const char *gh = strstr(gremote, "github.com");
    if (!gh) return;
    /* owner/repo aus …github.com[:/ ]owner/repo(.git) parsen */
    const char *s = gh + 10; if (*s == ':' || *s == '/') s++;
    char owner[80] = "", repo[80] = "";
    int i = 0; while (s[i] && s[i] != '/' && i < 79) { owner[i] = s[i]; i++; } owner[i] = 0;
    if (s[i] != '/') return;
    s += i + 1;
    int j = 0; while (s[j] && s[j] != '/' && s[j] != '.' && s[j] != ' ' && j < 79) { repo[j] = s[j]; j++; } repo[j] = 0;
    if (!owner[0] || !repo[0]) return;
    char url[300], resp[8192];
    snprintf(url, sizeof url, "https://api.github.com/repos/%s/%s", owner, repo);
    const char *tok = getenv("GITHUB_TOKEN");
    char xh[300] = "-H 'Accept: application/vnd.github+json' -H 'User-Agent: baukasten' ";
    if (tok && tok[0]) {
        char kh[260]; header_clean(kh, sizeof kh, tok);
        size_t l = strlen(xh);
        snprintf(xh + l, sizeof xh - l, "-H 'Authorization: Bearer %s' ", kh);
    }
    if (curl_http_x("GET", url, NULL, xh, NULL, resp, sizeof resp, 6) / 100 != 2) return;
    long stars = json_find_number(resp, "stargazers_count");
    long issues = json_find_number(resp, "open_issues_count");
    char desc[160] = ""; json_find_string(resp, "description", desc, sizeof desc);
    snprintf(ggh_info, sizeof ggh_info, "★ %ld · issues %ld%s%s",
             stars < 0 ? 0 : stars, issues < 0 ? 0 : issues,
             desc[0] ? " · " : "", desc);
}

static void gitviz_load(const char *path) {
    snprintf(grepo, sizeof grepo, "%s", path);
    git_cap(path, "rev-parse --abbrev-ref HEAD", gbranch, sizeof gbranch);
    if (!gbranch[0]) snprintf(gbranch, sizeof gbranch, "—");
    git_cap(path, "rev-parse --short HEAD", ghead, sizeof ghead);
    if (!ghead[0]) snprintf(ghead, sizeof ghead, "—");
    git_cap(path, "remote get-url origin", gremote, sizeof gremote);
    gcommits = git_int(path, "rev-list --count HEAD");
    gbranches = git_int(path, "branch -a --format=%(refname) | wc -l");
    gtags = git_int(path, "tag | wc -l");
    { char st[64]; git_cap(path, "status --porcelain", st, sizeof st); gdirty = st[0] != 0; }

    /* Top-Autoren: shortlog -sn -> "  <n>\t<name>" */
    ngauth = 0;
    { char buf[4096]; git_cap(path, "shortlog -sn --all --no-merges", buf, sizeof buf);
      char *save, *ln = strtok_r(buf, "\n", &save);
      while (ln && ngauth < 8) {
        while (*ln == ' ' || *ln == '\t') ln++;
        int cnt = atoi(ln);
        char *tab = strchr(ln, '\t');
        const char *nm = tab ? tab + 1 : ln;
        if (cnt > 0 && nm[0]) { snprintf(gauth[ngauth].name, 64, "%.63s", nm); gauth[ngauth].n = cnt; ngauth++; }
        ln = strtok_r(NULL, "\n", &save);
      } }

    /* Datei-Endungen aus ls-files zählen */
    ngext = 0;
    { char b[65536]; git_cap(path, "ls-files", b, sizeof b);
      char *save, *ln = strtok_r(b, "\n", &save);
      while (ln) {
        const char *base = strrchr(ln, '/'); base = base ? base + 1 : ln;
        const char *dot = strrchr(base, '.');
        char e[64];
        if (dot && dot != base) snprintf(e, sizeof e, "%.*s", (int)(sizeof e - 1),  dot + 1);
        else snprintf(e, sizeof e, "%s", "(kein)");
        tally_add(gext, &ngext, 8, e, 1);
        ln = strtok_r(NULL, "\n", &save);
      } }
    qsort(gext, (size_t)ngext, sizeof gext[0], tally_cmp);
    gitviz_github();
}

/* Repos unter den Wurzeln finden und auswählen (fzf-Stil). */
static void gitviz_pick(void) {
    static char buf[64 * 1024];
    static char *items[256];
    int n = 0; size_t used = 0;
    const char *home = getenv("HOME"); if (!home) home = ".";
    char roots[600];
    snprintf(roots, sizeof roots, "'%s' .", home);   /* HOME + cwd */
    char cmd[900];
    snprintf(cmd, sizeof cmd,
             "for r in %s; do find \"$r\" -maxdepth 4 -type d -name .git 2>/dev/null; done"
             " | sed 's:/\\.git$::' | sort -u | head -256", roots);
    FILE *p = popen(cmd, "r");
    if (p) {
        char line[600];
        while (fgets(line, sizeof line, p) && n < 256) {
            chomp(line);
            if (!line[0]) continue;
            size_t l = strlen(line) + 1;
            if (used + l >= sizeof buf) break;
            memcpy(buf + used, line, l); items[n++] = buf + used; used += l;
        }
        pclose(p);
    }
    if (!n) { setmsg("%s", L("Keine Git-Repos unter $HOME/cwd gefunden.",
                             "No git repos found under $HOME/cwd.")); return; }
    int i = fuzzy_pick(L("Git-Repo wählen", "Pick git repo"), items, n);
    if (i < 0) return;
    gitviz_load(items[i]);
    push(SC_GITVIZ);
}

/* Zusammenfassung ans aktive SERUM-Ziel (Claude/Ollama) oder Konnektor. */
static void gitviz_ask(void) {
    if (!grepo[0]) return;
    char log[2000], files[1500], q[4096];
    git_cap(grepo, "log --oneline -n 15", log, sizeof log);
    git_cap(grepo, "status --porcelain", files, sizeof files);
    snprintf(q, sizeof q,
             L("Erkläre dieses Git-Repo kurz und schlage die nächsten Schritte vor.\n"
               "Branch: %s (HEAD %s), %d Commits, %d Contributors.\n"
               "Letzte Commits:\n%s\n\nGeänderte Dateien:\n%s",
               "Briefly explain this git repo and propose next steps.\n"
               "Branch: %s (HEAD %s), %d commits, %d contributors.\n"
               "Recent commits:\n%s\n\nChanged files:\n%s"),
             gbranch, ghead, gcommits, ngauth, log, files[0] ? files : "(clean)");
    int serum = chat_serum_target(chat_target);
    if (serum >= 0) {
        char out[8192];
        setmsg("%s", L("Frage SERUM zum Repo …", "Asking SERUM about the repo …")); render();
        long code = serum_chat(serum, q, out, sizeof out);
        sanitize_text(out);
        if (out[0]) { pager_load(L("SERUM · Repo-Analyse", "SERUM · repo analysis"), out); push(SC_PAGER); }
        else setmsg(L("Keine Antwort (HTTP %ld) – Chat-Ziel/Healthcheck prüfen.",
                      "No reply (HTTP %ld) — check chat target/healthcheck."), code);
    } else {
        send_to_connector(q);
    }
}
