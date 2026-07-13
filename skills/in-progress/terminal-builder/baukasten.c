/*
 * baukasten — Terminal-Builder-Hub (reines Frontend, offline-first)
 * ----------------------------------------------------------------
 * Ein C-Terminal-Menü, das alle Arbeitsschritte lokal ohne KI plant
 * und KI nur für markierte Schritte über konfigurierbare Konnektoren
 * (MCP-Outbox, Webhook, Gateway-API, Discord) anbindet.
 *
 * Bedienung: Pfeiltasten + Zifferntasten, Enter, Esc = zurück.
 * Immer dabei: Hell/Dunkel, Deutsch/Englisch, Kontrastmodus
 * (an = tiefschwarz, Text grau, Überschriften/Highlights neonrot),
 * Vorlese-Button [v] auf jeder Seite, Nanobot [n] für Fragen zur
 * aktuellen Seite oder zum Steuern des Builders.
 *
 * Kompilieren:  cc -O2 -o baukasten baukasten.c
 * Keine Abhängigkeiten außer libc. KI-Versand nutzt curl, TTS nutzt
 * espeak/say/spd-say — beides optional, ohne läuft alles lokal.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/types.h>

/* ------------------------------------------------------------------ */
/* Konstanten & Typen                                                  */
/* ------------------------------------------------------------------ */

enum { K_UP = 1000, K_DN, K_RT, K_LT, K_ESC = 27, K_ENTER = 13, K_BS = 127 };

enum {
    SC_HOME, SC_SUB, SC_EVAL, SC_PLAN, SC_PICK, SC_WORKFLOWS,
    SC_SKILLS, SC_PROFILES, SC_CONNECT, SC_SETTINGS, SC_FREESTYLE, SC_HELP
};

enum { CONN_NONE, CONN_MCP, CONN_WEBHOOK, CONN_GATEWAY, CONN_DISCORD };

#define NCAT     8
#define NSUB     4
#define NQ       4
#define NOPT     3
#define MAXPROF  16
#define MAXSKILL 48
#define MAXSTEP  10
#define MAXWF    32

typedef struct { const char *de, *en; } S2;

typedef struct { char name[48]; char desc[96]; int ai; int builtin; } SkillDef;

typedef struct { S2 label; int skill; int ai; int tmpl; } Step;

typedef struct { int sc; int sel; } Frame;

typedef struct {
    const char *bg, *fg, *dim, *head, *hi, *sel, *box;
} Theme;

typedef struct {
    int lang;        /* 0 = de, 1 = en */
    int dark;        /* 1 = dunkel, 0 = hell (ignoriert bei Kontrast) */
    int contrast;    /* 1 = Kontrastmodus: schwarz/grau/neonrot */
    int conn_type;
    char conn_url[256];
    char conn_key[128];
    char tts[160];

    int nprof; char prof[MAXPROF][40]; int aprof;
    int nskill; SkillDef sk[MAXSKILL];

    /* aktuelles Projekt */
    int cat, sub, qidx;
    int ans[NQ];
    Step st[MAXSTEP]; int nst; int pick_step;

    /* UI */
    char msg[240];
    int nano_open; char nh[8][200]; int nhn;
    int rows, cols;
} App;

static App A;
static Theme th;
static Frame stk[12];
static int depth;
static struct termios orig_tio;
static char cfgdir[512];

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

