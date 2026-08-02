/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

/* Cosmo-Preset-Bundle: setzt Konnektor + Chat-Ziel + Theme anhand des
 * Profilnamens (desktop·vps·agent·builder·monitor). Unbekannte Namen
 * (eigene Profile) lassen alles unverändert. */
static void preset_apply(const char *name) {
    if (!strcmp(name, "desktop")) { A.theme = TH_COSMO; }
    else if (!strcmp(name, "vps")) { A.theme = TH_MATRIX; chat_target = CT_OLLAMA;
        A.conn_type = CONN_MATRIX; }
    else if (!strcmp(name, "agent")) { A.theme = TH_DUNKEL; chat_target = CT_HERMES;
        A.conn_type = CONN_BRIDGE; }
    else if (!strcmp(name, "builder")) { A.theme = TH_COSMO; A.conn_type = CONN_MCP; }
    else if (!strcmp(name, "monitor")) { A.contrast = 1; }
    apply_theme();
}

static void draw_header(void) {
    ln("%s ▚▚ BAUKASTEN%s %s· %s%s", th.head,
       chat_serum_target(chat_target) >= 0 ? "·SERUM" : "", th.dim,
       th.head, screen_name());
    ln("%s %s: %s%s%s  %s: %s%s%s  %s%s · %s · %s%s",
       th.dim, L("Profil", "Profile"), th.hi, A.prof[A.aprof], th.dim,
       L("Konnektor", "Connector"), th.hi,
       A.lang ? conn_names[A.conn_type].en : conn_names[A.conn_type].de, th.dim,
       th.dim, A.lang ? "EN" : "DE",
       A.contrast ? L("KONTRAST", "CONTRAST")
                  : (A.lang ? theme_names[A.theme].en : theme_names[A.theme].de),
       L("offline-first", "offline-first"), "");
    ln("%s%.*s", th.box,
       A.cols * 3, "──────────────────────────────────────────────────────────"
       "──────────────────────────────────────────────────────────"
       "──────────────────────────────────────────────────────────");
}

static void draw_item(int idx, int selected, const char *numkey, const char *text,
                      const char *suffix) {
    (void)idx;
    if (selected)
        ln("  %s %s  %s %s\x1b[0m%s%s", th.sel, numkey, text,
           suffix ? suffix : "", th.bg, th.fg);
    else
        ln("  %s%s %s%s  %s%s%s", th.hi, numkey, th.fg, "", text,
           th.dim, suffix ? suffix : "");
}

static void draw_tiles(int sel_offset) {
    /* Kacheln in 2 Reihen à 4, nummeriert 1–8 */
    int tw = (A.cols - 6) / 4; if (tw > 22) tw = 22; if (tw < 12) tw = 12;
    char lab[96], padd[96];
    for (int row = 0; row < 2; row++) {
        for (int part = 0; part < 3; part++) {
            o("%s%s  ", th.bg, th.box);
            for (int col = 0; col < 4; col++) {
                int i = row * 4 + col;
                int selected = (cur()->sel == sel_offset + i);
                const char *edge = selected ? (A.contrast ? th.hi : th.hi) : th.box;
                if (part == 0) {
                    o("%s┌", edge);
                    for (int x = 0; x < tw - 2; x++) o("─");
                    o("┐ ");
                } else if (part == 1) {
                    snprintf(lab, sizeof lab, "%d %s", i + 1,
                             A.lang ? cat_names[i].en : cat_names[i].de);
                    fitpad(padd, sizeof padd, lab, tw - 4);
                    if (selected)
                        o("%s│%s %s \x1b[0m%s%s│ ", edge, th.sel, padd, th.bg, edge);
                    else
                        o("%s│ %s%s%s │ ", edge, th.fg, padd, edge);
                } else {
                    o("%s└", edge);
                    for (int x = 0; x < tw - 2; x++) o("─");
                    o("┘ ");
                }
            }
            o("%s\x1b[K\r\n", th.fg);
        }
    }
}

static void draw_home(void) {
    ln("%s %s", th.head, L("Was willst du bauen? Unten Kacheln 1–8, oben das Menü.",
                           "What do you want to build? Tiles 1–8 below, menu above."));
    ln("");
    for (int i = 0; i < NMENU; i++) {
        char key[8]; snprintf(key, sizeof key, "[%c]", menu_keys[i]);
        draw_item(i, cur()->sel == i, key,
                  A.lang ? menu_names[i].en : menu_names[i].de, "");
    }
    ln("");
    ln("%s %s", th.head, L("── Kacheln ──", "── Tiles ──"));
    draw_tiles(NMENU);
}

