/*
 * svgkit_cli.c — Kommandozeile der C-Teilmenge (CONTRACT §3).
 *
 *   dsui-svg <spec.json>
 *
 * Erfolg: genau das SVG nach stdout, ohne abschließenden Zeilenumbruch, Exitcode 0.
 * Fehler: stdout leer, Exitcode 1, auf stderr eine Zeile "svgspec: <Feld>: <Meldung>".
 * Nicht lesbare Datei oder ungültiges JSON: Feld "spec". Typen außerhalb der
 * C-Teilmenge (line, monogram, graph, pattern waves|confetti): Exitcode 1, Feld "type"/"kind".
 */
#include "svgkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *field, const char *msg) {
    fprintf(stderr, "svgspec: %s: %s\n", field, msg);
    return 1;
}

int main(int argc, char **argv) {
    FILE *f;
    char *buf, *out;
    size_t cap = (size_t)SVG_MAX_JSON + 1, n = 0, got, olen = 0;
    char err[256];
    int rc;

    if (argc < 2) return fail("spec", "Aufruf: dsui-svg <spec.json>");
    f = fopen(argv[1], "rb");
    if (!f) return fail("spec", "Datei nicht lesbar");
    buf = (char *)malloc(cap + 1);
    out = (char *)malloc(SVG_OUT_MAX);
    if (!buf || !out) { fclose(f); free(buf); free(out); return fail("spec", "Speicher erschöpft"); }
    while (n < cap && (got = fread(buf + n, 1, cap - n, f)) > 0) n += got;
    if (ferror(f)) { fclose(f); free(buf); free(out); return fail("spec", "Datei nicht lesbar"); }
    fclose(f);
    if (n > SVG_MAX_JSON) { free(buf); free(out); return fail("spec", "Datei zu groß (höchstens 1 MiB)"); }

    rc = svg_generate_n(buf, n, out, SVG_OUT_MAX, &olen, err, sizeof err);
    free(buf);
    if (rc != SVG_OK) {
        fprintf(stderr, "svgspec: %s\n", err[0] ? err : "spec: unbekannter Fehler");
        free(out);
        return 1;
    }
    if (fwrite(out, 1, olen, stdout) != olen || fflush(stdout) != 0) { free(out); return fail("spec", "Schreibfehler"); }
    free(out);
    return 0;
}
