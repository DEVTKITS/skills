/* ------------------------------------------------------------------ */
/* Freestyle-Parser                                                    */
/* ------------------------------------------------------------------ */

static int freestyle_match(const char *text) {
    char lo[240]; size_t i;
    for (i = 0; text[i] && i + 1 < sizeof lo; i++)
        lo[i] = (char)tolower((unsigned char)text[i]);
    lo[i] = 0;
    static const struct { const char *kw; int cat; } map[] = {
        {"pitch", 0}, {"präsentation", 0}, {"presentation", 0}, {"vortrag", 0},
        {"folie", 1}, {"slide", 1},
        {"app", 2}, {"tool", 2}, {"cli", 2},
        {"site", 3}, {"web", 3}, {"landing", 3}, {"shop", 3}, {"blog", 3},
        {"dashboard", 4}, {"kpi", 4}, {"monitoring", 4},
        {"hub", 5}, {"wiki", 5},
        {"bericht", 6}, {"report", 6}, {"dokument", 6}, {"doc", 6},
        {"handbuch", 6}, {"whitepaper", 6},
        {"logo", 7}, {"grafik", 7}, {"bild", 7}, {"image", 7},
        {"diagramm", 7}, {"illustration", 7},
    };
    for (size_t m = 0; m < sizeof map / sizeof map[0]; m++)
        if (contains(lo, map[m].kw)) return map[m].cat;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Eingabe-Verarbeitung                                                */
/* ------------------------------------------------------------------ */

static int list_len(void) {
    switch (cur()->sc) {
        case SC_HOME: return NMENU + NCAT;
        case SC_SUB: return NSUB;
        case SC_EVAL: return NOPT;
        case SC_PLAN: return A.nst;
        case SC_PICK: return A.nskill;
        case SC_SKILLS: return A.nskill;
        case SC_PROFILES: return A.nprof;
        case SC_CONNECT: return NCONN;
        case SC_SETTINGS: return 6;
        case SC_TOOLS: return 7;
        case SC_MONITOR: return nsvc;
        case SC_SSH: return A.nssh;
        case SC_FILES: return nfent;
        case SC_WORKFLOWS: {
            char names[MAXWF][256];
            return list_workflows(names, MAXWF);
        }
        default: return 0;
    }
}

static void open_menu_entry(int i) {
    switch (i) {
        case 0: push(SC_WORKFLOWS); break;
        case 1: push(SC_SKILLS); break;
        case 2: push(SC_PROFILES); break;
        case 3: push(SC_CONNECT); break;
        case 4: push(SC_SETTINGS); break;
        case 5: push(SC_FREESTYLE); break;
        case 6: push(SC_TOOLS); break;
        case 7: push(SC_CHAT); break;
    }
}

static void start_eval(void) {
    A.qidx = 0; memset(A.ans, 0, sizeof A.ans);
    push(SC_EVAL);
}

static void confirm_eval_opt(int opt) {
    A.ans[A.qidx] = opt;
    A.qidx++;
    cur()->sel = 0;
    if (A.qidx >= NQ) {
        gen_plan();
        popscr();          /* EVAL verlassen */
        push(SC_PLAN);
        setmsg("%s", L("Plan erzeugt – Skills & Vorlagen pro Schritt anpassbar.",
                       "Plan generated — skills & templates adjustable per step."));
    }
}

static void nano_prompt(void) {
    char q[160] = "";
    A.nano_open = 1;
    render();
    if (prompt_input(L("Nanobot >", "Nanobot >"), q, sizeof q) && q[0]) {
        nano_add(L("Du:", "You:"), q);
        nano_answer(q);
    }
}

static void handle_enter(void) {
    Frame *f = cur();
    switch (f->sc) {
    case SC_HOME:
        if (f->sel < NMENU) open_menu_entry(f->sel);
        else { A.cat = f->sel - NMENU; push(SC_SUB); }
        break;
    case SC_SUB:
        A.sub = f->sel; start_eval();
        break;
    case SC_EVAL:
        confirm_eval_opt(f->sel);
        break;
    case SC_PLAN:
        A.pick_step = f->sel; push(SC_PICK);
        cur()->sel = A.st[A.pick_step].skill >= 0 ? A.st[A.pick_step].skill : 0;
        break;
    case SC_PICK: {
        Step *s = &A.st[A.pick_step];
        s->skill = f->sel;
        s->ai = A.sk[f->sel].ai;
        popscr();
        setmsg(L("Schritt %d nutzt jetzt Skill \"%s\"%s.",
                 "Step %d now uses skill \"%s\"%s."),
               A.pick_step + 1, A.sk[s->skill].name,
               s->ai ? L(" (KI)", " (AI)") : "");
        break;
    }
    case SC_PROFILES:
        A.aprof = f->sel;
        preset_apply(A.prof[A.aprof]);   /* Bundle: Konnektor+Chat-Ziel+Theme+Start */
        save_config();
        setmsg(L("Profil \"%s\" aktiv.", "Profile \"%s\" active."), A.prof[A.aprof]);
        break;
    case SC_CONNECT:
        A.conn_type = f->sel; save_config();
        setmsg("%s", L("Konnektor gesetzt.", "Connector set."));
        break;
    case SC_SETTINGS:
        if (f->sel == 0) { A.lang = !A.lang; save_config(); }
        else if (f->sel == 1) {
            if (A.contrast) setmsg("%s", L("Kontrast ist an – Theme fix dunkelschwarz.",
                                           "Contrast is on — theme locked to deep black."));
            else { A.theme = (A.theme + 1) % NTHEME; save_config(); }
        }
        else if (f->sel == 2) { A.contrast = !A.contrast; save_config(); }
        else if (f->sel == 3) {
            char t[160]; snprintf(t, sizeof t, "%s", A.tts);
            if (prompt_input(L("TTS-Befehl (%s = Datei) >", "TTS command (%s = file) >"),
                             t, sizeof t))
                { snprintf(A.tts, sizeof A.tts, "%s", t); save_config(); }
        }
        else if (f->sel == 4) first_run_setup(0);
        else if (f->sel == 5) { A.mod_rain = !A.mod_rain; save_config(); }
        break;
    case SC_TOOLS:
        if (f->sel == 0) push(SC_SSH);
        else if (f->sel == 1) shell_launch();
        else if (f->sel == 2) {
            const char *home = getenv("HOME"); if (!home) home = "/";
            snprintf(fstart, sizeof fstart, "%s", home);
            snprintf(fdir, sizeof fdir, "%s", home);
            files_load(fdir);
            push(SC_FILES);
        }
        else if (f->sel == 3) open_shell();
        else if (f->sel == 4) history_pick();
        else if (f->sel == 5) cheat_run();
        else { services_load(); push(SC_MONITOR); }
        break;
    case SC_CHAT:
        chat_send();
        break;
    case SC_MONITOR: {
        services_load();
        if (f->sel < nsvc && svc[f->sel].start[0]) run_capture_to_pager(svc[f->sel].start);
        break;
    }
    case SC_SSH:
        ssh_connect(f->sel);
        break;
    case SC_FILES:
        if (f->sel < nfent) {
            if (fent[f->sel].isdir) {
                size_t dl = strlen(fdir);
                snprintf(fdir + dl, sizeof fdir - dl, "/%s", fent[f->sel].name);
                files_load(fdir);
                f->sel = 0;
            } else {
                snprintf(A.pickpath, sizeof A.pickpath, "%s/%s", fdir, fent[f->sel].name);
                nano_add("→", A.pickpath);
                setmsg("%s", A.pickpath);
            }
        }
        break;
    case SC_WORKFLOWS: {
        char names[MAXWF][256];
        int n = list_workflows(names, MAXWF);
        if (f->sel < n)
            setmsg("%s/workflows/%s", cfgdir, names[f->sel]);
        break;
    }
    case SC_FREESTYLE: {
        char t[240] = "";
        if (prompt_input(L("Beschreibe frei >", "Describe freely >"), t, sizeof t) && t[0]) {
            int c = freestyle_match(t);
            if (c >= 0) {
                A.cat = c; A.sub = 0;
                setmsg(L("Erkannt: %s → Bedarfs-Check startet.",
                         "Detected: %s → needs check starting."),
                       A.lang ? cat_names[c].en : cat_names[c].de);
                push(SC_SUB);
            } else {
                setmsg("%s", L("Nicht zugeordnet – wähle eine Kachel oder formuliere anders.",
                               "Not matched — pick a tile or rephrase."));
            }
        }
        break;
    }
    default: break;
    }
}

static void handle_key(int k) {
    Frame *f = cur();
    int n = list_len();

    if (k == 'q') { save_config(); exit(0); }
    if (k == 't') {
        if (A.contrast)
            setmsg("%s", L("Kontrast an: Theme fix dunkelschwarz (k schaltet Kontrast aus).",
                           "Contrast on: theme locked to deep black (k turns contrast off)."));
        else { A.theme = (A.theme + 1) % NTHEME; save_config(); }
        return;
    }
    if (k == 'k') { A.contrast = !A.contrast; save_config(); return; }
    if (k == 'l') { A.lang = !A.lang; save_config(); return; }
    if (k == 'v') { char b[2048]; build_speech(b, sizeof b); speak(b); return; }
    if (k == 'n') { nano_prompt(); return; }
    if (k == '?') { push(SC_HELP); return; }
    if (k == 'm') { matrix_rain(); return; }

    /* Pager: eigene Scroll-Tasten (Esc fällt zum globalen Zurück durch) */
    if (f->sc == SC_PAGER && k != K_ESC) {
        int page = A.rows - 9; if (page < 1) page = 1;
        int maxtop = pager_n - page; if (maxtop < 0) maxtop = 0;
        if (k == K_UP)   { if (pager_top > 0) pager_top--; return; }
        if (k == K_DN)   { if (pager_top < maxtop) pager_top++; return; }
        if (k == K_PGUP) { pager_top -= page; if (pager_top < 0) pager_top = 0; return; }
        if (k == K_PGDN) { pager_top += page; if (pager_top > maxtop) pager_top = maxtop; return; }
        if (k == 'g') { pager_top = 0; return; }
        if (k == 'G') { pager_top = maxtop; return; }
    }

    /* KI-Chat: blättern + Ziel wechseln */
    if (f->sc == SC_CHAT && k != K_ESC) {
        int page = A.rows - 9; if (page < 1) page = 1;
        int maxs = chat_n - page; if (maxs < 0) maxs = 0;
        if (k == K_UP)   { if (chat_scroll < maxs) chat_scroll++; return; }
        if (k == K_DN)   { if (chat_scroll > 0) chat_scroll--; return; }
        if (k == K_PGUP) { chat_scroll += page; if (chat_scroll > maxs) chat_scroll = maxs; return; }
        if (k == K_PGDN) { chat_scroll -= page; if (chat_scroll < 0) chat_scroll = 0; return; }
        if (k == 'o') {
            chat_target = (chat_target + 1) % NCTARGET;
            setmsg(L("Chat-Ziel: %s", "Chat target: %s"),
                   A.lang ? chat_target_names[chat_target].en
                          : chat_target_names[chat_target].de);
            return;
        }
        if (k == 'p') {   /* Modell wählen */
            if (chat_target == CT_OLLAMA) {
                static char names[64][96];
                static char *items[64];
                int n = ollama_models(names, 64);
                if (!n) { setmsg("%s", L("Keine Ollama-Modelle – Healthcheck (Verbindungen h).",
                                         "No Ollama models — healthcheck (Connections h).")); return; }
                for (int i = 0; i < n; i++) items[i] = names[i];
                int i = fuzzy_pick(L("Ollama-Modell wählen", "Pick Ollama model"), items, n);
                if (i >= 0) { snprintf(A.ollama_model, sizeof A.ollama_model, "%s", names[i]);
                              save_config();
                              setmsg(L("Modell: %s", "Model: %s"), A.ollama_model); }
            } else if (chat_target == CT_ANTHROPIC) {
                char m[96]; snprintf(m, sizeof m, "%s", A.amodel);
                if (prompt_input(L("Anthropic-Modell >", "Anthropic model >"), m, sizeof m) && m[0])
                    { snprintf(A.amodel, sizeof A.amodel, "%s", m); save_config(); }
            } else if (chat_target == CT_OPENAI) {
                char m[96]; snprintf(m, sizeof m, "%s", A.oai_model);
                if (prompt_input(L("OpenAI-Modell >", "OpenAI model >"), m, sizeof m) && m[0])
                    { snprintf(A.oai_model, sizeof A.oai_model, "%s", m); save_config(); }
            } else setmsg("%s", L("Modellwahl nur für SERUM-Direktziele.",
                                  "Model pick only for SERUM direct targets."));
            return;
        }
        if (k == 'a') {   /* A2A-Triumvirat: 3 Rollen seriell (429-fest) */
            int serum = chat_serum_target(chat_target);
            if (serum < 0) { setmsg("%s", L("Trio nur für SERUM-Direktziele (o wechseln).",
                                            "Trio only for SERUM direct targets (switch with o).")); return; }
            char q[400] = "";
            if (!prompt_input(L("Frage ans Trio >", "Question to trio >"), q, sizeof q) || !q[0]) return;
            chat_add(L("Du  >", "You >"), q);
            static const struct { const char *tag; S2 persona; } roles[3] = {
                {"ARCHITEKT", {"Du bist Architekt & Datenmodell. Antworte knapp und strukturiert.",
                               "You are architect & data model. Answer concisely and structured."}},
                {"SECURITY",  {"Du bist Security & OCR. Prüfe Risiken, sei streng.",
                               "You are security & OCR. Check risks, be strict."}},
                {"INTEGRATION", {"Du bist Integration. Fokus auf Umsetzbarkeit und Schritte.",
                                 "You are integration. Focus on feasibility and steps."}},
            };
            for (int r = 0; r < 3; r++) {
                setmsg(L("Trio: Rolle %d/3 (%s) …", "Trio: role %d/3 (%s) …"), r + 1, roles[r].tag);
                render();
                if (in_waiting(1)) { if (readkey() == K_ESC) { setmsg("%s",
                    L("Trio abgebrochen.", "Trio aborted.")); break; } }
                char prompt[700], out[4096];
                snprintf(prompt, sizeof prompt, "%s\n\n%s",
                         A.lang ? roles[r].persona.en : roles[r].persona.de, q);
                long code = serum_chat(serum, prompt, out, sizeof out);
                sanitize_text(out);
                char who[16]; snprintf(who, sizeof who, "[%s]", roles[r].tag);
                chat_add(who, out[0] ? out : (code <= 0
                         ? L("(nicht erreichbar)", "(unreachable)")
                         : L("(kein Text)", "(no text)")));
            }
            setmsg("%s", L("Trio fertig.", "Trio done."));
            return;
        }
    }

    /* Dienst-Monitor: h = alle prüfen, a = neu, d = löschen */
    if (f->sc == SC_MONITOR) {
        if (k == 'h') { services_check_all(); return; }
        if (k == 'a') {
            if (nsvc >= MAXSVC) { setmsg("%s", L("Liste voll.", "List full.")); return; }
            char nm[40] = "", u[160] = "", st[160] = "";
            if (prompt_input(L("Dienst-Name >", "Service name >"), nm, sizeof nm) && nm[0]
                && prompt_input("URL >", u, sizeof u) && u[0]
                && prompt_input(L("Startbefehl (optional) >", "Start command (optional) >"), st, sizeof st)) {
                snprintf(svc[nsvc].name, 40, "%s", nm);
                snprintf(svc[nsvc].url, 160, "%s", u);
                snprintf(svc[nsvc].start, 160, "%s", st);
                svc[nsvc].up = -1; svc[nsvc].ms = -1; nsvc++;
                services_save();
                setmsg(L("Dienst \"%s\" angelegt.", "Service \"%s\" added."), nm);
            }
            return;
        }
        if (k == 'd') {
            if (f->sel < nsvc) {
                setmsg(L("Dienst \"%s\" gelöscht.", "Service \"%s\" deleted."), svc[f->sel].name);
                memmove(&svc[f->sel], &svc[f->sel + 1], (size_t)(nsvc - f->sel - 1) * sizeof svc[0]);
                nsvc--;
                if (f->sel >= nsvc && f->sel > 0) f->sel--;
                services_save();
            }
            return;
        }
    }

    /* Datei-Navigator: / = Fuzzy-Filter (fzf-Stil) */
    if (f->sc == SC_FILES && k == '/') {
        static char *items[MAXFENT];
        for (int i = 0; i < nfent; i++) items[i] = fent[i].name;
        int i = fuzzy_pick(fdir, items, nfent);
        if (i >= 0) f->sel = i;
        return;
    }

    /* Datei-Navigator: Esc/← geht hoch statt raus (bis zum Startordner) */
    if (f->sc == SC_FILES && (k == K_ESC || k == K_LT)) {
        if (!strcmp(fdir, fstart)) { popscr(); return; }
        char *sl = strrchr(fdir, '/');
        if (sl && sl != fdir) *sl = 0;
        else snprintf(fdir, sizeof fdir, "/");
        files_load(fdir);
        f->sel = 0;
        return;
    }

    if (k == K_ESC) {
        if (A.nano_open) { A.nano_open = 0; return; }
        if (f->sc == SC_EVAL && A.qidx > 0) { A.qidx--; f->sel = A.ans[A.qidx]; return; }
        popscr(); return;
    }

    /* Home: Kurztasten fürs Menü, Ziffern für Kacheln */
    if (f->sc == SC_HOME) {
        for (int i = 0; i < NMENU; i++)
            if (k == menu_keys[i]) { open_menu_entry(i); return; }
        if (k >= '1' && k <= '8') { A.cat = k - '1'; push(SC_SUB); return; }
    } else if (k >= '1' && k <= '9') {
        int idx = k - '1';
        if (idx < n) { f->sel = idx; handle_enter(); }
        return;
    }

    if (k == K_UP) {
        if (f->sc == SC_HOME && f->sel >= NMENU) {
            f->sel = f->sel - 4 >= NMENU ? f->sel - 4 : NMENU - 1;
        } else if (f->sel > 0) f->sel--;
        return;
    }
    if (k == K_DN) {
        if (f->sc == SC_HOME && f->sel < NMENU) {
            f->sel = f->sel == NMENU - 1 ? NMENU : f->sel + 1;
        } else if (f->sc == SC_HOME && f->sel >= NMENU) {
            if (f->sel + 4 < NMENU + NCAT) f->sel += 4;
        } else if (f->sel + 1 < n) f->sel++;
        return;
    }
    if (k == K_LT) { if (f->sel > 0) f->sel--; return; }
    if (k == K_RT) { if (f->sel + 1 < n) f->sel++; return; }
    if (k == K_ENTER || k == '\n') { handle_enter(); return; }

    /* Kontextaktionen */
    if (f->sc == SC_PLAN) {
        if (k == 'x') {
            char path[700];
            if (export_workflow(path, sizeof path))
                setmsg(L("✓ ComfyUI-Workflow: %s", "✓ ComfyUI workflow: %s"), path);
            else setmsg("%s", L("Export fehlgeschlagen.", "Export failed."));
            return;
        }
        if (k == 's') {
            char q[600] = "";
            size_t used = 0;
            int any = 0;
            for (int i = 0; i < A.nst; i++)
                if (A.st[i].ai) {
                    any = 1;
                    used += (size_t)snprintf(q + used, sizeof q - used, "%s%s",
                                             used ? "; " : "",
                                             A.lang ? A.st[i].label.en : A.st[i].label.de);
                    if (used >= sizeof q) break;
                }
            if (!any) {
                setmsg("%s", L("Keine KI-Schritte im Plan – alles läuft lokal.",
                               "No AI steps in plan — everything runs locally."));
            } else {
                char full[800];
                snprintf(full, sizeof full, "%s %s/%s: %s",
                         L("KI-Schritte für", "AI steps for"),
                         cat_names[A.cat].en, sub_names[A.cat][A.sub].en, q);
                send_to_connector(full);
            }
            return;
        }
    }
    if (f->sc == SC_PLAN && k == 'g') {
        A.st[f->sel].tmpl = (A.st[f->sel].tmpl + 1) % 4;
        return;
    }
    if (f->sc == SC_SKILLS) {
        if (k == 'i') { skill_import(); return; }
        if (k == 'a') {
            char nm[48] = "", ds[96] = "", ai[8] = "";
            if (prompt_input(L("Skill-Name >", "Skill name >"), nm, sizeof nm) && nm[0]
                && prompt_input(L("Beschreibung >", "Description >"), ds, sizeof ds)
                && prompt_input(L("KI-Skill? (j/n) >", "AI skill? (y/n) >"), ai, sizeof ai)) {
                if (A.nskill < MAXSKILL) {
                    SkillDef *s = &A.sk[A.nskill++];
                    snprintf(s->name, sizeof s->name, "%s", nm);
                    snprintf(s->desc, sizeof s->desc, "%s", ds[0] ? ds : "-");
                    s->ai = (ai[0] == 'j' || ai[0] == 'y' || ai[0] == 'J' || ai[0] == 'Y');
                    s->builtin = 0;
                    save_config();
                    setmsg(L("Skill \"%s\" angelegt%s.", "Skill \"%s\" added%s."),
                           s->name, s->ai ? L(" (KI)", " (AI)") : "");
                }
            }
            return;
        }
        if (k == 'd') {
            if (f->sel < A.nskill && !A.sk[f->sel].builtin) {
                setmsg(L("Skill \"%s\" gelöscht.", "Skill \"%s\" deleted."), A.sk[f->sel].name);
                memmove(&A.sk[f->sel], &A.sk[f->sel + 1],
                        (size_t)(A.nskill - f->sel - 1) * sizeof A.sk[0]);
                A.nskill--;
                if (f->sel >= A.nskill && f->sel > 0) f->sel--;
                save_config();
            } else setmsg("%s", L("Vorgebaute Skills sind fest.", "Prebuilt skills are fixed."));
            return;
        }
    }
    if (f->sc == SC_PROFILES) {
        if (k == 'a') {
            char nm[40] = "";
            if (prompt_input(L("Profil-Name >", "Profile name >"), nm, sizeof nm) && nm[0]
                && A.nprof < MAXPROF) {
                snprintf(A.prof[A.nprof++], sizeof A.prof[0], "%s", nm);
                save_config();
                setmsg(L("Profil \"%s\" angelegt.", "Profile \"%s\" added."), nm);
            }
            return;
        }
        if (k == 'd') {
            if (A.nprof > 1 && f->sel < A.nprof) {
                setmsg(L("Profil \"%s\" gelöscht.", "Profile \"%s\" deleted."), A.prof[f->sel]);
                memmove(&A.prof[f->sel], &A.prof[f->sel + 1],
                        (size_t)(A.nprof - f->sel - 1) * sizeof A.prof[0]);
                A.nprof--;
                if (A.aprof >= A.nprof) A.aprof = A.nprof - 1;
                if (f->sel >= A.nprof && f->sel > 0) f->sel--;
                save_config();
            }
            return;
        }
    }
    if (f->sc == SC_CONNECT) {
        if (k == 'u') {
            if (A.conn_type == CONN_MATRIX) {
                char u[256]; snprintf(u, sizeof u, "%s", A.matrix_hs);
                if (prompt_input(L("Matrix-Homeserver >", "Matrix homeserver >"), u, sizeof u))
                    { snprintf(A.matrix_hs, sizeof A.matrix_hs, "%s", u); save_config(); }
            } else {
                char u[256]; snprintf(u, sizeof u, "%s", A.conn_url);
                if (prompt_input("URL >", u, sizeof u))
                    { snprintf(A.conn_url, sizeof A.conn_url, "%s", u); save_config(); }
            }
            return;
        }
        if (k == 'y') {
            if (A.conn_type == CONN_MATRIX) {
                char kk[256] = "";
                if (prompt_input(L("Matrix-Access-Token >", "Matrix access token >"), kk, sizeof kk))
                    { snprintf(A.matrix_token, sizeof A.matrix_token, "%s", kk);
                      A.matrix_me[0] = 0; save_config(); }
            } else {
                char kk[128] = "";
                if (prompt_input(L("API-Key >", "API key >"), kk, sizeof kk))
                    { snprintf(A.conn_key, sizeof A.conn_key, "%s", kk); save_config(); }
            }
            return;
        }
        if (k == 'r') {
            char rr[128]; snprintf(rr, sizeof rr, "%s", A.matrix_room);
            if (prompt_input(L("Matrix-Raum-ID (!abc:matrix.org) >",
                               "Matrix room id (!abc:matrix.org) >"), rr, sizeof rr))
                { snprintf(A.matrix_room, sizeof A.matrix_room, "%s", rr); save_config(); }
            return;
        }
        if (k == 'b') {
            char bb[256]; snprintf(bb, sizeof bb, "%s", A.bridge_url);
            if (prompt_input(L("Bridge-URL >", "Bridge URL >"), bb, sizeof bb))
                { snprintf(A.bridge_url, sizeof A.bridge_url, "%s", bb); save_config(); }
            return;
        }
        if (k == 'p') {
            send_to_connector(L("Ping vom baukasten-Verbindungstest.",
                                "Ping from the baukasten connection test."));
            return;
        }
        if (k == 'h') {   /* SERUM-Healthcheck: o = Ollama, i = Anthropic, k = OpenAI */
            char c[8] = "";
            if (prompt_input(L("Healthcheck: o=Ollama a=Anthropic k=OpenAI >",
                               "Healthcheck: o=Ollama a=Anthropic k=OpenAI >"), c, sizeof c))
                serum_health(c[0] == 'a' ? 1 : c[0] == 'k' ? 2 : 0);
            return;
        }
        if (k == 'g') {   /* SERUM-Direktziele konfigurieren (URL/Modell/Key) */
            char v[256];
            snprintf(v, sizeof v, "%s", A.ollama_url);
            if (prompt_input(L("Ollama-URL (SSH-Tunnel localhost:11434) >",
                               "Ollama URL (SSH tunnel localhost:11434) >"), v, sizeof v))
                { snprintf(A.ollama_url, sizeof A.ollama_url, "%s", v); save_config(); }
            snprintf(v, sizeof v, "%s", A.oai_url);
            if (prompt_input(L("OpenAI-kompatible URL >", "OpenAI-compatible URL >"), v, sizeof v))
                { snprintf(A.oai_url, sizeof A.oai_url, "%s", v); save_config(); }
            char kk[256] = "";
            if (prompt_input(L("OpenAI-Key (leer = aus Env) >", "OpenAI key (empty = from env) >"),
                             kk, sizeof kk))
                { snprintf(A.oai_key, sizeof A.oai_key, "%s", kk); }
            return;
        }
    }
    if (f->sc == SC_SSH) {
        if (k == 'a') {
            if (A.nssh >= MAXSSH) { setmsg("%s", L("Liste voll.", "List full.")); return; }
            char nm[40] = "", ds[96] = "", po[8] = "";
            if (prompt_input(L("Profil-Name >", "Profile name >"), nm, sizeof nm) && nm[0]
                && prompt_input("user@host >", ds, sizeof ds) && ds[0]
                && prompt_input(L("Port (leer = 22) >", "Port (empty = 22) >"), po, sizeof po)) {
                if (!ssh_dest_ok(ds)) {
                    setmsg("%s", L("Ungültiges Ziel – nur Buchstaben/Zahlen @ . _ -",
                                   "Invalid target — letters/digits @ . _ - only"));
                    return;
                }
                SshProf *s = &A.sshp[A.nssh++];
                snprintf(s->name, sizeof s->name, "%s", nm);
                snprintf(s->dest, sizeof s->dest, "%s", ds);
                s->port = atoi(po); if (s->port <= 0 || s->port > 65535) s->port = 22;
                save_ssh();
                setmsg(L("SSH-Profil \"%s\" angelegt.", "SSH profile \"%s\" added."), nm);
            }
            return;
        }
        if (k == 'd') {
            if (f->sel < A.nssh) {
                setmsg(L("SSH-Profil \"%s\" gelöscht.", "SSH profile \"%s\" deleted."),
                       A.sshp[f->sel].name);
                memmove(&A.sshp[f->sel], &A.sshp[f->sel + 1],
                        (size_t)(A.nssh - f->sel - 1) * sizeof A.sshp[0]);
                A.nssh--;
                if (f->sel >= A.nssh && f->sel > 0) f->sel--;
                save_ssh();
            }
            return;
        }
    }
    if (f->sc == SC_WORKFLOWS && k == 'd') {
        char names[MAXWF][256];
        int wn = list_workflows(names, MAXWF);
        if (f->sel < wn) {
            char p[800]; snprintf(p, sizeof p, "%s/workflows/%s", cfgdir, names[f->sel]);
            if (unlink(p) == 0) setmsg(L("Gelöscht: %s", "Deleted: %s"), names[f->sel]);
            if (f->sel > 0) f->sel--;
        }
        return;
    }
}

/* ------------------------------------------------------------------ */

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    srand((unsigned)(time(NULL) ^ getpid()));
    ensure_dirs();
    int had_config = load_config();
    apply_theme();
    raw_on();
    if (!had_config) first_run_setup(1);   /* Ersteinrichtung + Rain-Intro */
    depth = 0; stk[0].sc = SC_HOME; stk[0].sel = NMENU; /* Start auf erster Kachel */
    setmsg("%s", "");
    nano_add("🤖", A.lang ? "Hi! Press n anytime to ask me about the current page."
                          : "Hi! Drück jederzeit n und frag mich zur aktuellen Seite.");
    A.nano_open = 0;
    for (;;) {
        render();
        int k = readkey();
        if (k < 0) { save_config(); exit(0); }   /* EOF / Lesefehler */
        if (k == 0) continue;
        A.msg[0] = 0;
        handle_key(k);
    }
}
