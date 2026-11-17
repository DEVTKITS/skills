/*
 * svgkit.c — C99-Teilmenge des SVG-Generators "svgspec/1" (Regeln: svg/SPEC.md,
 * Referenz: svgkit.js, erwartete Ausgaben: svg/golden/).
 *
 * Aufbau: (1) Mathe ohne libm, (2) Zahlenformat und Text, (3) JSON-Leser,
 * (4) Normalisierung der Spezifikation, (5) Ausgabe der Typen, (6) Einstieg.
 *
 * Alle Rechnungen sind IEEE-754-Doppelte in der Reihenfolge des JS-Quelltextes
 * (Gleitkomma ist nicht assoziativ). Übersetzen ohne -ffast-math und mit
 * -ffp-contract=off (Makefile), damit der Compiler nichts zusammenzieht.
 *
 * Die Datei ist auch als Teil von server.c einbindbar ("#include"): alles
 * Interne ist static und mit sk_/Sk-Präfix benannt.
 */
#include "svgkit.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SK_P 16.0
#define SK_PI 3.141592653589793
#define SK_NA "\xE2\x80\x94 ausstehend \xE2\x80\x94" /* — ausstehend — */

/* ═════════════════════════ 1. Mathe ohne libm ═════════════════════════ */

static int sk_finite(double x) { return x - x == 0.0; } /* false für NaN und ±Inf */
static double sk_min(double a, double b) { return b < a ? b : a; }
static double sk_max(double a, double b) { return b > a ? b : a; }

