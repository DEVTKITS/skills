/* ------------------------------------------------------------------ */
/* Monitor — Dienst-Matrix (Cosmo/Odysseus „geschlossenes System")     */
/* Zweck · Port · Healthcheck · Startbefehl, alles lokal.              */
/* ------------------------------------------------------------------ */

static Service svc[MAXSVC];
static int nsvc, svc_loaded;

static void services_save(void) {
    char p[600]; snprintf(p, sizeof p, "%s/services.txt", cfgdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int i = 0; i < nsvc; i++)
        fprintf(f, "%s|%s|%s\n", svc[i].name, svc[i].url, svc[i].start);
    fclose(f);
}

static void services_load(void) {
    if (svc_loaded) return;
    svc_loaded = 1; nsvc = 0;
    char p[600]; snprintf(p, sizeof p, "%s/services.txt", cfgdir);
    FILE *f = fopen(p, "r");
    if (!f) {
        static const char *seed[][3] = {
            {"Odysseus",  "http://localhost:7000",  "odysseus up"},
            {"NanoChat",  "http://localhost:3050",  "nanochat serve"},
            {"OpenCode",  "http://localhost:9900",  "opencode serve"},
            {"Ollama",    "http://localhost:11434", "ollama serve"},
            {"ChromaDB",  "http://localhost:8100",  "chroma run --port 8100"},
            {"Voicebox",  "http://localhost:17493", "voicebox"},
        };
        f = fopen(p, "w");
        for (size_t i = 0; i < sizeof seed / sizeof seed[0] && nsvc < MAXSVC; i++) {
            if (f) fprintf(f, "%s|%s|%s\n", seed[i][0], seed[i][1], seed[i][2]);
            snprintf(svc[nsvc].name, 40, "%s", seed[i][0]);
            snprintf(svc[nsvc].url, 160, "%s", seed[i][1]);
            snprintf(svc[nsvc].start, 160, "%s", seed[i][2]);
            svc[nsvc].up = -1; svc[nsvc].ms = -1;
            nsvc++;
        }
        if (f) fclose(f);
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, f) && nsvc < MAXSVC) {
        chomp(line);
        char *p1 = strchr(line, '|'); if (!p1) continue; *p1 = 0;
        char *p2 = strchr(p1 + 1, '|'); if (!p2) continue; *p2 = 0;
        snprintf(svc[nsvc].name, 40, "%s", line);
        snprintf(svc[nsvc].url, 160, "%s", p1 + 1);
        snprintf(svc[nsvc].start, 160, "%s", p2 + 1);
        svc[nsvc].up = -1; svc[nsvc].ms = -1;
        nsvc++;
    }
    fclose(f);
}

/* Einen Dienst prüfen: curl -m 2 auf die URL, Statuscode>0 = up. */
static void service_check(int i) {
    if (i < 0 || i >= nsvc) return;
    if (!url_ok(svc[i].url)) { svc[i].up = 0; svc[i].ms = -1; return; }
    long t0 = (long)time(NULL);
    long code = curl_http("GET", svc[i].url, NULL, NULL, NULL, 0, 2);
    svc[i].up = code > 0;                 /* jede HTTP-Antwort zählt als „läuft" */
    svc[i].ms = ((long)time(NULL) - t0) * 1000;
}

static void services_check_all(void) {
    services_load();
    for (int i = 0; i < nsvc; i++) {
        setmsg(L("Prüfe Dienste … %d/%d", "Checking services … %d/%d"), i + 1, nsvc);
        render();
        service_check(i);
    }
    setmsg("%s", L("Healthcheck fertig.", "Healthcheck done."));
}