static void draw_list_screen(void) {
    Frame *f = cur();
    switch (f->sc) {
    case SC_SUB:
        ln("%s %s", th.head,
           L("Typ wählen – die Auswahl fächert den Bedarfs-Check auf:",
             "Pick a type — the choice fans out into the needs check:"));
        ln("");
        for (int i = 0; i < NSUB; i++) {
            char k[16]; snprintf(k, sizeof k, "%d.", i + 1);
            draw_item(i, f->sel == i, k,
                      A.lang ? sub_names[A.cat][i].en : sub_names[A.cat][i].de, "");
        }
        break;
    case SC_EVAL: {
        ln("%s %s %d/%d — %s", th.head, L("Frage", "Question"), A.qidx + 1, NQ,
           A.lang ? q_names[A.qidx].en : q_names[A.qidx].de);
        ln("%s %s", th.dim,
           L("(Je genauer die Auswahl, desto besser passt der Plan.)",
             "(The tighter the choice, the better the plan fits.)"));
        ln("");
        for (int i = 0; i < NOPT; i++) {
            char k[16]; snprintf(k, sizeof k, "%d.", i + 1);
            draw_item(i, f->sel == i, k,
                      A.lang ? q_opts[A.qidx][i].en : q_opts[A.qidx][i].de, "");
        }
        break;
    }
    case SC_PLAN: {
        ln("%s %s: %s → %s", th.head, L("Plan", "Plan"),
           A.lang ? cat_names[A.cat].en : cat_names[A.cat].de,
           A.lang ? sub_names[A.cat][A.sub].en : sub_names[A.cat][A.sub].de);
        ln("%s %s", th.dim,
           L("Enter = Skill tauschen · g = Vorlage wechseln · x = ComfyUI-Export · s = KI-Schritte senden",
             "Enter = swap skill · g = switch template · x = ComfyUI export · s = send AI steps"));
        ln("");
        for (int i = 0; i < A.nst; i++) {
            Step *s = &A.st[i];
            char k[16]; snprintf(k, sizeof k, "%d.", i + 1);
            char text[220], suf[160];
            snprintf(text, sizeof text, "%s",
                     A.lang ? s->label.en : s->label.de);
            snprintf(suf, sizeof suf, "  [%s: %s | %s%s%s]",
                     s->skill >= 0 ? A.sk[s->skill].name : L("manuell", "manual"),
                     A.lang ? tmpl_names[s->tmpl].en : tmpl_names[s->tmpl].de,
                     s->ai ? th.hi : "",
                     s->ai ? L("KI ⚡", "AI ⚡") : L("lokal", "local"),
                     s->ai ? th.dim : "");
            draw_item(i, f->sel == i, k, text, suf);
        }
        break;
    }
    case SC_PICK: {
        Step *s = &A.st[A.pick_step];
        ln("%s %s: %s", th.head, L("Skill für Schritt", "Skill for step"),
           A.lang ? s->label.en : s->label.de);
        ln("");
        for (int i = 0; i < A.nskill; i++) {
            char k[16]; snprintf(k, sizeof k, i < 9 ? "%d." : "  ", i + 1);
            char suf[160];
            snprintf(suf, sizeof suf, "  — %s%s", A.sk[i].desc,
                     A.sk[i].ai ? L("  [KI]", "  [AI]") : "");
            draw_item(i, f->sel == i, k, A.sk[i].name, suf);
        }
        break;
    }
    case SC_WORKFLOWS: {
        char names[MAXWF][256];
        int n = list_workflows(names, MAXWF);
        ln("%s %s (%d)", th.head,
           L("Exportierte ComfyUI-Workflows", "Exported ComfyUI workflows"), n);
        ln("%s %s/workflows", th.dim, cfgdir);
        ln("");
        if (!n) ln("  %s%s", th.dim,
                   L("Noch keine – im Plan mit x exportieren.",
                     "None yet — export from a plan with x."));
        for (int i = 0; i < n; i++) {
            char k[16]; snprintf(k, sizeof k, i < 9 ? "%d." : "  ", i + 1);
            draw_item(i, f->sel == i, k, names[i], "");
        }
        break;
    }
    case SC_SKILLS:
        ln("%s %s", th.head,
           L("Skills — a = neu · d = löschen · i = Import (Repo/YouTube-URL → FLOWS)",
             "Skills — a = add · d = delete · i = import (repo/YouTube URL → FLOWS)"));
        ln("");
        for (int i = 0; i < A.nskill; i++) {
            char k[16]; snprintf(k, sizeof k, i < 9 ? "%d." : "  ", i + 1);
            char suf[200];
            snprintf(suf, sizeof suf, "  — %s%s%s", A.sk[i].desc,
                     A.sk[i].ai ? L("  [KI]", "  [AI]") : L("  [lokal]", "  [local]"),
                     A.sk[i].builtin ? "" : L("  (eigen)", "  (custom)"));
            draw_item(i, f->sel == i, k, A.sk[i].name, suf);
        }
        break;
    case SC_PROFILES:
        ln("%s %s", th.head,
           L("Profile — Enter = aktivieren, a = neu, d = löschen",
             "Profiles — Enter = activate, a = add, d = delete"));
        ln("");
        for (int i = 0; i < A.nprof; i++) {
            char k[16]; snprintf(k, sizeof k, "%d.", i + 1);
            draw_item(i, f->sel == i, k, A.prof[i],
                      i == A.aprof ? L("  ✓ aktiv", "  ✓ active") : "");
        }
        break;
    case SC_CONNECT: {
        ln("%s %s", th.head,
           L("Konnektor — Enter = aktiv, u = URL/HS, y = Key/Token, r = Matrix-Raum, b = Bridge, p = Test",
             "Connector — Enter = activate, u = URL/HS, y = key/token, r = Matrix room, b = bridge, p = test"));
        ln("%s %s", th.dim,
           L("Nur KI-Schritte laufen hierüber. Alles andere bleibt lokal auf deinem Rechner.",
             "Only AI steps go through this. Everything else stays local on your machine."));
        ln("");
        for (int i = 0; i < NCONN; i++) {
            char k[16]; snprintf(k, sizeof k, "%d.", i + 1);
            draw_item(i, f->sel == i, k,
                      A.lang ? conn_names[i].en : conn_names[i].de,
                      i == A.conn_type ? L("  ✓ aktiv", "  ✓ active") : "");
        }
        ln("");
        ln("  %sURL:%s %s", th.dim, th.fg, A.conn_url[0] ? A.conn_url : "—");
        ln("  %sKey:%s %s", th.dim, th.fg, A.conn_key[0] ? "•••••" : "—");
        ln("  %sMatrix:%s %s · %s · Token %s", th.dim, th.fg,
           A.matrix_hs[0] ? A.matrix_hs : "https://matrix.org",
           A.matrix_room[0] ? A.matrix_room
                            : (getenv("MATRIX_ROOM") ? L("(aus Env)", "(from env)") : "—"),
           A.matrix_token[0] ? "•••••"
                             : (getenv("MATRIX_ACCESS_TOKEN") ? L("(aus Env)", "(from env)") : "—"));
        ln("  %sBridge:%s %s", th.dim, th.fg, A.bridge_url);
        ln("  %sMCP-Outbox:%s %s/outbox.jsonl", th.dim, th.fg, cfgdir);
        break;
    }
    case SC_TOOLS: {
        ln("%s %s", th.head,
           L("Terminal-Werkzeuge — von SSH über zsh bis fzf/navi",
             "Terminal tools — from SSH to zsh to fzf/navi"));
        ln("");
        const char *tools[8] = {
            L("SSH-Profile (verbinden per Enter)", "SSH profiles (connect with Enter)"),
            L("Shell-Launcher (Ausgabe im Pager, ! am Ende = interaktiv)",
              "Shell launcher (output in pager, append ! for interactive)"),
            L("Datei-Navigator (nur lesen, / = Fuzzy-Filter)",
              "File navigator (read-only, / = fuzzy filter)"),
            L("Shell öffnen (zsh bzw. $SHELL)", "Open shell (zsh or $SHELL)"),
            L("Befehls-Historie durchsuchen (fzf-Stil)",
              "Search command history (fzf style)"),
            L("Cheat-Sheets mit Platzhaltern (navi-Stil)",
              "Cheat sheets with placeholders (navi style)"),
            L("Dienst-Monitor (Odysseus/Ollama/… Healthcheck)",
              "Service monitor (Odysseus/Ollama/… healthcheck)"),
            L("Git-Repo-Visualizer (Graph/Stats + Claude/SERUM fragen)",
              "Git repo visualizer (graph/stats + ask Claude/SERUM)") };
        for (int i = 0; i < 8; i++) {
            char k[16]; snprintf(k, sizeof k, "%d.", i + 1);
            draw_item(i, f->sel == i, k, tools[i], "");
        }
        break;
    }
    case SC_CHAT: {
        ln("%s %s — %s%s", th.head, L("KI-Chat", "AI chat"), th.hi,
           A.lang ? chat_target_names[chat_target].en
                  : chat_target_names[chat_target].de);
        ln("%s %s", th.dim,
           L("Enter = schreiben · o = Ziel · p = Modell · a = Trio · ↑↓ = blättern · Esc = zurück",
             "Enter = write · o = target · p = model · a = trio · ↑↓ = scroll · Esc = back"));
        ln("");
        int page = A.rows - 9; if (page < 1) page = 1;
        int end = chat_n - chat_scroll; if (end < 0) end = 0;
        int start = end - page; if (start < 0) start = 0;
        if (!chat_n)
            ln("  %s%s", th.dim,
               L("Noch leer – Enter drücken und OpenCode oder Hermes anschreiben.",
                 "Empty — press Enter and message OpenCode or Hermes."));
        for (int i = start; i < end; i++) {
            int is_you = !strncmp(chat_ln_buf[i], "Du", 2) || !strncmp(chat_ln_buf[i], "You", 3);
            ln("  %s%s", is_you ? th.fg : th.hi, chat_ln_buf[i]);
        }
        break;
    }
    case SC_MONITOR: {
        services_load();
        ln("%s %s", th.head,
           L("Dienst-Monitor — h = alle prüfen · Enter = Startbefehl · a/d = pflegen",
             "Service monitor — h = check all · Enter = start command · a/d = manage"));
        ln("");
        for (int i = 0; i < nsvc; i++) {
            char kk[16]; snprintf(kk, sizeof kk, "%d.", i + 1);
            char suf[200];
            const char *st = svc[i].up < 0 ? L("? ungeprüft", "? unchecked")
                            : svc[i].up ? L("● läuft", "● up") : L("○ down", "○ down");
            if (svc[i].up == 1) snprintf(suf, sizeof suf, "  %s · %ldms · %s", st, svc[i].ms, svc[i].url);
            else snprintf(suf, sizeof suf, "  %s · %s", st, svc[i].url);
            char nm[64]; snprintf(nm, sizeof nm, "%s", svc[i].name);
            draw_item(i, f->sel == i, kk, nm, suf);
        }
        break;
    }
    case SC_GITVIZ: {
        if (!grepo[0]) { ln("%s %s", th.dim,
            L("Kein Repo geladen – r = wählen.", "No repo loaded — r = pick.")); break; }
        const char *rn = strrchr(grepo, '/'); rn = rn ? rn + 1 : grepo;
        ln("%s ▚ %s %s· %s%s%s  HEAD %s  %s", th.head, rn, th.dim, th.hi, gbranch, th.dim,
           ghead, gdirty ? L("● geändert", "● dirty") : L("○ sauber", "○ clean"));
        if (gremote[0]) ln("%s %s", th.dim, gremote);
        if (ggh_info[0]) ln("%s %s", th.hi, ggh_info);
        ln("%s %s: %d  ·  %s: %d  ·  %s: %d  ·  %s: %d", th.fg,
           L("Commits", "Commits"), gcommits, L("Branches", "Branches"), gbranches,
           L("Tags", "Tags"), gtags, L("Contributors", "Contributors"), ngauth);
        ln("%s %s", th.dim,
           L("2=Graph · 3=Branches · 4=Dateien · 5=Commits · c=Claude/SERUM fragen · r=Repo · Esc",
             "2=graph · 3=branches · 4=files · 5=commits · c=ask Claude/SERUM · r=repo · Esc"));
        int bw = A.cols > 34 ? A.cols - 30 : 12; if (bw > 40) bw = 40;
        static const char *blk[] = {"","▏","▎","▍","▌","▋","▊","▉"};
        ln("");
        ln("%s %s", th.head, L("Top-Autoren (Commits)", "Top authors (commits)"));
        int amax = ngauth ? gauth[0].n : 1;
        for (int i = 0; i < ngauth; i++) {
            int full = amax ? gauth[i].n * bw * 8 / amax : 0;
            char barbuf[400] = ""; size_t d = 0;
            for (int c = 0; c < full / 8 && d + 4 < sizeof barbuf; c++) d += (size_t)snprintf(barbuf+d, sizeof barbuf-d, "█");
            if (full % 8 && d + 4 < sizeof barbuf) snprintf(barbuf+d, sizeof barbuf-d, "%s", blk[full % 8]);
            char nm[26]; fitpad(nm, sizeof nm, gauth[i].name, 18);
            ln("  %s%s %s%s %s%d", th.fg, nm, th.hi, barbuf, th.dim, gauth[i].n);
        }
        ln("");
        ln("%s %s", th.head, L("Datei-Endungen", "File extensions"));
        int emax = ngext ? gext[0].n : 1;
        for (int i = 0; i < ngext; i++) {
            int full = emax ? gext[i].n * bw * 8 / emax : 0;
            char barbuf[400] = ""; size_t d = 0;
            for (int c = 0; c < full / 8 && d + 4 < sizeof barbuf; c++) d += (size_t)snprintf(barbuf+d, sizeof barbuf-d, "█");
            if (full % 8 && d + 4 < sizeof barbuf) snprintf(barbuf+d, sizeof barbuf-d, "%s", blk[full % 8]);
            char nm[26]; fitpad(nm, sizeof nm, gext[i].name, 18);
            ln("  %s%s %s%s %s%d", th.fg, nm, th.hi, barbuf, th.dim, gext[i].n);
        }
        break;
    }
    case SC_SSH: {
        ln("%s %s", th.head,
           L("SSH-Profile — Enter = verbinden, a = neu, d = löschen",
             "SSH profiles — Enter = connect, a = add, d = delete"));
        ln("");
        if (!A.nssh) ln("  %s%s", th.dim,
                        L("Noch keine – mit a anlegen.", "None yet — add with a."));
        for (int i = 0; i < A.nssh; i++) {
            char k[16]; snprintf(k, sizeof k, "%d.", i + 1);
            char suf[140];
            snprintf(suf, sizeof suf, "  %s :%d", A.sshp[i].dest, A.sshp[i].port);
            draw_item(i, f->sel == i, k, A.sshp[i].name, suf);
        }
        break;
    }
    case SC_PAGER: {
        int page = A.rows - 9; if (page < 1) page = 1;
        ln("%s %s %s(%d–%d/%d · ↑↓ BildAuf/Ab g/G · Esc)", th.head, pager_title, th.dim,
           pager_n ? pager_top + 1 : 0,
           pager_top + page > pager_n ? pager_n : pager_top + page, pager_n);
        ln("");
        for (int i = pager_top; i < pager_n && i < pager_top + page; i++) {
            char clip[280];
            fitpad(clip, sizeof clip, pager_line[i], A.cols > 4 ? A.cols - 4 : 40);
            ln("  %s%s", th.fg, clip);
        }
        break;
    }
    case SC_FILES: {
        int page = A.rows - 9; if (page < 1) page = 1;
        int top = f->sel - page + 1; if (top < 0) top = 0;
        ln("%s %s %s%s", th.head, fdir, th.dim,
           L("(Enter = rein/übernehmen · Esc/← = hoch · nur lesen)",
             "(Enter = open/pick · Esc/← = up · read-only)"));
        ln("");
        if (!nfent) ln("  %s%s", th.dim, L("(leer)", "(empty)"));
        for (int i = top; i < nfent && i < top + page; i++) {
            char k[16]; snprintf(k, sizeof k, i < 9 ? "%d." : "  ", i + 1);
            char suf[64];
            if (fent[i].isdir) snprintf(suf, sizeof suf, "  [DIR]");
            else { char sz[24]; fmt_size(fent[i].size, sz, sizeof sz);
                   snprintf(suf, sizeof suf, "  %s", sz); }
            draw_item(i, f->sel == i, k, fent[i].name, suf);
        }
        break;
    }
    case SC_SETTINGS: {
        const char *rows[6];
        char r0[80], r1[80], r2[80], r3[200], r4[80], r5[80];
        snprintf(r0, sizeof r0, "%s: %s", L("Sprache", "Language"), A.lang ? "English" : "Deutsch");
        snprintf(r1, sizeof r1, "%s: %s", L("Theme", "Theme"),
                 A.lang ? theme_names[A.theme].en : theme_names[A.theme].de);
        snprintf(r2, sizeof r2, "%s: %s", L("Kontrastmodus", "Contrast mode"),
                 A.contrast ? L("AN (schwarz/grau/neonrot)", "ON (black/gray/neon red)")
                            : L("aus", "off"));
        snprintf(r3, sizeof r3, "%s: %s", L("Vorlese-Befehl (TTS)", "Read-aloud command (TTS)"),
                 A.tts[0] ? A.tts : L("automatisch (espeak/say/spd-say)",
                                      "automatic (espeak/say/spd-say)"));
        snprintf(r4, sizeof r4, "%s", L("Setup erneut ausführen (Sprache/Theme/Konnektor)",
                                        "Run setup again (language/theme/connector)"));
        snprintf(r5, sizeof r5, "%s: %s", L("Modul Matrix-Regen-Intro", "Module matrix-rain intro"),
                 A.mod_rain ? L("an", "on") : L("aus", "off"));
        rows[0] = r0; rows[1] = r1; rows[2] = r2; rows[3] = r3; rows[4] = r4; rows[5] = r5;
        ln("%s %s", th.head, L("Einstellungen — Enter = umschalten/bearbeiten",
                               "Settings — Enter = toggle/edit"));
        ln("");
        for (int i = 0; i < 6; i++) {
            char k[16]; snprintf(k, sizeof k, "%d.", i + 1);
            draw_item(i, f->sel == i, k, rows[i], "");
        }
        ln("");
        ln("  %s%s %s", th.dim, L("Konfiguration:", "Config:"), cfgdir);
        if (A.contrast)
            ln("  %s%s", th.dim,
               L("Hinweis: Kontrast an erzwingt Dunkelschwarz – Theme ist dann inaktiv.",
                 "Note: contrast on forces deep black — theme is inactive then."));
        break;
    }
    case SC_FREESTYLE:
        ln("%s %s", th.head, L("Freestyle — kein Code, einfach beschreiben.",
                               "Freestyle — no code, just describe it."));
        ln("");
        ln("  %s%s", th.fg,
           L("Enter drücken und frei tippen, z. B. \"pitch deck für investoren\"",
             "Press Enter and type freely, e.g. \"pitch deck for investors\""));
        ln("  %s%s", th.dim,
           L("Der Builder ordnet die Beschreibung einer Kachel zu und startet den Bedarfs-Check.",
             "The builder maps your description to a tile and starts the needs check."));
        break;
    case SC_HELP:
        ln("%s %s", th.head, L("Tasten", "Keys"));
        ln("");
        ln("  %s↑ ↓ ← →%s   %s", th.hi, th.fg, L("bewegen (auch durch die Kacheln)",
                                                  "move (also across the tiles)"));
        ln("  %s1–9%s       %s", th.hi, th.fg, L("Eintrag/Kachel direkt wählen", "pick entry/tile directly"));
        ln("  %sEnter%s     %s", th.hi, th.fg, L("öffnen / bestätigen", "open / confirm"));
        ln("  %sEsc%s       %s", th.hi, th.fg, L("zurück", "back"));
        ln("  %sv%s         %s", th.hi, th.fg, L("Vorlesen (Seite als Sprache)", "read page aloud"));
        ln("  %sn%s         %s", th.hi, th.fg, L("Nanobot fragen / Builder steuern", "ask nanobot / steer builder"));
        ln("  %st%s         %s", th.hi, th.fg,
           L("Theme: Hell → Dunkel → Matrix → Cosmo", "theme: light → dark → Matrix → Cosmo"));
        ln("  %sk%s         %s", th.hi, th.fg, L("Kontrastmodus an/aus", "contrast mode on/off"));
        ln("  %sl%s         %s", th.hi, th.fg, L("Sprache DE/EN", "language DE/EN"));
        ln("  %sm%s         %s", th.hi, th.fg, L("Matrix-Regen (Taste beendet)", "matrix rain (any key stops)"));
        ln("  %sz%s         %s", th.hi, th.fg,
           L("Terminal-Werkzeuge: SSH, Shell, fzf, navi, Dateien",
             "terminal tools: SSH, shell, fzf, navi, files"));
        ln("  %si%s         %s", th.hi, th.fg,
           L("KI-Chat: SERUM (Ollama/Claude/OpenAI), OpenCode, Hermes",
             "AI chat: SERUM (Ollama/Claude/OpenAI), OpenCode, Hermes"));
        ln("  %sz→8%s       %s", th.hi, th.fg,
           L("Git-Repo-Visualizer (Graph/Stats + Claude/SERUM fragen)",
             "Git repo visualizer (graph/stats + ask Claude/SERUM)"));
        ln("  %sq%s         %s", th.hi, th.fg, L("beenden", "quit"));
        break;
    default: break;
    }
}

