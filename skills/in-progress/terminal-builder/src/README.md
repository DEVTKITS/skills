# src/ — Modulare Struktur (Unity-Build)

`baukasten`/SERUM ist in Module aufgeteilt. `serum.c` bindet sie in
Abhängigkeits-Reihenfolge zu **einer** Übersetzungseinheit ein
(`#include "0N_*.c"`). Vorteil: jede Konzern-Ebene ist eine eigene,
editierbare Datei, aber der Build bleibt robust — keine extern/Linkage-
Fallen, alle `static` behalten Unit-Scope.

| Modul | Inhalt |
|---|---|
| `01_types.h` | Includes, Enums (Screens, Konnektoren, Themes), Makros, Typen, globaler App-Zustand |
| `02_i18n.c` | Sprachtabellen (DE/EN), Kachel-/Menü-Namen |
| `03_term.c` | Terminal-I/O (termios, readkey), Ausgabepuffer, Themes, Renderprimitive |
| `04_config.c` | Persistenz (config/profiles/skills/ssh …), Navigation, Texteingabe |
| `05_net.c` | TTS, Konnektoren, LLM-Adapter (SERUM: Ollama/Anthropic/OpenAI, Matrix, Bridge) |
| `06_domain.c` | Planerzeugung, ComfyUI-Workflow-Export, Nanobot, Vorlesen |
| `07_tools.c` | Fuzzy-Picker (fzf), Pager, Shell, navi-Cheats, KI-Chat, Skill-Import |
| `07b_gitviz.c` | Git-Repo-Visualizer (Picker, Stats, Balken, SERUM-Analyse, GitHub-API) |
| `08_views.c` | Rendering aller Screens, Matrix-Rain, Ersteinrichtung |
| `09_input.c` | Freestyle-Parser, Tastenrouting, `main()` |

## Warum so — Ausblick „Builder für moderne Terminals"

Die Modulgrenzen sind die Nahtstellen des künftigen Builders: eine
Terminal-App entsteht später durch **Auswahl/Austausch von Modulen**.
Setup-Toggles (Config `mod_*`) schalten Module zur Laufzeit im Home-Menü
frei/aus; ein Compile-Zeit-Schlankbau ließe einzelne `0N_*.c` aus
`serum.c` weg. Neue Fähigkeiten kommen als neue `0N_*.c` + Eintrag in
`serum.c` dazu, ohne die anderen Module zu berühren.

## Bauen

```sh
make            # cc -O2 -Wall -Wextra -o baukasten src/serum.c
./baukasten
```
