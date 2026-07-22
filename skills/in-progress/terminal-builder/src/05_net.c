/* ------------------------------------------------------------------ */
/* Vorlesen (TTS) & Konnektor-Versand                                  */
/* ------------------------------------------------------------------ */

static const char *tts_template(void) {
    if (A.tts[0]) return A.tts;
    static char cached[160];
    if (cached[0]) return cached;
    FILE *p = popen("command -v espeak-ng espeak say spd-say 2>/dev/null | head -1", "r");
    if (p) {
        char line[256] = "";
        if (fgets(line, sizeof line, p)) chomp(line);
        pclose(p);
        const char *base = strrchr(line, '/'); base = base ? base + 1 : line;
        if (!strcmp(base, "espeak-ng") || !strcmp(base, "espeak"))
            snprintf(cached, sizeof cached, "%s -f %%s", base);
        else if (!strcmp(base, "say"))
            snprintf(cached, sizeof cached, "say -f %%s");
        else if (!strcmp(base, "spd-say"))
            snprintf(cached, sizeof cached, "spd-say \"$(cat %%s)\"");
    }
    return cached[0] ? cached : NULL;
}

static void speak(const char *text) {
    const char *tpl = tts_template();
    if (!tpl) {
        setmsg("%s", L("Kein TTS gefunden (espeak/say/spd-say) – Text bleibt sichtbar.",
                       "No TTS found (espeak/say/spd-say) — text stays on screen."));
        return;
    }
    char p[600]; snprintf(p, sizeof p, "%s/speak.txt", cfgdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    fputs(text, f); fclose(f);
    char cmd[900], base[760];
    snprintf(base, sizeof base, tpl, p);
    snprintf(cmd, sizeof cmd, "(%s) >/dev/null 2>&1 &", base);
    if (system(cmd) == -1) setmsg("%s", L("TTS-Start fehlgeschlagen.", "TTS launch failed."));
    else setmsg("%s", L("▶ Vorlesen gestartet.", "▶ Reading aloud."));
}

static void json_escape(char *dst, size_t cap, const char *s) {
    size_t d = 0;
    for (; *s && d + 6 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { dst[d++] = '\\'; dst[d++] = (char)c; }
        else if (c == '\n') { dst[d++] = '\\'; dst[d++] = 'n'; }
        else if (c < 32) { d += (size_t)snprintf(dst + d, cap - d, "\\u%04x", c); }
        else dst[d++] = (char)c;
    }
    dst[d] = 0;
}

static int url_ok(const char *u) {
    if (strncmp(u, "http://", 7) && strncmp(u, "https://", 8)) return 0;
    for (const char *p = u; *p; p++)
        if (!isalnum((unsigned char)*p) && !strchr(":/.?&=%_+~#-", *p)) return 0;
    return 1;
}

static void nano_add(const char *prefix, const char *text);
static void nano_add_wrapped(const char *prefix, const char *text, size_t maxtotal);
static void render(void);

/* letzte KI-Antwort (für den Chat-Screen zusätzlich zur Nanobot-Historie) */
static char last_reply[2048];

static void urlenc(char *dst, size_t cap, const char *s) {
    size_t d = 0;
    for (; *s && d + 4 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || strchr("._~-", c)) dst[d++] = (char)c;
        else d += (size_t)snprintf(dst + d, cap - d, "%%%02X", c);
    }
    dst[d] = 0;
}

static void sanitize_text(char *s) {
    for (; *s; s++)
        if ((unsigned char)*s < 32) *s = ' ';
}

/* "key":"wert" mit Backslash-Zustandsmaschine extrahieren (kein naives strchr) */
static int json_find_string(const char *json, const char *key, char *out, size_t cap) {
    char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (*p != '"') return 0;
    p++;
    size_t d = 0;
    while (*p && *p != '"' && d + 2 < cap) {
        if (*p == '\\') {
            p++;
            if (!*p) break;
            if (*p == 'n' || *p == 't') out[d++] = ' ';
            else if (*p == 'r') { /* weg */ }
            else if (*p == 'u') { out[d++] = '?'; for (int i = 0; i < 4 && p[1]; i++) p++; }
            else out[d++] = *p;
            p++;
        } else out[d++] = *p++;
    }
    out[d] = 0;
    return 1;
}