static void draw_nanobot(void) {
    ln("");
    ln("%s ┌─ 🤖 Nanobot %s%s", th.hi, L("(Esc schließt, n fragt erneut, ! = an KI)",
                                          "(Esc closes, n asks again, ! = to AI)"), "");
    int start = A.nhn > 4 ? A.nhn - 4 : 0;
    for (int i = start; i < A.nhn; i++)
        ln("%s │ %s%s", th.hi, th.fg, A.nh[i]);
    if (!A.nhn)
        ln("%s │ %s%s", th.hi, th.dim,
           L("Frag mich etwas zur aktuellen Seite …", "Ask me about the current page …"));
    ln("%s └─", th.hi);
}

static void draw_footer(void) {
    ln("");
    ln("%s [v]%s %s %s[n]%s %s %s[?]%s %s %s[Esc]%s %s %s[q]%s %s",
       th.hi, th.dim, L("▶ Vorlesen", "▶ Read aloud"),
       th.hi, th.dim, "Nanobot",
       th.hi, th.dim, L("Hilfe", "Help"),
       th.hi, th.dim, L("Zurück", "Back"),
       th.hi, th.dim, L("Ende", "Quit"));
    if (A.msg[0]) ln("%s %s", th.hi, A.msg);
    else ln("");
}