static const S2 conn_names[5] = {
    {"Kein Konnektor", "No connector"},
    {"MCP (Outbox-Datei)", "MCP (outbox file)"},
    {"Webhook (HTTP POST)", "Webhook (HTTP POST)"},
    {"Gateway-API (POST + Key)", "Gateway API (POST + key)"},
    {"Discord (Webhook)", "Discord (webhook)"},
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
#define NMENU 6
static const S2 menu_names[NMENU] = {
    {"Workflows (ComfyUI-Export)", "Workflows (ComfyUI export)"},
    {"Skills verwalten", "Manage skills"},
    {"Profile", "Profiles"},
    {"Verbindungen (MCP/Webhook/Gateway/Discord)",
     "Connections (MCP/webhook/gateway/Discord)"},
    {"Einstellungen", "Settings"},
    {"Freestyle (kein Code, freie Eingabe)", "Freestyle (no code, free input)"},
};
static const char menu_keys[NMENU] = { 'w', 's', 'p', 'c', 'e', 'f' };

/* ------------------------------------------------------------------ */
/* Ausgabepuffer & Terminal                                            */
/* ------------------------------------------------------------------ */

static char ob[1 << 18];
static size_t olen;

static void o(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    if (olen < sizeof ob - 1)
        olen += (size_t)vsnprintf(ob + olen, sizeof ob - olen, fmt, ap);
    va_end(ap);
}

static void oflush(void) {
    if (olen) { ssize_t r = write(STDOUT_FILENO, ob, olen); (void)r; }
    olen = 0;
}

static void term_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        A.cols = ws.ws_col; A.rows = ws.ws_row;
    } else { A.cols = 80; A.rows = 24; }
    if (A.cols > 200) A.cols = 200;
}

static void raw_off(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_tio);
    const char *s = "\x1b[?25h\x1b[0m\x1b[?1049l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s)); (void)r;
}

static void raw_on(void) {
    tcgetattr(STDIN_FILENO, &orig_tio);
    atexit(raw_off);
    struct termios t = orig_tio;
    t.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG);
    t.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    const char *s = "\x1b[?1049h\x1b[?25l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s)); (void)r;
}

static int in_waiting(int ms) {
    fd_set f; struct timeval tv = { 0, ms * 1000 };
    FD_ZERO(&f); FD_SET(STDIN_FILENO, &f);
    return select(STDIN_FILENO + 1, &f, NULL, NULL, &tv) > 0;
}

static int readkey(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    if (c != 27) return c;
    if (!in_waiting(30)) return K_ESC;
    unsigned char b;
    if (read(STDIN_FILENO, &b, 1) != 1) return K_ESC;
    if (b != '[' && b != 'O') return K_ESC;
    if (!in_waiting(30)) return K_ESC;
    unsigned char d;
    if (read(STDIN_FILENO, &d, 1) != 1) return K_ESC;
    switch (d) {
        case 'A': return K_UP; case 'B': return K_DN;
        case 'C': return K_RT; case 'D': return K_LT;
        default:
            /* Rest einer längeren Sequenz (z. B. "3~") verwerfen */
            while (in_waiting(5)) {
                unsigned char x;
                if (read(STDIN_FILENO, &x, 1) != 1) break;
                if (x == '~' || (x >= 'A' && x <= 'Z')) break;
            }
            return 0;
    }
}

/* s auf Breite w kürzen/auffüllen (byte-sicher für UTF-8) */
static void fitpad(char *dst, size_t cap, const char *s, int w) {
    size_t di = 0; int vis = 0;
    for (const char *p = s; *p && di + 4 < cap; p++) {
        if (((unsigned char)*p & 0xC0) != 0x80) {
            if (vis >= w) break;
            vis++;
        }
        dst[di++] = *p;
    }
    while (vis < w && di + 1 < cap) { dst[di++] = ' '; vis++; }
    dst[di] = 0;
}

/* ------------------------------------------------------------------ */
/* Theme                                                               */
/* ------------------------------------------------------------------ */

