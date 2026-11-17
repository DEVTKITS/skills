# svgspec/1 — Spezifikation des SVG-Generators

Dieses Dokument beschreibt den Generator so genau, dass eine Fassung in Python, Rust oder C **ohne den
JS-Quelltext zu lesen** dieselben Bytes erzeugt. Die Referenz ist `svgkit.js`; die erwarteten Ausgaben
liegen in `svg/golden/*.svg` (Eingaben: `svg/fixtures/*.json`). Wo dieses Dokument und die Golden-Dateien
sich widersprechen, gelten die Golden-Dateien — und das Dokument ist zu korrigieren.

Alle Rechnungen sind **IEEE-754-Doppelte (f64)**. Die Reihenfolge der Operationen ist Teil der Spezifikation
(Gleitkomma ist nicht assoziativ): `a+b*c/d` heißt `a + ((b*c)/d)`, Mal und Geteilt von links nach rechts.
Schreibweisen: `F` = Abrunden (Richtung −∞), `mn`/`mx` = Minimum/Maximum, `ab` = Betrag, `len(s)` = Anzahl der
**Unicode-Codepunkte** (nicht Bytes, nicht UTF-16-Einheiten), `P` = 16.

Kennzeichnung: **Annahme** = eine Festlegung dieses Pakets, die das DkZ-Designsystem (CONTRACT §1) nicht
vorgibt. Alles andere ist aus dem DkZ-System übernommen oder rein technisch.

---

## 1. Überblick und Schnittstellen

| Fassung | Aufruf | Rückgabe |
|---|---|---|
| JS (Referenz) | `node svgkit.js <spec.json>` · Browser `window.SvgKit` · Node `require('./svgkit.js')` | SVG nach stdout |
| Python | `.venv/bin/python svgkit.py <spec.json>` | wie JS |
| Rust | `rust/target/release/dsui-svg <spec.json>` | wie JS |
| C | `c/dsui-svg <spec.json>` (Teilmenge, §12) | wie JS |

**CLI-Vertrag aller Fassungen:** Die Datei ist UTF-8-JSON. Bei Erfolg wird **exakt** das SVG nach stdout
geschrieben, ohne abschließenden Zeilenumbruch, Exitcode 0. Bei Fehler: stdout leer, Exitcode 1, auf stderr
**eine Zeile** der Form `svgspec: <Feld>: <Meldung>` (`<Feld>` = Fehlerfeld aus §4; `<Meldung>` frei, deutsch
empfohlen). Nicht lesbare Datei oder ungültiges JSON → Feld `spec`. Unbekannte Schlüssel in der Eingabe
(auch `_impl`, `_note`, `_error_field`) werden **ignoriert**.

**JS-API:** `SvgKit.generate(spec)` → `{ok:true, svg, spec:<normalisiert>}` oder `{ok:false, error, field}`;
`SvgKit.schema` (je Typ `{label, felder:[{name,type,min,max,step,default,choices,label,unit,len}]}`);
`SvgKit.themes`; `SvgKit.tokens(theme)` (Hex-Palette); `SvgKit.sanitize(svg)` → `{svg, removed:[…]}` (§11);
`SvgKit.fmt(n)`; `SvgKit.version === "svgspec/1"`. `svg/types.json` entsteht aus Schema/Themes/Tokens mit
`node svg/make_types.mjs` und ist die einzige Quelle für `GET /api/svg/types`.

**Parität:** `svg/run_parity.sh <js|py|rs|c>` fährt jede Fixture, deren `_impl` die Fassung nennt, und vergleicht
mit `cmp` byte-genau; danach die Dateien in `svg/fixtures-invalid/` (erwartet: Exitcode 1 und
`svgspec: <Feld>:` am Zeilenanfang von stderr, wobei `<Feld>` aus `"_error_field"` bzw. aus der Datei
`<name>.expect` stammt, falls die Eingabe kein JSON-Objekt ist).

---

## 2. Zahlenformat `fmt(n)`

Die **einzige** Regel, nach der Zahlen in die Ausgabe gelangen (CONTRACT §3). Kein `toFixed`, kein `%g`, kein `repr`.

```
fmt(n):
  wenn n nicht endlich (NaN, ±Inf): "0"
  k = F( ab(n) * 100 + 0.5 )          # f64-Rechnung: erst *100, dann +0.5, dann abrunden
  wenn k == 0: "0"                    # auch für -0 und für -0.004
  s  = (n < 0 ? "-" : "") + dezimal(k div 100)
  f  = k mod 100
  wenn f != 0:
      f < 10          -> s += ".0" + f          # 5   -> ".05"
      f mod 10 != 0   -> s += "." + f           # 25  -> ".25"
      sonst           -> s += "." + (f div 10)  # 50  -> ".5"
  s
```

Grenzfälle (aus der Referenz; die Doppelte-Arithmetik entscheidet, nicht die Dezimalschreibweise):

| n | fmt(n) | Anmerkung |
|---|---|---|
| 0.005 | `0.01` | 0.005·100 = 0.5 exakt, +0.5 = 1 |
| 1.005 | `1` | 1.005·100 = 100.49999999999999 |
| −0.005 | `-0.01` | Betrag gerundet, Vorzeichen von n |
| 2.675 | `2.68` | 2.675·100 = 267.5 (f64), +0.5 = 268 |
| 1e-7 | `0` | |
| 123456.789 | `123456.79` | |
| −0 | `0` | |
| 0.07 | `0.07` | |

`fmt` ist für |n| < 9·10¹³ definiert; der Generator erzeugt nur Werte in diesem Bereich (alle Eingaben sind
geklemmt, §4). Außerhalb ist das Verhalten nicht festgelegt. `svg/test_fmt.py` + `svg/check_fmt.mjs` prüfen
`fmt` gegen eine unabhängige Python-Umsetzung (4000+ Werte).

**Zahlen in Attributen:** Ein Zahlenwert (Typ Zahl) wird mit `fmt` ausgegeben, ein Textwert mit `esc`. **Null und
`false` lassen das Attribut weg; die Zahl 0 wird ausgegeben** (`rx="0"`).

**Pfadtext `D(a1, a2, …)`:** Argumente werden verkettet; ein Zahlenargument wird mit `fmt` geschrieben, ein
Textargument (`M L H V C Q T A Z` oder ein Leerzeichen) unverändert. Zwischen **zwei aufeinanderfolgenden
Zahlen** steht genau ein Leerzeichen, sonst nichts. `D('M',1,11,'V',1)` → `M1 11V1`; `D(0,0,480,280)` →
`0 0 480 280`; `D('A',44,44,0,1,1,100,104)` → `A44 44 0 1 1 100 104`.

---

## 3. Text, Escaping, Begrenzung

* **`esc(s)`:** ersetzt `&`→`&amp;`, `<`→`&lt;`, `>`→`&gt;`, `"`→`&quot;`, `'`→`&apos;`. Sonst nichts (Nicht-ASCII bleibt
  als UTF-8 stehen). Gilt für Textinhalt, `<title>`, `aria-label` und alle Textattribute.
