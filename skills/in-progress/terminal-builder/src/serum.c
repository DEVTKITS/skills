/*
 * serum.c — Aggregator (Unity-Build) für baukasten / SERUM
 * --------------------------------------------------------
 * baukasten ist modular in src/ aufgeteilt: jede Datei ist ein Modul,
 * hier in Abhängigkeits-Reihenfolge zu EINER Übersetzungseinheit
 * zusammengezogen. Das hält den Build robust (keine extern/Linkage-
 * Fallen), während jede Konzern-Ebene als eigene Datei editierbar
 * bleibt — die Grundlage für den späteren „Builder für moderne
 * Terminals": Module lassen sich ein-/ausschalten oder austauschen.
 *
 * Reihenfolge:
 *   01_types   gemeinsame Typen, Enums, Globals   (Header)
 *   02_i18n    Sprachtabellen + Home-Menü
 *   03_term    Terminal-I/O, Themes, Renderprimitive
 *   04_config  Persistenz, Navigation, Texteingabe
 *   05_net     TTS, Konnektoren, LLM-Adapter (SERUM)
 *   06_domain  Planerzeugung, Workflows, Nanobot, Vorlesen
 *   07_tools   Fuzzy/Pager/Shell/navi, KI-Chat
 *   08_views   Rendering, Matrix-Rain, Ersteinrichtung
 *   09_input   Freestyle, Tastenrouting, main()
 *
 * Kompilieren:  make   (baut src/serum.c -> baukasten)
 */
#define _GNU_SOURCE
#include "01_types.h"
#include "02_i18n.c"
#include "03_term.c"
#include "04_config.c"
#include "05_net.c"
#include "06_domain.c"
#include "06b_monitor.c"
#include "07_tools.c"
#include "08_views.c"
#include "09_input.c"