static void apply_theme(void) {
    if (A.contrast) {
        /* Kontrast an: immer tiefschwarz, Text grau, Neonrot als Akzent */
        th.bg   = "\x1b[48;2;0;0;0m";
        th.fg   = "\x1b[38;2;170;170;170m";
        th.dim  = "\x1b[38;2;105;105;105m";
        th.head = "\x1b[1;38;2;255;42;42m";
        th.hi   = "\x1b[38;2;255;42;42m";
        th.sel  = "\x1b[48;2;255;42;42m\x1b[38;2;0;0;0m\x1b[1m";
        th.box  = "\x1b[38;2;120;120;120m";
    } else if (A.dark) {
        th.bg   = "\x1b[48;2;24;26;32m";
        th.fg   = "\x1b[38;2;220;223;228m";
        th.dim  = "\x1b[38;2;130;136;146m";
        th.head = "\x1b[1;38;2;255;198;110m";
        th.hi   = "\x1b[38;2;110;220;220m";
        th.sel  = "\x1b[48;2;72;84;118m\x1b[38;2;255;255;255m";
        th.box  = "\x1b[38;2;95;100;112m";
    } else {
        th.bg   = "\x1b[48;2;246;246;243m";
        th.fg   = "\x1b[38;2;42;44;50m";
        th.dim  = "\x1b[38;2;125;128;136m";
        th.head = "\x1b[1;38;2;28;82;178m";
        th.hi   = "\x1b[38;2;148;42;148m";
        th.sel  = "\x1b[48;2;198;214;244m\x1b[38;2;12;14;40m";
        th.box  = "\x1b[38;2;165;168;176m";
    }
}

/* eine Bildschirmzeile: Hintergrund setzen, Inhalt, bis Zeilenende füllen */
static void ln(const char *fmt, ...) {
    o("%s%s", th.bg, th.fg);
    va_list ap; va_start(ap, fmt);
    if (olen < sizeof ob - 1)
        olen += (size_t)vsnprintf(ob + olen, sizeof ob - olen, fmt, ap);
    va_end(ap);
    o("\x1b[K\r\n");
}

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