static long json_find_number(const char *json, const char *key) {
    char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    return atol(p);
}

/* Header-Token säubern (nur Zeichen, die in einem HTTP-Header-Wert
 * unkritisch sind) — gegen Shell-/Header-Injection. */
static void header_clean(char *dst, size_t cap, const char *s) {
    size_t d = 0;
    for (; *s && d + 1 < cap; s++)
        if (isalnum((unsigned char)*s) || strchr("._-:/+= ", *s)) dst[d++] = *s;
    dst[d] = 0;
}

/* Ein curl fuer alles, mit optionalen Zusatz-Headern (xheaders: fertige
 * "-H '…' "-Segmente, deren Werte bereits gesäubert sind).
 * Payload via --data @file, Response nach response.tmp. HTTP-Code, -1 bei Fehler. */
static long curl_http_x(const char *method, const char *url, const char *bearer,
                        const char *xheaders, const char *body,
                        char *resp, size_t rcap, int timeout_s) {
    if (resp && rcap) resp[0] = 0;
    if (!url_ok(url)) return -1;
    char pf[600], rf[600], data[700] = "";
    snprintf(rf, sizeof rf, "%s/response.tmp", cfgdir);
    unlink(rf);
    if (body) {
        snprintf(pf, sizeof pf, "%s/payload.json", cfgdir);
        FILE *f = fopen(pf, "w"); if (!f) return -1;
        fputs(body, f); fclose(f);
        snprintf(data, sizeof data, "--data @'%s' ", pf);
    }
    char auth[300] = "";
    if (bearer && bearer[0]) {
        char kk[220]; size_t d = 0;
        for (const char *q = bearer; *q && d + 1 < sizeof kk; q++)
            if (isalnum((unsigned char)*q) || strchr("._-", *q)) kk[d++] = *q;
        kk[d] = 0;
        snprintf(auth, sizeof auth, "-H 'Authorization: Bearer %s' ", kk);
    }
    char cmd[2600];
    snprintf(cmd, sizeof cmd,
             "curl -m %d -s -o '%s' -w '%%{http_code}' -X %s "
             "-H 'Content-Type: application/json' %s%s%s'%s' 2>/dev/null",
             timeout_s, rf, method, auth, xheaders ? xheaders : "", data, url);
    FILE *cp = popen(cmd, "r");
    if (!cp) return -1;
    char code[16] = "";
    if (!fgets(code, sizeof code, cp)) code[0] = 0;
    pclose(cp);
    long http = atol(code);
    if (resp && rcap > 1) {
        FILE *f = fopen(rf, "r");
        if (f) { size_t n = fread(resp, 1, rcap - 1, f); resp[n] = 0; fclose(f); }
    }
    return http > 0 ? http : -1;
}

static long curl_http(const char *method, const char *url, const char *bearer,
                      const char *body, char *resp, size_t rcap, int timeout_s) {
    return curl_http_x(method, url, bearer, NULL, body, resp, rcap, timeout_s);
}

/* Mit Backoff wiederholen, solange die Verbindung scheitert (-1/0).
 * Abbrechbar mit Esc; UI bleibt via render() lebendig. */
static long curl_retry(const char *method, const char *url, const char *bearer,
                       const char *xheaders, const char *body,
                       char *resp, size_t rcap, int timeout_s) {
    long code = curl_http_x(method, url, bearer, xheaders, body, resp, rcap, timeout_s);
    int wait = 2;
    for (int try = 0; try < 2 && code <= 0; try++) {
        setmsg(L("Verbindung fehlgeschlagen – neuer Versuch in %ds (Esc bricht ab) …",
                 "Connection failed — retrying in %ds (Esc aborts) …"), wait);
        render();
        if (in_waiting(wait * 1000)) { if (readkey() == K_ESC) return code; }
        code = curl_http_x(method, url, bearer, xheaders, body, resp, rcap, timeout_s);
        wait *= 2;
    }
    return code;
}

/* ---- Matrix (Client-Server-API), passend zur DkZ-Bridge-Einrichtung ---- */

