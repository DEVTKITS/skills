/*
 * Design System UI — C-Gerüst des Backends.
 *
 * Ein eigenständiger HTTP-Server in reinem C99 (POSIX sockets, pthreads).
 * Keine Bibliotheken außer der C-Standardbibliothek und pthread.
 *
 *   cc -O2 -std=c99 -pthread -o dsui-c server.c
 *   ./dsui-c            # Port 8080, oder PORT=… setzen
 *
 * Bedient dieselbe Oberfläche und dieselben Kernendpunkte wie server.py:
 *   GET  /                       die Single-Page-UI
 *   GET  /app.js /styles.css /llms.txt
 *   GET  /health
 *   GET  /api/edition
 *   GET  /api/patterns
 *   POST /api/workflow/execute   topologische Ausführung + Validierung
 *   POST /api/workflow/evaluate  Bewertung mit Urteil iterate/done
 *   GET  /api/agent/activity     Aktivitäts-Feed
 *   POST /api/agent/activity     Schritt melden (roter Monitorrand)
 *   POST /api/svg/generate       SVG aus einer svgspec/1-Spezifikation (C-Teilmenge, svgkit.c)
 *   GET  /api/svg/types          svg/types.json (Felder, Grenzen, Standardwerte)
 *
 * Bewusst schlank: der C-Server ist das Gerüst für Umgebungen ohne Python.
 * Für Provider-Aufrufe und die Agenten-Shell bleibt server.py zuständig.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* SVG-Generator (C-Teilmenge). Direkt eingebunden, damit der Einzeiler
 * "cc -O2 -std=c99 -pthread -o dsui-c server.c" ohne weitere Dateien und ohne -lm baut. */
#include "svgkit.c"

#define MAX_REQ      (256 * 1024)
#define MAX_NODES    256
#define MAX_EDGES    512
#define ID_LEN       64
#define MAX_EVENTS   200
#define EVENT_TEXT   256

/* ───────────────────────── Hilfsfunktionen ───────────────────────── */

/* Groß-/kleinschreibungsunabhängige Suche. strcasestr ist eine GNU-Erweiterung
 * und unter striktem C99/POSIX nicht deklariert — hier portabel selbst gebaut. */
static char *ci_find(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    if (!nl) return (char *)hay;
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl) return (char *)p;
    }
    return NULL;
}