static void save_config(void) {
    char p[600]; snprintf(p, sizeof p, "%s/config", cfgdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "lang=%d\ndark=%d\ncontrast=%d\nconn=%d\nurl=%s\nkey=%s\ntts=%s\naprof=%d\n",
            A.lang, A.dark, A.contrast, A.conn_type, A.conn_url, A.conn_key,
            A.tts, A.aprof);
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

static void load_config(void) {
    A.lang = 0; A.dark = 1; A.contrast = 0; A.conn_type = CONN_NONE;
    A.conn_url[0] = A.conn_key[0] = A.tts[0] = 0;
    A.nprof = 1; snprintf(A.prof[0], sizeof A.prof[0], "standard"); A.aprof = 0;
    A.nskill = SK_BUILTIN_N;
    memcpy(A.sk, builtin_skills, sizeof builtin_skills);

    char p[600], line[512];
    snprintf(p, sizeof p, "%s/config", cfgdir);
    FILE *f = fopen(p, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            chomp(line);
            char *eq = strchr(line, '='); if (!eq) continue;
            *eq = 0; const char *v = eq + 1;
            if (!strcmp(line, "lang")) A.lang = atoi(v) ? 1 : 0;
            else if (!strcmp(line, "dark")) A.dark = atoi(v) ? 1 : 0;
            else if (!strcmp(line, "contrast")) A.contrast = atoi(v) ? 1 : 0;
            else if (!strcmp(line, "conn")) { A.conn_type = atoi(v);
                if (A.conn_type < 0 || A.conn_type > 4) A.conn_type = 0; }
            else if (!strcmp(line, "url")) snprintf(A.conn_url, sizeof A.conn_url, "%s", v);
            else if (!strcmp(line, "key")) snprintf(A.conn_key, sizeof A.conn_key, "%s", v);
            else if (!strcmp(line, "tts")) snprintf(A.tts, sizeof A.tts, "%s", v);
            else if (!strcmp(line, "aprof")) A.aprof = atoi(v);
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

/* KI-Anfrage über den konfigurierten Konnektor schicken.
 * Reines Frontend: MCP legt in eine Outbox-Datei, HTTP-Typen POSTen via curl. */
static void send_to_connector(const char *question) {
    if (A.conn_type == CONN_NONE) {
        setmsg("%s", L("Kein Konnektor konfiguriert → Verbindungen (c).",
                       "No connector configured → Connections (c)."));
        return;
    }
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
    char pf[600]; snprintf(pf, sizeof pf, "%s/payload.json", cfgdir);
    FILE *f = fopen(pf, "w"); if (!f) return;
    fputs(payload, f); fclose(f);

    char auth[200] = "";
    if (A.conn_type == CONN_GATEWAY && A.conn_key[0]) {
        char kk[140]; size_t d = 0;
        for (const char *p = A.conn_key; *p && d + 1 < sizeof kk; p++)
            if (isalnum((unsigned char)*p) || strchr("._-", *p)) kk[d++] = *p;
        kk[d] = 0;
        snprintf(auth, sizeof auth, "-H 'Authorization: Bearer %s' ", kk);
    }
    char cmd[1200];
    snprintf(cmd, sizeof cmd,
             "curl -m 6 -s -o /dev/null -w '%%{http_code}' -X POST "
             "-H 'Content-Type: application/json' %s--data @'%s' '%s' 2>/dev/null",
             auth, pf, A.conn_url);
    FILE *cp = popen(cmd, "r");
    char code[16] = "";
    if (cp) { if (!fgets(code, sizeof code, cp)) code[0] = 0; pclose(cp); }
    if (code[0] == '2')
        setmsg(L("✓ Gesendet über %s (HTTP %s)", "✓ Sent via %s (HTTP %s)"),
               A.lang ? conn_names[A.conn_type].en : conn_names[A.conn_type].de, code);
    else
        setmsg(L("Versand fehlgeschlagen (HTTP %s) – curl/Netz prüfen.",
                 "Send failed (HTTP %s) — check curl/network."),
               code[0] ? code : "?");
}

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

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static void draw_header(void) {
    ln("%s ▚▚ BAUKASTEN %s· %s%s", th.head, th.dim,
       th.head, screen_name());
    ln("%s %s: %s%s%s  %s: %s%s%s  %s%s · %s · %s%s",
       th.dim, L("Profil", "Profile"), th.hi, A.prof[A.aprof], th.dim,
       L("Konnektor", "Connector"), th.hi,
       A.lang ? conn_names[A.conn_type].en : conn_names[A.conn_type].de, th.dim,
       th.dim, A.lang ? "EN" : "DE",
       A.contrast ? L("KONTRAST", "CONTRAST") : (A.dark ? L("Dunkel", "Dark") : L("Hell", "Light")),
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
            snprintf(suf, sizeof suf, "  [%s: %s | %s]",
                     s->skill >= 0 ? A.sk[s->skill].name : L("manuell", "manual"),
                     A.lang ? tmpl_names[s->tmpl].en : tmpl_names[s->tmpl].de,
                     s->ai ? L("KI ⚡", "AI ⚡") : L("lokal", "local"));
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
           L("Skills — vorgebaut + eigene (a = neu anlegen, d = eigenen löschen)",
             "Skills — prebuilt + custom (a = add, d = delete custom)"));
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
           L("Konnektor wählen — Enter = aktiv setzen, u = URL, y = API-Key, p = Test",
             "Pick connector — Enter = activate, u = URL, y = API key, p = test"));
        ln("%s %s", th.dim,
           L("Nur KI-Schritte laufen hierüber. Alles andere bleibt lokal auf deinem Rechner.",
             "Only AI steps go through this. Everything else stays local on your machine."));
        ln("");
        for (int i = 0; i < 5; i++) {
            char k[16]; snprintf(k, sizeof k, "%d.", i + 1);
            draw_item(i, f->sel == i, k,
                      A.lang ? conn_names[i].en : conn_names[i].de,
                      i == A.conn_type ? L("  ✓ aktiv", "  ✓ active") : "");
        }
        ln("");
        ln("  %sURL:%s %s", th.dim, th.fg, A.conn_url[0] ? A.conn_url : "—");
        ln("  %sKey:%s %s", th.dim, th.fg, A.conn_key[0] ? "•••••" : "—");
        ln("  %sMCP-Outbox:%s %s/outbox.jsonl", th.dim, th.fg, cfgdir);
        break;
    }
    case SC_SETTINGS: {
        const char *rows[5];
        char r0[80], r1[80], r2[80], r3[200];
        snprintf(r0, sizeof r0, "%s: %s", L("Sprache", "Language"), A.lang ? "English" : "Deutsch");
        snprintf(r1, sizeof r1, "%s: %s", L("Theme", "Theme"),
                 A.dark ? L("Dunkel", "Dark") : L("Hell", "Light"));
        snprintf(r2, sizeof r2, "%s: %s", L("Kontrastmodus", "Contrast mode"),
                 A.contrast ? L("AN (schwarz/grau/neonrot)", "ON (black/gray/neon red)")
                            : L("aus", "off"));
        snprintf(r3, sizeof r3, "%s: %s", L("Vorlese-Befehl (TTS)", "Read-aloud command (TTS)"),
                 A.tts[0] ? A.tts : L("automatisch (espeak/say/spd-say)",
                                      "automatic (espeak/say/spd-say)"));
        rows[0] = r0; rows[1] = r1; rows[2] = r2; rows[3] = r3;
        ln("%s %s", th.head, L("Einstellungen — Enter = umschalten/bearbeiten",
                               "Settings — Enter = toggle/edit"));
        ln("");
        for (int i = 0; i < 4; i++) {
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
        ln("  %st%s         %s", th.hi, th.fg, L("Theme hell/dunkel", "theme light/dark"));
        ln("  %sk%s         %s", th.hi, th.fg, L("Kontrastmodus an/aus", "contrast mode on/off"));
        ln("  %sl%s         %s", th.hi, th.fg, L("Sprache DE/EN", "language DE/EN"));
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
    o("\x1b[H");
    draw_header();
    if (cur()->sc == SC_HOME) draw_home();
    else draw_list_screen();
    if (A.nano_open) draw_nanobot();
    draw_footer();
    o("%s\x1b[J\x1b[0m", th.bg);
    oflush();
}

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
        case SC_CONNECT: return 5;
        case SC_SETTINGS: return 4;
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
        A.aprof = f->sel; save_config();
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
            else { A.dark = !A.dark; save_config(); }
        }
        else if (f->sel == 2) { A.contrast = !A.contrast; save_config(); }
        else if (f->sel == 3) {
            char t[160]; snprintf(t, sizeof t, "%s", A.tts);
            if (prompt_input(L("TTS-Befehl (%s = Datei) >", "TTS command (%s = file) >"),
                             t, sizeof t))
                { snprintf(A.tts, sizeof A.tts, "%s", t); save_config(); }
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
        else { A.dark = !A.dark; save_config(); }
        return;
    }
    if (k == 'k') { A.contrast = !A.contrast; save_config(); return; }
    if (k == 'l') { A.lang = !A.lang; save_config(); return; }
    if (k == 'v') { char b[2048]; build_speech(b, sizeof b); speak(b); return; }
    if (k == 'n') { nano_prompt(); return; }
    if (k == '?') { push(SC_HELP); return; }

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
            char u[256]; snprintf(u, sizeof u, "%s", A.conn_url);
            if (prompt_input("URL >", u, sizeof u))
                { snprintf(A.conn_url, sizeof A.conn_url, "%s", u); save_config(); }
            return;
        }
        if (k == 'y') {
            char kk[128] = "";
            if (prompt_input(L("API-Key >", "API key >"), kk, sizeof kk))
                { snprintf(A.conn_key, sizeof A.conn_key, "%s", kk); save_config(); }
            return;
        }
        if (k == 'p') {
            send_to_connector(L("Ping vom baukasten-Verbindungstest.",
                                "Ping from the baukasten connection test."));
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
    ensure_dirs();
    load_config();
    apply_theme();
    raw_on();
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