static const char *matrix_hs_get(void) {
    return A.matrix_hs[0] ? A.matrix_hs : "https://matrix.org";
}
static const char *matrix_token_get(void) {
    if (A.matrix_token[0]) return A.matrix_token;
    const char *e = getenv("MATRIX_ACCESS_TOKEN");
    return (e && e[0]) ? e : NULL;
}
static const char *matrix_room_get(void) {
    if (A.matrix_room[0]) return A.matrix_room;
    const char *e = getenv("MATRIX_ROOM");
    return (e && e[0]) ? e : NULL;
}

static int matrix_whoami(void) {
    if (A.matrix_me[0]) return 1;
    char url[600], resp[1024];
    snprintf(url, sizeof url, "%s/_matrix/client/v3/account/whoami", matrix_hs_get());
    if (curl_http("GET", url, matrix_token_get(), NULL, resp, sizeof resp, 6) / 100 != 2)
        return 0;
    return json_find_string(resp, "user_id", A.matrix_me, sizeof A.matrix_me);
}

static void matrix_send_and_poll(const char *question) {
    const char *tok = matrix_token_get(), *room = matrix_room_get();
    if (!tok || !room) {
        setmsg("%s", L("Matrix: Token/Raum fehlen – unter Verbindungen y/r setzen "
                       "(oder MATRIX_ACCESS_TOKEN/MATRIX_ROOM exportieren).",
                       "Matrix: token/room missing — set y/r under Connections "
                       "(or export MATRIX_ACCESS_TOKEN/MATRIX_ROOM)."));
        return;
    }
    char roomenc[300]; urlenc(roomenc, sizeof roomenc, room);
    static int txn_ctr;
    char url[800], body[900], eq[600], resp[8192];
    json_escape(eq, sizeof eq, question);
    snprintf(url, sizeof url,
             "%s/_matrix/client/v3/rooms/%s/send/m.room.message/bk%ld_%d",
             matrix_hs_get(), roomenc, (long)time(NULL), ++txn_ctr);
    snprintf(body, sizeof body, "{\"msgtype\":\"m.text\",\"body\":\"%s\"}", eq);
    long code = curl_http("PUT", url, tok, body, resp, sizeof resp, 8);
    if (code / 100 != 2) {
        setmsg(L("Matrix-Versand fehlgeschlagen (HTTP %ld) – Token/Raum/Netz pruefen.",
                 "Matrix send failed (HTTP %ld) — check token/room/network."), code);
        return;
    }
    char myeid[200] = "";
    json_find_string(resp, "event_id", myeid, sizeof myeid);
    long sent_ts = (long)time(NULL) * 1000L;
    matrix_whoami();
    nano_add("→", L("An Matrix gesendet – warte auf Antwort …",
                    "Sent to Matrix — waiting for reply …"));
    A.nano_open = 1;

    for (int i = 1; i <= 15; i++) {
        setmsg(L("Warte auf Matrix-Antwort … %d/15 (Esc bricht ab)",
                 "Waiting for Matrix reply … %d/15 (Esc aborts)"), i);
        render();
        if (in_waiting(2000)) { if (readkey() == K_ESC) { setmsg("%s",
            L("Warten abgebrochen.", "Wait aborted.")); return; } }
        snprintf(url, sizeof url,
                 "%s/_matrix/client/v3/rooms/%s/messages?dir=b&limit=5",
                 matrix_hs_get(), roomenc);
        if (curl_http("GET", url, tok, NULL, resp, sizeof resp, 6) / 100 != 2)
            continue;
        /* dir=b: neueste zuerst; bei unserem event_id aufhoeren (alles danach ist aelter) */
        const char *ev = strstr(resp, "\"event_id\"");
        while (ev) {
            const char *nxt = strstr(ev + 10, "\"event_id\"");
            size_t slen = nxt ? (size_t)(nxt - ev) : strlen(ev);
            char slice[4096];
            if (slen >= sizeof slice) slen = sizeof slice - 1;
            memcpy(slice, ev, slen); slice[slen] = 0;
            char eid[200] = "", sender[200] = "", mbody[1024] = "";
            json_find_string(slice, "event_id", eid, sizeof eid);
            if (myeid[0] && !strcmp(eid, myeid)) break;
            json_find_string(slice, "sender", sender, sizeof sender);
            long ts = json_find_number(slice, "origin_server_ts");
            if (strstr(slice, "m.room.message")
                && json_find_string(slice, "body", mbody, sizeof mbody)
                && sender[0] && strcmp(sender, A.matrix_me)
                && (!myeid[0] ? ts >= sent_ts - 2000 : 1)) {
                sanitize_text(mbody);
                snprintf(last_reply, sizeof last_reply, "%s", mbody);
                nano_add_wrapped("⚡", mbody, 300);
                A.nano_open = 1;
                setmsg(L("✓ Antwort von %s", "✓ Reply from %s"), sender);
                return;
            }
            ev = nxt;
        }
    }
    setmsg("%s", L("Keine Antwort in 30 s – Raum/Token pruefen (Nanobot ! sendet erneut).",
                   "No reply within 30 s — check room/token (nanobot ! resends)."));
}

