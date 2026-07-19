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
#include <strings.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/types.h>

/* ------------------------------------------------------------------ */
/* Konstanten & Typen                                                  */
/* ------------------------------------------------------------------ */

enum { K_UP = 1000, K_DN, K_RT, K_LT, K_PGUP, K_PGDN,
       K_ESC = 27, K_ENTER = 13, K_BS = 127 };

enum {
    SC_HOME, SC_SUB, SC_EVAL, SC_PLAN, SC_PICK, SC_WORKFLOWS,
    SC_SKILLS, SC_PROFILES, SC_CONNECT, SC_SETTINGS, SC_FREESTYLE, SC_HELP,
    SC_TOOLS, SC_SSH, SC_PAGER, SC_FILES, SC_CHAT
};

enum { CONN_NONE, CONN_MCP, CONN_WEBHOOK, CONN_GATEWAY, CONN_DISCORD,
       CONN_MATRIX, CONN_BRIDGE, NCONN };

/* Themes: DkZ-Farbschema (Neonrot #fa1e4e, Pink #ec4899, Gruen #00D084,
 * Blau #55ACEE, Dark #0e0e10, Soft White #f6f6f7) */
enum { TH_HELL, TH_DUNKEL, TH_MATRIX, TH_COSMO, NTHEME };

#define NCAT     8
#define NSUB     4
#define NQ       4
#define NOPT     3
#define MAXPROF  16
#define MAXSKILL 48
#define MAXSTEP  10
#define MAXWF    32
#define MAXSSH   16
#define MAXFENT  512

typedef struct { const char *de, *en; } S2;

typedef struct { char name[48]; char desc[96]; int ai; int builtin; } SkillDef;

typedef struct { S2 label; int skill; int ai; int tmpl; } Step;

typedef struct { char name[40]; char dest[96]; int port; } SshProf;

typedef struct { int sc; int sel; } Frame;

typedef struct {
    const char *bg, *fg, *dim, *head, *hi, *sel, *box;
} Theme;

typedef struct {
    int lang;        /* 0 = de, 1 = en */
    int theme;       /* TH_* (ignoriert bei Kontrast) */
    int contrast;    /* 1 = Kontrastmodus: schwarz/grau/neonrot */
    int conn_type;
    char conn_url[256];
    char conn_key[128];
    char tts[160];

    /* Matrix (direkt) + DkZ NanoChat-Bridge */
    char matrix_hs[256];
    char matrix_token[256];
    char matrix_room[128];
    char matrix_me[128];
    char bridge_url[256];
    char opencode_url[256];

    int nprof; char prof[MAXPROF][40]; int aprof;
    int nskill; SkillDef sk[MAXSKILL];
    int nssh; SshProf sshp[MAXSSH];
    char pickpath[800];

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
    const char *s = "\x1b[?2026l\x1b[?25h\x1b[0m\x1b[?1049l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s)); (void)r;
}

static void raw_enter(void) {
    struct termios t = orig_tio;
    t.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG);
    t.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    const char *s = "\x1b[?1049h\x1b[?25l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s)); (void)r;
}

static void raw_on(void) {
    tcgetattr(STDIN_FILENO, &orig_tio);
    atexit(raw_off);
    raw_enter();
}

/* TUI fuer interaktive Kindprozesse (ssh, Shell) sauber verlassen/wiederholen */
static void tui_suspend(void) {
    const char *s = "\x1b[?2026l\x1b[0m\x1b[?25h\x1b[?1049l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s)); (void)r;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_tio);
}

static void tui_resume(void) { raw_enter(); }