* **`clean(s, n)`** (bei jedem Texteingabefeld): entfernt `U+0000–U+001F`, `U+007F`, `U+FFFE`, `U+FFFF` und
  einzelne Ersatzzeichen-Hälften (lone surrogates); danach die ersten `n` **Codepunkte**. Kein Trimmen. (`\n` und
  `\t` sind Steuerzeichen und verschwinden.) C1-Steuerzeichen (`U+0080–U+009F`) bleiben.
* **`trunc(s, n)`:** `len(s) ≤ n` → `s`; sonst bei `n < 1` → `""`; sonst die ersten `n−1` Codepunkte + `…` (U+2026).
* **`fit(px, fs)`** = `F(px / (fs * 0.6))` — geschätzte Zeichenzahl, die in `px` passt (mittlere Zeichenbreite 0,6 em).
* **`short(v)`:** `ab(v) ≥ 1e6` → `fmt(v/1e6)+"M"`; sonst `ab(v) ≥ 1e4` → `fmt(v/1e3)+"k"`; sonst `fmt(v)`.
* **PRNG** (nur `pattern/confetti`): Zustand `sd = seed` (0 … 4294967295).
  `rnd()`: `sd = (sd * 1664525 + 1013904223) mod 2^32` (ganzzahlig, 64-Bit-Zwischenwert reicht; in f64 ebenfalls
  exakt), Ergebnis `sd / 4294967296` (f64, exakt). **Erst Zustand ändern, dann zurückgeben.**
* **Textbreite** wird nirgends gemessen, nur geschätzt (`fit`, `6.6` px je Zeichen bei 11 px, `0.6·fs` sonst).
  Breite Schriftzeichen (CJK, Emoji) können deshalb über ihren Platz hinaus laufen — bekannte Grenze, kein Fehler.

---

## 4. Normalisierung und Fehler

Die Eingabe muss ein JSON-Objekt sein (sonst Fehlerfeld `spec`). `type` muss eine Zeichenkette sein und genau einer
der neun Typnamen (`bar line donut pattern badge icon monogram gradient graph`; Vererbungsnamen wie
`constructor` zählen nicht) — sonst Feld `type`.

Dann wird **jedes Feld in der Reihenfolge der Tabellen (§4.1: zuerst die gemeinsamen, dann die des Typs)** geprüft;
der **erste** Fehler beendet die Prüfung (ein Fehlerfeld pro Spezifikation). Ein fehlendes Feld oder `null`
ergibt den Standardwert.

| Feldtyp | Regel | Fehlerfall |
|---|---|---|
| `int` | muss endliche Zahl sein (kein Bool, kein Text); **erst klemmen** auf `[min,max]`, **dann abrunden** (`F`) | Feld, „Zahl erwartet" |
| `num` | endliche Zahl, geklemmt auf `[min,max]`, **nicht** gerundet | wie `int` |
| `bool` | muss JSON-`true`/`false` sein | „true/false erwartet" |
| `enum` | muss gleich einem Eintrag aus `choices` sein (Textvergleich, Groß/Klein beachtet) | „erlaubt: …" |
| `str` | muss Text sein; `clean(s, len)` | „Text erwartet" |
| `color` | Text; entweder Token (`bg card elevated text muted accent green yellow red blue pink amber cyan lila`) oder `#rgb`/`#rrggbb` (Hex-Ziffern beider Schreibweisen); Ergebnis klein geschrieben, `#rgb` → `#rrggbb` | „Token oder #rgb/#rrggbb" |
| `colorx` | wie `color`, zusätzlich leerer Text (= nicht gesetzt) | |
| `nums` | Liste; nur die ersten `len` Elemente; jedes endliche Zahl, geklemmt auf `[min,max]` (nicht gerundet) | Feld, „Liste erwartet" / „Zahl erwartet" |
| `series` | Liste (auf `len`=200 gekürzt). Ist das **erste** Element eine Liste: die ersten **4** Elemente sind Reihen (jedes muss eine Liste sein; je Reihe die ersten 200 Zahlen, geklemmt). Sonst (flache Zahlenliste, nicht leer): eine Reihe. Leere Liste: keine Reihe (`[]`) | |
| `strs` | Liste, auf `len` gekürzt; jedes Element Text, `clean(e, 40)` | „Text erwartet" |
| `nodes` | Liste, auf 60 gekürzt; Elemente sind JSON-Objekte (nicht `null`, nicht Liste); `id = clean(e.id,32)` (muss nicht leer und **eindeutig** sein, Vergleich nach `clean`), `type = clean(e.type,24)`, `label = clean(e.label,40)` (fehlend = `""`); `x`,`y`: sobald eines von beiden vorhanden ist, müssen **beide** endliche Zahlen sein, geklemmt auf `[0,4000]` | Feld `nodes` |
| `edges` | Liste, auf 120 gekürzt; Objekte mit `from = clean(e.from,32)`, `to = clean(e.to,32)` | Feld `edges` |

**Weitere Prüfungen nach den Feldern (in dieser Reihenfolge):**