static void iso_now(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

/* Escapet einen String für die Ausgabe in JSON. */
static void json_escape(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 7 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        switch (c) {
            case '"':  out[o++] = '\\'; out[o++] = '"';  break;
            case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
            case '\n': out[o++] = '\\'; out[o++] = 'n';  break;
            case '\r': out[o++] = '\\'; out[o++] = 'r';  break;
            case '\t': out[o++] = '\\'; out[o++] = 't';  break;
            default:
                if (c < 0x20) { o += (size_t)snprintf(out + o, cap - o, "\\u%04x", c); }
                else out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

/*
 * Liest den Wert eines flachen JSON-Strings: "key":"wert".
 * Bewusst minimal — der Server nimmt nur bekannte, kurze Felder entgegen.
 * Gibt 1 zurück, wenn etwas gefunden wurde.
 */
static int json_str(const char *json, const char *key, char *out, size_t cap) {
    char pat[96];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) {
        if (*p == '\\' && p[1]) p++;      /* Escapes überspringen */
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

/* ───────────────────────── Workflow-Modell ───────────────────────── */

typedef struct { char id[ID_LEN]; char type[ID_LEN]; } Node;
typedef struct { char from[ID_LEN]; char to[ID_LEN]; } Edge;

typedef struct {
    Node nodes[MAX_NODES]; int n_nodes;
    Edge edges[MAX_EDGES]; int n_edges;
} Workflow;

/*
 * Zerlegt das Workflow-JSON. Kein vollwertiger Parser: er sucht die
 * "nodes"- und "edges"-Abschnitte und liest daraus die id/type- bzw.
 * from/to-Paare in Reihenfolge. Das reicht für das Schema aus llms.txt.
 */
static void parse_workflow(const char *body, Workflow *wf) {
    wf->n_nodes = 0;
    wf->n_edges = 0;

    const char *nodes = strstr(body, "\"nodes\"");
    const char *edges = strstr(body, "\"edges\"");

    if (nodes) {
        const char *p = nodes;
        const char *stop = edges && edges > nodes ? edges : body + strlen(body);
        while (wf->n_nodes < MAX_NODES) {
            const char *obj = strchr(p + 1, '{');
            if (!obj || obj >= stop) break;
            const char *end = strchr(obj, '}');
            if (!end) break;
            size_t len = (size_t)(end - obj) + 1;
            char chunk[1024];
            if (len >= sizeof chunk) len = sizeof chunk - 1;
            memcpy(chunk, obj, len);
            chunk[len] = '\0';

            Node *n = &wf->nodes[wf->n_nodes];
            if (json_str(chunk, "id", n->id, ID_LEN) &&
                json_str(chunk, "type", n->type, ID_LEN)) {
                wf->n_nodes++;
            }
            p = end;
        }
    }

    if (edges) {
        const char *p = edges;
        while (wf->n_edges < MAX_EDGES) {
            const char *obj = strchr(p + 1, '{');
            if (!obj) break;
            const char *end = strchr(obj, '}');
            if (!end) break;
            size_t len = (size_t)(end - obj) + 1;
            char chunk[512];
            if (len >= sizeof chunk) len = sizeof chunk - 1;
            memcpy(chunk, obj, len);
            chunk[len] = '\0';

            Edge *e = &wf->edges[wf->n_edges];
            if (json_str(chunk, "from", e->from, ID_LEN) &&
                json_str(chunk, "to", e->to, ID_LEN)) {
                wf->n_edges++;
            }
            p = end;
        }
    }
}

static int ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && strcmp(s + ls - lf, suffix) == 0;
}

static int node_index(const Workflow *wf, const char *id) {
    for (int i = 0; i < wf->n_nodes; i++)
        if (strcmp(wf->nodes[i].id, id) == 0) return i;
    return -1;
}

/* Topologische Reihenfolge (Kahn). Zyklen landen hinten statt zu verschwinden. */
static void topo_order(const Workflow *wf, int *order) {
    int indeg[MAX_NODES] = {0};
    int seen[MAX_NODES] = {0};

    for (int i = 0; i < wf->n_edges; i++) {
        int d = node_index(wf, wf->edges[i].to);
        if (d >= 0) indeg[d]++;
    }

    int count = 0;
    for (int pass = 0; pass < wf->n_nodes; pass++) {
        int pick = -1;
        for (int i = 0; i < wf->n_nodes; i++)
            if (!seen[i] && indeg[i] == 0) { pick = i; break; }
        if (pick < 0) break;
        seen[pick] = 1;
        order[count++] = pick;
        for (int i = 0; i < wf->n_edges; i++) {
            if (strcmp(wf->edges[i].from, wf->nodes[pick].id) == 0) {
                int d = node_index(wf, wf->edges[i].to);
                if (d >= 0) indeg[d]--;
            }
        }
    }
    for (int i = 0; i < wf->n_nodes; i++)
        if (!seen[i]) order[count++] = i;
}

/* ───────────────────────── Aktivitäts-Feed ───────────────────────── */

typedef struct {
    int seq;
    char actor[ID_LEN], action[ID_LEN], target[ID_LEN];
    char detail[EVENT_TEXT], state[16], ts[32];
    time_t at;
} Event;

static Event  g_events[MAX_EVENTS];
static int    g_event_count = 0;   /* Ringpuffer-Füllstand */
static int    g_seq = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int activity_post(const char *body) {
    pthread_mutex_lock(&g_lock);
    if (g_event_count == MAX_EVENTS) {
        memmove(g_events, g_events + 1, sizeof(Event) * (MAX_EVENTS - 1));
        g_event_count--;
    }
    Event *e = &g_events[g_event_count++];
    memset(e, 0, sizeof *e);
    e->seq = ++g_seq;
    if (!json_str(body, "actor",  e->actor,  ID_LEN))     strcpy(e->actor, "agent");
    if (!json_str(body, "action", e->action, ID_LEN))     strcpy(e->action, "work");
    json_str(body, "target", e->target, ID_LEN);
    json_str(body, "detail", e->detail, EVENT_TEXT);
    if (!json_str(body, "state", e->state, 16))           strcpy(e->state, "working");
    iso_now(e->ts, sizeof e->ts);
    e->at = time(NULL);
    int seq = e->seq;
    pthread_mutex_unlock(&g_lock);
    return seq;
}

/* ───────────────────────── HTTP-Ausgabe ───────────────────────── */

static void send_raw(int fd, int status, const char *ctype,
                     const char *body, size_t len) {
    const char *reason = status == 200 ? "OK"
                       : status == 404 ? "Not Found"
                       : status == 400 ? "Bad Request"
                       : status == 413 ? "Payload Too Large"
                       : status == 501 ? "Not Implemented"
                       : status == 204 ? "No Content" : "Error";
    char head[512];
    int hn = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        status, reason, ctype, len);
    (void)!write(fd, head, (size_t)hn);
    if (len) (void)!write(fd, body, len);
}

static void send_json(int fd, int status, const char *json) {
    send_raw(fd, status, "application/json", json, strlen(json));
}

/* Liefert eine Datei aus dem Elternverzeichnis (dort liegt die Oberfläche). */
static void send_file(int fd, const char *name, const char *ctype) {
    char path[512];
    snprintf(path, sizeof path, "../%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(path, sizeof path, "%s", name); f = fopen(path, "rb"); }
    if (!f) { send_json(fd, 404, "{\"error\":\"not found\"}"); return; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); send_json(fd, 404, "{\"error\":\"oom\"}"); return; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    send_raw(fd, 200, ctype, buf, got);
    free(buf);
}

/* ───────────────────────── Endpunkte ───────────────────────── */

static void handle_execute(int fd, const char *body) {
    Workflow wf;
    parse_workflow(body, &wf);

    int has_in = 0, has_out = 0;
    for (int i = 0; i < wf.n_nodes; i++) {
        if (ends_with(wf.nodes[i].type, "input"))  has_in = 1;
        if (ends_with(wf.nodes[i].type, "output")) has_out = 1;
    }

    char out[16384];
    size_t o = 0;
    char ts[32]; iso_now(ts, sizeof ts);

    if (wf.n_nodes == 0 || !has_in || !has_out) {
        char lines[1024];
        snprintf(lines, sizeof lines,
                 "Workflow execution started\\nNodes: %d, Edges: %d\\n%s%s%s",
                 wf.n_nodes, wf.n_edges,
                 wf.n_nodes == 0 ? "Workflow must contain at least one node\\n" : "",
                 !has_in  ? "No input node found\\n"  : "",
                 !has_out ? "No output node found"    : "");
        snprintf(out, sizeof out,
                 "{\"status\":\"error\",\"output\":\"%s\",\"results\":{},"
                 "\"timestamp\":\"%s\",\"engine\":\"c\"}", lines, ts);
        send_json(fd, 200, out);
        return;
    }

    int order[MAX_NODES];
    topo_order(&wf, order);

    o += (size_t)snprintf(out + o, sizeof out - o,
        "{\"status\":\"success\",\"engine\":\"c\",\"output\":\""
        "Workflow execution started (C engine)\\nNodes: %d, Edges: %d",
        wf.n_nodes, wf.n_edges);

    for (int i = 0; i < wf.n_nodes && o < sizeof out - 512; i++) {
        Node *n = &wf.nodes[order[i]];
        char eid[ID_LEN * 2], etype[ID_LEN * 2];
        json_escape(n->id, eid, sizeof eid);
        json_escape(n->type, etype, sizeof etype);
        o += (size_t)snprintf(out + o, sizeof out - o, "\\n%s: %s", eid, etype);
    }
    o += (size_t)snprintf(out + o, sizeof out - o,
                          "\\nWorkflow execution completed\",\"results\":{");

    for (int i = 0; i < wf.n_nodes && o < sizeof out - 256; i++) {
        Node *n = &wf.nodes[order[i]];
        char eid[ID_LEN * 2];
        json_escape(n->id, eid, sizeof eid);
        const char *kind = strcmp(n->type, "llm-node") == 0 ? "llm_output"
                         : ends_with(n->type, "input")      ? "input"
                         : ends_with(n->type, "output")     ? "output" : "step";
        o += (size_t)snprintf(out + o, sizeof out - o, "%s\"%s\":{\"type\":\"%s\"}",
                              i ? "," : "", eid, kind);
    }
    snprintf(out + o, sizeof out - o, "},\"timestamp\":\"%s\"}", ts);
    send_json(fd, 200, out);
}

static void handle_evaluate(int fd, const char *body) {
    Workflow wf;
    parse_workflow(body, &wf);

    int has_llm = 0;
    for (int i = 0; i < wf.n_nodes; i++)
        if (strcmp(wf.nodes[i].type, "llm-node") == 0) has_llm = 1;

    int score = 50;
    if (wf.n_nodes >= 3) score += 20;
    if (wf.n_edges > 0)  score += 15;
    if (has_llm)         score += 15;
    if (score > 100) score = 100;

    const char *verdict = score >= 85 ? "done" : "iterate";
    char ts[32]; iso_now(ts, sizeof ts);

    char sugg[512] = "";
    size_t s = 0;
    if (!has_llm)
        s += (size_t)snprintf(sugg + s, sizeof sugg - s,
             "{\"title\":\"Add LLM node\",\"description\":\"AI-Verarbeitung ergaenzen\","
             "\"action\":\"ADD_NODE(type='llm-node')\"}");
    if (wf.n_edges < wf.n_nodes - 1)
        s += (size_t)snprintf(sugg + s, sizeof sugg - s,
             "%s{\"title\":\"Connect more nodes\",\"description\":\"Datenfluss herstellen\","
             "\"action\":\"CONNECT_NODES(fromId, toId)\"}", s ? "," : "");

    char out[2048];
    snprintf(out, sizeof out,
        "{\"score\":%d,\"verdict\":\"%s\",\"model\":\"c-heuristic\","
        "\"gemma_live\":false,\"engine\":\"c\",\"suggestions\":[%s],"
        "\"feedback\":\"Workflow Evaluation Report (C engine)\\nScore: %d/100\\n"
        "Structure: %d nodes, %d connections\\n%s\\n%s\",\"timestamp\":\"%s\"}",
        score, verdict, sugg, score, wf.n_nodes, wf.n_edges,
        has_llm ? "Contains LLM node" : "Consider adding an LLM node",
        wf.n_edges ? "Nodes are connected" : "Nodes are disconnected", ts);
    send_json(fd, 200, out);
}

/* ───────────────────────── SVG-Endpunkte (svgkit.c) ───────────────────────── */

static int file_readable(const char *name) {
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "../%s", name);
    f = fopen(path, "rb");
    if (!f) { snprintf(path, sizeof path, "%s", name); f = fopen(path, "rb"); }
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* GET /api/svg/types — Inhalt von svg/types.json (einzige Quelle, wie bei der Python-Fassung). */
static void handle_svg_types(int fd) {
    if (!file_readable("svg/types.json")) {
        send_json(fd, 404, "{\"ok\":false,\"error\":\"\xE2\x80\x94 ausstehend \xE2\x80\x94\",\"needs\":\"svg/types.json\"}");
        return;
    }
    send_file(fd, "svg/types.json", "application/json; charset=utf-8");
}

/* POST /api/svg/generate — Rumpf = Spezifikation (JSON). */
static void handle_svg_generate(int fd, const char *body) {
    char *svg = (char *)malloc(SVG_OUT_MAX);
    char err[256], field[64] = "spec", msg[256] = "", ef[160], em[600], out[1100];
    size_t svg_len = 0;
    int rc;
    if (!svg) { send_json(fd, 500, "{\"ok\":false,\"error\":\"Speicher erschoepft\",\"engine\":\"c\"}"); return; }
    rc = svg_generate_n(body, strlen(body), svg, SVG_OUT_MAX, &svg_len, err, sizeof err);
    if (rc == SVG_OK) {
        size_t rcap = svg_len * 6 + 256, o;
        char *resp = (char *)malloc(rcap);
        if (!resp) { free(svg); send_json(fd, 500, "{\"ok\":false,\"error\":\"Speicher erschoepft\",\"engine\":\"c\"}"); return; }
        o = (size_t)snprintf(resp, rcap, "{\"ok\":true,\"svg\":\"");
        json_escape(svg, resp + o, rcap - o);
        o += strlen(resp + o);
        snprintf(resp + o, rcap - o, "\",\"bytes\":%zu,\"engine\":\"c\"}", svg_len);
        send_json(fd, 200, resp);
        free(resp);
        free(svg);
        return;
    }
    free(svg);
    {   /* err = "<Feld>: <Meldung>" */
        const char *sep = strstr(err, ": ");
        if (sep) {
            size_t fl = (size_t)(sep - err);
            if (fl >= sizeof field) fl = sizeof field - 1;
            memcpy(field, err, fl); field[fl] = 0;
            snprintf(msg, sizeof msg, "%s", sep + 2);
        } else snprintf(msg, sizeof msg, "%s", err);
    }
    json_escape(field, ef, sizeof ef);
    json_escape(msg, em, sizeof em);
    if (rc == SVG_E_SUBSET)
        snprintf(out, sizeof out, "{\"ok\":false,\"error\":\"%s\",\"field\":\"%s\",\"supported\":%s,\"engine\":\"c\"}",
                 em, ef, SVG_SUPPORTED_JSON);
    else
        snprintf(out, sizeof out, "{\"ok\":false,\"error\":\"%s\",\"field\":\"%s\",\"engine\":\"c\"}", em, ef);
    send_json(fd, rc == SVG_E_SUBSET ? 501 : rc == SVG_E_LIMIT ? 413 : rc == SVG_E_BUF ? 500 : 400, out);
}

static void handle_activity_get(int fd, const char *path) {
    long since = 0;
    const char *q = strstr(path, "since=");
    if (q) since = strtol(q + 6, NULL, 10);

    pthread_mutex_lock(&g_lock);
    time_t now = time(NULL);
    int active = 0;
    for (int i = 0; i < g_event_count; i++)
        if (strcmp(g_events[i].state, "working") == 0 &&
            now - g_events[i].at < 6) active = 1;

    char out[32768];
    size_t o = (size_t)snprintf(out, sizeof out,
        "{\"active\":%s,\"latest_seq\":%d,\"engine\":\"c\",\"events\":[",
        active ? "true" : "false", g_seq);

    int first = 1;
    for (int i = 0; i < g_event_count && o < sizeof out - 1024; i++) {
        Event *e = &g_events[i];
        if (e->seq <= since) continue;
        char a[ID_LEN*2], ac[ID_LEN*2], t[ID_LEN*2], d[EVENT_TEXT*2];
        json_escape(e->actor, a, sizeof a);
        json_escape(e->action, ac, sizeof ac);
        json_escape(e->target, t, sizeof t);
        json_escape(e->detail, d, sizeof d);
        o += (size_t)snprintf(out + o, sizeof out - o,
            "%s{\"seq\":%d,\"actor\":\"%s\",\"action\":\"%s\",\"target\":\"%s\","
            "\"detail\":\"%s\",\"state\":\"%s\",\"timestamp\":\"%s\"}",
            first ? "" : ",", e->seq, a, ac, t, d, e->state, e->ts);
        first = 0;
    }
    snprintf(out + o, sizeof out - o, "]}");
    pthread_mutex_unlock(&g_lock);
    send_json(fd, 200, out);
}

/* ───────────────────────── Verbindung ───────────────────────── */

static void *handle_conn(void *arg) {
    int fd = (int)(intptr_t)arg;
    char *req = malloc(MAX_REQ);
    if (!req) { close(fd); return NULL; }

    size_t total = 0;
    ssize_t n;
    while (total < MAX_REQ - 1 &&
           (n = read(fd, req + total, MAX_REQ - 1 - total)) > 0) {
        total += (size_t)n;
        req[total] = '\0';
        char *hdr_end = strstr(req, "\r\n\r\n");
        if (!hdr_end) continue;
        /* Body vollständig? */
        char *cl = ci_find(req, "Content-Length:");
        if (!cl) break;
        long want = strtol(cl + 15, NULL, 10);
        size_t have = total - (size_t)(hdr_end + 4 - req);
        if ((long)have >= want) break;
    }
    req[total] = '\0';

    char method[16] = {0}, path[512] = {0};
    sscanf(req, "%15s %511s", method, path);
    char *body = strstr(req, "\r\n\r\n");
    body = body ? body + 4 : (char *)"";

    if (strcmp(method, "OPTIONS") == 0) {
        send_raw(fd, 204, "text/plain", "", 0);
    } else if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
            send_file(fd, "index.html", "text/html; charset=utf-8");
        else if (strcmp(path, "/app.js") == 0)
            send_file(fd, "app.js", "application/javascript; charset=utf-8");
        else if (strcmp(path, "/styles.css") == 0)
            send_file(fd, "styles.css", "text/css; charset=utf-8");
        else if (strcmp(path, "/llms.txt") == 0)
            send_file(fd, "llms.txt", "text/plain; charset=utf-8");
        else if (strcmp(path, "/health") == 0) {
            char ts[32], out[256];
            iso_now(ts, sizeof ts);
            snprintf(out, sizeof out,
                "{\"status\":\"healthy\",\"service\":\"design-system-ui\","
                "\"engine\":\"c\",\"timestamp\":\"%s\"}", ts);
            send_json(fd, 200, out);
        }
        else if (strcmp(path, "/api/edition") == 0) {
            const char *ed = getenv("EDITION");
            int claw = ed && strcmp(ed, "openclaw") == 0;
            char out[512];
            snprintf(out, sizeof out,
                "{\"edition\":{\"id\":\"%s\",\"name\":\"%s\",\"engine\":\"c\","
                "\"tagline\":\"C-Geruest: gleiche Oberflaeche, Kern in C\","
                "\"features\":{\"openclaw\":%s}},"
                "\"available\":[\"normal\",\"openclaw\"]}",
                claw ? "openclaw" : "normal", claw ? "OpenCLAW" : "Normal",
                claw ? "true" : "false");
            send_json(fd, 200, out);
        }
        else if (strcmp(path, "/api/patterns") == 0) {
            send_json(fd, 200,
              "{\"patterns\":{"
              "\"text-classifier\":{\"name\":\"Text Classifier\",\"description\":\"Text in Kategorien einordnen\",\"icon\":\"T\"},"
              "\"rag-pipeline\":{\"name\":\"RAG Pipeline\",\"description\":\"Retrieval-Augmented Generation\",\"icon\":\"R\"},"
              "\"agent-loop\":{\"name\":\"Agent Loop\",\"description\":\"Think-Act-Observe\",\"icon\":\"A\"},"
              "\"eval-loop\":{\"name\":\"Evaluation Loop\",\"description\":\"Ausfuehren, bewerten, iterieren\",\"icon\":\"E\"}}}");
        }
        else if (strncmp(path, "/api/agent/activity", 19) == 0)
            handle_activity_get(fd, path);
        else if (strcmp(path, "/api/svg/types") == 0 || strncmp(path, "/api/svg/types?", 15) == 0)
            handle_svg_types(fd);
        else if (strcmp(path, "/favicon.ico") == 0) {
            const char *svg =
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">"
              "<rect width=\"32\" height=\"32\" rx=\"7\" fill=\"#0ea5e9\"/></svg>";
            send_raw(fd, 200, "image/svg+xml", svg, strlen(svg));
        }
        else send_json(fd, 404, "{\"error\":\"not found\"}");
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/workflow/execute") == 0)
            handle_execute(fd, body);
        else if (strcmp(path, "/api/workflow/evaluate") == 0)
            handle_evaluate(fd, body);
        else if (strcmp(path, "/api/svg/generate") == 0)
            handle_svg_generate(fd, body);
        else if (strcmp(path, "/api/agent/activity") == 0) {
            char out[128];
            snprintf(out, sizeof out, "{\"ok\":true,\"seq\":%d,\"engine\":\"c\"}",
                     activity_post(body));
            send_json(fd, 200, out);
        }
        else send_json(fd, 404, "{\"error\":\"not found\"}");
    } else {
        send_json(fd, 404, "{\"error\":\"unsupported method\"}");
    }

    free(req);
    close(fd);
    return NULL;
}

int main(void) {
    const char *ps = getenv("PORT");
    int port = ps ? atoi(ps) : 8080;

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); return 1;
    }
    if (listen(srv, 64) < 0) { perror("listen"); return 1; }

    fprintf(stderr, "Design System UI (C-Geruest) auf http://localhost:%d\n", port);
    fprintf(stderr, "  Agenten: http://localhost:%d/llms.txt\n", port);
    fflush(stderr);

    for (;;) {
        int cfd = accept(srv, NULL, NULL);
        if (cfd < 0) continue;
        pthread_t th;
        if (pthread_create(&th, NULL, handle_conn, (void *)(intptr_t)cfd) == 0)
            pthread_detach(th);
        else
            close(cfd);
    }
}