/* ---- DkZ NanoChat-Bridge (HTTP, Default localhost:3050) ---- */

static void bridge_send(const char *question) {
    char eq[600], body[900], resp[2048];
    json_escape(eq, sizeof eq, question);
    snprintf(body, sizeof body,
             "{\"agent\":\"baukasten\",\"status\":\"frage\",\"befunde\":\"%s\","
             "\"naechste_aktion\":\"antwort_an_baukasten\"}", eq);
    long code = curl_http("POST", A.bridge_url, NULL, body, resp, sizeof resp, 5);
    if (code < 0 || code == 0) {
        setmsg(L("Bridge auf %s nicht erreichbar – laeuft der Dienst?",
                 "Bridge at %s unreachable — is the service running?"), A.bridge_url);
        return;
    }
    if (code / 100 == 2) {
        sanitize_text(resp);
        if (resp[0]) {
            snprintf(last_reply, sizeof last_reply, "%s", resp);
            nano_add_wrapped("⚡", resp, 300); A.nano_open = 1;
        }
        setmsg(L("✓ Bridge hat geantwortet (HTTP %ld)", "✓ Bridge replied (HTTP %ld)"), code);
    } else
        setmsg(L("Bridge-Fehler (HTTP %ld)", "Bridge error (HTTP %ld)"), code);
}

/* KI-Anfrage über den konfigurierten Konnektor schicken — MIT Rückkanal:
 * Antworten landen in der Nanobot-Historie statt im Nirwana. */
static void send_to_connector(const char *question) {
    if (A.conn_type == CONN_NONE) {
        setmsg("%s", L("Kein Konnektor konfiguriert → Verbindungen (c).",
                       "No connector configured → Connections (c)."));
        return;
    }
    if (A.conn_type == CONN_MATRIX) { matrix_send_and_poll(question); return; }
    if (A.conn_type == CONN_BRIDGE) { bridge_send(question); return; }

    char eq[600], ep[200];
    json_escape(eq, sizeof eq, question);
    json_escape(ep, sizeof ep, A.prof[A.aprof]);

    char payload[1200];
    if (A.conn_type == CONN_DISCORD)
        snprintf(payload, sizeof payload,
                 "{\"content\":\"[baukasten/%s] %s\"}", ep, eq);
    else
        snprintf(payload, sizeof payload,
                 "{\"source\":\"baukasten\",\"profile\":\"%s\",\"screen\":\"%s\","
                 "\"lang\":\"%s\",\"question\":\"%s\"}",
                 ep, screen_name(), A.lang ? "en" : "de", eq);

    if (A.conn_type == CONN_MCP) {
        char p[600]; snprintf(p, sizeof p, "%s/outbox.jsonl", cfgdir);
        FILE *f = fopen(p, "a");
        if (!f) { setmsg("%s", L("Outbox nicht schreibbar.", "Outbox not writable.")); return; }
        fprintf(f, "%s\n", payload); fclose(f);
        setmsg(L("→ MCP-Outbox: %s", "→ MCP outbox: %s"), p);
        return;
    }
    if (!url_ok(A.conn_url)) {
        setmsg("%s", L("Ungültige URL – bitte unter Verbindungen setzen.",
                       "Invalid URL — set it under Connections."));
        return;
    }
    char resp[1024];
    long code = curl_http("POST", A.conn_url,
                          (A.conn_type == CONN_GATEWAY && A.conn_key[0]) ? A.conn_key : NULL,
                          payload, resp, sizeof resp, 8);
    if (code / 100 == 2) {
        sanitize_text(resp);
        if (resp[0]) {
            snprintf(last_reply, sizeof last_reply, "%s", resp);
            nano_add_wrapped("⚡", resp, 300); A.nano_open = 1;
        }
        setmsg(L("✓ Gesendet über %s (HTTP %ld)", "✓ Sent via %s (HTTP %ld)"),
               A.lang ? conn_names[A.conn_type].en : conn_names[A.conn_type].de, code);
    } else
        setmsg(L("Versand fehlgeschlagen (HTTP %ld) – curl/Netz prüfen.",
                 "Send failed (HTTP %ld) — check curl/network."), code);
}