static void render(void) {
    term_size(); apply_theme();
    olen = 0;
    o("\x1b[?2026h\x1b[H");
    draw_header();
    if (cur()->sc == SC_HOME) draw_home();
    else draw_list_screen();
    if (A.nano_open) draw_nanobot();
    draw_footer();
    o("%s\x1b[J\x1b[0m\x1b[?2026l", th.bg);
    oflush();
}

/* ------------------------------------------------------------------ */
/* Matrix-Regen (cmatrix-Stil): Intro & Taste m — jede Taste beendet   */
/* ------------------------------------------------------------------ */

static void matrix_rain(void) {
    /* halbbreite Katakana (1 Zelle breit!) + ASCII */
    static const char *gly[] = {
        "ｦ","ｱ","ｲ","ｳ","ｴ","ｵ","ｶ","ｷ","ｸ","ｹ","ｺ","ｻ","ｼ","ｽ","ｾ","ｿ",
        "ﾀ","ﾁ","ﾂ","ﾃ","ﾄ","ﾅ","ﾆ","ﾇ","ﾈ","ﾉ","ﾊ","ﾋ","ﾌ","ﾍ","ﾎ","ﾏ",
        "0","1","2","3","4","5","7","9","$","+","*","=","<",">" };
    enum { NGLY = sizeof gly / sizeof gly[0] };
    typedef struct { int head, len, speed, tick; } RainCol;
    static RainCol rc[200];
    term_size();
    int cols = A.cols > 200 ? 200 : A.cols, rows = A.rows;
    if (rows < 4 || cols < 4) return;
    for (int c = 0; c < cols; c++) {
        rc[c].head = -(rand() % (rows * 2));
        rc[c].len = 4 + rand() % (rows / 2 + 4);
        rc[c].speed = 1 + rand() % 3;
        rc[c].tick = 0;
    }
    Theme mt; theme_colors(TH_MATRIX, 0, &mt);
    olen = 0;
    o("\x1b[?2026h\x1b[48;2;0;0;0m\x1b[2J\x1b[?2026l"); oflush();
    for (;;) {
        if (in_waiting(60)) { readkey(); break; }
        olen = 0;
        o("\x1b[?2026h\x1b[48;2;0;0;0m");
        for (int c = 0; c < cols; c++) {
            RainCol *r = &rc[c];
            if (++r->tick % r->speed) continue;
            r->head++;
            int h = r->head, tail = h - r->len;
            if (h >= 1 && h <= rows)
                o("\x1b[%d;%dH%s%s", h, c + 1, mt.head, gly[rand() % NGLY]);
            if (h - 1 >= 1 && h - 1 <= rows)
                o("\x1b[%d;%dH%s%s", h - 1, c + 1, mt.fg, gly[rand() % NGLY]);
            if (tail >= 1 && tail <= rows)
                o("\x1b[%d;%dH ", tail, c + 1);
            if (tail > rows) {
                r->head = -(rand() % rows);
                r->len = 4 + rand() % (rows / 2 + 4);
                r->speed = 1 + rand() % 3;
            }
        }
        o("\x1b[0m\x1b[?2026l");
        oflush();
    }
}