static int run_interactive(const char *cmd) {
    tui_suspend();
    int rc = system(cmd);
    fprintf(stderr, "\n[baukasten] Enter = zurueck / back ");
    char tmp[8]; if (!fgets(tmp, sizeof tmp, stdin)) { /* EOF ok */ }
    tui_resume();
    return rc;
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
        case '5': case '6': {
            unsigned char t2 = 0;
            if (in_waiting(30)) { if (read(STDIN_FILENO, &t2, 1) != 1) t2 = 0; }
            if (t2 == '~') return d == '5' ? K_PGUP : K_PGDN;
            return 0;
        }
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

static void theme_colors(int theme, int contrast, Theme *t) {
    if (contrast) {
        /* Kontrast an: immer tiefschwarz, Text grau, DkZ-Neonrot #fa1e4e */
        t->bg   = "\x1b[48;2;0;0;0m";
        t->fg   = "\x1b[38;2;170;170;170m";
        t->dim  = "\x1b[38;2;105;105;105m";
        t->head = "\x1b[1;38;2;250;30;78m";
        t->hi   = "\x1b[38;2;250;30;78m";
        t->sel  = "\x1b[48;2;250;30;78m\x1b[38;2;0;0;0m\x1b[1m";
        t->box  = "\x1b[38;2;120;120;120m";
        return;
    }
    switch (theme) {
    case TH_MATRIX: /* DkZ-Gruen #00D084 auf Schwarz */
        t->bg   = "\x1b[48;2;0;0;0m";
        t->fg   = "\x1b[38;2;0;208;132m";
        t->dim  = "\x1b[38;2;0;110;70m";
        t->head = "\x1b[1;38;2;140;255;190m";
        t->hi   = "\x1b[38;2;0;208;132m";
        t->sel  = "\x1b[48;2;0;208;132m\x1b[38;2;0;0;0m";
        t->box  = "\x1b[38;2;0;90;60m";
        break;
    case TH_COSMO: /* Soft White auf Blauschwarz, DkZ-Blau + Pink */
        t->bg   = "\x1b[48;2;5;10;20m";
        t->fg   = "\x1b[38;2;246;246;247m";
        t->dim  = "\x1b[38;2;120;140;170m";
        t->head = "\x1b[1;38;2;85;172;238m";
        t->hi   = "\x1b[38;2;236;72;153m";
        t->sel  = "\x1b[48;2;85;172;238m\x1b[38;2;5;10;20m";
        t->box  = "\x1b[38;2;60;80;110m";
        break;
    case TH_HELL:
        t->bg   = "\x1b[48;2;246;246;243m";
        t->fg   = "\x1b[38;2;42;44;50m";
        t->dim  = "\x1b[38;2;125;128;136m";
        t->head = "\x1b[1;38;2;28;82;178m";
        t->hi   = "\x1b[38;2;148;42;148m";
        t->sel  = "\x1b[48;2;198;214;244m\x1b[38;2;12;14;40m";
        t->box  = "\x1b[38;2;165;168;176m";
        break;
    default: /* TH_DUNKEL */
        t->bg   = "\x1b[48;2;24;26;32m";
        t->fg   = "\x1b[38;2;220;223;228m";
        t->dim  = "\x1b[38;2;130;136;146m";
        t->head = "\x1b[1;38;2;255;198;110m";
        t->hi   = "\x1b[38;2;110;220;220m";
        t->sel  = "\x1b[48;2;72;84;118m\x1b[38;2;255;255;255m";
        t->box  = "\x1b[38;2;95;100;112m";
    }
}

static void apply_theme(void) { theme_colors(A.theme, A.contrast, &th); }

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
    fprintf(f, "lang=%d\ntheme=%d\ndark=%d\ncontrast=%d\nconn=%d\nurl=%s\nkey=%s\ntts=%s\naprof=%d\n",
            A.lang, A.theme, A.theme != TH_HELL, A.contrast, A.conn_type,
            A.conn_url, A.conn_key, A.tts, A.aprof);
    fprintf(f, "mhs=%s\nmtoken=%s\nmroom=%s\nbridge=%s\nopencode=%s\n",
            A.matrix_hs, A.matrix_token, A.matrix_room, A.bridge_url, A.opencode_url);
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
    A.lang = 0; A.theme = TH_MATRIX; A.contrast = 0; A.conn_type = CONN_NONE;
    A.conn_url[0] = A.conn_key[0] = A.tts[0] = 0;
    A.matrix_hs[0] = A.matrix_token[0] = A.matrix_room[0] = A.matrix_me[0] = 0;
    snprintf(A.bridge_url, sizeof A.bridge_url, "http://localhost:3050");
    snprintf(A.opencode_url, sizeof A.opencode_url, "http://localhost:9900");
    A.nprof = 1; snprintf(A.prof[0], sizeof A.prof[0], "standard"); A.aprof = 0;
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
            else if (!strcmp(line, "mhs")) snprintf(A.matrix_hs, sizeof A.matrix_hs, "%s", v);
            else if (!strcmp(line, "mtoken")) snprintf(A.matrix_token, sizeof A.matrix_token, "%s", v);
            else if (!strcmp(line, "mroom")) snprintf(A.matrix_room, sizeof A.matrix_room, "%s", v);
            else if (!strcmp(line, "bridge")) snprintf(A.bridge_url, sizeof A.bridge_url, "%s", v);
            else if (!strcmp(line, "opencode")) snprintf(A.opencode_url, sizeof A.opencode_url, "%s", v);
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

/* Ein curl fuer alles: Payload via --data @file, Response nach response.tmp.
 * Gibt HTTP-Code zurueck, -1 bei Fehler. */
static long curl_http(const char *method, const char *url, const char *bearer,
                      const char *body, char *resp, size_t rcap, int timeout_s) {
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
    char cmd[2200];
    snprintf(cmd, sizeof cmd,
             "curl -m %d -s -o '%s' -w '%%{http_code}' -X %s "
             "-H 'Content-Type: application/json' %s%s'%s' 2>/dev/null",
             timeout_s, rf, method, auth, data, url);
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

enum { CT_KONNEKTOR, CT_OPENCODE, CT_HERMES, NCTARGET };
static const S2 chat_target_names[NCTARGET] = {
    {"Aktiver Konnektor", "Active connector"},
    {"OpenCode", "OpenCode"},
    {"Hermes (Bridge)", "Hermes (bridge)"},
};
static int  chat_target = CT_OPENCODE;
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
    if (chat_target == CT_KONNEKTOR) {
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
        const char *tools[6] = {
            L("SSH-Profile (verbinden per Enter)", "SSH profiles (connect with Enter)"),
            L("Shell-Launcher (Ausgabe im Pager, ! am Ende = interaktiv)",
              "Shell launcher (output in pager, append ! for interactive)"),
            L("Datei-Navigator (nur lesen, / = Fuzzy-Filter)",
              "File navigator (read-only, / = fuzzy filter)"),
            L("Shell öffnen (zsh bzw. $SHELL)", "Open shell (zsh or $SHELL)"),
            L("Befehls-Historie durchsuchen (fzf-Stil)",
              "Search command history (fzf style)"),
            L("Cheat-Sheets mit Platzhaltern (navi-Stil)",
              "Cheat sheets with placeholders (navi style)") };
        for (int i = 0; i < 6; i++) {
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
           L("Enter = schreiben · o = Ziel wechseln (Konnektor/OpenCode/Hermes) · ↑↓ = blättern · Esc = zurück",
             "Enter = write · o = switch target (connector/OpenCode/Hermes) · ↑↓ = scroll · Esc = back"));
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
        const char *rows[5];
        char r0[80], r1[80], r2[80], r3[200], r4[80];
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
        rows[0] = r0; rows[1] = r1; rows[2] = r2; rows[3] = r3; rows[4] = r4;
        ln("%s %s", th.head, L("Einstellungen — Enter = umschalten/bearbeiten",
                               "Settings — Enter = toggle/edit"));
        ln("");
        for (int i = 0; i < 5; i++) {
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
           L("KI-Chat: OpenCode, Hermes oder Konnektor", "AI chat: OpenCode, Hermes or connector"));
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
    if (with_rain) matrix_rain();
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
        case SC_CONNECT: return NCONN;
        case SC_SETTINGS: return 5;
        case SC_TOOLS: return 6;
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
        else cheat_run();
        break;
    case SC_CHAT:
        chat_send();
        break;
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