/* ------------------------------------------------------------------ */
/* SERUM — Direkt-LLM-Adapter (Ollama / Anthropic / OpenAI-kompatibel) */
/* ------------------------------------------------------------------ */

/* Alias-Tabelle: Frontend-Modellname -> echtes Ollama-Tag.
 * Behebt "model 'qwen3.5:9b' not found" transparent (Cosmo/Odysseus-Erbe). */
#define MAXALIAS 32
static char alias_from[MAXALIAS][96], alias_to[MAXALIAS][96];
static int  nalias, alias_loaded;

static void aliases_load(void) {
    if (alias_loaded) return;
    alias_loaded = 1; nalias = 0;
    char p[600]; snprintf(p, sizeof p, "%s/aliases.txt", cfgdir);
    FILE *f = fopen(p, "r");
    if (!f) {
        static const char *seed[][2] = {
            {"qwen3.6:27b", "qwen2.5:32b"}, {"qwen3.5:27b", "qwen2.5:32b"},
            {"qwen3.5:9b", "qwen2.5:7b"}, {"qwen3.5:3b", "qwen2.5:3b"},
        };
        f = fopen(p, "w");
        for (size_t i = 0; i < sizeof seed / sizeof seed[0] && nalias < MAXALIAS; i++) {
            if (f) fprintf(f, "%s|%s\n", seed[i][0], seed[i][1]);
            snprintf(alias_from[nalias], 96, "%s", seed[i][0]);
            snprintf(alias_to[nalias], 96, "%s", seed[i][1]);
            nalias++;
        }
        if (f) fclose(f);
        return;
    }
    char line[256];
    while (fgets(line, sizeof line, f) && nalias < MAXALIAS) {
        chomp(line);
        char *bar = strchr(line, '|'); if (!bar) continue;
        *bar = 0;
        snprintf(alias_from[nalias], 96, "%s", line);
        snprintf(alias_to[nalias], 96, "%s", bar + 1);
        nalias++;
    }
    fclose(f);
}

/* Übersetzt einen Modellnamen; meldet die Übersetzung transparent im Nanobot. */
static const char *alias_translate(const char *model) {
    aliases_load();
    for (int i = 0; i < nalias; i++)
        if (!strcmp(model, alias_from[i])) {
            char note[220];
            snprintf(note, sizeof note, L("Alias: %s → %s", "alias: %s → %s"),
                     alias_from[i], alias_to[i]);
            nano_add("↺", note);
            return alias_to[i];
        }
    return model;
}

static const char *anthropic_key_get(void) {
    const char *e = getenv("ANTHROPIC_API_KEY");
    return (e && e[0]) ? e : NULL;
}
static const char *openai_key_get(void) {
    if (A.oai_key[0]) return A.oai_key;
    const char *e = getenv("OPENAI_API_KEY");
    return (e && e[0]) ? e : NULL;
}

/* GET {ollama}/api/tags → alle "name":"…" in names[] (max), Anzahl zurück. */
static int ollama_models(char names[][96], int max) {
    char url[320], resp[8192];
    snprintf(url, sizeof url, "%s/api/tags", A.ollama_url);
    if (curl_http("GET", url, NULL, NULL, resp, sizeof resp, 5) / 100 != 2) return 0;
    int n = 0;
    const char *p = resp;
    while (n < max && (p = strstr(p, "\"name\"")) != NULL) {
        char nm[96];
        if (json_find_string(p, "name", nm, sizeof nm) && nm[0]) {
            int dup = 0;
            for (int i = 0; i < n; i++) if (!strcmp(names[i], nm)) { dup = 1; break; }
            if (!dup) snprintf(names[n++], 96, "%s", nm);
        }
        p += 6;
    }
    return n;
}