/* ------------------------------------------------------------------ */
/* Ersteinrichtung: Sprache → Theme (Live-Vorschau) → Konnektor        */
/* ------------------------------------------------------------------ */

static void first_run_setup(int with_rain) {
    int step = 0;
    int sel[3]; sel[0] = A.lang; sel[1] = A.theme; sel[2] = A.conn_type;
    const int nopt[3] = { 2, NTHEME, NCONN };
    while (step < 3) {
        term_size(); apply_theme();
        olen = 0;
        o("\x1b[?2026h\x1b[H");
        ln("%s ▚▚ BAUKASTEN · %s %d/3", th.head,
           L("Ersteinrichtung", "First-run setup"), step + 1);
        ln("%s %s", th.dim,
           L("Pfeile/Ziffern + Enter · Esc = Vorgabe übernehmen",
             "Arrows/digits + Enter · Esc = keep default"));
        ln("");
        if (step == 0) {
            ln("%s %s", th.head, L("Sprache wählen:", "Pick language:"));
            for (int i = 0; i < 2; i++) {
                const char *nm = i ? "English" : "Deutsch";
                if (sel[0] == i) ln("  %s %d. %s \x1b[0m%s", th.sel, i + 1, nm, th.bg);
                else ln("  %s%d. %s", th.fg, i + 1, nm);
            }
        } else if (step == 1) {
            ln("%s %s", th.head, L("Theme wählen (Vorschau in Echtfarben):",
                                   "Pick theme (previewed in real colors):"));
            for (int i = 0; i < NTHEME; i++) {
                Theme pv; theme_colors(i, 0, &pv);
                o("%s%s  %c %d. %s%-8s%s %s  \x1b[0m\x1b[K\r\n",
                  pv.bg, pv.fg, sel[1] == i ? '>' : ' ', i + 1,
                  pv.head, A.lang ? theme_names[i].en : theme_names[i].de,
                  pv.fg, L("Beispiel AaBb ⚡ 0123", "Sample AaBb ⚡ 0123"));
            }
            ln("");
            ln("%s %s", th.dim,
               L("(Kontrastmodus schwarz/neonrot jederzeit mit k, Theme später mit t)",
                 "(Contrast mode black/neon-red anytime with k, theme later with t)"));
        } else {
            ln("%s %s", th.head, L("Konnektor für KI-Schritte:", "Connector for AI steps:"));
            for (int i = 0; i < NCONN; i++) {
                const char *nm = A.lang ? conn_names[i].en : conn_names[i].de;
                if (sel[2] == i) ln("  %s %d. %s \x1b[0m%s", th.sel, i + 1, nm, th.bg);
                else ln("  %s%d. %s", th.fg, i + 1, nm);
            }
            ln("");
            ln("%s %s", th.dim,
               L("(URLs/Token danach unter Verbindungen – alles außer KI bleibt lokal)",
                 "(URLs/tokens later under Connections — everything but AI stays local)"));
        }
        o("%s\x1b[J\x1b[0m\x1b[?2026l", th.bg);
        oflush();

        int k = readkey();
        if (k < 0) break;
        if (k == K_UP && sel[step] > 0) sel[step]--;
        else if (k == K_DN && sel[step] + 1 < nopt[step]) sel[step]++;
        else if (k >= '1' && k < '1' + nopt[step]) sel[step] = k - '1';
        else if (k == K_ENTER || k == '\n' || k == K_ESC) {
            if (k != K_ESC) {
                if (step == 0) A.lang = sel[0];
                else if (step == 1) { A.theme = sel[1]; apply_theme(); }
                else A.conn_type = sel[2];
            }
            step++;
        }
    }
    save_config();
    if (with_rain && A.mod_rain) matrix_rain();
}

