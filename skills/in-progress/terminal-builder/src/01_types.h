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
    SC_TOOLS, SC_SSH, SC_PAGER, SC_FILES, SC_CHAT, SC_MONITOR, SC_GITVIZ
};

#define MAXSVC 16
typedef struct { char name[40]; char url[160]; char start[160]; int up; long ms; } Service;

/* Git-Repo-Visualizer: Zählung für Autoren/Datei-Endungen */
typedef struct { char name[64]; int n; } Tally;

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
#define MAXSKILL 128
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
    int mod_rain;    /* Matrix-Rain-Intro-Modul aktiv (1) / aus (0) */
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

    /* SERUM: Direkt-LLM (Ollama / Anthropic / OpenAI-kompatibel) */
    char ollama_url[256];
    char ollama_model[96];
    char amodel[96];
    char oai_url[256];
    char oai_model[96];
    char oai_key[256];

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

