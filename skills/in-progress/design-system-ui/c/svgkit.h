/*
 * svgkit.h — C99-Teilmenge des SVG-Generators "svgspec/1" (Regeln: svg/SPEC.md).
 *
 * Umfang: bar, donut, badge, icon, gradient, pattern (kind grid | dots | stripes).
 * Nicht enthalten: line, monogram, graph, pattern waves | confetti (siehe SPEC §12).
 *
 * Eigenschaften der Bibliothek:
 *   - reines C99, keine externe Bibliothek, auch nicht libm (sin/cos nach fdlibm,
 *     floor/ceil selbst gebaut) — "cc -std=c99 -pthread server.c" genügt,
 *   - wiederbetretbar (kein globaler Zustand), also in mehreren Threads nutzbar,
 *   - jede Schreiboperation ist begrenzt: nie ein Pufferüberlauf, auch nicht bei
 *     verstümmelter, riesiger oder tief verschachtelter Eingabe,
 *   - Ausgabe byte-gleich zu svgkit.js (geprüft mit svg/run_parity.sh c).
 */
#ifndef SVGKIT_H
#define SVGKIT_H

#include <stddef.h>

/* Rückgabecodes */
#define SVG_OK        0   /* Erfolg: SVG steht NUL-terminiert in out                     */
#define SVG_E_SPEC    1   /* Spezifikation ungültig (err = "<Feld>: <Meldung>")          */
#define SVG_E_SUBSET  2   /* gültig angefragt, aber nicht in der C-Teilmenge             */
#define SVG_E_BUF     3   /* Ausgabepuffer zu klein (out ist dann leer)                  */
#define SVG_E_LIMIT   4   /* Eingabe zu groß oder Speicher erschöpft                     */

/* Grenzen */
#define SVG_MAX_JSON  (1u << 20)   /* größte Eingabe: 1 MiB                              */
#define SVG_OUT_MAX   (256u * 1024u) /* empfohlene Puffergröße; größte Ausgabe ist weit kleiner */

/* Liste der unterstützten Typen als JSON-Array-Text (für die Ablehnungsantwort). */
#define SVG_SUPPORTED_JSON "[\"bar\",\"donut\",\"badge\",\"icon\",\"gradient\",\"pattern\"]"

/*
 * Erzeugt das SVG zur JSON-Spezifikation (UTF-8, NUL-terminiert).
 *   json, json_len  Eingabe (json_len: Bytes, ohne NUL)
 *   out, cap        Ausgabepuffer; bei Erfolg SVG + NUL, bei Fehler leer
 *   out_len         (optional, darf NULL sein) Länge des SVG ohne NUL
 *   err, errcap     (optional) bei Fehler "<Feld>: <Meldung>", immer NUL-terminiert
 * Rückgabe: SVG_OK, SVG_E_SPEC, SVG_E_SUBSET, SVG_E_BUF oder SVG_E_LIMIT.
 */
int svg_generate_n(const char *json, size_t json_len, char *out, size_t cap,
                   size_t *out_len, char *err, size_t errcap);

/* Wie svg_generate_n, mit NUL-terminierter Eingabe; Länge der Ausgabe = strlen(out). */
int svg_generate(const char *json, char *out, size_t cap, char *err, size_t errcap);

#endif /* SVGKIT_H */