static double sk_floor(double x) {
    double t;
    if (!(x == x) || x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    t = (double)(int64_t)x;
    return t > x ? t - 1.0 : t;
}

static double sk_ceil(double x) {
    double t;
    if (!(x == x) || x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    t = (double)(int64_t)x;
    return t < x ? t + 1.0 : t;
}

/* sin/cos: Port von fdlibm (wie V8 ieee754::sin/cos), damit die Bits mit svgkit.js
 * übereinstimmen. Gebraucht werden nur kleine Argumente (|x| < 8). */
static int32_t sk_hi(double x) {
    uint64_t u;
    memcpy(&u, &x, sizeof u);
    return (int32_t)(u >> 32);
}

static double sk_from_hi(int32_t hi) {
    uint64_t u = (uint64_t)(uint32_t)hi << 32;
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

static double sk_ksin(double x, double y, int iy) {
    static const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
                        S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
                        S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    double z, r, v;
    int32_t ix = sk_hi(x) & 0x7fffffff;
    if (ix < 0x3e400000 && (int)x == 0) return x;
    z = x * x;
    v = z * x;
    r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) return x + v * (S1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

static double sk_kcos(double x, double y) {
    static const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
                        C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
                        C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    double a, iz, z, r, qx;
    int32_t ix = sk_hi(x) & 0x7fffffff;
    if (ix < 0x3e400000 && (int)x == 0) return 1.0;
    z = x * x;
    r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3FD33333) return 1.0 - (0.5 * z - (z * r - x * y));
    qx = ix > 0x3fe90000 ? 0.28125 : sk_from_hi(ix - 0x00200000);
    iz = 0.5 * z - qx;
    a = 1.0 - qx;
    return a - (iz - (z * r - x * y));
}

/* Argumentreduktion modulo pi/2 (Bereich |x| <= 2^19*pi/2; darüber wird vorab
 * grob reduziert — im Generator unerreichbar, nur zur Sicherheit). */
static int sk_rem_pio2(double x, double *y) {
    static const double invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079632673412561417e+00,
                        pio2_1t = 6.07710050650619224932e-11, pio2_2 = 6.07710050630396597660e-11,
                        pio2_2t = 2.02226624879595063154e-21, pio2_3 = 2.02226624871116645580e-21,
                        pio2_3t = 8.47842766036889956997e-32;
    static const int32_t npio2_hw[4] = {0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB};
    double z, w, t, r, fn;
    int32_t hx = sk_hi(x), ix = hx & 0x7fffffff, i, j, n;
    if (ix <= 0x3fe921fb) { y[0] = x; y[1] = 0; return 0; }
    if (ix < 0x4002d97c) { /* |x| < 3pi/4: n = ±1 */
        if (hx > 0) {
            z = x - pio2_1;
            if (ix != 0x3ff921fb) { y[0] = z - pio2_1t; y[1] = (z - y[0]) - pio2_1t; }
            else { z -= pio2_2; y[0] = z - pio2_2t; y[1] = (z - y[0]) - pio2_2t; }
            return 1;
        }
        z = x + pio2_1;
        if (ix != 0x3ff921fb) { y[0] = z + pio2_1t; y[1] = (z - y[0]) + pio2_1t; }
        else { z += pio2_2; y[0] = z + pio2_2t; y[1] = (z - y[0]) + pio2_2t; }
        return -1;
    }
    t = x < 0 ? -x : x;
    if (t > 1.0e5) t = t - 6.283185307179586 * sk_floor(t / 6.283185307179586); /* unerreichbar */
    ix = sk_hi(t) & 0x7fffffff;
    n = (int32_t)(t * invpio2 + 0.5);
    fn = (double)n;
    r = t - fn * pio2_1;
    w = fn * pio2_1t;
    if (n >= 1 && n <= 4 && ix != npio2_hw[n - 1]) {
        y[0] = r - w;
    } else {
        j = ix >> 20;
        y[0] = r - w;
        i = j - ((sk_hi(y[0]) >> 20) & 0x7ff);
        if (i > 16) {
            t = r; w = fn * pio2_2; r = t - w; w = fn * pio2_2t - ((t - r) - w);
            y[0] = r - w;
            i = j - ((sk_hi(y[0]) >> 20) & 0x7ff);
            if (i > 49) {
                t = r; w = fn * pio2_3; r = t - w; w = fn * pio2_3t - ((t - r) - w);
                y[0] = r - w;
            }
        }
    }
    y[1] = (r - y[0]) - w;
    if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; return -n; }
    return n;
}

static double sk_sin(double x) {
    double y[2];
    int32_t ix = sk_hi(x) & 0x7fffffff;
    int n;
    if (ix <= 0x3fe921fb) return sk_ksin(x, 0.0, 0);
    if (ix >= 0x7ff00000) return 0.0;
    n = sk_rem_pio2(x, y);
    switch (n & 3) {
        case 0: return sk_ksin(y[0], y[1], 1);
        case 1: return sk_kcos(y[0], y[1]);
        case 2: return -sk_ksin(y[0], y[1], 1);
        default: return -sk_kcos(y[0], y[1]);
    }
}

static double sk_cos(double x) {
    double y[2];
    int32_t ix = sk_hi(x) & 0x7fffffff;
    int n;
    if (ix <= 0x3fe921fb) return sk_kcos(x, 0.0);
    if (ix >= 0x7ff00000) return 1.0;
    n = sk_rem_pio2(x, y);
    switch (n & 3) {
        case 0: return sk_kcos(y[0], y[1]);
        case 1: return -sk_ksin(y[0], y[1], 1);
        case 2: return -sk_kcos(y[0], y[1]);
        default: return sk_ksin(y[0], y[1], 1);
    }
}

/* ═════════════════════ 2. Zahlenformat, Text, Ausgabepuffer ═════════════════════ */

/* fmt(n) nach SPEC §2: auf 2 Nachkommastellen (halb weg von null), ohne überflüssige Nullen.
 * out braucht mindestens 32 Bytes. Rückgabe: Länge. */
static size_t sk_fmt(double n, char *out) {
    double k;
    uint64_t ki, ip;
    unsigned f;
    char tmp[24];
    size_t o = 0, t = 0;
    if (!sk_finite(n)) { out[0] = '0'; out[1] = 0; return 1; }
    k = sk_floor((n < 0 ? -n : n) * 100 + .5);
    if (k > 9.0e15) k = 9.0e15; /* außerhalb des definierten Bereichs, nur gegen Überlauf */
    if (k == 0) { out[0] = '0'; out[1] = 0; return 1; }
    ki = (uint64_t)k;
    ip = ki / 100;
    f = (unsigned)(ki % 100);
    if (n < 0) out[o++] = '-';
    if (ip == 0) tmp[t++] = '0';
    while (ip) { tmp[t++] = (char)('0' + ip % 10); ip /= 10; }
    while (t) out[o++] = tmp[--t];
    if (f) {
        out[o++] = '.';
        if (f < 10) { out[o++] = '0'; out[o++] = (char)('0' + f); }
        else if (f % 10) { out[o++] = (char)('0' + f / 10); out[o++] = (char)('0' + f % 10); }
        else out[o++] = (char)('0' + f / 10);
    }
    out[o] = 0;
    return o;
}

/* short(v): 1,2k / 3,4M ... nach SPEC §3. out ≥ 40 Bytes. */
static void sk_short(double v, char *out) {
    double a = v < 0 ? -v : v;
    size_t n;
    if (a >= 1e6) { n = sk_fmt(v / 1e6, out); out[n] = 'M'; out[n + 1] = 0; }
    else if (a >= 1e4) { n = sk_fmt(v / 1e3, out); out[n] = 'k'; out[n + 1] = 0; }
    else sk_fmt(v, out);
}

/* Anzahl Codepunkte einer wohlgeformten UTF-8-Zeichenkette (Fortsetzungsbytes zählen nicht). */
static size_t sk_cps(const char *s) {
    size_t n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

/* Länge in Bytes der UTF-8-Folge ab Leitbyte c (wohlgeformte Eingabe). */
static size_t sk_seqlen(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

/* trunc(s, n) nach SPEC §3; out braucht strlen(s) + 4 Bytes. */
static void sk_trunc(const char *s, double n, char *out) {
    size_t len = sk_cps(s), keep, i = 0, o = 0;
    if ((double)len <= n) { memcpy(out, s, strlen(s) + 1); return; }
    if (n < 1) { out[0] = 0; return; }
    keep = (size_t)(n - 1); /* n < len, also keep < len */
    while (s[i] && keep) {
        size_t l = sk_seqlen((unsigned char)s[i]), q;
        for (q = 0; q < l && s[i + q]; q++) out[o++] = s[i + q];
        i += l;
        keep--;
    }
    out[o++] = (char)0xE2; out[o++] = (char)0x80; out[o++] = (char)0xA6; /* … */
    out[o] = 0;
}

static double sk_fit(double px, double fs) { return sk_floor(px / (fs * 0.6)); }

/* Ausgabepuffer mit Überlaufschutz: nach dem ersten Überlauf wird nichts mehr geschrieben. */
typedef struct { char *b; size_t cap, len; int over; } SkW;

static void w_raw(SkW *o, const char *s, size_t n) {
    if (o->over) return;
    if (n >= o->cap - o->len) { o->over = 1; return; } /* cap - len ≥ 1, Platz für NUL bleibt */
    memcpy(o->b + o->len, s, n);
    o->len += n;
}
static void w_s(SkW *o, const char *s) { w_raw(o, s, strlen(s)); }
static void w_c(SkW *o, char c) { w_raw(o, &c, 1); }
static void w_nl(SkW *o) { w_c(o, '\n'); }
static void w_num(SkW *o, double v) { char b[40]; size_t n = sk_fmt(v, b); w_raw(o, b, n); }

/* esc(): & < > " ' */
static void w_esc(SkW *o, const char *s, size_t n) {
    size_t i, from = 0;
    for (i = 0; i < n; i++) {
        const char *r = 0;
        switch (s[i]) {
            case '&': r = "&amp;"; break;
            case '<': r = "&lt;"; break;
            case '>': r = "&gt;"; break;
            case '"': r = "&quot;"; break;
            case '\'': r = "&apos;"; break;
            default: break;
        }
        if (r) { w_raw(o, s + from, i - from); w_s(o, r); from = i + 1; }
    }
    w_raw(o, s + from, n - from);
}

static void a_num(SkW *o, const char *k, double v) {
    w_c(o, ' '); w_s(o, k); w_s(o, "=\""); w_num(o, v); w_c(o, '"');
}
static void a_str(SkW *o, const char *k, const char *v) {
    w_c(o, ' '); w_s(o, k); w_s(o, "=\""); w_esc(o, v, strlen(v)); w_c(o, '"');
}

/* Pfadtext: zwischen zwei aufeinanderfolgenden Zahlen genau ein Leerzeichen (D() aus SPEC §2). */
typedef struct { SkW *w; int prev; } SkP;
static void p_c(SkP *p, char c) { w_c(p->w, c); p->prev = 0; }
static void p_n(SkP *p, double v) { if (p->prev) w_c(p->w, ' '); w_num(p->w, v); p->prev = 1; }

/* ═════════════════════════ 3. JSON-Leser ═════════════════════════ */
/*
 * Vollständiger JSON-Leser (RFC 8259, wie JSON.parse): Baum aus Knoten in einem
 * Feld (Indizes statt Zeiger, Feld darf wachsen), Zeichenketten UTF-8 in einem
 * eigenen Speicher mit fester Obergrenze (3 × Eingabelänge). Ungültiges UTF-8 wird
 * wie in Node durch U+FFFD ersetzt (größte Teilfolge), \uXXXX-Paare werden
 * zusammengesetzt, einzelne Ersatzzeichen als WTF-8 (3 Bytes) gehalten und später
 * von clean() entfernt. Tiefe, Knotenzahl und Eingabegröße sind begrenzt.
 */
enum { SK_NULL = 0, SK_BOOL, SK_NUM, SK_STR, SK_ARR, SK_OBJ };

typedef struct {
    uint8_t t;
    uint32_t next, first, count, koff, klen;
    union { double n; struct { uint32_t off, len; } s; } u;
} SkVal;

typedef struct {
    const unsigned char *src;
    size_t n, pos;
    SkVal *v;
    uint32_t nv, cap, maxv;
    char *str;
    size_t slen, scap;
    int code; /* SVG_E_SPEC oder SVG_E_LIMIT */
    char msg[96];
} SkJ;

#define SK_MAX_DEPTH 100

static int j_fail(SkJ *j, int code, const char *m) {
    size_t n = strlen(m);
    if (n >= sizeof j->msg) n = sizeof j->msg - 1;
    memcpy(j->msg, m, n);
    j->msg[n] = 0;
    j->code = code;
    return -1;
}

static void j_ws(SkJ *j) {
    while (j->pos < j->n) {
        unsigned char c = j->src[j->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') j->pos++;
        else break;
    }
}

static uint32_t j_new(SkJ *j, uint8_t t) {
    SkVal *nv;
    uint32_t i;
    if (j->nv >= j->cap) {
        uint32_t nc = j->cap ? j->cap * 2 : 64;
        if (nc > j->maxv) nc = j->maxv;
        if (j->nv >= nc) { j_fail(j, SVG_E_LIMIT, "zu viele Werte"); return 0; }
        nv = (SkVal *)realloc(j->v, (size_t)nc * sizeof(SkVal));
        if (!nv) { j_fail(j, SVG_E_LIMIT, "Speicher erschöpft"); return 0; }
        j->v = nv;
        j->cap = nc;
    }
    i = j->nv++;
    memset(&j->v[i], 0, sizeof(SkVal));
    j->v[i].t = t;
    return i;
}

static void j_put(SkJ *j, uint32_t cp) {
    char *d = j->str + j->slen;
    if (cp < 0x80) { d[0] = (char)cp; j->slen += 1; }
    else if (cp < 0x800) { d[0] = (char)(0xC0 | (cp >> 6)); d[1] = (char)(0x80 | (cp & 0x3F)); j->slen += 2; }
    else if (cp < 0x10000) {
        d[0] = (char)(0xE0 | (cp >> 12)); d[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        d[2] = (char)(0x80 | (cp & 0x3F)); j->slen += 3;
    } else {
        d[0] = (char)(0xF0 | (cp >> 18)); d[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        d[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); d[3] = (char)(0x80 | (cp & 0x3F)); j->slen += 4;
    }
}

static int j_hex4(const unsigned char *p, uint32_t *out) {
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        unsigned char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

/* WHATWG-UTF-8-Dekodierung eines Nicht-ASCII-Zeichens ab p; ungültig → U+FFFD und
 * nur die gültige Teilfolge verbrauchen. Rückgabe: verbrauchte Bytes (≥ 1). */
static size_t j_utf8(const unsigned char *p, size_t avail, uint32_t *cp) {
    unsigned char b0 = p[0], lo = 0x80, hi = 0xBF;
    size_t need, i;
    uint32_t v;
    if (b0 >= 0xC2 && b0 <= 0xDF) { need = 1; v = b0 & 0x1F; }
    else if (b0 >= 0xE0 && b0 <= 0xEF) {
        need = 2; v = b0 & 0x0F;
        if (b0 == 0xE0) lo = 0xA0;
        if (b0 == 0xED) hi = 0x9F;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        need = 3; v = b0 & 0x07;
        if (b0 == 0xF0) lo = 0x90;
        if (b0 == 0xF4) hi = 0x8F;
    } else { *cp = 0xFFFD; return 1; }
    for (i = 1; i <= need; i++) {
        unsigned char b;
        if (i >= avail) { *cp = 0xFFFD; return i; }
        b = p[i];
        if (b < lo || b > hi) { *cp = 0xFFFD; return i; }
        lo = 0x80; hi = 0xBF;
        v = (v << 6) | (b & 0x3F);
    }
    *cp = v;
    return need + 1;
}

static int j_string(SkJ *j, uint32_t *off, uint32_t *len) {
    size_t start = j->slen;
    j->pos++; /* öffnendes " */
    for (;;) {
        unsigned char c;
        if (j->slen + 8 > j->scap) return j_fail(j, SVG_E_LIMIT, "Zeichenkettenspeicher erschöpft");
        if (j->pos >= j->n) return j_fail(j, SVG_E_SPEC, "Zeichenkette nicht beendet");
        c = j->src[j->pos];
        if (c == '"') { j->pos++; break; }
        if (c < 0x20) return j_fail(j, SVG_E_SPEC, "Steuerzeichen in Zeichenkette");
        if (c == '\\') {
            unsigned char e;
            uint32_t u, u2;
            if (j->pos + 1 >= j->n) return j_fail(j, SVG_E_SPEC, "Zeichenkette nicht beendet");
            e = j->src[j->pos + 1];
            j->pos += 2;
            switch (e) {
                case '"': j->str[j->slen++] = '"'; break;
                case '\\': j->str[j->slen++] = '\\'; break;
                case '/': j->str[j->slen++] = '/'; break;
                case 'b': j->str[j->slen++] = '\b'; break;
                case 'f': j->str[j->slen++] = '\f'; break;
                case 'n': j->str[j->slen++] = '\n'; break;
                case 'r': j->str[j->slen++] = '\r'; break;
                case 't': j->str[j->slen++] = '\t'; break;
                case 'u':
                    if (j->pos + 4 > j->n || !j_hex4(j->src + j->pos, &u))
                        return j_fail(j, SVG_E_SPEC, "ungültige \\u-Folge");
                    j->pos += 4;
                    if (u >= 0xD800 && u <= 0xDBFF && j->pos + 6 <= j->n && j->src[j->pos] == '\\' &&
                        j->src[j->pos + 1] == 'u' && j_hex4(j->src + j->pos + 2, &u2) &&
                        u2 >= 0xDC00 && u2 <= 0xDFFF) {
                        j->pos += 6;
                        u = 0x10000 + ((u - 0xD800) << 10) + (u2 - 0xDC00);
                    }
                    j_put(j, u); /* einzelne Ersatzzeichen als 3-Byte-Folge (WTF-8) */
                    break;
                default: return j_fail(j, SVG_E_SPEC, "ungültige Escape-Folge");
            }
        } else if (c < 0x80) {
            j->str[j->slen++] = (char)c;
            j->pos++;
        } else {
            uint32_t cp;
            j->pos += j_utf8(j->src + j->pos, j->n - j->pos, &cp);
            j_put(j, cp);
        }
    }
    *off = (uint32_t)start;
    *len = (uint32_t)(j->slen - start);
    return 0;
}

static int j_number(SkJ *j, double *out) {
    size_t s = j->pos, p = j->pos, n = j->n, len;
    const unsigned char *a = j->src;
    char small[64], *buf = small;
    if (p < n && a[p] == '-') p++;
    if (p < n && a[p] == '0') p++;
    else if (p < n && a[p] >= '1' && a[p] <= '9') { while (p < n && a[p] >= '0' && a[p] <= '9') p++; }
    else return j_fail(j, SVG_E_SPEC, "ungültige Zahl");
    if (p < n && a[p] == '.') {
        p++;
        if (!(p < n && a[p] >= '0' && a[p] <= '9')) return j_fail(j, SVG_E_SPEC, "ungültige Zahl");
        while (p < n && a[p] >= '0' && a[p] <= '9') p++;
    }
    if (p < n && (a[p] == 'e' || a[p] == 'E')) {
        p++;
        if (p < n && (a[p] == '+' || a[p] == '-')) p++;
        if (!(p < n && a[p] >= '0' && a[p] <= '9')) return j_fail(j, SVG_E_SPEC, "ungültige Zahl");
        while (p < n && a[p] >= '0' && a[p] <= '9') p++;
    }
    len = p - s;
    if (len >= sizeof small) {
        buf = (char *)malloc(len + 1);
        if (!buf) return j_fail(j, SVG_E_LIMIT, "Speicher erschöpft");
    }
    memcpy(buf, a + s, len);
    buf[len] = 0;
    *out = strtod(buf, NULL); /* korrekt gerundet wie JSON.parse; 1e999 → Inf, wird später abgelehnt */
    if (buf != small) free(buf);
    j->pos = p;
    return 0;
}

static int j_lit(SkJ *j, const char *w) {
    size_t l = strlen(w);
    if (j->n - j->pos >= l && memcmp(j->src + j->pos, w, l) == 0) { j->pos += l; return 0; }
    return j_fail(j, SVG_E_SPEC, "unerwartetes Zeichen");
}

static int j_value(SkJ *j, int depth, uint32_t *out) {
    unsigned char c;
    uint32_t idx;
    j_ws(j);
    if (j->pos >= j->n) return j_fail(j, SVG_E_SPEC, "unerwartetes Ende der Eingabe");
    c = j->src[j->pos];
    if (c == '{' || c == '[') {
        uint32_t last = 0;
        unsigned char close = c == '{' ? '}' : ']';
        if (depth >= SK_MAX_DEPTH) return j_fail(j, SVG_E_LIMIT, "zu tief verschachtelt");
        idx = j_new(j, c == '{' ? SK_OBJ : SK_ARR);
        if (!idx) return -1;
        j->pos++;
        j_ws(j);
        if (j->pos < j->n && j->src[j->pos] == close) { j->pos++; *out = idx; return 0; }
        for (;;) {
            uint32_t child, koff = 0, klen = 0;
            if (c == '{') {
                j_ws(j);
                if (j->pos >= j->n || j->src[j->pos] != '"') return j_fail(j, SVG_E_SPEC, "Schlüssel erwartet");
                if (j_string(j, &koff, &klen)) return -1;
                j_ws(j);
                if (j->pos >= j->n || j->src[j->pos] != ':') return j_fail(j, SVG_E_SPEC, "':' erwartet");
                j->pos++;
            }
            if (j_value(j, depth + 1, &child)) return -1;
            j->v[child].koff = koff;
            j->v[child].klen = klen;
            if (last) j->v[last].next = child; else j->v[idx].first = child;
            last = child;
            j->v[idx].count++;
            j_ws(j);
            if (j->pos < j->n && j->src[j->pos] == ',') { j->pos++; continue; }
            if (j->pos < j->n && j->src[j->pos] == close) { j->pos++; break; }
            return j_fail(j, SVG_E_SPEC, "',' oder Klammer erwartet");
        }
        *out = idx;
        return 0;
    }
    if (c == '"') {
        uint32_t off, len;
        idx = j_new(j, SK_STR);
        if (!idx) return -1;
        if (j_string(j, &off, &len)) return -1;
        j->v[idx].u.s.off = off;
        j->v[idx].u.s.len = len;
        *out = idx;
        return 0;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        double d = 0;
        if (j_number(j, &d)) return -1;
        idx = j_new(j, SK_NUM);
        if (!idx) return -1;
        j->v[idx].u.n = d;
        *out = idx;
        return 0;
    }
    if (c == 't' || c == 'f' || c == 'n') {
        if (j_lit(j, c == 't' ? "true" : c == 'f' ? "false" : "null")) return -1;
        idx = j_new(j, c == 'n' ? SK_NULL : SK_BOOL);
        if (!idx) return -1;
        j->v[idx].u.n = c == 't' ? 1.0 : 0.0;
        *out = idx;
        return 0;
    }
    return j_fail(j, SVG_E_SPEC, "unerwartetes Zeichen");
}

/* Mitglied "key" eines Objekts (bei doppelten Schlüsseln gewinnt das letzte); 0 = fehlt. */
static uint32_t j_get(const SkJ *j, uint32_t obj, const char *key) {
    size_t kl = strlen(key);
    uint32_t c, r = 0;
    for (c = j->v[obj].first; c; c = j->v[c].next)
        if (j->v[c].klen == kl && memcmp(j->str + j->v[c].koff, key, kl) == 0) r = c;
    return r;
}

static int j_isnull(const SkJ *j, uint32_t i) { return i == 0 || j->v[i].t == SK_NULL; }

/* ═════════════════════════ 4. Normalisierung ═════════════════════════ */

enum { TY_BAR, TY_DONUT, TY_PATTERN, TY_BADGE, TY_ICON, TY_GRADIENT };
enum { TH_MASTER, TH_LIGHT, TH_CONTRAST, TH_CONTROL, TH_CONTENT };
enum { KD_GRID, KD_DOTS, KD_STRIPES, KD_WAVES, KD_CONFETTI };

#define SK_NLAB 64
#define SK_LABB 161 /* 40 Codepunkte × 4 Bytes + NUL */

typedef struct {
    int type, theme, vars, kind, gkind, icon, bg, values, grid, heading, legend;
    double seed, width, height, size, stroke, angle, density, opacity, thickness, cx, cy;
    double data[SK_NLAB];
    int n, nlab;
    char idprefix[129], title[481], center[65], label[129], value[129], text[241];
    char color[16], from[16], to[16], via[16];
    char labels[SK_NLAB][SK_LABB];
} SkSpec;

typedef struct {
    SkJ j;
    uint32_t root;
    SkSpec *s;
    char efield[32], emsg[160];
} SkC;

static int c_fail(SkC *c, const char *field, const char *msg) {
    size_t n;
    n = strlen(field); if (n >= sizeof c->efield) n = sizeof c->efield - 1;
    memcpy(c->efield, field, n); c->efield[n] = 0;
    n = strlen(msg); if (n >= sizeof c->emsg) n = sizeof c->emsg - 1;
    memcpy(c->emsg, msg, n); c->emsg[n] = 0;
    return SVG_E_SPEC;
}

/* clean(s, maxcp): entfernt U+0000–U+001F, U+007F, U+FFFE, U+FFFF und Ersatzzeichen-Hälften,
 * danach die ersten maxcp Codepunkte. out braucht maxcp*4+1 Bytes. */
static void sk_clean(const char *s, size_t n, size_t maxcp, char *out) {
    size_t i = 0, o = 0, cnt = 0;
    while (i < n && cnt < maxcp) {
        unsigned char b = (unsigned char)s[i];
        size_t l = sk_seqlen(b), q;
        uint32_t cp;
        if (i + l > n) l = n - i;
        if (l == 1) cp = b;
        else if (l == 2) cp = ((uint32_t)(b & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
        else if (l == 3) cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)((unsigned char)s[i + 1] & 0x3F) << 6) |
                              ((unsigned char)s[i + 2] & 0x3F);
        else cp = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)((unsigned char)s[i + 1] & 0x3F) << 12) |
                  ((uint32_t)((unsigned char)s[i + 2] & 0x3F) << 6) | ((unsigned char)s[i + 3] & 0x3F);
        if (cp < 0x20 || cp == 0x7F || cp == 0xFFFE || cp == 0xFFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { i += l; continue; }
        for (q = 0; q < l; q++) out[o++] = s[i + q];
        i += l;
        cnt++;
    }
    out[o] = 0;
}

static int v_is_num(const SkC *c, uint32_t v) { return c->j.v[v].t == SK_NUM && sk_finite(c->j.v[v].u.n); }

/* int / num: erst klemmen, bei int dann abrunden. */
static int f_num(SkC *c, const char *name, double mn, double mx, double def, int is_int, double *out) {
    uint32_t v = j_get(&c->j, c->root, name);
    double d;
    if (j_isnull(&c->j, v)) { *out = def; return 0; }
    if (!v_is_num(c, v)) return c_fail(c, name, "Zahl erwartet");
    d = sk_min(sk_max(c->j.v[v].u.n, mn), mx);
    *out = is_int ? sk_floor(d) : d;
    return 0;
}

static int f_bool(SkC *c, const char *name, int def, int *out) {
    uint32_t v = j_get(&c->j, c->root, name);
    if (j_isnull(&c->j, v)) { *out = def; return 0; }
    if (c->j.v[v].t != SK_BOOL) return c_fail(c, name, "true/false erwartet");
    *out = c->j.v[v].u.n != 0;
    return 0;
}

/* enum: choices mit '/' getrennt; Ergebnis ist der Index. */
static int f_enum(SkC *c, const char *name, const char *choices, int def, int *out) {
    uint32_t v = j_get(&c->j, c->root, name);
    const char *p = choices;
    int idx = 0;
    char msg[160] = "erlaubt: ";
    size_t ml = strlen(msg);
    if (j_isnull(&c->j, v)) { *out = def; return 0; }
    while (*p) {
        const char *e = strchr(p, '/');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (c->j.v[v].t == SK_STR && c->j.v[v].u.s.len == l && memcmp(c->j.str + c->j.v[v].u.s.off, p, l) == 0) {
            *out = idx;
            return 0;
        }
        if (ml + l + 3 < sizeof msg) {
            if (idx) { msg[ml++] = ','; msg[ml++] = ' '; }
            memcpy(msg + ml, p, l); ml += l; msg[ml] = 0;
        }
        idx++;
        p += l + (e ? 1 : 0);
    }
    return c_fail(c, name, msg);
}

/* str: clean(s, len); out braucht len*4+1 Bytes. */
static int f_str(SkC *c, const char *name, size_t len, const char *def, char *out) {
    uint32_t v = j_get(&c->j, c->root, name);
    if (j_isnull(&c->j, v)) { strcpy(out, def); return 0; }
    if (c->j.v[v].t != SK_STR) return c_fail(c, name, "Text erwartet");
    sk_clean(c->j.str + c->j.v[v].u.s.off, c->j.v[v].u.s.len, len, out);
    return 0;
}

static const char *const SK_TOK[14] = {"bg", "card", "elevated", "text", "muted", "accent", "green",
                                       "yellow", "red", "blue", "pink", "amber", "cyan", "lila"};

static int sk_tokidx(const char *t) {
    int i;
    for (i = 0; i < 14; i++) if (strcmp(t, SK_TOK[i]) == 0) return i;
    return -1;
}

static int f_color(SkC *c, const char *name, const char *def, int allow_empty, char *out /* 16 */) {
    uint32_t v = j_get(&c->j, c->root, name);
    const char *s;
    size_t l, i;
    if (j_isnull(&c->j, v)) { strcpy(out, def); return 0; }
    if (c->j.v[v].t != SK_STR) return c_fail(c, name, "Token oder #rgb/#rrggbb");
    s = c->j.str + c->j.v[v].u.s.off;
    l = c->j.v[v].u.s.len;
    if (l == 0 && allow_empty) { out[0] = 0; return 0; }
    for (i = 0; i < 14; i++)
        if (strlen(SK_TOK[i]) == l && memcmp(SK_TOK[i], s, l) == 0) { strcpy(out, SK_TOK[i]); return 0; }
    if ((l == 4 || l == 7) && s[0] == '#') {
        char h[8];
        size_t k;
        for (k = 1; k < l; k++) {
            char ch = s[k];
            if (ch >= 'A' && ch <= 'F') ch = (char)(ch - 'A' + 'a');
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return c_fail(c, name, "Token oder #rgb/#rrggbb");
            h[k] = ch;
        }
        out[0] = '#';
        if (l == 4) { out[1] = h[1]; out[2] = h[1]; out[3] = h[2]; out[4] = h[2]; out[5] = h[3]; out[6] = h[3]; }
        else for (k = 1; k < 7; k++) out[k] = h[k];
        out[7] = 0;
        return 0;
    }
    return c_fail(c, name, "Token oder #rgb/#rrggbb");
}

/* nums: Liste, die ersten len Elemente, jedes endlich und auf [mn,mx] geklemmt (nicht gerundet). */
static int f_nums(SkC *c, const char *name, size_t len, double mn, double mx, const double *def, int ndef,
                  double *out, int *nout) {
    uint32_t v = j_get(&c->j, c->root, name), e;
    size_t k = 0;
    if (j_isnull(&c->j, v)) {
        int i;
        for (i = 0; i < ndef; i++) out[i] = def[i];
        *nout = ndef;
        return 0;
    }
    if (c->j.v[v].t != SK_ARR) return c_fail(c, name, "Liste erwartet");
    for (e = c->j.v[v].first; e && k < len; e = c->j.v[e].next, k++) {
        if (!v_is_num(c, e)) return c_fail(c, name, "Zahl erwartet");
        out[k] = sk_min(sk_max(c->j.v[e].u.n, mn), mx);
    }
    *nout = (int)k;
    return 0;
}

static int f_strs(SkC *c, const char *name, size_t len, char (*out)[SK_LABB], int *nout) {
    uint32_t v = j_get(&c->j, c->root, name), e;
    size_t k = 0;
    if (j_isnull(&c->j, v)) { *nout = 0; return 0; }
    if (c->j.v[v].t != SK_ARR) return c_fail(c, name, "Liste erwartet");
    for (e = c->j.v[v].first; e && k < len; e = c->j.v[e].next, k++) {
        if (c->j.v[e].t != SK_STR) return c_fail(c, name, "Text erwartet");
        sk_clean(c->j.str + c->j.v[e].u.s.off, c->j.v[e].u.s.len, 40, out[k]);
    }
    *nout = (int)k;
    return 0;
}

#define TRY(x) do { int rc_ = (x); if (rc_) return rc_; } while (0)

static const char *const SK_TITLES[6] = {"Balkendiagramm", "Ringdiagramm", "Muster", "Badge", "Symbol", "Verlauf"};

static const struct { const char *name, *d; } SK_ICONS[14] = {
    {"check", "M5 12.5L9.5 17L19 7.5"},
    {"cross", "M6 6L18 18M18 6L6 18"},
    {"play", "M8 5V19L19 12Z"},
    {"pause", "M8 5V19M16 5V19"},
    {"arrow-right", "M4 12H20M14 6L20 12L14 18"},
    {"arrow-left", "M20 12H4M10 6L4 12L10 18"},
    {"star", "M12 3L14.4 9.3L21 9.6L15.8 13.7L17.6 20.2L12 16.5L6.4 20.2L8.2 13.7L3 9.6L9.6 9.3Z"},
    {"heart", "M12 20C12 20 4 15.5 4 9.5A4 4 0 0 1 12 7.5A4 4 0 0 1 20 9.5C20 15.5 12 20 12 20Z"},
    {"menu", "M4 6H20M4 12H20M4 18H20"},
    {"plus", "M12 5V19M5 12H19"},
    {"minus", "M5 12H19"},
    {"info", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM12 11V17M12 7.5V8"},
    {"search", "M4 10.5A6.5 6.5 0 1 0 17 10.5A6.5 6.5 0 1 0 4 10.5ZM15.5 15.5L20 20"},
    {"download", "M12 4V15M7 10L12 15L17 10M5 20H19"},
};
#define SK_ICON_CHOICES "check/cross/play/pause/arrow-right/arrow-left/star/heart/menu/plus/minus/info/search/download"

/* Prüft die Spezifikation und füllt s. Rückgabe: SVG_OK, SVG_E_SPEC oder SVG_E_SUBSET. */
static int sk_normalize(SkC *c) {
    SkSpec *s = c->s;
    SkJ *j = &c->j;
    uint32_t t;
    int tyi = -1, i;
    static const char *const names[9] = {"bar", "line", "donut", "pattern", "badge", "icon", "monogram", "gradient", "graph"};
    static const double def_bar[6] = {12, 19, 7, 15, 22, 10}, def_donut[3] = {45, 30, 25};
    int b;

    if (j->v[c->root].t != SK_OBJ) return c_fail(c, "spec", "Objekt erwartet");
    t = j_get(j, c->root, "type");
    if (t && j->v[t].t == SK_STR) {
        for (i = 0; i < 9; i++)
            if (strlen(names[i]) == j->v[t].u.s.len && memcmp(names[i], j->str + j->v[t].u.s.off, j->v[t].u.s.len) == 0) tyi = i;
    }
    if (tyi < 0) return c_fail(c, "type", "unbekannter Typ");
    if (tyi == 1 || tyi == 6 || tyi == 8) return c_fail(c, "type", "Typ nicht in der C-Teilmenge") | 0x100;
    s->type = tyi == 0 ? TY_BAR : tyi == 2 ? TY_DONUT : tyi == 3 ? TY_PATTERN : tyi == 4 ? TY_BADGE : tyi == 5 ? TY_ICON : TY_GRADIENT;

    /* gemeinsame Felder */
    TRY(f_enum(c, "theme", "master/light/contrast/control/content", TH_MASTER, &s->theme));
    TRY(f_enum(c, "colors", "vars/static", 0, &b));
    s->vars = b == 0;
    TRY(f_str(c, "idprefix", 32, "sk", s->idprefix));
    TRY(f_num(c, "seed", 0, 4294967295.0, 1, 1, &s->seed));
    TRY(f_str(c, "title", 120, SK_TITLES[s->type], s->title));

    switch (s->type) {
        case TY_BAR:
            TRY(f_num(c, "width", 160, 2400, 480, 1, &s->width));
            TRY(f_num(c, "height", 120, 2400, 280, 1, &s->height));
            TRY(f_nums(c, "data", 64, -1e9, 1e9, def_bar, 6, s->data, &s->n));
            TRY(f_strs(c, "labels", 64, s->labels, &s->nlab));
            TRY(f_color(c, "color", "accent", 0, s->color));
            TRY(f_bool(c, "values", 1, &s->values));
            TRY(f_bool(c, "grid", 1, &s->grid));
            TRY(f_bool(c, "heading", 1, &s->heading));
            break;
        case TY_DONUT:
            TRY(f_num(c, "width", 160, 2400, 360, 1, &s->width));
            TRY(f_num(c, "height", 120, 2400, 240, 1, &s->height));
            TRY(f_nums(c, "data", 12, 0, 1e9, def_donut, 3, s->data, &s->n));
            TRY(f_strs(c, "labels", 12, s->labels, &s->nlab));
            TRY(f_num(c, "thickness", 0.1, 0.9, 0.34, 0, &s->thickness));
            TRY(f_str(c, "center", 16, "", s->center));
            TRY(f_bool(c, "legend", 1, &s->legend));
            TRY(f_bool(c, "heading", 1, &s->heading));
            break;
        case TY_PATTERN:
            TRY(f_num(c, "width", 16, 2400, 240, 1, &s->width));
            TRY(f_num(c, "height", 16, 2400, 240, 1, &s->height));
            TRY(f_enum(c, "kind", "grid/dots/stripes/waves/confetti", KD_GRID, &s->kind));
            TRY(f_num(c, "size", 4, 200, 24, 1, &s->size));
            TRY(f_num(c, "stroke", 0.5, 12, 1.5, 0, &s->stroke));
            TRY(f_num(c, "angle", -90, 90, 45, 1, &s->angle));
            TRY(f_num(c, "density", 1, 400, 60, 1, &s->density));
            TRY(f_num(c, "opacity", 0.05, 1, 0.6, 0, &s->opacity));
            TRY(f_color(c, "color", "accent", 0, s->color));
            TRY(f_bool(c, "bg", 1, &s->bg));
            break;
        case TY_BADGE:
            TRY(f_num(c, "height", 16, 64, 24, 1, &s->height));
            TRY(f_str(c, "label", 32, "DkZ", s->label));
            TRY(f_str(c, "value", 32, "aktiv", s->value));
            TRY(f_color(c, "color", "accent", 0, s->color));
            break;
        case TY_ICON:
            TRY(f_enum(c, "name", SK_ICON_CHOICES, 0, &s->icon));
            TRY(f_num(c, "size", 12, 512, 48, 1, &s->size));
            TRY(f_color(c, "color", "accent", 0, s->color));
            TRY(f_num(c, "stroke", 1, 4, 2, 0, &s->stroke));
            TRY(f_bool(c, "bg", 0, &s->bg));
            break;
        default: /* TY_GRADIENT */
            TRY(f_num(c, "width", 16, 2400, 480, 1, &s->width));
            TRY(f_num(c, "height", 16, 2400, 240, 1, &s->height));
            TRY(f_enum(c, "kind", "linear/radial", 0, &s->gkind));
            TRY(f_color(c, "from", "accent", 0, s->from));
            TRY(f_color(c, "to", "lila", 0, s->to));
            TRY(f_color(c, "via", "", 1, s->via));
            TRY(f_num(c, "angle", 0, 360, 45, 1, &s->angle));
            TRY(f_num(c, "cx", 0, 100, 50, 0, &s->cx));
            TRY(f_num(c, "cy", 0, 100, 50, 0, &s->cy));
            TRY(f_str(c, "text", 60, "", s->text));
            break;
    }

    /* idprefix: ^[A-Za-z][A-Za-z0-9_-]*$ */
    {
        const char *p = s->idprefix;
        int okp = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z');
        for (p++; okp && *p; p++)
            if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) okp = 0;
        if (!okp) return c_fail(c, "idprefix", "[A-Za-z][A-Za-z0-9_-]*");
    }
    if (!s->title[0]) strcpy(s->title, SK_TITLES[s->type]);

    if (s->type == TY_PATTERN && (s->kind == KD_WAVES || s->kind == KD_CONFETTI))
        return c_fail(c, "kind", "Typ nicht in der C-Teilmenge (pattern: nur grid, dots, stripes)") | 0x100;
    return SVG_OK;
}

/* ═════════════════════════ 5. Ausgabe der Typen ═════════════════════════ */

typedef struct {
    const char *hex[14];
    const char *series[8];
    int nseries;
    double radius, lineop;
    int line_accent;
    const char *font;
} SkTheme;

#define SK_FONT "Inter,system-ui,-apple-system,sans-serif"
#define SK_HEX_BASE "#060608", "#0e0e10", "#141418", "#f6f6f7", "#8e8e8f", "#fa1e4e", "#00ff88", "#ffb800", \
                    "#ff3b5c", "#55acee", "#ec4899", "#f5a623", "#3fe0d0", "#9d7bff"

static const SkTheme SK_THEMES[5] = {
    /* master */ {{SK_HEX_BASE}, {"accent", "cyan", "lila", "amber", "green", "blue", "pink", "yellow"}, 8, 12, .08, 0, SK_FONT},
    /* light */ {{"#f6f6f7", "#ffffff", "#f0f0f2", "#0e0e10", "#6a6a6b", "#fa1e4e", "#00ff88", "#ffb800", "#ff3b5c",
                  "#55acee", "#ec4899", "#f5a623", "#3fe0d0", "#9d7bff"},
              {"accent", "blue", "lila", "pink", "amber", "cyan", "green", "yellow"}, 8, 12, .08, 0, SK_FONT},
    /* contrast */ {{SK_HEX_BASE}, {"accent"}, 1, 12, 1, 0, SK_FONT},
    /* control */ {{SK_HEX_BASE}, {"accent", "cyan", "lila", "amber", "green", "blue", "pink", "yellow"}, 8, 0, .3, 1,
                   "Archivo Expanded," SK_FONT},
    /* content */ {{SK_HEX_BASE}, {"accent", "lila", "cyan", "pink", "amber", "green", "blue", "yellow"}, 8, 20, .08, 0,
                   "Syne," SK_FONT},
};

typedef struct {
    SkW w;
    const SkTheme *T;
    const SkSpec *s;
    int V, K;
} SkR;

/* hexOf(t): Literal-Hex bleibt, im Theme contrast nur #000000 / #00ff88. */
static const char *sk_hexof(const SkR *R, const char *t) {
    int i = t[0] == '#' ? -1 : sk_tokidx(t);
    if (R->K) return (i >= 0 && i < 3) ? "#000000" : "#00ff88";
    return t[0] == '#' ? t : R->T->hex[i < 0 ? 0 : i];
}

/* C(t): var(--NAME, HEX) im Modus vars, sonst Hex. out ≥ 64 Bytes. */
static void sk_C(const SkR *R, const char *t, char *out) {
    if (R->V && t[0] != '#') {
        const char *nm = strcmp(t, "card") == 0 ? "bg-card" : strcmp(t, "elevated") == 0 ? "bg-elevated" :
                         strcmp(t, "muted") == 0 ? "text-muted" : t;
        strcpy(out, "var(--");
        strcat(out, nm);
        strcat(out, ", ");
        strcat(out, sk_hexof(R, t));
        strcat(out, ")");
    } else strcpy(out, sk_hexof(R, t));
}

/* ink(t): lesbare Schrift auf Farbfläche t (immer als Hex). */
static const char *sk_ink(const SkR *R, const char *t) {
    const char *h;
    unsigned v = 0, r, g, b;
    int i;
    if (R->K) return "#000000";
    h = sk_hexof(R, t);
    for (i = 1; i < 7; i++) {
        char ch = h[i];
        v = (v << 4) | (unsigned)(ch >= 'a' ? ch - 'a' + 10 : ch - '0');
    }
    r = v >> 16; g = (v >> 8) & 255; b = v & 255;
    return r * 299 + g * 587 + b * 114 >= 128000 ? "#060608" : "#ffffff";
}

static void a_col(const SkR *R, SkW *o, const char *k, const char *tok) {
    char b[64];
    sk_C(R, tok, b);
    a_str(o, k, b);
}

/* LN: stroke=C(line) stroke-opacity=op(lineop) */
static void a_ln(SkR *R) {
    a_col(R, &R->w, "stroke", R->T->line_accent ? "accent" : "text");
    if (!R->K) a_num(&R->w, "stroke-opacity", R->T->lineop);
}

/* fr(w,h,rx): Rechteck 0.5/0.5 abzüglich 1, danach Füllung (a_fill) und optional LN. */
static void sk_text(SkR *R, double x, double y, double fs, const char *fill, const char *s, const char *anchor,
                    int weight, int outline) {
    SkW *o = &R->w;
    w_nl(o);
    w_s(o, "<text");
    a_num(o, "x", x); a_num(o, "y", y); a_num(o, "font-size", fs);
    if (weight) a_num(o, "font-weight", weight);
    if (anchor) a_str(o, "text-anchor", anchor);
    a_str(o, "fill", fill);
    if (outline) { a_col(R, o, "stroke", "bg"); a_num(o, "stroke-width", 3); a_str(o, "paint-order", "stroke"); }
    w_c(o, '>');
    w_esc(o, s, strlen(s));
    w_s(o, "</text>");
}

/* Text mit Farb-Token statt fertigem Fill. */
static void sk_text_t(SkR *R, double x, double y, double fs, const char *tok, const char *s, const char *anchor, int weight) {
    char c[64];
    sk_C(R, tok, c);
    sk_text(R, x, y, fs, c, s, anchor, weight, 0);
}

static void sk_card(SkR *R, double w, double h) {
    SkW *o = &R->w;
    double rad = R->T->radius;
    w_nl(o);
    w_s(o, "<rect");
    a_num(o, "x", .5); a_num(o, "y", .5); a_num(o, "width", w - 1); a_num(o, "height", h - 1); a_num(o, "rx", rad);
    a_col(R, o, "fill", "card");
    a_ln(R);
    w_s(o, "/>");
    if (rad == 0) { /* HUD-Ecken im Theme control */
        SkP p;
        p.w = o; p.prev = 0;
        w_nl(o);
        w_s(o, "<path d=\"");
        p_c(&p, 'M'); p_n(&p, 1); p_n(&p, 11); p_c(&p, 'V'); p_n(&p, 1); p_c(&p, 'H'); p_n(&p, 11);
        p_c(&p, 'M'); p_n(&p, w - 11); p_n(&p, 1); p_c(&p, 'H'); p_n(&p, w - 1); p_c(&p, 'V'); p_n(&p, 11);
        p_c(&p, 'M'); p_n(&p, w - 1); p_n(&p, h - 11); p_c(&p, 'V'); p_n(&p, h - 1); p_c(&p, 'H'); p_n(&p, w - 11);
        p_c(&p, 'M'); p_n(&p, 11); p_n(&p, h - 1); p_c(&p, 'H'); p_n(&p, 1); p_c(&p, 'V'); p_n(&p, h - 11);
        w_c(o, '"');
        a_str(o, "fill", "none");
        a_col(R, o, "stroke", "accent");
        a_num(o, "stroke-width", 2);
        w_s(o, "/>");
    }
}

static void sk_head(SkR *R, double w) {
    char t[640];
    if (!R->s->heading) return;
    sk_trunc(R->s->title, sk_fit(w - 32, 13), t);
    sk_text_t(R, SK_P, 29, 13, "text", t, NULL, 600);
}

static void sk_none(SkR *R, double x, double y, double fs) { sk_text_t(R, x, y, fs, "muted", SK_NA, "middle", 0); }

/* Achsen für bar (SPEC §7). Schreibt Raster und Beschriftung sofort. */
typedef struct { double x0, x1, y0, y1, lo, hi; } SkAx;

static int sk_axes(SkR *R, double w, double h, double lo, double hi, SkAx *a) {
    const SkSpec *s = R->s;
    double raw, mag = 1, r, st, y0, y1, x1, x0;
    int nT, i;
    char tl[32][40];
    size_t ml = 0;
    if (hi - lo < .01) { lo -= 1; hi += 1; }
    raw = (hi - lo) / 4;
    while (mag * 10 <= raw) mag *= 10;
    while (mag > raw) mag /= 10;
    r = raw / mag;
    st = mag * (r <= 1 ? 1 : r <= 2 ? 2 : r <= 5 ? 5 : 10);
    lo = sk_floor(lo / st) * st;
    hi = sk_ceil(hi / st) * st;
    nT = (int)sk_floor((hi - lo) / st + .5);
    if (nT < 0 || nT > 31) return -1; /* mathematisch höchstens 6 */
    y0 = SK_P + (s->heading ? 26 : 0) + 8;
    y1 = h - SK_P - (s->nlab ? 20 : 0);
    x1 = w - SK_P;
    for (i = 0; i <= nT; i++) {
        size_t l;
        sk_short(lo + i * st, tl[i]);
        l = strlen(tl[i]);
        if (l > ml) ml = l;
    }
    x0 = SK_P + (s->grid ? (double)ml * 6.6 + 8 : 0);
    if (s->grid) {
        for (i = 0; i <= nT; i++) {
            double y = y0 + (hi - (lo + i * st)) * (y1 - y0) / (hi - lo);
            SkW *o = &R->w;
            w_nl(o);
            w_s(o, "<line");
            a_num(o, "x1", x0); a_num(o, "y1", y); a_num(o, "x2", x1); a_num(o, "y2", y);
            a_ln(R);
            if (R->K) a_str(o, "stroke-dasharray", "2 4");
            w_s(o, "/>");
            sk_text_t(R, x0 - 8, y + 4, 11, "muted", tl[i], "end", 0);
        }
    }
    a->x0 = x0; a->x1 = x1; a->y0 = y0; a->y1 = y1; a->lo = lo; a->hi = hi;
    return 0;
}

static double sk_Y(const SkAx *a, double v) { return a->y0 + (a->hi - v) * (a->y1 - a->y0) / (a->hi - a->lo); }

static int sk_bar(SkR *R, double w, double h) {
    const SkSpec *s = R->s;
    SkW *o = &R->w;
    SkAx a;
    double lo = 0, hi = 0, sl, bw;
    char col[64];
    int i;
    sk_card(R, w, h);
    sk_head(R, w);
    if (!s->n) { sk_none(R, w / 2, h / 2 + 4, 13); return 0; }
    for (i = 0; i < s->n; i++) { lo = sk_min(lo, s->data[i]); hi = sk_max(hi, s->data[i]); }
    if (sk_axes(R, w, h, lo, hi, &a)) return -1;
    sl = (a.x1 - a.x0) / s->n;
    bw = sk_min(sl * .64, 64);
    sk_C(R, s->color, col);
    for (i = 0; i < s->n; i++) {
        double v = s->data[i], cx = a.x0 + i * sl + sl / 2, yv = sk_Y(&a, v), yz = sk_Y(&a, 0);
        char lb[40];
        sk_short(v, lb);
        w_nl(o);
        w_s(o, "<rect");
        a_num(o, "x", cx - bw / 2); a_num(o, "y", sk_min(yv, yz)); a_num(o, "width", bw);
        a_num(o, "height", yv - yz < 0 ? yz - yv : yv - yz);
        a_num(o, "rx", sk_min(R->T->radius / 3, bw / 2));
        a_str(o, "fill", col);
        w_s(o, "/>");
        if (s->values && (double)strlen(lb) * 6.6 <= sl)
            sk_text_t(R, cx, v < 0 ? sk_min(yv + 14, a.y1 - 4) : yv - 6, 11, "text", lb, "middle", 0);
        if (i < s->nlab) {
            char t[700];
            sk_trunc(s->labels[i], sk_fit(sl - 4, 11), t);
            sk_text_t(R, cx, h - SK_P - 4, 11, "muted", t, "middle", 0);
        }
    }
    return 0;
}

static const char *sk_dcol(const SkR *R, int i) { return R->K ? (i % 2 ? "bg" : "accent") : R->T->series[i % R->T->nseries]; }

static void sk_ring(SkP *p, double cx, double cy, double q) {
    p_c(p, 'M'); p_n(p, cx); p_n(p, cy - q);
    p_c(p, 'A'); p_n(p, q); p_n(p, q); p_n(p, 0); p_n(p, 1); p_n(p, 1); p_n(p, cx); p_n(p, cy + q);
    p_c(p, 'A'); p_n(p, q); p_n(p, q); p_n(p, 0); p_n(p, 1); p_n(p, 1); p_n(p, cx); p_n(p, cy - q);
    p_c(p, 'Z');
}

static int sk_donut(SkR *R, double w, double h) {
    const SkSpec *s = R->s;
    SkW *o = &R->w;
    double tot = 0, top, ah, aw, r, cx, cy, ri, cum = 0, fs, k, lx, ly;
    int i;
    SkP p;
    p.w = o; p.prev = 0;
    sk_card(R, w, h);
    sk_head(R, w);
    for (i = 0; i < s->n; i++) tot += s->data[i];
    top = SK_P + (s->heading ? 26 : 0);
    ah = h - top - SK_P;
    aw = s->legend ? (w - 2 * SK_P) * .5 : w - 2 * SK_P;
    r = sk_min(ah, aw) / 2;
    cx = s->legend ? SK_P + aw / 2 : w / 2;
    cy = top + ah / 2;
    ri = r * (1 - s->thickness);
    if (!(tot > 0)) {
        w_nl(o);
        w_s(o, "<path d=\"");
        sk_ring(&p, cx, cy, r);
        sk_ring(&p, cx, cy, ri);
        w_c(o, '"');
        a_col(R, o, "fill", "muted");
        if (!R->K) a_num(o, "fill-opacity", .25);
        a_str(o, "fill-rule", "evenodd");
        w_s(o, "/>");
        sk_none(R, cx, cy + 4, sk_min(13, ri / 4.5));
        return 0;
    }
    fs = sk_min(sk_max(sk_floor(ri * .45), 10), 36);
    k = s->legend ? sk_min((double)s->n, sk_floor(ah / 20)) : 0;
    lx = SK_P + aw + 16;
    ly = cy - k * 10;
    for (i = 0; i < s->n; i++) {
        double v = s->data[i], a0, a1;
        int L, full;
        if (v <= 0) continue;
        a0 = -SK_PI / 2 + 2 * SK_PI * cum / tot;
        a1 = -SK_PI / 2 + 2 * SK_PI * (cum + v) / tot;
        L = a1 - a0 > SK_PI;
        full = v >= tot;
        cum += v;
        w_nl(o);
        w_s(o, "<path d=\"");
        p.prev = 0;
        if (full) {
            sk_ring(&p, cx, cy, r);
            sk_ring(&p, cx, cy, ri);
        } else {
            p_c(&p, 'M'); p_n(&p, cx + r * sk_cos(a0)); p_n(&p, cy + r * sk_sin(a0));
            p_c(&p, 'A'); p_n(&p, r); p_n(&p, r); p_n(&p, 0); p_n(&p, L); p_n(&p, 1);
            w_c(o, ' '); p.prev = 0;
            p_n(&p, cx + r * sk_cos(a1)); p_n(&p, cy + r * sk_sin(a1));
            p_c(&p, 'L'); p_n(&p, cx + ri * sk_cos(a1)); p_n(&p, cy + ri * sk_sin(a1));
            p_c(&p, 'A'); p_n(&p, ri); p_n(&p, ri); p_n(&p, 0); p_n(&p, L); p_n(&p, 0);
            w_c(o, ' '); p.prev = 0;
            p_n(&p, cx + ri * sk_cos(a0)); p_n(&p, cy + ri * sk_sin(a0));
            p_c(&p, 'Z');
        }
        w_c(o, '"');
        a_col(R, o, "fill", sk_dcol(R, i));
        if (full) a_str(o, "fill-rule", "evenodd");
        a_col(R, o, "stroke", R->K ? "accent" : "card");
        a_num(o, "stroke-width", R->K ? 1 : 2);
        w_s(o, "/>");
    }
    {
        char t[700], sh[40];
        const char *src = s->center;
        if (!s->center[0]) { sk_short(tot, sh); src = sh; }
        sk_trunc(src, sk_fit(ri * 2 - 8, fs), t);
        sk_text_t(R, cx, cy + fs * .35, fs, "text", t, "middle", 700);
    }
    for (i = 0; i < k; i++) {
        double y = ly + i * 20;
        char t[700], nm[24], pc[48];
        size_t n;
        w_nl(o);
        w_s(o, "<rect");
        a_num(o, "x", lx); a_num(o, "y", y + 5); a_num(o, "width", 10); a_num(o, "height", 10); a_num(o, "rx", 2);
        a_col(R, o, "fill", sk_dcol(R, i));
        if (R->K) a_col(R, o, "stroke", "accent");
        w_s(o, "/>");
        if (i < s->nlab) sk_trunc(s->labels[i], sk_fit(w - SK_P - lx - 68, 12), t);
        else {
            char num[24];
            size_t l = sk_fmt((double)(i + 1), num);
            memcpy(nm, "Teil ", 5);
            memcpy(nm + 5, num, l + 1);
            sk_trunc(nm, sk_fit(w - SK_P - lx - 68, 12), t);
        }
        sk_text_t(R, lx + 16, y + 14, 12, "text", t, NULL, 0);
        n = sk_fmt(sk_floor(s->data[i] * 1000 / tot + .5) / 10, pc);
        pc[n] = '%'; pc[n + 1] = 0;
        sk_text_t(R, w - SK_P, y + 14, 12, "muted", pc, "end", 0);
    }
    return 0;
}

static int sk_pattern(SkR *R, double w, double h) {
    const SkSpec *s = R->s;
    SkW *o = &R->w;
    double z = s->size, m = z / 2;
    char c[64], id[160];
    SkP p;
    p.w = o; p.prev = 0;
    sk_C(R, s->color, c);
    if (s->bg) sk_card(R, w, h);
    w_nl(o);
    w_s(o, "<defs><pattern");
    strcpy(id, s->idprefix); strcat(id, "-pat");
    a_str(o, "id", id);
    a_num(o, "width", z); a_num(o, "height", z);
    a_str(o, "patternUnits", "userSpaceOnUse");
    if (s->kind == KD_STRIPES) {
        w_s(o, " patternTransform=\"rotate(");
        w_num(o, s->angle);
        w_s(o, ")\"");
    }
    w_c(o, '>');
    if (s->kind == KD_GRID) {
        w_s(o, "<path d=\"");
        p_c(&p, 'M'); p_n(&p, z); p_n(&p, 0); p_c(&p, 'L'); p_n(&p, 0); p_n(&p, 0); p_c(&p, 'L'); p_n(&p, 0); p_n(&p, z);
        w_c(o, '"');
        a_str(o, "fill", "none"); a_str(o, "stroke", c); a_num(o, "stroke-width", s->stroke);
        w_s(o, "/>");
    } else if (s->kind == KD_DOTS) {
        w_s(o, "<circle");
        a_num(o, "cx", m); a_num(o, "cy", m); a_num(o, "r", s->stroke); a_str(o, "fill", c);
        w_s(o, "/>");
    } else {
        w_s(o, "<rect");
        a_num(o, "width", s->stroke); a_num(o, "height", z); a_str(o, "fill", c);
        w_s(o, "/>");
    }
    w_s(o, "</pattern></defs>");
    w_nl(o);
    w_s(o, "<rect");
    a_num(o, "width", w); a_num(o, "height", h); a_num(o, "rx", R->T->radius);
    strcpy(id, "url(#"); strcat(id, s->idprefix); strcat(id, "-pat)");
    a_str(o, "fill", id);
    if (!R->K) a_num(o, "opacity", s->opacity);
    w_s(o, "/>");
    return 0;
}

static int sk_badge(SkR *R) {
    const SkSpec *s = R->s;
    SkW *o = &R->w;
    double h = s->height, fs = h * .46, cw = fs * .6, nl = (double)sk_cps(s->label);
    double lw = nl > 0 ? nl * cw + h * .8 : 0, vw = (double)sk_cps(s->value) * cw + h * .8, w = lw + vw;
    double r = R->T->radius == 20 ? h / 2 : sk_min(R->T->radius, h) / 2, y = h / 2 + fs * .35;
    char c[64], fillc[64];
    sk_C(R, s->color, c);
    sk_C(R, "elevated", fillc);
    w_nl(o);
    w_s(o, "<rect");
    a_num(o, "x", .5); a_num(o, "y", .5); a_num(o, "width", w - 1); a_num(o, "height", h - 1); a_num(o, "rx", r);
    a_str(o, "fill", fillc);
    a_ln(R);
    w_s(o, "/>");
    w_nl(o);
    w_s(o, "<rect");
    a_num(o, "x", lw); a_num(o, "width", vw); a_num(o, "height", h); a_num(o, "rx", r); a_str(o, "fill", c);
    w_s(o, "/>");
    if (lw != 0 && r != 0) {
        w_nl(o);
        w_s(o, "<rect");
        a_num(o, "x", lw); a_num(o, "width", sk_min(r, vw)); a_num(o, "height", h); a_str(o, "fill", c);
        w_s(o, "/>");
    }
    if (nl > 0) sk_text_t(R, lw / 2, y, fs, "text", s->label, "middle", 500);
    sk_text(R, lw + vw / 2, y, fs, sk_ink(R, s->color), s->value, "middle", 700, 0);
    return 0;
}

static int sk_icon(SkR *R) {
    const SkSpec *s = R->s;
    SkW *o = &R->w;
    char c[64], fc[64];
    if (s->bg) {
        sk_C(R, "card", fc);
        w_nl(o);
        w_s(o, "<rect");
        a_num(o, "x", -4); a_num(o, "y", -4); a_num(o, "width", 32); a_num(o, "height", 32);
        a_num(o, "rx", R->T->radius / 2);
        a_str(o, "fill", fc);
        a_ln(R);
        a_num(o, "stroke-width", .5);
        w_s(o, "/>");
    }
    sk_C(R, s->color, c);
    w_nl(o);
    w_s(o, "<path");
    a_str(o, "d", SK_ICONS[s->icon].d);
    a_str(o, "fill", "none"); a_str(o, "stroke", c); a_num(o, "stroke-width", s->stroke);
    a_str(o, "stroke-linejoin", "round"); a_str(o, "stroke-linecap", "round");
    w_s(o, "/>");
    return 0;
}

static void sk_stop(SkR *R, double offset, const char *tok) {
    SkW *o = &R->w;
    w_s(o, "<stop");
    a_num(o, "offset", offset);
    a_col(R, o, "stop-color", tok);
    w_s(o, "/>");
}

static void sk_pc(SkW *o, const char *k, double v) {
    char b[48];
    size_t n = sk_fmt(v, b);
    b[n] = '%'; b[n + 1] = 0;
    a_str(o, k, b);
}

static int sk_gradient(SkR *R, double w, double h) {
    const SkSpec *s = R->s;
    SkW *o = &R->w;
    double a = s->angle * SK_PI / 180, cs = sk_cos(a) * 50, sn = sk_sin(a) * 50;
    char id[160], t[700], fillv[200];
    strcpy(id, s->idprefix); strcat(id, "-grad");
    w_nl(o);
    w_s(o, "<defs>");
    if (s->gkind == 0) {
        w_s(o, "<linearGradient");
        a_str(o, "id", id);
        sk_pc(o, "x1", 50 - cs); sk_pc(o, "y1", 50 - sn); sk_pc(o, "x2", 50 + cs); sk_pc(o, "y2", 50 + sn);
    } else {
        w_s(o, "<radialGradient");
        a_str(o, "id", id);
        sk_pc(o, "cx", s->cx); sk_pc(o, "cy", s->cy);
        a_str(o, "r", "75%");
    }
    w_c(o, '>');
    if (R->K) { sk_stop(R, 0, "accent"); sk_stop(R, .5, "accent"); sk_stop(R, .5, "bg"); sk_stop(R, 1, "bg"); }
    else {
        sk_stop(R, 0, s->from);
        if (s->via[0]) sk_stop(R, .5, s->via);
        sk_stop(R, 1, s->to);
    }
    w_s(o, s->gkind == 0 ? "</linearGradient>" : "</radialGradient>");
    w_s(o, "</defs>");
    w_nl(o);
    strcpy(fillv, "url(#"); strcat(fillv, id); strcat(fillv, ")");
    w_s(o, "<rect");
    a_num(o, "x", .5); a_num(o, "y", .5); a_num(o, "width", w - 1); a_num(o, "height", h - 1); a_num(o, "rx", R->T->radius);
    a_str(o, "fill", fillv);
    if (R->K) a_col(R, o, "stroke", "accent");
    w_s(o, "/>");
    sk_trunc(s->text, sk_fit(w - 48, 12), t);
    if (t[0]) {
        double fs = sk_min(sk_min(sk_max(h * .16, 12), 72), (w - 48) / ((double)sk_cps(t) * .6));
        char fc[64];
        if (R->K) sk_C(R, "accent", fc); else strcpy(fc, sk_ink(R, s->from));
        sk_text(R, w / 2, h / 2 + fs * .35, fs, fc, t, "middle", 700, R->K);
    }
    return 0;
}

/* Gesamtausgabe: Kopf, Elemente, Fuß. */
static int sk_render(const SkSpec *s, char *out, size_t cap, size_t *out_len) {
    SkR R;
    double W = s->width, H = s->height;
    char title[2400], font[160];
    int rc = 0;
    SkP p;
    R.w.b = out; R.w.cap = cap; R.w.len = 0; R.w.over = 0;
    R.T = &SK_THEMES[s->theme]; R.s = s;
    R.V = s->vars; R.K = s->theme == TH_CONTRAST;

    if (s->type == TY_BADGE) {
        double h = s->height, fs = h * .46, cw = fs * .6, nl = (double)sk_cps(s->label);
        double lw = nl > 0 ? nl * cw + h * .8 : 0, vw = (double)sk_cps(s->value) * cw + h * .8;
        W = lw + vw; H = h;
    } else if (s->type == TY_ICON) { W = s->size; H = s->size; }

    {
        SkW tw;
        tw.b = title; tw.cap = sizeof title; tw.len = 0; tw.over = 0;
        w_esc(&tw, s->title, strlen(s->title));
        if (tw.over) return SVG_E_BUF;
        title[tw.len] = 0;
    }
    if (R.V) { strcpy(font, "var(--font-main, "); strcat(font, R.T->font); strcat(font, ")"); }
    else strcpy(font, R.T->font);

    w_s(&R.w, "<svg xmlns=\"http://www.w3.org/2000/svg\"");
    a_num(&R.w, "width", W); a_num(&R.w, "height", H);
    w_s(&R.w, " viewBox=\"");
    if (s->type == TY_ICON) w_s(&R.w, s->bg ? "-4 -4 32 32" : "0 0 24 24");
    else { p.w = &R.w; p.prev = 0; p_n(&p, 0); p_n(&p, 0); p_n(&p, W); p_n(&p, H); }
    w_s(&R.w, "\" role=\"img\" aria-label=\"");
    w_s(&R.w, title);
    w_s(&R.w, "\"");
    a_str(&R.w, "font-family", font);
    w_s(&R.w, ">\n<title>");
    w_s(&R.w, title);
    w_s(&R.w, "</title>");

    switch (s->type) {
        case TY_BAR: rc = sk_bar(&R, W, H); break;
        case TY_DONUT: rc = sk_donut(&R, W, H); break;
        case TY_PATTERN: rc = sk_pattern(&R, W, H); break;
        case TY_BADGE: rc = sk_badge(&R); break;
        case TY_ICON: rc = sk_icon(&R); break;
        default: rc = sk_gradient(&R, W, H); break;
    }
    if (rc) return SVG_E_LIMIT;
    w_s(&R.w, "\n</svg>");
    if (R.w.over) return SVG_E_BUF;
    out[R.w.len] = 0;
    if (out_len) *out_len = R.w.len;
    return SVG_OK;
}

/* ═════════════════════════ 6. Einstieg ═════════════════════════ */

static void sk_set_err(char *err, size_t errcap, const char *field, const char *msg) {
    size_t i = 0, k;
    if (!err || !errcap) return;
    for (k = 0; field[k] && i + 1 < errcap; k++) err[i++] = field[k];
    if (i + 3 < errcap) { err[i++] = ':'; err[i++] = ' '; }
    for (k = 0; msg[k] && i + 1 < errcap; k++) err[i++] = msg[k];
    err[i] = 0;
}

int svg_generate_n(const char *json, size_t json_len, char *out, size_t cap, size_t *out_len, char *err, size_t errcap) {
    SkC *c;
    SkSpec *s;
    int rc;
    uint32_t root = 0;
    if (out_len) *out_len = 0;
    if (out && cap) out[0] = 0;
    if (err && errcap) err[0] = 0;
    if (!json || !out || cap < 2) { sk_set_err(err, errcap, "spec", "keine Eingabe oder Ausgabepuffer zu klein"); return SVG_E_BUF; }
    if (json_len > SVG_MAX_JSON) { sk_set_err(err, errcap, "spec", "Eingabe zu groß (höchstens 1 MiB)"); return SVG_E_LIMIT; }

    c = (SkC *)calloc(1, sizeof *c);
    s = (SkSpec *)calloc(1, sizeof *s);
    if (!c || !s) { free(c); free(s); sk_set_err(err, errcap, "spec", "Speicher erschöpft"); return SVG_E_LIMIT; }
    c->s = s;
    c->j.src = (const unsigned char *)json;
    c->j.n = json_len;
    c->j.maxv = (uint32_t)json_len + 2;
    c->j.nv = 1; /* Index 0 = „kein Knoten" */
    c->j.scap = json_len * 3 + 16;
    c->j.str = (char *)malloc(c->j.scap);
    if (!c->j.str) { free(c); free(s); sk_set_err(err, errcap, "spec", "Speicher erschöpft"); return SVG_E_LIMIT; }

    rc = SVG_OK;
    if (j_value(&c->j, 0, &root)) {
        rc = c->j.code;
        sk_set_err(err, errcap, "spec", c->j.msg);
    } else {
        j_ws(&c->j);
        if (c->j.pos != c->j.n) {
            rc = SVG_E_SPEC;
            sk_set_err(err, errcap, "spec", "Text nach dem JSON-Wert");
        } else {
            c->root = root;
            rc = sk_normalize(c);
            if (rc & 0x100) { /* außerhalb der Teilmenge */
                rc = SVG_E_SUBSET;
                sk_set_err(err, errcap, c->efield, c->emsg);
            } else if (rc) {
                sk_set_err(err, errcap, c->efield, c->emsg);
            } else {
                rc = sk_render(s, out, cap, out_len);
                if (rc == SVG_E_BUF) sk_set_err(err, errcap, "spec", "Ausgabepuffer zu klein");
                else if (rc) sk_set_err(err, errcap, "spec", "interner Fehler");
            }
        }
    }
    if (rc != SVG_OK) { out[0] = 0; if (out_len) *out_len = 0; }
    free(c->j.v);
    free(c->j.str);
    free(c);
    free(s);
    return rc;
}

int svg_generate(const char *json, char *out, size_t cap, char *err, size_t errcap) {
    return svg_generate_n(json, json ? strlen(json) : 0, out, cap, NULL, err, errcap);
}