/* Gemeinsamer Chat-Aufruf. Rückgabe: HTTP-Code (<=0 = Netzfehler),
 * Antworttext in out. target: 0=Ollama 1=Anthropic 2=OpenAI. */
static long serum_chat(int target, const char *prompt, char *out, size_t ocap) {
    char eq[3000]; json_escape(eq, sizeof eq, prompt);
    if (out && ocap) out[0] = 0;

    if (target == 0) {
        const char *model = alias_translate(A.ollama_model[0] ? A.ollama_model : "qwen2.5:7b");
        char url[320], body[3400], resp[16384];
        snprintf(url, sizeof url, "%s/api/chat", A.ollama_url);
        snprintf(body, sizeof body,
                 "{\"model\":\"%s\",\"stream\":false,"
                 "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}", model, eq);
        long code = curl_retry("POST", url, NULL, NULL, body, resp, sizeof resp, 120);
        if (code / 100 == 2) json_find_string(resp, "content", out, ocap);
        else if (strstr(resp, "not found"))
            snprintf(out, ocap, L("Modell nicht gefunden – Name/Alias prüfen (%s).",
                                  "model not found — check name/alias (%s)."), model);
        return code;
    }
    if (target == 1) {
        const char *key = anthropic_key_get();
        if (!key) { snprintf(out, ocap, "%s",
            L("ANTHROPIC_API_KEY nicht gesetzt (Umgebungsvariable).",
              "ANTHROPIC_API_KEY not set (environment variable).")); return -2; }
        char kh[300], xh[360], body[3400], resp[16384];
        header_clean(kh, sizeof kh, key);
        snprintf(xh, sizeof xh,
                 "-H 'x-api-key: %s' -H 'anthropic-version: 2023-06-01' ", kh);
        snprintf(body, sizeof body,
                 "{\"model\":\"%s\",\"max_tokens\":1024,"
                 "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
                 A.amodel[0] ? A.amodel : "claude-haiku-4-5-20251001", eq);
        long code = curl_retry("POST", "https://api.anthropic.com/v1/messages",
                               NULL, xh, body, resp, sizeof resp, 60);
        if (code / 100 == 2) json_find_string(resp, "text", out, ocap);
        else json_find_string(resp, "message", out, ocap);
        return code;
    }
    /* target == 2: OpenAI-kompatibel */
    const char *key = openai_key_get();
    char url[320], body[3400], resp[16384];
    snprintf(url, sizeof url, "%s/v1/chat/completions", A.oai_url);
    snprintf(body, sizeof body,
             "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
             A.oai_model[0] ? A.oai_model : "gpt-4o-mini", eq);
    long code = curl_retry("POST", url, key, NULL, body, resp, sizeof resp, 90);
    if (code / 100 == 2) json_find_string(resp, "content", out, ocap);
    else json_find_string(resp, "message", out, ocap);
    return code;
}

/* Healthcheck für ein Direkt-Ziel: Latenz grob, Modellzahl bei Ollama. */
static void serum_health(int target) {
    char resp[8192];
    if (target == 0) {
        char names[64][96];
        int n = ollama_models(names, 64);
        if (n) setmsg(L("✓ Ollama erreichbar – %d Modelle.", "✓ Ollama up — %d models."), n);
        else setmsg(L("Ollama nicht erreichbar (%s) – Tunnel/Dienst prüfen.",
                      "Ollama unreachable (%s) — check tunnel/service."), A.ollama_url);
    } else if (target == 1) {
        setmsg("%s", anthropic_key_get()
               ? L("Anthropic-Key gesetzt (aus Env).", "Anthropic key set (from env).")
               : L("Anthropic-Key fehlt (ANTHROPIC_API_KEY).", "Anthropic key missing (ANTHROPIC_API_KEY)."));
    } else {
        long code = curl_http("GET", A.oai_url, NULL, NULL, resp, sizeof resp, 5);
        if (code > 0)
            setmsg(L("OpenAI-Endpoint antwortet (HTTP %ld).",
                     "OpenAI endpoint responds (HTTP %ld)."), code);
        else
            setmsg(L("OpenAI-Endpoint nicht erreichbar (%s).",
                     "OpenAI endpoint unreachable (%s)."), A.oai_url);
    }
}