1. `idprefix` muss `^[A-Za-z][A-Za-z0-9_-]*$` erfüllen (sonst Feld `idprefix`). (Der Standard `sk` passt.)
2. Ist `title` leer (nach `clean`), wird er auf die Bezeichnung des Typs gesetzt (Tabellen unten, Spalte *Titel*).
3. `graph`: Wurde `nodes` angegeben, `edges` aber nicht, wird `edges = []` (Standard-Kanten nur, wenn **beide** fehlen).
4. `graph`: Jede Kante muss auf vorhandene Knoten-IDs zeigen (sonst Feld `edges`, „unbekannter Knoten").

Alle Felder, die nicht in den Tabellen des Typs stehen, werden ignoriert. Das normalisierte Objekt (`spec` in der
Rückgabe) enthält `type` und dann alle Felder in Tabellenreihenfolge.

### 4.1 Felder

**Gemeinsam (für jeden Typ, in dieser Reihenfolge):**

| Feld | Typ | Bereich / Auswahl | Standard | len |
|---|---|---|---|---|
| `theme` | enum | `master light contrast control content` | `master` | |
| `colors` | enum | `vars static` | `vars` | |
| `idprefix` | str | siehe oben | `sk` | 32 |
| `seed` | int | 0 … 4294967295 | 1 | |
| `title` | str | | Titel des Typs | 120 |

**Je Typ** (nach den gemeinsamen Feldern, in dieser Reihenfolge; Standardwerte in der Spalte *Std.*):

| Typ (Titel) | Felder |
|---|---|
| **bar** (Balkendiagramm) | `width` int 160…2400 Std. 480 · `height` int 120…2400 Std. 280 · `data` nums −1e9…1e9 Std. `[12,19,7,15,22,10]` len 64 · `labels` strs Std. `[]` len 64 · `color` color Std. `accent` · `values` bool 1 · `grid` bool 1 · `heading` bool 1 |
| **line** (Liniendiagramm) | `width` 160…2400 / 480 · `height` 120…2400 / 280 · `data` series −1e9…1e9 Std. `[[4,9,6,12,10,15,13]]` len 200 · `labels` strs `[]` len 200 · `color` `accent` · `area` bool 1 · `dots` bool 1 · `grid` bool 1 · `heading` bool 1 |
| **donut** (Ringdiagramm) | `width` 160…2400 / 360 · `height` 120…2400 / 240 · `data` nums **0**…1e9 Std. `[45,30,25]` len 12 · `labels` strs `[]` len 12 · `thickness` num 0.1…0.9 / 0.34 · `center` str Std. `""` len 16 · `legend` bool 1 · `heading` bool 1 |
| **pattern** (Muster) | `width` int 16…2400 / 240 · `height` 16…2400 / 240 · `kind` enum `grid dots stripes waves confetti` / `grid` · `size` int 4…200 / 24 · `stroke` num 0.5…12 / 1.5 · `angle` int −90…90 / 45 · `density` int 1…400 / 60 · `opacity` num 0.05…1 / 0.6 · `color` `accent` · `bg` bool 1 |
| **badge** (Badge) | `height` int 16…64 / 24 · `label` str Std. `DkZ` len 32 · `value` str Std. `aktiv` len 32 · `color` `accent` |
| **icon** (Symbol) | `name` enum (14 Symbole, §8) / `check` · `size` int 12…512 / 48 · `color` `accent` · `stroke` num 1…4 / 2 · `bg` bool **0** |
| **monogram** (Monogramm) | `text` str Std. `DZ` len **3** · `size` int 24…512 / 96 · `shape` enum `circle square hex` / `circle` · `color` `accent` · `color2` `lila` |
| **gradient** (Verlauf) | `width` int 16…2400 / 480 · `height` 16…2400 / 240 · `kind` enum `linear radial` / `linear` · `from` color `accent` · `to` color `lila` · `via` colorx `""` · `angle` int 0…360 / 45 · `cx` num 0…100 / 50 · `cy` num 0…100 / 50 · `text` str `""` len 60 |
| **graph** (Workflow) | `width` int **0**…2400 / **0** · `height` int 0…2400 / 0 · `nodes` nodes Std. `[{"id":"A","type":"input"},{"id":"B","type":"model"},{"id":"C","type":"output"}]` (je mit `label:""`) len 60 · `edges` edges Std. `[{"from":"A","to":"B"},{"from":"B","to":"C"}]` len 120 · `heading` bool 1 |

Die maschinenlesbare Fassung samt Beschriftungen und Einheiten steht in `svg/types.json`.

**Fehlerbeispiele (alle in `svg/fixtures-invalid/`):** unbekannter Typ → `type`; `"width":"480"` → `width`;
`"data":[1,"x"]` → `data`; `"color":"rgb(1,2,3)"` → `color`; zwei Knoten mit gleicher `id` → `nodes`;
Kante auf unbekannten Knoten → `edges`.

---

## 5. Paletten und Themes

### 5.1 Token-Hex je Theme

Die Hex-Werte der ersten Spalten stammen aus dem DkZ-Designsystem (CONTRACT §1). **Annahme** = nicht im
DkZ-System festgelegt, hier gewählt.

| Token | master | light | contrast | control | content |
|---|---|---|---|---|---|
| `bg` | `#060608` | `#f6f6f7` | `#000000` | `#060608` | `#060608` |
| `card` (CSS `--bg-card`) | `#0e0e10` | `#ffffff` | `#000000` | `#0e0e10` | `#0e0e10` |
| `elevated` (CSS `--bg-elevated`) | `#141418` | `#f0f0f2` **Annahme** | `#000000` | `#141418` | `#141418` |
| `text` | `#f6f6f7` | `#0e0e10` | `#00ff88` | `#f6f6f7` | `#f6f6f7` |
| `muted` (CSS `--text-muted`) | `#8e8e8f` **Annahme** | `#6a6a6b` **Annahme** | `#00ff88` | `#8e8e8f` **Annahme** | `#8e8e8f` **Annahme** |
| `accent` | `#fa1e4e` | `#fa1e4e` | `#00ff88` | `#fa1e4e` | `#fa1e4e` |
| `green` | `#00ff88` | `#00ff88` | `#00ff88` | `#00ff88` | `#00ff88` |
| `yellow` | `#ffb800` | `#ffb800` | `#00ff88` | `#ffb800` | `#ffb800` |
| `red` | `#ff3b5c` | `#ff3b5c` | `#00ff88` | `#ff3b5c` | `#ff3b5c` |
| `blue` | `#55acee` | `#55acee` | `#00ff88` | `#55acee` | `#55acee` |
| `pink` | `#ec4899` | `#ec4899` | `#00ff88` | `#ec4899` | `#ec4899` |
| `amber` | `#f5a623` | `#f5a623` | `#00ff88` | `#f5a623` | `#f5a623` |
| `cyan` | `#3fe0d0` | `#3fe0d0` | `#00ff88` | `#3fe0d0` | `#3fe0d0` |
| `lila` | `#9d7bff` | `#9d7bff` | `#00ff88` | `#9d7bff` | `#9d7bff` |

Begründung der Annahmen: `muted` (master/control/content) ist `rgba(246,246,247,.55)` auf `--bg-card` gemischt
(142/142/143 → `#8e8e8f`). `muted` (light) ist bewusst **dunkler** als ein 0,55-Mischwert, damit der Kontrast auf
Weiß ≥ 4,5:1 bleibt (`#6a6a6b` auf `#ffffff` ≈ 5,4:1). `elevated` (light) ist ein leicht abgesetztes Hellgrau, das
DkZ nennt dafür keinen Wert. Der Akzent bleibt im Theme `light` rot (DkZ: „Akzent bleibt").

**Theme `contrast`: ausschließlich `#000000` und `#00ff88`** in der gesamten Ausgabe. Ein als Literal angegebenes
Hex (`"color":"#fa1e4e"`) wird in `contrast` zu `#00ff88`. Unterschiede, die sonst durch Farbe entstehen, werden durch
Form ersetzt (Strichelung, Umrandung, abwechselnd gefüllt/leer, Textkontur).

### 5.2 Theme-Parameter

| Parameter | master | light | contrast | control | content |
|---|---|---|---|---|---|
| `radius` (Kartenradius) | 12 | 12 | 12 | **0** (HUD-Ecken, §6) **Annahme** | **20** **Annahme** |
| `font` (Schriftliste) | `Inter,system-ui,-apple-system,sans-serif` | wie master | wie master | `Archivo Expanded,` + master | `Syne,` + master |
| `line` (Rahmen-/Rasterfarbe) | `text` | `text` | `text` | `accent` | `text` |
| `lineop` (Rahmen-/Rasterdeckkraft) | 0.08 | 0.08 | 1 (→ Attribut entfällt) | 0.3 | 0.08 |
| `series` (Reihenfarben) | accent cyan lila amber green blue pink yellow | accent blue lila pink amber cyan green yellow **Annahme** | accent | accent cyan lila amber green blue pink yellow | accent lila cyan pink amber green blue yellow |

`font` und Rahmen folgen den DkZ-Themes (`--font-main`, `--border`); `radius` für `control`/`content` und die
Reihenfolge der Reihenfarben sind **Annahmen**. Die Rahmenlinie ist technisch ein 1-px-Strich in `line` mit Deckkraft
`lineop` (nähert `1px solid rgba(255,255,255,.08)` bzw. `rgba(250,30,78,.3)` an).

### 5.3 Farbausgabe `C(token)` und Modi

`hex(t)`: ist das Theme `contrast`: Token `bg`, `card`, `elevated` → `#000000`, jedes andere Token **und jedes
Literal-Hex** → `#00ff88`. Sonst: Literal-Hex → unverändert, Token → Wert aus der Tabelle 5.1.

`C(t)`:

* Modus `static`, oder `t` ist ein Literal-Hex: `hex(t)`.
* Modus `vars`: `var(--NAME, HEX)` mit `HEX = hex(t)`, genau ein Komma und ein Leerzeichen. `NAME` = Token, nur
  `card`→`bg-card`, `elevated`→`bg-elevated`, `muted`→`text-muted`. Beispiel: `var(--accent, #fa1e4e)`.

Die Schrift: `font-family` = Wert der Theme-Schriftliste; im Modus `vars`: `var(--font-main, <Schriftliste>)`.

`ink(t)` (lesbare Schrift auf Farbfläche `t`): Theme `contrast` → `#000000`. Sonst `n = hex(t)` als Zahl, Kanäle
`r,g,b`; `r*299 + g*587 + b*114 ≥ 128000` → `#060608`, sonst `#ffffff` (immer als **Hex**, auch im Modus `vars`).

`CM()` = `C('muted')`, `CT()` = `C('text')`, `op(x)` = `null` im Theme `contrast`, sonst `x`,
`SC(i)` = `series[i mod Anzahl]`, `U(id)` = `url(#id)`, `LN` = `{stroke: C(line), stroke-opacity: op(lineop)}`.

---

## 6. Serialisierung, Hüllen, Reihenfolge

**Element** `E(tag, attrs, inhalt)`: `<tag` + je Attribut in **Einfügereihenfolge** ` name="wert"` (Wert nach §2 bzw.
`esc`; `null`/`false` entfällt) + bei `inhalt == null`: `/>`, sonst `>` + inhalt + `</tag>`. (Ein leerer Text `""`
ist Inhalt: `<text …></text>`.)

Bausteine (Attributreihenfolge verbindlich):

| Baustein | Ausgabe |
|---|---|
| `RC(x,y,width,height,rx,a)` | `<rect x y width height rx …a/>` (fehlende/`null` entfallen) |
| `CI(cx,cy,r,a)` | `<circle cx cy r …a/>` |
| `PA(d,a)` | `<path d …a/>` |
| `fr(w,h,rx,a)` | `RC(0.5, 0.5, w−1, h−1, rx, a)` |
| `txt(x,y,fs,fill,s,anchor,weight,extra)` | `<text x y font-size font-weight text-anchor fill …extra>esc(s)</text>` (Gewicht/Anker `null` entfallen) |
| `DF(inhalt)` | `<defs>inhalt</defs>` |
| `card(w,h)` | `fr(w,h,radius,{fill:C('card'),…LN})`; bei `radius == 0` (control) zusätzlich ein Pfad `PA(D('M',1,11,'V',1,'H',11,'M',w−11,1,'H',w−1,'V',11,'M',w−1,h−11,'V',h−1,'H',w−11,'M',11,h−1,'H',1,'V',h−11), {fill:'none', stroke:C('accent'), stroke-width:2})` |
| `head(s,w)` | wenn `heading`: `txt(16, 29, 13, CT(), trunc(title, fit(w−32,13)), null, 600)`, sonst nichts |
| `base(s,w,h)` | `card(w,h)` dann `head(s,w)` |
| `none(x,y,fs=13)` | `txt(x, y, fs, CM(), "— ausstehend —", "middle")` (Gedankenstrich U+2014, Leerzeichen) |

**Ausgabeschablone** (Zeilenumbruch = `\n`, **kein** Umbruch nach `</svg>`):

```
<svg xmlns="http://www.w3.org/2000/svg" width="{fmt(w)}" height="{fmt(h)}" viewBox="{vb}" role="img" aria-label="{esc(title)}" font-family="{font}">\n
<title>{esc(title)}</title>\n
{element 1}\n{element 2}\n … {element n}\n
</svg>
```

`vb` = der vom Typ gelieferte `viewBox`-Text (icon, graph) oder `D(0,0,w,h)`. Jeder Typ liefert entweder eine
Elementliste (dann `w = width`, `h = height` aus der Normalisierung) oder `[w, h, vb, Elemente]` (badge, icon,
monogram, graph). Elemente stehen in der Reihenfolge, in der sie unten aufgeführt sind, je Zeile ein Element
(ein Element kann intern mehrere verschachtelte Tags ohne Zeilenumbruch enthalten, z. B. `<defs>…</defs>`).

Beispiel (Symbol, `colors:"static"`):

```
<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 24 24" role="img" aria-label="Symbol" font-family="Inter,system-ui,-apple-system,sans-serif">
<title>Symbol</title>
<path d="M5 12.5L9.5 17L19 7.5" fill="none" stroke="#fa1e4e" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>
</svg>
```

Weitere Beispiele: jede Datei in `svg/golden/` (z. B. `badge-light-static.svg`, `bar-light-static.svg`).

---

## 7. Achsen (bar, line)

`ticks(lo, hi)`:

```
raw = (hi - lo) / 4;  mag = 1
solange mag*10 <= raw: mag = mag*10
solange mag > raw:     mag = mag/10          # wiederholte Division durch 10, kein pow
r = raw / mag
st = mag * (r<=1 ? 1 : r<=2 ? 2 : r<=5 ? 5 : 10)
lo = F(lo/st) * st;  hi = ceil(hi/st) * st
Schritte = F((hi-lo)/st + 0.5)
-> [lo, hi, st, Schritte]
```

`axes(s, w, h, lo, hi, o)`; `o` ist die Elementliste, in die Raster und Beschriftung **sofort** eingefügt werden:

```
wenn hi - lo < 0.01: lo = lo - 1; hi = hi + 1
[lo, hi, st, nT] = ticks(lo, hi)
y0 = 16 + (heading ? 26 : 0) + 8
y1 = h - 16 - (labels.length > 0 ? 20 : 0)
x1 = w - 16
tl[i] = short(lo + i*st)                  für i = 0 … nT
x0 = 16 + (grid ? maxlen(tl)*6.6 + 8 : 0)       # maxlen = größte len() der tl
Y(v) = y0 + (hi - v)*(y1 - y0)/(hi - lo)
wenn grid, für i = 0 … nT (aufsteigend):
    y = Y(lo + i*st)
    <line x1=x0 y1=y x2=x1 y2=y stroke=C(line) stroke-opacity=op(lineop) stroke-dasharray=(contrast ? "2 4" : entfällt)/>
    txt(x0 - 8, y + 4, 11, CM(), tl[i], "end")
-> [x0, x1, y0, y1, Y]
```

`labels.length` ist die Länge der **normalisierten** Liste (bei `line` vor dem Kürzen auf die Punktzahl).

---

## 8. Typen

### 8.1 bar

```
d = data; n = d.length; o = base(s,w,h)
n == 0:  o + none(w/2, h/2 + 4)
sonst:   [x0,x1,_,y1,Y] = axes(s,w,h, mn(0,…d), mx(0,…d), o)
         sl = (x1-x0)/n;  bw = mn(sl*0.64, 64);  c = C(color)
         für i = 0 … n-1:
           cx = x0 + i*sl + sl/2;  yv = Y(v);  yz = Y(0);  lb = short(v)
           <rect x=cx-bw/2 y=mn(yv,yz) width=bw height=ab(yv-yz) rx=mn(radius/3, bw/2) fill=c/>
           wenn values und len(lb)*6.6 <= sl:
              txt(cx, (v<0 ? mn(yv+14, y1-4) : yv-6), 11, CT(), lb, "middle")
           wenn i < labels.length:
              txt(cx, h-16-4, 11, CM(), trunc(labels[i], fit(sl-4,11)), "middle")
```

Reihenfolge je Balken: Rechteck, Wertetext, Beschriftung. `rx` ist 0 im Theme `control` (`rx="0"`).

### 8.2 line

```
S = data (Reihen); o = base; alle = alle Werte aller Reihen; m = mx(0, Längen der Reihen)
m == 0:  o + none(w/2, h/2 + 4)
[x0,x1,_,y1,Y] = axes(s,w,h, mn(…alle), mx(…alle), o)
X(j) = (m==1) ? (x0+x1)/2 : x0 + j*(x1-x0)/(m-1)
ls = labels.slice(0,m).map(l => trunc(l,12));  lw = maxlen(ls)*6.6 + 8;  ev = 1
solange m>1 und (ev*(x1-x0))/(m-1) < lw: ev = ev+1
für j, l in ls: wenn j mod ev == 0:
     txt(X(j), h-16-4, 11, CM(), l, anker)     anker = (m>1 und j==0) ? "start" : (m>1 und j==m-1) ? "end" : "middle"
für jede Reihe r (Index i), Reihe leer → überspringen:
   c = C(i == 0 ? color : SC(i))
   p = verkette für j: D(j==0 ? 'M' : 'L', X(j), Y(v))          # "M10 20L30 40…"
   wenn area und S.length == 1 und Theme != contrast:
        <path d=p+D('L',X(r.length-1),y1,'L',X(0),y1,'Z') fill=c fill-opacity=0.16/>
   <path d=p fill="none" stroke=c stroke-width=2 stroke-linejoin="round" stroke-linecap="round" stroke-dasharray=(contrast und i ungerade ? "6 4" : entfällt)/>
   wenn dots und m <= 40: für jeden Punkt: <circle cx=X(j) cy=Y(v) r=3 fill=C('card') stroke=c stroke-width=2/>
```

Reihenfolge: zuerst alle Beschriftungen, dann je Reihe Fläche, Linie, Punkte. Reihen können kürzer als `m` sein; sie
werden mit denselben `X(j)` gezeichnet.

### 8.3 donut

```
d = data; n = d.length; o = base; tot = Summe (von links nach rechts, Start 0)
top = 16 + (heading ? 26 : 0);  ah = h - top - 16
aw = legend ? (w - 2*16)*0.5 : w - 2*16
r = mn(ah, aw)/2;  cx = legend ? 16 + aw/2 : w/2;  cy = top + ah/2;  ri = r*(1 - thickness)
ring(q) = D('M',cx,cy-q,'A',q,q,0,1,1,cx,cy+q,'A',q,q,0,1,1,cx,cy-q,'Z')
pt(q,a) = D(cx + q*cos(a), cy + q*sin(a))                        # "x y"
col(i)  = contrast ? (i ungerade ? 'bg' : 'accent') : SC(i)
wenn nicht (tot > 0):
     o + <path d=ring(r)+ring(ri) fill=CM() fill-opacity=op(0.25) fill-rule="evenodd"/>
       + none(cx, cy+4, mn(13, ri/4.5))
cum = 0;  fs = mn(mx(F(ri*0.45),10),36);  k = legend ? mn(n, F(ah/20)) : 0;  lx = 16 + aw + 16;  ly = cy - k*10
für i, v in d:  wenn v <= 0 → überspringen
     a0 = -π/2 + 2π*cum/tot;  a1 = -π/2 + 2π*(cum+v)/tot       # ((2π*cum)/tot)
     L = (a1 - a0 > π) ? 1 : 0;  voll = (v >= tot);  cum = cum + v
     d_ = voll ? ring(r)+ring(ri)
               : 'M' + pt(r,a0) + D('A',r,r,0,L,1) + ' ' + pt(r,a1) + 'L' + pt(ri,a1) + D('A',ri,ri,0,L,0) + ' ' + pt(ri,a0) + 'Z'
     <path d=d_ fill=C(col(i)) fill-rule=(voll ? "evenodd" : entfällt) stroke=C(contrast ? 'accent' : 'card') stroke-width=(contrast ? 1 : 2)/>
txt(cx, cy + fs*0.35, fs, CT(), trunc(center != "" ? center : short(tot), fit(ri*2 - 8, fs)), "middle", 700)
für i = 0 … k-1:                                                 # Legende, auch für Werte 0
     y = ly + i*20
     <rect x=lx y=y+5 width=10 height=10 rx=2 fill=C(col(i)) stroke=(contrast ? C('accent') : entfällt)/>
     txt(lx+16, y+14, 12, CT(), trunc(i < labels.length ? labels[i] : "Teil " + (i+1), fit(w - 16 - lx - 68, 12)))
     txt(w-16, y+14, 12, CM(), fmt(F(d[i]*1000/tot + 0.5)/10) + "%", "end")
```

`π` = `3.141592653589793`; `cos`/`sin` siehe §13. Zeichenreihenfolge: alle Segmente, Mitteltext, Legendenzeilen
(Quadrat, Name, Prozent). `d[i]*1000/tot` = `(d[i]*1000)/tot`. Die Prozent-Zeichenkette ist `fmt(...)` + `%`
(zwei Stufen: erst auf Zehntel, dann `fmt`).

### 8.4 pattern

```
z = size;  c = C(color);  id = idprefix;  o = bg ? card(w,h) : [];  sd = seed

kind == confetti:                       # je Stück 6 Aufrufe von rnd() in dieser Reihenfolge
  für i = 0 … density-1:
     x = rnd()*w;  y = rnd()*h;  k = F(rnd()*2)
     f = C( series[ F(rnd()*Anzahl_series) ] )
     q = z*0.3 + rnd()*z*0.4                    # z*0.3 + ((rnd()*z)*0.4)
     tr = 'rotate(' + D(F(rnd()*360), x, y) + ')'
     k == 1:  <rect x=x-q/2 y=y-q*0.2 width=q height=q*0.4 rx=1 fill=f transform=tr/>
     k == 0:  <circle cx=x cy=y r=q/2 fill=f/>
  Ausgabe: o + DF('<clipPath id="{id}-clip"><rect width=w height=h rx=radius/></clipPath>')
             + '<g clip-path="url(#{id}-clip)" opacity=op(opacity)>' + alle Stücke OHNE Zeilenumbruch verkettet + '</g>'
  (das Attribut `fill` der Stücke trägt `C(token)`; im Theme contrast ist `series = [accent]`, rnd() wird trotzdem aufgerufen)

sonst:
  tw = (kind == waves) ? z*2 : z;  m = z/2;  ln = {fill:'none', stroke:c, stroke-width:stroke}
  Zelle: grid    -> <path d=D('M',z,0,'L',0,0,'L',0,z) …ln/>
         dots    -> <circle cx=m cy=m r=stroke fill=c/>
         stripes -> <rect width=stroke height=z fill=c/>              (x, y, rx entfallen)
         waves   -> <path d=D('M',0,m,'Q',m,0,z,m,'T',tw,m) …ln/>
  Ausgabe: o
      + DF('<pattern id="{id}-pat" width=tw height=z patternUnits="userSpaceOnUse" patternTransform=(stripes ? "rotate(" + fmt(angle) + ")" : entfällt)>' + Zelle + '</pattern>')
      + <rect width=w height=h rx=radius fill="url(#{id}-pat)" opacity=op(opacity)/>
```

`pattern` und `rect` in einer Zeile je Element: Karte (wenn `bg`; im Theme control zwei Elemente), `defs`, dann
Fläche bzw. `g`. Bei `confetti` ignoriert `size` nichts: es steuert die Stückgröße (`z`).

### 8.5 badge

```
h = height;  fs = h*0.46;  cw = fs*0.6;  nl = len(label)
lw = nl > 0 ? nl*cw + h*0.8 : 0;  vw = len(value)*cw + h*0.8;  w = lw + vw
r = (radius == 20) ? h/2 : mn(radius, h)/2;  c = C(color);  y = h/2 + fs*0.35
1. fr(w, h, r, {fill:C('elevated'), …LN})
2. <rect x=lw width=vw height=h rx=r fill=c/>                       (y entfällt)
3. wenn lw != 0 und r != 0: <rect x=lw width=mn(r,vw) height=h fill=c/>   (rx entfällt)
4. wenn nl > 0: txt(lw/2, y, fs, CT(), label, "middle", 500)
5. txt(lw + vw/2, y, fs, ink(color), value, "middle", 700)          (immer, auch bei leerem Wert)
Hülle: [w, h, D(0,0,w,h)]
```

### 8.6 icon

Hülle `[size, size, vb, Elemente]`; `vb` = `-4 -4 32 32` wenn `bg`, sonst `0 0 24 24`. Elemente:
(wenn `bg`) `<rect x=-4 y=-4 width=32 height=32 rx=radius/2 fill=C('card') stroke=C(line) stroke-opacity=op(lineop) stroke-width=0.5/>`,
dann `<path d=PFAD fill="none" stroke=C(color) stroke-width=stroke stroke-linejoin="round" stroke-linecap="round"/>`.
(`stroke-width` ist der Wert `stroke` in viewBox-Einheiten.)

| name | d |
|---|---|
| check | `M5 12.5L9.5 17L19 7.5` |
| cross | `M6 6L18 18M18 6L6 18` |
| play | `M8 5V19L19 12Z` |
| pause | `M8 5V19M16 5V19` |
| arrow-right | `M4 12H20M14 6L20 12L14 18` |
| arrow-left | `M20 12H4M10 6L4 12L10 18` |
| star | `M12 3L14.4 9.3L21 9.6L15.8 13.7L17.6 20.2L12 16.5L6.4 20.2L8.2 13.7L3 9.6L9.6 9.3Z` |
| heart | `M12 20C12 20 4 15.5 4 9.5A4 4 0 0 1 12 7.5A4 4 0 0 1 20 9.5C20 15.5 12 20 12 20Z` |
| menu | `M4 6H20M4 12H20M4 18H20` |
| plus | `M12 5V19M5 12H19` |
| minus | `M5 12H19` |
| info | `M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM12 11V17M12 7.5V8` |
| search | `M4 10.5A6.5 6.5 0 1 0 17 10.5A6.5 6.5 0 1 0 4 10.5ZM15.5 15.5L20 20` |
| download | `M12 4V15M7 10L12 15L17 10M5 20H19` |

Die Pfadtexte sind fertige Zeichenketten und werden **nicht** durch `fmt` geschickt.

### 8.7 monogram

```
z = size;  c = z/2;  q = c - 1;  k = 0.8660254037844386;  n = len(text)
fs = z*(n < 2 ? 0.5 : n < 3 ? 0.42 : 0.34);  id = idprefix + "-grad"
fill = contrast ? C('accent') : "url(#" + id + ")"
nicht contrast: DF('<linearGradient id=id x1=0 y1=0 x2=1 y2=1><stop offset=0 stop-color=C(color)/><stop offset=1 stop-color=C(color2)/></linearGradient>')
Form: circle -> <circle cx=c cy=c r=q fill=fill/>
      square -> <rect x=1 y=1 width=z-2 height=z-2 rx=(radius != 0 ? z*0.22 : 0) fill=fill/>
      hex    -> <path d=D('M',c,1,'L',c+q*k,c-q/2,'L',c+q*k,c+q/2,'L',c,z-1,'L',c-q*k,c+q/2,'L',c-q*k,c-q/2,'Z') fill=fill/>
wenn n > 0: txt(c, c + fs*0.35, fs, ink(color), text, "middle", 700)
Hülle: [z, z, D(0,0,z,z)]
```

### 8.8 gradient

```
id = idprefix + "-grad";  a = angle*π/180;  cs = cos(a)*50;  sn = sin(a)*50;  pc(v) = fmt(v) + "%"
Stopps (je <stop offset=… stop-color=C(token)/>, ohne Zeilenumbruch hintereinander):
   contrast:  (0,accent) (0.5,accent) (0.5,bg) (1,bg)
   sonst:     (0,from) [(0.5,via) wenn via != ""] (1,to)
linear: <linearGradient id=id x1=pc(50-cs) y1=pc(50-sn) x2=pc(50+cs) y2=pc(50+sn)>stopps</linearGradient>
radial: <radialGradient id=id cx=pc(cx) cy=pc(cy) r="75%">stopps</radialGradient>
1. DF(Verlauf)
2. fr(w, h, radius, {fill:"url(#"+id+")", stroke:(contrast ? C('accent') : entfällt)})
3. tx = trunc(text, fit(w-48,12));  wenn tx != "":
     fs = mn( mx(h*0.16, 12), 72, (w-48)/(len(tx)*0.6) )
     txt(w/2, h/2 + fs*0.35, fs, (contrast ? C('accent') : ink(from)), tx, "middle", 700,
         contrast ? {stroke:C('bg'), stroke-width:3, paint-order:"stroke"} : nichts)
```

`ink(from)` verwendet `from` (nicht `via`/`to`) als Referenz. Die Stopp-Offsets sind Zahlen: `0`, `0.5`, `1`.

### 8.9 graph

Konstanten: `nw=136, nh=48, gx=56, gy=20`, `top = 16 + (heading ? 28 : 0)`, `id = idprefix + "-arrow"`,
`ix(k)` = Index des **ersten** Knotens mit `id == k`.

**Layout:**

```
nn == 0:                                    cw = 320;  ch = top + 80
alle Knoten haben x (und y):                (manuell)
     pos[i] = [16 + x, top + y]
     cw = 16 + mx(alle x) + nw + 16;  ch = top + mx(alle y) + nh + 16
sonst:                                      (automatisch, Schichten; x/y einzelner Knoten werden ignoriert)
     rk = Nullen (Länge nn);  go = 1;  it = 0
     solange go und (it++ < nn):             # it wird nur geprüft/erhöht, wenn go wahr ist
         go = 0
         für jede Kante e in Reihenfolge: a = ix(e.from); b = ix(e.to)
             wenn a != b und rk[b] < rk[a]+1 und rk[a]+1 < nn:  rk[b] = rk[a]+1;  go = 1
     cnt = Nullen (Länge mx(rk)+1);  idx[i] = cnt[rk[i]]; cnt[rk[i]] += 1       # in Knotenreihenfolge
     mc = mx(cnt);  hh = mc*nh + (mc-1)*gy
     pos[i] = [ 16 + r*(nw+gx),  top + (hh - (cnt[r]*nh + (cnt[r]-1)*gy))/2 + idx[i]*(nh+gy) ]     mit r = rk[i]
     cw = 2*16 + cnt.length*nw + (cnt.length-1)*gx;  ch = top + hh + 16
wenn irgendeine Kante e mit e.from != e.to und pos[ix(e.to)][0] <= pos[ix(e.from)][0] + nw:  ch = ch + 28
```

Die Kantenliste wird in Eingabereihenfolge durchlaufen (die Schichtung hängt davon ab). Zyklen enden durch die
Grenze `rk[a]+1 < nn` und die Schleifenzahl.

**Ausgabe:** `o = card(cw,ch)` + `head(s,cw)`; bei `nn == 0` außerdem `none(160, top+36)` (kein Pfeil-Marker).
Sonst:

```
DF('<marker id=id viewBox="0 0 10 10" refX=9 refY=5 markerWidth=7 markerHeight=7 orient="auto-start-reverse"><path d="M0 0L10 5L0 10Z" fill=CM()/></marker>')
für jede Kante e (Reihenfolge), mit e.from != e.to:
     [ax,ay] = pos[ix(from)];  [bx,by] = pos[ix(to)];  bk = (bx <= ax + nw)
     sx = bk ? ax + nw/2 : ax + nw;           sy = ay + (bk ? nh : nh/2)
     tx = bk ? bx + nw/2 : bx;                ty = by + (bk ? nh : nh/2)
     dx = bk ? 0 : mx(24, (tx - sx)/2);       dy = bk ? 40 : 0
     <path d=D('M',sx,sy,'C',sx+dx,sy+dy,tx-dx,ty+dy,tx,ty) fill="none" stroke=CM() stroke-width=1.5 stroke-dasharray=(bk ? "4 4" : entfällt) marker-end="url(#id)"/>
für jeden Knoten i in Reihenfolge:   [x,y] = pos[i];  c = C( SC( Summe der Codepunkte von n.type ) )      # SC = series[Summe mod Anzahl]
     <rect x y width=nw height=nh rx=mn(radius,10) fill=C('elevated') stroke=c stroke-opacity=op(0.45)/>
     <rect x=x+6 y=y+10 width=3 height=nh-20 rx=1.5 fill=c/>
     txt(x+16, y+21, 12, CT(), trunc(label != "" ? label : id, fit(nw-22,12)), null, 600)
     wenn type != "": txt(x+16, y+36, 10, CM(), trunc(type, fit(nw-22,10)))
Hülle: [ width != 0 ? width : cw,  height != 0 ? height : ch,  D(0,0,cw,ch) ]
```

Alle Kanten stehen vor allen Knoten. `width`/`height` (wenn ≠ 0) setzen nur die Ausgabegröße; die `viewBox` bleibt die
natürliche Größe `cw × ch` (das Bild skaliert dann). Rückkanten (`bk`) laufen unter den Knoten herum, gestrichelt.

---

## 9. Zugänglichkeit

Jedes SVG trägt `role="img"`, `aria-label` und ein `<title>` mit demselben Text (`esc(title)`), `title` ist
höchstens 120 Codepunkte lang und nie leer (Standard: Typbezeichnung).

## 10. Sicherheit des Generators

Es entstehen nur diese Elemente: `svg title g defs rect circle path line text pattern clipPath linearGradient
radialGradient stop marker`. Nie `<script>`, `on*`-Attribute, `javascript:`-URLs, `<foreignObject>`, `<use>`, `<image>`,
`<style>`, externe Verweise. Alle Texte sind escaped (§3), Farben kommen nur aus Tokens oder validierten Literalen
(`#rgb`/`#rrggbb`, §4), IDs aus `idprefix` (`^[A-Za-z][A-Za-z0-9_-]*$`). Angriffsstrings in `title`/`labels`/`label`
erscheinen ausschließlich als escapter Text (siehe `svg/fixtures/bar-attack-strings-light-vars.json`).

**Grenzen:** `width`/`height` ≤ 2400 (graph: Ausgabegröße ≤ 2400, natürliche Größe bis ≈ 16 + 60·192 breit bei 60
Schichten), Titel 120, Beschriftungen 40 je Eintrag, Werte: bar 64, line 4 × 200, donut 12, Knoten 60 (id 32, type
24, label 40), Kanten 120, Konfetti 400 Stück, `idprefix` 32, `center` 16, `badge`-Texte 32, `monogram`-Text 3,
`gradient`-Text 60. Größte erzeugte Datei in `svg/golden/`: siehe `ls -S svg/golden | head -1` (wenige zehn KB).

---

## 11. `sanitize(svgString)` (Modul `svgkit-safe.js`)

Für importierte oder eingefügte SVGs. Schnittstelle `SvgKit.sanitize(s)` → `{svg, removed:[…]}`; im Browser
`window.SvgKitSafe.sanitize`, in Node `require('./svgkit-safe.js').sanitize`. Zwei Wege mit **derselben Allowlist**:
`DOMParser` (Browser; fällt bei Parserfehlern auf den Zeichenkettenweg zurück) und ein eigenständiger
Zeichenketten-Tokenizer (Node/Bun/Python). Die Ausgabe wird **aus erlaubten Tokens neu geschrieben** (Text und
Attributwerte neu escaped, `<tag a="v">` kompakt ohne Einrückung); es wird nie ein Teil der Eingabe unverändert
durchgereicht.

* **Elemente (Allowlist, Groß/Klein beachtet):** `svg g defs title desc path rect circle ellipse line polyline polygon
  text tspan textPath linearGradient radialGradient stop pattern clipPath mask marker symbol use`. Alles andere —
  `script`, `foreignObject`, `style`, `image`, `a`, `animate`, `set`, `iframe`, Elemente fremder Namespaces
  (`x:script`) — wird samt Inhalt entfernt (`removed`: `element:<name>`). Das Wurzelelement muss `svg` sein, sonst
  ist das Ergebnis leer (`kein svg-Element`).
* **Attribute (Allowlist):** `id class style transform opacity role lang xml:space aria-* (label, hidden, labelledby,
  describedby) tabindex focusable x y x1 y1 x2 y2 cx cy r rx ry fx fy dx dy width height d points viewBox
  preserveAspectRatio version xmlns fill fill-opacity fill-rule clip-rule stroke stroke-width stroke-opacity
  stroke-linecap stroke-linejoin stroke-miterlimit stroke-dasharray stroke-dashoffset vector-effect paint-order
  font-family font-size font-weight font-style text-anchor dominant-baseline letter-spacing text-decoration offset
  stop-color stop-opacity gradientUnits gradientTransform spreadMethod patternUnits patternContentUnits
  patternTransform clipPathUnits maskUnits maskContentUnits clip-path mask marker-start marker-mid marker-end
  markerWidth markerHeight markerUnits refX refY orient startOffset href xlink:href`. Jedes `on*`-Attribut, jedes
  `xmlns:*` (außer dem stillschweigend verworfenen `xmlns:xlink`) und alles Weitere entfällt. `xlink:href` wird zu `href`.
  `xmlns` muss `http://www.w3.org/2000/svg` sein; die Wurzel bekommt es immer.
* **Werte:** `href`/`xlink:href` nur lokal (`#id`) — externe, `data:` und `javascript:` fallen weg; `use` ohne lokalen
  Verweis wird entfernt. In allen anderen Attributen (Ausnahme `aria-label`) werden nach Entfernen von Leerraum,
  Steuer- und Unsichtbarzeichen (Tab, `U+200B…`, `U+00AD`, BOM) und nach Auflösen von Entities `javascript:`,
  `vbscript:`, `livescript:`, `data:`, `expression(`, `@import`, `behavior:`, `-moz-binding`, `\` und `&#` abgelehnt.
  `url(…)` ist nur als `url(#id)` erlaubt. Steuerzeichen im Wert verwerfen das Attribut.
* **Text:** nur in `text tspan textPath title desc`; Entities werden aufgelöst (unbekannte benannte Entities → leer,
  gemeldet), numerische Referenzen auf Steuerzeichen entfallen; ausgegeben wird neu escaped. CDATA wird zu Text.
  Kommentare, `<?…?>` und `<!DOCTYPE …>` (samt Entity-Deklarationen: keine externen Entities, keine
  Entity-Bomben) werden verworfen.
* **Grenzen:** Eingabe > 1 000 000 Zeichen → leeres Ergebnis (`zu groß`); > 5000 Elemente oder Tiefe > 64 →
  Elemente verworfen (`limit`).
* **Selbstprüfung (fail-closed):** Enthält das Ergebnis (ohne Textinhalte und `aria-label`) trotzdem ein Muster wie
  `<script`, `<foreignObject`, `on…=` oder `javascript:`, ist das Ergebnis leer (`selbstprüfung`).
* **Unterschied der beiden Wege:** Gleiche Eingabe → gleiche Sicherheitseigenschaften; die Ausgabe kann sich in
  Randfällen unterscheiden (leere Elemente `<g></g>` gegenüber `<g/>`, DOCTYPE/Entity-Eingaben, die der Browser
  als Fehler ablehnt). Geprüft mit 49 Angriffsbeispielen (`svg/test_sanitize.mjs`, optional `--browser`).

Die Python-Fassung (`svgkit.py`) soll dieselbe Allowlist und dieselben Wertregeln mit `html.parser`/`xml` umsetzen; die
Ausgabe muss nicht byte-gleich mit der JS-Fassung sein, aber jede Prüfung aus `svg/test_sanitize.mjs` bestehen.

---

## 12. Umfang je Fassung

| Fassung | Typen | `_impl` in den Fixtures |
|---|---|---|
| JS (Referenz) | alle neun | `js` |
| Python | alle neun | `py` |
| Rust | alle neun | `rs` |
| C | `bar`, `donut`, `badge`, `icon`, `gradient`, `pattern` (nur `kind` `grid`, `dots`, `stripes`) | `c` |

Alle Fixtures nennen `js`, `py`, `rs`; die Fixtures der C-Teilmenge nennen zusätzlich `c`. Für `line`, `monogram`,
`graph` und `pattern` (`waves`, `confetti`) gibt es keine C-Fassung — `c/dsui-svg` darf dort mit Exitcode 1 enden.

## 13. Hinweise für Portierungen (bekannte Stolpersteine)

1. **Alle Zahlen f64**, nie `f32`, nie Ganzzahl-Division. `F` ist `floor`, auch für negative Werte (`-0.5 → -1`).
2. `fmt` rundet über `F(ab(n)*100+0.5)`; **nicht** `round()` (Rust/Python runden anders), nicht `{:.2}`.
3. Ganzzahlfelder: erst auf `[min,max]` klemmen, dann `floor`. JSON-`true` ist **keine** Zahl (Python: `bool` vor
   `int` prüfen). `5.0` ist eine Zahl; `"5"` ist keine.
4. Codepunkte statt Bytes: `len`, `trunc`, `clean`, `fit`-Eingaben und Knotentyp-Summen arbeiten auf Codepunkten
   (Rust: `chars()`, Python: `str`). Texte werden als UTF-8 ausgegeben.
5. `clean` entfernt Steuerzeichen **vor** dem Kürzen. `\n`/`\t` verschwinden. JSON-Ersatzzeichenpaare (`😀`)
   sind ein Codepunkt; einzelne Hälften werden verworfen.
6. Attribute stehen in der hier beschriebenen Reihenfolge; `null`/`false` fehlen, `0` bleibt (`rx="0"`).
7. `cos`/`sin` kommen aus der Laufzeit-Bibliothek; JS (V8, fdlibm), glibc (Python/Rust/C) können sich im letzten Bit
   unterscheiden. Das wirkt nur, wenn ein Ergebnis genau auf einer Rundungsgrenze von `fmt` liegt (praktisch nie);
   in den Fixtures ist es nicht aufgetreten (in JS gegen Python-`math` für `donut`/`gradient` **nicht** geprüft — das
   leistet der Paritätstest der jeweiligen Fassung).
8. **Reihenfolge der Zufallsaufrufe** im Konfetti (6 je Stück, siehe §8.4) und der Kantendurchlauf im Graph-Layout
   (Eingabereihenfolge) sind verbindlich.
9. Die Zeilenumbrüche (`\n`) stehen zwischen `<svg …>`, `<title>`, jedem Element und `</svg>`; nirgends sonst, und nicht
   am Ende der Ausgabe.
10. Unbekannte Eingabeschlüssel ignorieren; leere Liste `[]`, `null` und fehlendes Feld sind beim Standardwert nicht
    dasselbe: `data: []` ist eine **leere** Liste (zeigt „— ausstehend —"), `data: null` bzw. fehlend ergibt den
    Standardwert.
