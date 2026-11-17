//! dsui-svg — svgspec/1 (DkZ): Rust-Fassung des SVG-Generators, ohne externe Abhängigkeiten.
//!
//! Regeln: `svg/SPEC.md`. Referenz: `svgkit.js`. Die Ausgabe ist byte-gleich zur Referenz
//! (`svg/run_parity.sh rs`). Alle Rechnungen sind f64 in der Reihenfolge der Referenz; Zahlen
//! gelangen nur über [`fmt`] in die Ausgabe (nie `{:.2}`, nie `round()`).
//!
//! Aufbau: [`json`] (eigener, strenger Parser nach ECMA-404/`JSON.parse`), Normalisierung
//! (`normalize`), Themes/Farben, Typen (`bar` … `graph`), Einstieg [`generate_bytes`].

use std::fmt::Write as _;

pub const VERSION: &str = "svgspec/1";

// ───────────────────────────── Fehler ─────────────────────────────

/// Fehler der Normalisierung: `field` ist das Fehlerfeld aus SPEC §4 (`spec`, `type`, `width` …).
#[derive(Debug, Clone, PartialEq)]
pub struct SpecError {
    pub field: String,
    pub msg: String,
}

impl SpecError {
    fn new(field: &str, msg: &str) -> Self {
        SpecError { field: field.to_string(), msg: msg.to_string() }
    }
}

impl std::fmt::Display for SpecError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "svgspec: {}: {}", self.field, self.msg)
    }
}

impl std::error::Error for SpecError {}

type R<T> = Result<T, SpecError>;

fn bad<T>(field: &str, msg: &str) -> R<T> {
    Err(SpecError::new(field, msg))
}

// ───────────────────────────── JSON ─────────────────────────────

pub mod json {
    //! Strenger JSON-Parser (wie `JSON.parse`): RFC 8259, keine Kommentare, keine Kommas am Ende,
    //! keine führenden Nullen, Steuerzeichen in Strings verboten. Doppelte Schlüssel: der letzte gewinnt.
    //! Tiefe ist begrenzt ([`MAX_DEPTH`]); jeder Fehler ist ein `Err`, nie eine Panik.
    //!
    //! Einzelne (nicht gepaarte) Ersatzzeichen-Escapes (`\ud800`) sind in Rust-Strings nicht darstellbar
    //! und werden zu U+FFFE; der Generator entfernt U+FFFE ohnehin (`clean`), wie die Referenz die
    //! Ersatzzeichen-Hälften entfernt.

    /// Maximale Verschachtelungstiefe (JS erlaubt mehr; die Grenze schützt den Stapel).
    pub const MAX_DEPTH: usize = 512;

    #[derive(Debug, Clone, PartialEq)]
    pub enum Json {
        Null,
        Bool(bool),
        Num(f64),
        Str(String),
        Arr(Vec<Json>),
        Obj(Vec<(String, Json)>),
    }

    impl Json {
        /// Wert zu einem Schlüssel; bei doppelten Schlüsseln der letzte.
        pub fn get(&self, key: &str) -> Option<&Json> {
            match self {
                Json::Obj(v) => v.iter().rev().find(|(k, _)| k == key).map(|(_, x)| x),
                _ => None,
            }
        }
    }

    struct P<'a> {
        b: &'a [u8],
        i: usize,
    }

    /// Parst `text` vollständig (nur Leerraum nach dem Wert erlaubt).
    pub fn parse(text: &str) -> Result<Json, String> {
        let mut p = P { b: text.as_bytes(), i: 0 };
        p.ws();
        let v = p.value(0)?;
        p.ws();
        if p.i != p.b.len() {
            return Err(format!("unerwartetes Zeichen an Position {}", p.i));
        }
        Ok(v)
    }

    impl<'a> P<'a> {
        fn ws(&mut self) {
            while self.i < self.b.len() && matches!(self.b[self.i], b' ' | b'\t' | b'\n' | b'\r') {
                self.i += 1;
            }
        }

        fn err<T>(&self, what: &str) -> Result<T, String> {
            Err(format!("{} an Position {}", what, self.i))
        }

        fn lit(&mut self, word: &str, v: Json) -> Result<Json, String> {
            if self.b[self.i..].starts_with(word.as_bytes()) {
                self.i += word.len();
                Ok(v)
            } else {
                self.err("ungültiges Literal")
            }
        }

        fn value(&mut self, depth: usize) -> Result<Json, String> {
            if self.i >= self.b.len() {
                return Err("unerwartetes Ende der Eingabe".to_string());
            }
            match self.b[self.i] {
                b'{' => self.object(depth),
                b'[' => self.array(depth),
                b'"' => Ok(Json::Str(self.string()?)),
                b't' => self.lit("true", Json::Bool(true)),
                b'f' => self.lit("false", Json::Bool(false)),
                b'n' => self.lit("null", Json::Null),
                b'-' | b'0'..=b'9' => self.number(),
                _ => self.err("unerwartetes Zeichen"),
            }
        }

        fn object(&mut self, depth: usize) -> Result<Json, String> {
            if depth >= MAX_DEPTH {
                return Err(format!("zu tief verschachtelt (> {})", MAX_DEPTH));
            }
            self.i += 1; // {
            let mut v: Vec<(String, Json)> = Vec::new();
            self.ws();
            if self.i < self.b.len() && self.b[self.i] == b'}' {
                self.i += 1;
                return Ok(Json::Obj(v));
            }
            loop {
                self.ws();
                if self.i >= self.b.len() || self.b[self.i] != b'"' {
                    return self.err("Schlüssel (String) erwartet");
                }
                let k = self.string()?;
                self.ws();
                if self.i >= self.b.len() || self.b[self.i] != b':' {
                    return self.err("':' erwartet");
                }
                self.i += 1;
                self.ws();
                let x = self.value(depth + 1)?;
                v.push((k, x));
                self.ws();
                match self.b.get(self.i) {
                    Some(b',') => self.i += 1,
                    Some(b'}') => {
                        self.i += 1;
                        return Ok(Json::Obj(v));
                    }
                    _ => return self.err("',' oder '}' erwartet"),
                }
            }
        }

        fn array(&mut self, depth: usize) -> Result<Json, String> {
            if depth >= MAX_DEPTH {
                return Err(format!("zu tief verschachtelt (> {})", MAX_DEPTH));
            }
            self.i += 1; // [
            let mut v: Vec<Json> = Vec::new();
            self.ws();
            if self.i < self.b.len() && self.b[self.i] == b']' {
                self.i += 1;
                return Ok(Json::Arr(v));
            }
            loop {
                self.ws();
                v.push(self.value(depth + 1)?);
                self.ws();
                match self.b.get(self.i) {
                    Some(b',') => self.i += 1,
                    Some(b']') => {
                        self.i += 1;
                        return Ok(Json::Arr(v));
                    }
                    _ => return self.err("',' oder ']' erwartet"),
                }
            }
        }

        fn hex4(&self, at: usize) -> Option<u32> {
            if at + 4 > self.b.len() {
                return None;
            }
            let mut n = 0u32;
            for k in 0..4 {
                let d = (self.b[at + k] as char).to_digit(16)?;
                n = n * 16 + d;
            }
            Some(n)
        }

        fn string(&mut self) -> Result<String, String> {
            self.i += 1; // "
            let mut out = String::new();
            let mut run = self.i; // Beginn eines unveränderten Abschnitts
            loop {
                let c = match self.b.get(self.i) {
                    Some(c) => *c,
                    None => return Err("String nicht beendet".to_string()),
                };
                match c {
                    b'"' => {
                        out.push_str(&String::from_utf8_lossy(&self.b[run..self.i]));
                        self.i += 1;
                        return Ok(out);
                    }
                    0..=0x1f => return self.err("Steuerzeichen im String"),
                    b'\\' => {
                        out.push_str(&String::from_utf8_lossy(&self.b[run..self.i]));
                        self.i += 1;
                        let e = match self.b.get(self.i) {
                            Some(e) => *e,
                            None => return Err("String nicht beendet".to_string()),
                        };
                        self.i += 1;
                        match e {
                            b'"' => out.push('"'),
                            b'\\' => out.push('\\'),
                            b'/' => out.push('/'),
                            b'b' => out.push('\u{8}'),
                            b'f' => out.push('\u{c}'),
                            b'n' => out.push('\n'),
                            b'r' => out.push('\r'),
                            b't' => out.push('\t'),
                            b'u' => {
                                let hi = match self.hex4(self.i) {
                                    Some(h) => h,
                                    None => return self.err("ungültiges \\u-Escape"),
                                };
                                self.i += 4;
                                if (0xD800..0xDC00).contains(&hi) {
                                    // Paar? \uDC00–\uDFFF muss direkt folgen
                                    let lo = if self.b.get(self.i) == Some(&b'\\') && self.b.get(self.i + 1) == Some(&b'u') {
                                        self.hex4(self.i + 2).filter(|l| (0xDC00..0xE000).contains(l))
                                    } else {
                                        None
                                    };
                                    match lo {
                                        Some(l) => {
                                            self.i += 6;
                                            let cp = 0x10000 + ((hi - 0xD800) << 10) + (l - 0xDC00);
                                            out.push(char::from_u32(cp).unwrap_or('\u{FFFE}'));
                                        }
                                        None => out.push('\u{FFFE}'),
                                    }
                                } else if (0xDC00..0xE000).contains(&hi) {
                                    out.push('\u{FFFE}');
                                } else {
                                    out.push(char::from_u32(hi).unwrap_or('\u{FFFE}'));
                                }
                            }
                            _ => return self.err("ungültiges Escape"),
                        }
                        run = self.i;
                    }
                    _ => self.i += 1,
                }
            }
        }

        fn digits(&mut self) -> usize {
            let s = self.i;
            while self.i < self.b.len() && self.b[self.i].is_ascii_digit() {
                self.i += 1;
            }
            self.i - s
        }

        fn number(&mut self) -> Result<Json, String> {
            let start = self.i;
            if self.b[self.i] == b'-' {
                self.i += 1;
            }
            match self.b.get(self.i) {
                Some(b'0') => self.i += 1,
                Some(b'1'..=b'9') => {
                    self.digits();
                }
                _ => return self.err("Ziffer erwartet"),
            }
            if self.b.get(self.i) == Some(&b'.') {
                self.i += 1;
                if self.digits() == 0 {
                    return self.err("Ziffer nach '.' erwartet");
                }
            }
            if matches!(self.b.get(self.i), Some(b'e') | Some(b'E')) {
                self.i += 1;
                if matches!(self.b.get(self.i), Some(b'+') | Some(b'-')) {
                    self.i += 1;
                }
                if self.digits() == 0 {
                    return self.err("Ziffer im Exponenten erwartet");
                }
            }
            // Die Grammatik ist geprüft; die Zeichen sind reines ASCII.
            let t = std::str::from_utf8(&self.b[start..self.i]).unwrap_or("0");
            match t.parse::<f64>() {
                Ok(n) => Ok(Json::Num(n)),
                Err(_) => self.err("ungültige Zahl"),
            }
        }
    }
}

use json::Json;

// ───────────────────────────── Zahlen, Text, PRNG ─────────────────────────────

/// Die einzige Zahlenregel (SPEC §2): auf 2 Nachkommastellen („half away from zero“ über
/// `floor(|n|*100+0.5)`), ohne überflüssige Nullen, `-0` wird `0`. Nicht endlich → `0`.
pub fn fmt(n: f64) -> String {
    if !n.is_finite() {
        return "0".to_string();
    }
    let k = (n.abs() * 100.0 + 0.5).floor();
    if k == 0.0 {
        return "0".to_string();
    }
    let sign = if n < 0.0 { "-" } else { "" };
    if !k.is_finite() {
        // außerhalb des definierten Bereichs (|n| < 9e13); wie die Referenz
        return format!("{}Infinity", sign);
    }
    let f = k % 100.0;
    let q = (k / 100.0).floor();
    let mut s = String::with_capacity(16);
    s.push_str(sign);
    let _ = write!(s, "{}", q as u64);
    if f != 0.0 {
        let f = f as u32;
        if f < 10 {
            let _ = write!(s, ".0{}", f);
        } else if !f.is_multiple_of(10) {
            let _ = write!(s, ".{}", f);
        } else {
            let _ = write!(s, ".{}", f / 10);
        }
    }
    s
}

/// Ganzzahliger Zufallsgenerator (nur `pattern/confetti`): LCG `s = (s*1664525 + 1013904223) mod 2^32`.
#[derive(Debug, Clone)]
pub struct Prng(u64);

impl Prng {
    pub fn new(seed: u64) -> Self {
        Prng(seed & 0xFFFF_FFFF)
    }
    /// Erst Zustand ändern, dann `zustand / 2^32` zurückgeben.
    pub fn next_f64(&mut self) -> f64 {
        self.0 = (self.0 * 1664525 + 1013904223) % 4294967296;
        self.0 as f64 / 4294967296.0
    }
}

/// XML-Escaping für `& < > " '`.
pub fn esc(s: &str) -> String {
    let mut o = String::with_capacity(s.len() + 8);
    for c in s.chars() {
        match c {
            '&' => o.push_str("&amp;"),
            '<' => o.push_str("&lt;"),
            '>' => o.push_str("&gt;"),
            '"' => o.push_str("&quot;"),
            '\'' => o.push_str("&apos;"),
            _ => o.push(c),
        }
    }
    o
}

fn len(s: &str) -> usize {
    s.chars().count()
}

/// Steuerzeichen (U+0000–U+001F, U+007F, U+FFFE, U+FFFF) entfernen, dann die ersten `n` Codepunkte.
pub fn clean(s: &str, n: usize) -> String {
    s.chars()
        .filter(|&c| !(c < '\u{20}' || c == '\u{7f}' || c == '\u{fffe}' || c == '\u{ffff}'))
        .take(n)
        .collect()
}

/// `n` Codepunkte; sonst `n-1` Codepunkte plus `…`; `n < 1` → leer.
pub fn trunc(s: &str, n: f64) -> String {
    if (len(s) as f64) <= n {
        s.to_string()
    } else if n < 1.0 {
        String::new()
    } else {
        let mut o: String = s.chars().take((n - 1.0) as usize).collect();
        o.push('…');
        o
    }
}

fn fit(px: f64, fs: f64) -> f64 {
    (px / (fs * 0.6)).floor()
}

fn short(v: f64) -> String {
    if v.abs() >= 1e6 {
        fmt(v / 1e6) + "M"
    } else if v.abs() >= 1e4 {
        fmt(v / 1e3) + "k"
    } else {
        fmt(v)
    }
}

fn mn(a: f64, b: f64) -> f64 {
    if a < b || b.is_nan() { a } else { b }
}

fn mx(a: f64, b: f64) -> f64 {
    if a > b || b.is_nan() { a } else { b }
}

// ───────────────────────────── Elemente ─────────────────────────────

/// Attributwert: Zahl (über [`fmt`]), Text (über [`esc`]) oder weggelassen.
#[derive(Debug, Clone)]
enum Av {
    N(f64),
    S(String),
    X,
}
use Av::{N, S, X};

type At = Vec<(&'static str, Av)>;

fn s(t: &str) -> Av {
    S(t.to_string())
}

fn so(t: Option<&str>) -> Av {
    match t {
        Some(t) => s(t),
        None => X,
    }
}

fn el(tag: &str, a: &[(&str, Av)], c: Option<&str>) -> String {
    let mut o = String::with_capacity(64);
    o.push('<');
    o.push_str(tag);
    for (k, v) in a {
        match v {
            N(x) => {
                o.push(' ');
                o.push_str(k);
                o.push_str("=\"");
                o.push_str(&fmt(*x));
                o.push('"');
            }
            S(t) => {
                o.push(' ');
                o.push_str(k);
                o.push_str("=\"");
                o.push_str(&esc(t));
                o.push('"');
            }
            X => {}
        }
    }
    match c {
        None => o.push_str("/>"),
        Some(c) => {
            o.push('>');
            o.push_str(c);
            o.push_str("</");
            o.push_str(tag);
            o.push('>');
        }
    }
    o
}

/// Pfad-Argument: Zahl (fmt) oder Text (unverändert).
enum Da {
    T(&'static str),
    V(f64),
}
impl From<f64> for Da {
    fn from(v: f64) -> Self {
        Da::V(v)
    }
}
impl From<i32> for Da {
    fn from(v: i32) -> Self {
        Da::V(v as f64)
    }
}
impl From<&'static str> for Da {
    fn from(v: &'static str) -> Self {
        Da::T(v)
    }
}

/// `D(...)` der Referenz: zwischen zwei aufeinanderfolgenden Zahlen genau ein Leerzeichen.
fn dj(a: &[Da]) -> String {
    let mut o = String::new();
    let mut prev_num = false;
    for x in a {
        match x {
            Da::V(v) => {
                if prev_num {
                    o.push(' ');
                }
                o.push_str(&fmt(*v));
                prev_num = true;
            }
            Da::T(t) => {
                o.push_str(t);
                prev_num = false;
            }
        }
    }
    o
}

macro_rules! d {
    ($($x:expr),* $(,)?) => { dj(&[$(Da::from($x)),*]) };
}

fn rc(x: Av, y: Av, w: f64, h: f64, rx: Av, a: At) -> String {
    let mut v: At = vec![("x", x), ("y", y), ("width", N(w)), ("height", N(h)), ("rx", rx)];
    v.extend(a);
    el("rect", &v, None)
}

fn ci(cx: f64, cy: f64, r: f64, a: At) -> String {
    let mut v: At = vec![("cx", N(cx)), ("cy", N(cy)), ("r", N(r))];
    v.extend(a);
    el("circle", &v, None)
}

fn pa(dd: String, a: At) -> String {
    let mut v: At = vec![("d", S(dd))];
    v.extend(a);
    el("path", &v, None)
}

#[allow(clippy::too_many_arguments)]
fn txt(x: f64, y: f64, fs: f64, fill: &str, t: &str, anchor: Option<&str>, weight: Option<f64>, ex: At) -> String {
    let mut v: At = vec![
        ("x", N(x)),
        ("y", N(y)),
        ("font-size", N(fs)),
        ("font-weight", weight.map_or(X, N)),
        ("text-anchor", so(anchor)),
        ("fill", s(fill)),
    ];
    v.extend(ex);
    el("text", &v, Some(&esc(t)))
}

fn df(c: &str) -> String {
    el("defs", &[], Some(c))
}

// ───────────────────────────── Themes ─────────────────────────────

const TOK: [&str; 14] = [
    "bg", "card", "elevated", "text", "muted", "accent", "green", "yellow", "red", "blue", "pink", "amber", "cyan", "lila",
];
const BASE: [&str; 14] = [
    "060608", "0e0e10", "141418", "f6f6f7", "8e8e8f", "fa1e4e", "00ff88", "ffb800", "ff3b5c", "55acee", "ec4899", "f5a623",
    "3fe0d0", "9d7bff",
];
const LIGHT: [&str; 14] = [
    "f6f6f7", "ffffff", "f0f0f2", "0e0e10", "6a6a6b", "fa1e4e", "00ff88", "ffb800", "ff3b5c", "55acee", "ec4899", "f5a623",
    "3fe0d0", "9d7bff",
];
const FONT: &str = "Inter,system-ui,-apple-system,sans-serif";

struct Theme {
    hex: [&'static str; 14],
    series: &'static [&'static str],
    radius: f64,
    font: &'static str,
    line: &'static str,
    lineop: f64,
}

const THEME_NAMES: [&str; 5] = ["master", "light", "contrast", "control", "content"];
const SER: &[&str] = &["accent", "cyan", "lila", "amber", "green", "blue", "pink", "yellow"];

static THEMES: [Theme; 5] = [
    Theme { hex: BASE, series: SER, radius: 12.0, font: FONT, line: "text", lineop: 0.08 },
    Theme {
        hex: LIGHT,
        series: &["accent", "blue", "lila", "pink", "amber", "cyan", "green", "yellow"],
        radius: 12.0,
        font: FONT,
        line: "text",
        lineop: 0.08,
    },
    Theme {
        hex: [
            "000000", "000000", "000000", "00ff88", "00ff88", "00ff88", "00ff88", "00ff88", "00ff88", "00ff88", "00ff88",
            "00ff88", "00ff88", "00ff88",
        ],
        series: &["accent"],
        radius: 12.0,
        font: FONT,
        line: "text",
        lineop: 1.0,
    },
    Theme {
        hex: BASE,
        series: SER,
        radius: 0.0,
        font: "Archivo Expanded,Inter,system-ui,-apple-system,sans-serif",
        line: "accent",
        lineop: 0.3,
    },
    Theme {
        hex: BASE,
        series: &["accent", "lila", "cyan", "pink", "amber", "green", "blue", "yellow"],
        radius: 20.0,
        font: "Syne,Inter,system-ui,-apple-system,sans-serif",
        line: "text",
        lineop: 0.08,
    },
];

/// Aktives Theme samt Modus (`vars`) und Kontrast-Flag (`K` der Referenz).
struct Cx {
    th: &'static Theme,
    vars: bool,
    k: bool,
}

impl Cx {
    fn tok_index(t: &str) -> Option<usize> {
        TOK.iter().position(|x| *x == t)
    }

    fn hex(&self, t: &str) -> String {
        if self.k {
            return match Cx::tok_index(t) {
                Some(i) if i < 3 => "#000000".to_string(),
                _ => "#00ff88".to_string(),
            };
        }
        if t.starts_with('#') {
            t.to_string()
        } else {
            match Cx::tok_index(t) {
                Some(i) => format!("#{}", self.th.hex[i]),
                None => "#00ff88".to_string(), // unerreichbar: Tokens sind validiert
            }
        }
    }

    fn c(&self, t: &str) -> String {
        if self.vars && !t.starts_with('#') {
            let name = match t {
                "card" => "bg-card",
                "elevated" => "bg-elevated",
                "muted" => "text-muted",
                o => o,
            };
            format!("var(--{}, {})", name, self.hex(t))
        } else {
            self.hex(t)
        }
    }

    fn ink(&self, t: &str) -> &'static str {
        if self.k {
            return "#000000";
        }
        let h = self.hex(t);
        let n = u32::from_str_radix(h.get(1..).unwrap_or("0"), 16).unwrap_or(0);
        if (n >> 16) * 299 + (n >> 8 & 255) * 587 + (n & 255) * 114 >= 128000 {
            "#060608"
        } else {
            "#ffffff"
        }
    }

    fn op(&self, x: f64) -> Av {
        if self.k { X } else { N(x) }
    }

    fn sc(&self, i: usize) -> &'static str {
        self.th.series[i % self.th.series.len()]
    }

    fn cm(&self) -> String {
        self.c("muted")
    }

    fn ct(&self) -> String {
        self.c("text")
    }

    fn ln(&self) -> At {
        vec![("stroke", S(self.c(self.th.line))), ("stroke-opacity", self.op(self.th.lineop))]
    }

    fn fr(&self, w: f64, h: f64, rx: f64, a: At) -> String {
        rc(N(0.5), N(0.5), w - 1.0, h - 1.0, N(rx), a)
    }

    fn card(&self, w: f64, h: f64) -> Vec<String> {
        let mut a: At = vec![("fill", S(self.c("card")))];
        a.extend(self.ln());
        let mut o = vec![self.fr(w, h, self.th.radius, a)];
        if self.th.radius == 0.0 {
            o.push(pa(
                d!("M", 1, 11, "V", 1, "H", 11, "M", w - 11.0, 1, "H", w - 1.0, "V", 11, "M", w - 1.0, h - 11.0, "V", h - 1.0, "H", w - 11.0, "M", 11, h - 1.0, "H", 1, "V", h - 11.0),
                vec![("fill", s("none")), ("stroke", S(self.c("accent"))), ("stroke-width", N(2.0))],
            ));
        }
        o
    }

    fn head(&self, sp: &Spec, w: f64) -> Option<String> {
        if sp.heading {
            Some(txt(16.0, 29.0, 13.0, &self.ct(), &trunc(&sp.title, fit(w - 32.0, 13.0)), None, Some(600.0), vec![]))
        } else {
            None
        }
    }

    fn base(&self, sp: &Spec, w: f64, h: f64) -> Vec<String> {
        let mut o = self.card(w, h);
        o.extend(self.head(sp, w));
        o
    }

    fn none(&self, x: f64, y: f64, f: Option<f64>) -> String {
        txt(x, y, f.unwrap_or(13.0), &self.cm(), "— ausstehend —", Some("middle"), None, vec![])
    }
}

// ───────────────────────────── Normalisierung ─────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq)]
enum Ty {
    Bar,
    Line,
    Donut,
    Pattern,
    Badge,
    Icon,
    Monogram,
    Gradient,
    Graph,
}

const TYPES: [(&str, Ty, &str); 9] = [
    ("bar", Ty::Bar, "Balkendiagramm"),
    ("line", Ty::Line, "Liniendiagramm"),
    ("donut", Ty::Donut, "Ringdiagramm"),
    ("pattern", Ty::Pattern, "Muster"),
    ("badge", Ty::Badge, "Badge"),
    ("icon", Ty::Icon, "Symbol"),
    ("monogram", Ty::Monogram, "Monogramm"),
    ("gradient", Ty::Gradient, "Verlauf"),
    ("graph", Ty::Graph, "Workflow"),
];

const ICONS: [(&str, &str); 14] = [
    ("check", "M5 12.5L9.5 17L19 7.5"),
    ("cross", "M6 6L18 18M18 6L6 18"),
    ("play", "M8 5V19L19 12Z"),
    ("pause", "M8 5V19M16 5V19"),
    ("arrow-right", "M4 12H20M14 6L20 12L14 18"),
    ("arrow-left", "M20 12H4M10 6L4 12L10 18"),
    ("star", "M12 3L14.4 9.3L21 9.6L15.8 13.7L17.6 20.2L12 16.5L6.4 20.2L8.2 13.7L3 9.6L9.6 9.3Z"),
    ("heart", "M12 20C12 20 4 15.5 4 9.5A4 4 0 0 1 12 7.5A4 4 0 0 1 20 9.5C20 15.5 12 20 12 20Z"),
    ("menu", "M4 6H20M4 12H20M4 18H20"),
    ("plus", "M12 5V19M5 12H19"),
    ("minus", "M5 12H19"),
    ("info", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM12 11V17M12 7.5V8"),
    ("search", "M4 10.5A6.5 6.5 0 1 0 17 10.5A6.5 6.5 0 1 0 4 10.5ZM15.5 15.5L20 20"),
    ("download", "M12 4V15M7 10L12 15L17 10M5 20H19"),
];

#[derive(Debug, Clone)]
struct Node {
    id: String,
    ty: String,
    label: String,
    xy: Option<(f64, f64)>,
}

#[derive(Debug, Clone)]
struct Edge {
    from: String,
    to: String,
}

/// Normalisierte Spezifikation (alle Felder; nicht benutzte behalten ihren Standardwert).
#[derive(Debug, Clone)]
struct Spec {
    ty: Ty,
    theme: usize,
    vars: bool,
    idprefix: String,
    seed: u64,
    title: String,
    width: f64,
    height: f64,
    data: Vec<f64>,
    series: Vec<Vec<f64>>,
    labels: Vec<String>,
    color: String,
    color2: String,
    values: bool,
    grid: bool,
    heading: bool,
    area: bool,
    dots: bool,
    legend: bool,
    bg: bool,
    thickness: f64,
    center: String,
    kind: &'static str,
    size: f64,
    stroke: f64,
    angle: f64,
    density: f64,
    opacity: f64,
    label: String,
    value: String,
    name: &'static str,
    text: String,
    shape: &'static str,
    from: String,
    to: String,
    via: String,
    cx: f64,
    cy: f64,
    nodes: Vec<Node>,
    edges: Vec<Edge>,
}

/// Feldleser über einem JSON-Objekt (Fehlerfeld = Feldname; `null` und fehlend = Standardwert).
struct Rd<'a> {
    o: &'a Json,
}

impl<'a> Rd<'a> {
    fn get(&self, k: &str) -> Option<&'a Json> {
        match self.o.get(k) {
            None | Some(Json::Null) => None,
            Some(v) => Some(v),
        }
    }

    fn num(&self, k: &str, lo: f64, hi: f64, def: f64) -> R<f64> {
        match self.get(k) {
            None => Ok(def),
            Some(Json::Num(n)) if n.is_finite() => Ok(mn(mx(*n, lo), hi)),
            Some(_) => bad(k, "Zahl erwartet"),
        }
    }

    fn int(&self, k: &str, lo: f64, hi: f64, def: f64) -> R<f64> {
        Ok(self.num(k, lo, hi, def)?.floor())
    }

    fn boolean(&self, k: &str, def: bool) -> R<bool> {
        match self.get(k) {
            None => Ok(def),
            Some(Json::Bool(b)) => Ok(*b),
            Some(_) => bad(k, "true/false erwartet"),
        }
    }

    fn choice(&self, k: &str, choices: &[&'static str], def: &'static str) -> R<&'static str> {
        match self.get(k) {
            None => Ok(def),
            Some(Json::Str(t)) => match choices.iter().find(|c| **c == t.as_str()) {
                Some(c) => Ok(c),
                None => bad(k, &format!("erlaubt: {}", choices.join(", "))),
            },
            Some(_) => bad(k, &format!("erlaubt: {}", choices.join(", "))),
        }
    }

    fn string(&self, k: &str, n: usize, def: &str) -> R<String> {
        match self.get(k) {
            None => Ok(def.to_string()),
            Some(Json::Str(t)) => Ok(clean(t, n)),
            Some(_) => bad(k, "Text erwartet"),
        }
    }

    fn color(&self, k: &str, def: &str, allow_empty: bool) -> R<String> {
        match self.get(k) {
            None => Ok(def.to_string()),
            Some(Json::Str(t)) => {
                if allow_empty && t.is_empty() {
                    return Ok(String::new());
                }
                if TOK.contains(&t.as_str()) {
                    return Ok(t.clone());
                }
                let b = t.as_bytes();
                if (b.len() == 4 || b.len() == 7) && b[0] == b'#' && b[1..].iter().all(|c| c.is_ascii_hexdigit()) {
                    let l = t.to_ascii_lowercase();
                    if l.len() == 4 {
                        let c: Vec<char> = l.chars().collect();
                        return Ok(format!("#{0}{0}{1}{1}{2}{2}", c[1], c[2], c[3]));
                    }
                    return Ok(l);
                }
                bad(k, "Token oder #rgb/#rrggbb")
            }
            Some(_) => bad(k, "Token oder #rgb/#rrggbb"),
        }
    }

    fn list(&self, k: &str, n: usize) -> R<Option<&'a [Json]>> {
        match self.get(k) {
            None => Ok(None),
            Some(Json::Arr(v)) => Ok(Some(&v[..v.len().min(n)])),
            Some(_) => bad(k, "Liste erwartet"),
        }
    }

    fn nums(&self, k: &str, lo: f64, hi: f64, n: usize, def: &[f64]) -> R<Vec<f64>> {
        match self.list(k, n)? {
            None => Ok(def.to_vec()),
            Some(v) => v.iter().map(|e| clamp_num(k, e, lo, hi)).collect(),
        }
    }

    fn series(&self, k: &str, lo: f64, hi: f64, n: usize, def: &[Vec<f64>]) -> R<Vec<Vec<f64>>> {
        match self.list(k, n)? {
            None => Ok(def.to_vec()),
            Some(v) => {
                if let Some(Json::Arr(_)) = v.first() {
                    v.iter()
                        .take(4)
                        .map(|r| match r {
                            Json::Arr(r) => r.iter().take(200).map(|e| clamp_num(k, e, lo, hi)).collect(),
                            _ => bad(k, "ungültiger Wert"),
                        })
                        .collect()
                } else if v.is_empty() {
                    Ok(vec![])
                } else {
                    Ok(vec![v.iter().map(|e| clamp_num(k, e, lo, hi)).collect::<R<Vec<f64>>>()?])
                }
            }
        }
    }

    fn strs(&self, k: &str, n: usize) -> R<Vec<String>> {
        match self.list(k, n)? {
            None => Ok(vec![]),
            Some(v) => v
                .iter()
                .map(|e| match e {
                    Json::Str(t) => Ok(clean(t, 40)),
                    _ => bad(k, "Text erwartet"),
                })
                .collect(),
        }
    }

    fn nodes(&self, k: &str, n: usize, def: &[Node]) -> R<Vec<Node>> {
        let v = match self.list(k, n)? {
            None => return Ok(def.to_vec()),
            Some(v) => v,
        };
        let mut o: Vec<Node> = Vec::new();
        for e in v {
            if !matches!(e, Json::Obj(_)) {
                return bad(k, "ungültiger Wert");
            }
            let r = Rd { o: e };
            let id = r.nstr(k, "id", 32)?;
            let ty = r.nstr(k, "type", 24)?;
            let label = r.nstr(k, "label", 40)?;
            if id.is_empty() || o.iter().any(|q| q.id == id) {
                return bad(k, "id fehlt oder doppelt");
            }
            let xy = if e.get("x").is_some_and(|x| *x != Json::Null) || e.get("y").is_some_and(|y| *y != Json::Null) {
                match (e.get("x"), e.get("y")) {
                    (Some(Json::Num(x)), Some(Json::Num(y))) if x.is_finite() && y.is_finite() => {
                        Some((mn(mx(*x, 0.0), 4000.0), mn(mx(*y, 0.0), 4000.0)))
                    }
                    _ => return bad(k, "x/y: Zahlen"),
                }
            } else {
                None
            };
            o.push(Node { id, ty, label, xy });
        }
        Ok(o)
    }

    fn edges(&self, k: &str, n: usize, def: &[Edge]) -> R<Vec<Edge>> {
        let v = match self.list(k, n)? {
            None => return Ok(def.to_vec()),
            Some(v) => v,
        };
        let mut o = Vec::new();
        for e in v {
            if !matches!(e, Json::Obj(_)) {
                return bad(k, "ungültiger Wert");
            }
            let r = Rd { o: e };
            o.push(Edge { from: r.nstr(k, "from", 32)?, to: r.nstr(k, "to", 32)? });
        }
        Ok(o)
    }

    /// Text in einem Knoten/einer Kante: fehlend = `""`, Nicht-Text → Fehler im Feld `field`.
    fn nstr(&self, field: &str, k: &str, n: usize) -> R<String> {
        match self.o.get(k) {
            None | Some(Json::Null) => Ok(String::new()),
            Some(Json::Str(t)) => Ok(clean(t, n)),
            Some(_) => bad(field, "Text erwartet"),
        }
    }
}

fn clamp_num(k: &str, e: &Json, lo: f64, hi: f64) -> R<f64> {
    match e {
        Json::Num(n) if n.is_finite() => Ok(mn(mx(*n, lo), hi)),
        _ => bad(k, "Zahl erwartet"),
    }
}

fn default_nodes() -> Vec<Node> {
    [("A", "input"), ("B", "model"), ("C", "output")]
        .iter()
        .map(|(i, t)| Node { id: i.to_string(), ty: t.to_string(), label: String::new(), xy: None })
        .collect()
}

fn default_edges() -> Vec<Edge> {
    [("A", "B"), ("B", "C")].iter().map(|(a, b)| Edge { from: a.to_string(), to: b.to_string() }).collect()
}

fn valid_idprefix(p: &str) -> bool {
    let b = p.as_bytes();
    !b.is_empty() && b[0].is_ascii_alphabetic() && b.iter().all(|c| c.is_ascii_alphanumeric() || *c == b'_' || *c == b'-')
}

fn normalize(root: &Json) -> R<Spec> {
    if !matches!(root, Json::Obj(_)) {
        return bad("spec", "Objekt erwartet");
    }
    let (ty, tlabel) = match root.get("type") {
        Some(Json::Str(t)) => match TYPES.iter().find(|x| x.0 == t.as_str()) {
            Some(x) => (x.1, x.2),
            None => return bad("type", "unbekannter Typ"),
        },
        _ => return bad("type", "unbekannter Typ"),
    };
    let r = Rd { o: root };
    let mut sp = Spec {
        ty,
        theme: 0,
        vars: true,
        idprefix: String::new(),
        seed: 1,
        title: String::new(),
        width: 0.0,
        height: 0.0,
        data: vec![],
        series: vec![],
        labels: vec![],
        color: "accent".to_string(),
        color2: "lila".to_string(),
        values: true,
        grid: true,
        heading: true,
        area: true,
        dots: true,
        legend: true,
        bg: true,
        thickness: 0.34,
        center: String::new(),
        kind: "",
        size: 0.0,
        stroke: 0.0,
        angle: 0.0,
        density: 0.0,
        opacity: 0.6,
        label: String::new(),
        value: String::new(),
        name: "check",
        text: String::new(),
        shape: "circle",
        from: "accent".to_string(),
        to: "lila".to_string(),
        via: String::new(),
        cx: 50.0,
        cy: 50.0,
        nodes: vec![],
        edges: vec![],
    };
    // gemeinsame Felder, in Tabellenreihenfolge
    let th = r.choice("theme", &THEME_NAMES, "master")?;
    sp.theme = THEME_NAMES.iter().position(|x| *x == th).unwrap_or(0);
    sp.vars = r.choice("colors", &["vars", "static"], "vars")? == "vars";
    sp.idprefix = r.string("idprefix", 32, "sk")?;
    sp.seed = r.int("seed", 0.0, 4294967295.0, 1.0)? as u64;
    sp.title = r.string("title", 120, tlabel)?;
    let mut nodes_given = false;
    let mut edges_given = false;
    match ty {
        Ty::Bar => {
            sp.width = r.int("width", 160.0, 2400.0, 480.0)?;
            sp.height = r.int("height", 120.0, 2400.0, 280.0)?;
            sp.data = r.nums("data", -1e9, 1e9, 64, &[12.0, 19.0, 7.0, 15.0, 22.0, 10.0])?;
            sp.labels = r.strs("labels", 64)?;
            sp.color = r.color("color", "accent", false)?;
            sp.values = r.boolean("values", true)?;
            sp.grid = r.boolean("grid", true)?;
            sp.heading = r.boolean("heading", true)?;
        }
        Ty::Line => {
            sp.width = r.int("width", 160.0, 2400.0, 480.0)?;
            sp.height = r.int("height", 120.0, 2400.0, 280.0)?;
            sp.series = r.series("data", -1e9, 1e9, 200, &[vec![4.0, 9.0, 6.0, 12.0, 10.0, 15.0, 13.0]])?;
            sp.labels = r.strs("labels", 200)?;
            sp.color = r.color("color", "accent", false)?;
            sp.area = r.boolean("area", true)?;
            sp.dots = r.boolean("dots", true)?;
            sp.grid = r.boolean("grid", true)?;
            sp.heading = r.boolean("heading", true)?;
        }
        Ty::Donut => {
            sp.width = r.int("width", 160.0, 2400.0, 360.0)?;
            sp.height = r.int("height", 120.0, 2400.0, 240.0)?;
            sp.data = r.nums("data", 0.0, 1e9, 12, &[45.0, 30.0, 25.0])?;
            sp.labels = r.strs("labels", 12)?;
            sp.thickness = r.num("thickness", 0.1, 0.9, 0.34)?;
            sp.center = r.string("center", 16, "")?;
            sp.legend = r.boolean("legend", true)?;
            sp.heading = r.boolean("heading", true)?;
        }
        Ty::Pattern => {
            sp.width = r.int("width", 16.0, 2400.0, 240.0)?;
            sp.height = r.int("height", 16.0, 2400.0, 240.0)?;
            sp.kind = r.choice("kind", &["grid", "dots", "stripes", "waves", "confetti"], "grid")?;
            sp.size = r.int("size", 4.0, 200.0, 24.0)?;
            sp.stroke = r.num("stroke", 0.5, 12.0, 1.5)?;
            sp.angle = r.int("angle", -90.0, 90.0, 45.0)?;
            sp.density = r.int("density", 1.0, 400.0, 60.0)?;
            sp.opacity = r.num("opacity", 0.05, 1.0, 0.6)?;
            sp.color = r.color("color", "accent", false)?;
            sp.bg = r.boolean("bg", true)?;
        }
        Ty::Badge => {
            sp.height = r.int("height", 16.0, 64.0, 24.0)?;
            sp.label = r.string("label", 32, "DkZ")?;
            sp.value = r.string("value", 32, "aktiv")?;
            sp.color = r.color("color", "accent", false)?;
        }
        Ty::Icon => {
            let names: Vec<&'static str> = ICONS.iter().map(|x| x.0).collect();
            sp.name = r.choice("name", &names, "check")?;
            sp.size = r.int("size", 12.0, 512.0, 48.0)?;
            sp.color = r.color("color", "accent", false)?;
            sp.stroke = r.num("stroke", 1.0, 4.0, 2.0)?;
            sp.bg = r.boolean("bg", false)?;
        }
        Ty::Monogram => {
            sp.text = r.string("text", 3, "DZ")?;
            sp.size = r.int("size", 24.0, 512.0, 96.0)?;
            sp.shape = r.choice("shape", &["circle", "square", "hex"], "circle")?;
            sp.color = r.color("color", "accent", false)?;
            sp.color2 = r.color("color2", "lila", false)?;
        }
        Ty::Gradient => {
            sp.width = r.int("width", 16.0, 2400.0, 480.0)?;
            sp.height = r.int("height", 16.0, 2400.0, 240.0)?;
            sp.kind = r.choice("kind", &["linear", "radial"], "linear")?;
            sp.from = r.color("from", "accent", false)?;
            sp.to = r.color("to", "lila", false)?;
            sp.via = r.color("via", "", true)?;
            sp.angle = r.int("angle", 0.0, 360.0, 45.0)?;
            sp.cx = r.num("cx", 0.0, 100.0, 50.0)?;
            sp.cy = r.num("cy", 0.0, 100.0, 50.0)?;
            sp.text = r.string("text", 60, "")?;
        }
        Ty::Graph => {
            sp.width = r.int("width", 0.0, 2400.0, 0.0)?;
            sp.height = r.int("height", 0.0, 2400.0, 0.0)?;
            sp.nodes = r.nodes("nodes", 60, &default_nodes())?;
            sp.edges = r.edges("edges", 120, &default_edges())?;
            sp.heading = r.boolean("heading", true)?;
            nodes_given = r.get("nodes").is_some();
            edges_given = r.get("edges").is_some();
        }
    }
    if ty == Ty::Graph && nodes_given && !edges_given {
        sp.edges = vec![];
    }
    if !valid_idprefix(&sp.idprefix) {
        return bad("idprefix", "[A-Za-z][A-Za-z0-9_-]*");
    }
    if sp.title.is_empty() {
        sp.title = tlabel.to_string();
    }
    if ty == Ty::Graph {
        let has = |id: &str| sp.nodes.iter().any(|n| n.id == id);
        if sp.edges.iter().any(|e| !has(&e.from) || !has(&e.to)) {
            return bad("edges", "unbekannter Knoten");
        }
    }
    Ok(sp)
}

// ───────────────────────────── Achsen ─────────────────────────────

fn ticks(lo: f64, hi: f64) -> (f64, f64, f64, f64) {
    let raw = (hi - lo) / 4.0;
    let mut mag = 1.0_f64;
    while mag * 10.0 <= raw {
        mag *= 10.0;
    }
    while mag > raw {
        mag /= 10.0;
    }
    let r = raw / mag;
    let st = mag * (if r <= 1.0 { 1.0 } else if r <= 2.0 { 2.0 } else if r <= 5.0 { 5.0 } else { 10.0 });
    let lo = (lo / st).floor() * st;
    let hi = (hi / st).ceil() * st;
    (lo, hi, st, ((hi - lo) / st + 0.5).floor())
}

fn maxlen(a: &[String]) -> usize {
    a.iter().map(|x| len(x)).max().unwrap_or(0)
}

struct Axes {
    x0: f64,
    x1: f64,
    y0: f64,
    y1: f64,
    lo: f64,
    hi: f64,
}

impl Axes {
    fn y(&self, v: f64) -> f64 {
        self.y0 + (self.hi - v) * (self.y1 - self.y0) / (self.hi - self.lo)
    }
}

fn axes(cx: &Cx, s: &Spec, w: f64, h: f64, lo: f64, hi: f64, o: &mut Vec<String>) -> Axes {
    let (mut lo, mut hi) = (lo, hi);
    if hi - lo < 0.01 {
        lo -= 1.0;
        hi += 1.0;
    }
    let t = ticks(lo, hi);
    let y0 = 16.0 + (if s.heading { 26.0 } else { 0.0 }) + 8.0;
    let y1 = h - 16.0 - (if s.labels.is_empty() { 0.0 } else { 20.0 });
    let x1 = w - 16.0;
    lo = t.0;
    hi = t.1;
    let steps = if t.3 >= 0.0 { t.3 as usize } else { 0 };
    let tl: Vec<String> = (0..=steps).map(|i| short(lo + i as f64 * t.2)).collect();
    let x0 = 16.0 + (if s.grid { maxlen(&tl) as f64 * 6.6 + 8.0 } else { 0.0 });
    let ax = Axes { x0, x1, y0, y1, lo, hi };
    if s.grid {
        for (i, label) in tl.iter().enumerate() {
            let y = ax.y(lo + i as f64 * t.2);
            let mut a: At = vec![("x1", N(x0)), ("y1", N(y)), ("x2", N(x1)), ("y2", N(y))];
            a.extend(cx.ln());
            a.push(("stroke-dasharray", if cx.k { s_("2 4") } else { X }));
            o.push(el("line", &a, None));
            o.push(txt(x0 - 8.0, y + 4.0, 11.0, &cx.cm(), label, Some("end"), None, vec![]));
        }
    }
    ax
}

fn s_(t: &str) -> Av {
    s(t)
}

// ───────────────────────────── Typen ─────────────────────────────

fn r_bar(cx: &Cx, s: &Spec, w: f64, h: f64) -> Vec<String> {
    let d = &s.data;
    let n = d.len();
    let mut o = cx.base(s, w, h);
    if n == 0 {
        o.push(cx.none(w / 2.0, h / 2.0 + 4.0, None));
        return o;
    }
    let lo = d.iter().fold(0.0, |a, &v| mn(a, v));
    let hi = d.iter().fold(0.0, |a, &v| mx(a, v));
    let ax = axes(cx, s, w, h, lo, hi, &mut o);
    let nf = n as f64;
    let sl = (ax.x1 - ax.x0) / nf;
    let bw = mn(sl * 0.64, 64.0);
    let c = cx.c(&s.color);
    for (i, &v) in d.iter().enumerate() {
        let x = ax.x0 + i as f64 * sl + sl / 2.0;
        let yv = ax.y(v);
        let yz = ax.y(0.0);
        let lb = short(v);
        o.push(rc(N(x - bw / 2.0), N(mn(yv, yz)), bw, (yv - yz).abs(), N(mn(cx.th.radius / 3.0, bw / 2.0)), vec![("fill", S(c.clone()))]));
        if s.values && (len(&lb) as f64) * 6.6 <= sl {
            let ty = if v < 0.0 { mn(yv + 14.0, ax.y1 - 4.0) } else { yv - 6.0 };
            o.push(txt(x, ty, 11.0, &cx.ct(), &lb, Some("middle"), None, vec![]));
        }
        if i < s.labels.len() {
            o.push(txt(x, h - 16.0 - 4.0, 11.0, &cx.cm(), &trunc(&s.labels[i], fit(sl - 4.0, 11.0)), Some("middle"), None, vec![]));
        }
    }
    o
}

fn rj() -> At {
    vec![("stroke-linejoin", s("round")), ("stroke-linecap", s("round"))]
}

fn r_line(cx: &Cx, s: &Spec, w: f64, h: f64) -> Vec<String> {
    let sr = &s.series;
    let mut o = cx.base(s, w, h);
    let all: Vec<f64> = sr.iter().flatten().copied().collect();
    let m = sr.iter().map(|r| r.len()).max().unwrap_or(0);
    if m == 0 {
        o.push(cx.none(w / 2.0, h / 2.0 + 4.0, None));
        return o;
    }
    let lo = all.iter().copied().fold(f64::INFINITY, mn);
    let hi = all.iter().copied().fold(f64::NEG_INFINITY, mx);
    let ax = axes(cx, s, w, h, lo, hi, &mut o);
    let (x0, x1, y1) = (ax.x0, ax.x1, ax.y1);
    let mf = m as f64;
    let xf = |j: usize| if m == 1 { (x0 + x1) / 2.0 } else { x0 + j as f64 * (x1 - x0) / (mf - 1.0) };
    let ls: Vec<String> = s.labels.iter().take(m).map(|l| trunc(l, 12.0)).collect();
    let lw = maxlen(&ls) as f64 * 6.6 + 8.0;
    let mut ev = 1.0_f64;
    while m > 1 && ev * (x1 - x0) / (mf - 1.0) < lw {
        ev += 1.0;
    }
    for (j, l) in ls.iter().enumerate() {
        if (j as f64) % ev == 0.0 {
            let anchor = if m > 1 && j == 0 { "start" } else if m > 1 && j == m - 1 { "end" } else { "middle" };
            o.push(txt(xf(j), h - 16.0 - 4.0, 11.0, &cx.cm(), l, Some(anchor), None, vec![]));
        }
    }
    for (i, r) in sr.iter().enumerate() {
        if r.is_empty() {
            continue;
        }
        let c = cx.c(if i > 0 { cx.sc(i) } else { &s.color });
        let mut p = String::new();
        for (j, &v) in r.iter().enumerate() {
            p.push_str(&dj(&[Da::T(if j > 0 { "L" } else { "M" }), Da::V(xf(j)), Da::V(ax.y(v))]));
        }
        if s.area && sr.len() == 1 && !cx.k {
            let pp = p.clone() + &d!("L", xf(r.len() - 1), y1, "L", xf(0), y1, "Z");
            o.push(pa(pp, vec![("fill", S(c.clone())), ("fill-opacity", N(0.16))]));
        }
        let mut a: At = vec![("fill", s_("none")), ("stroke", S(c.clone())), ("stroke-width", N(2.0))];
        a.extend(rj());
        a.push(("stroke-dasharray", if cx.k && i % 2 == 1 { s_("6 4") } else { X }));
        o.push(pa(p, a));
        if s.dots && m <= 40 {
            for (j, &v) in r.iter().enumerate() {
                o.push(ci(xf(j), ax.y(v), 3.0, vec![("fill", S(cx.c("card"))), ("stroke", S(c.clone())), ("stroke-width", N(2.0))]));
            }
        }
    }
    o
}

fn r_donut(cx: &Cx, s: &Spec, w: f64, h: f64) -> Vec<String> {
    use std::f64::consts::PI;
    let d = &s.data;
    let n = d.len();
    let mut o = cx.base(s, w, h);
    let tot = d.iter().fold(0.0, |q, v| q + v);
    let top = 16.0 + (if s.heading { 26.0 } else { 0.0 });
    let ah = h - top - 16.0;
    let aw = if s.legend { (w - 2.0 * 16.0) * 0.5 } else { w - 2.0 * 16.0 };
    let r = mn(ah, aw) / 2.0;
    let cxx = if s.legend { 16.0 + aw / 2.0 } else { w / 2.0 };
    let cy = top + ah / 2.0;
    let ri = r * (1.0 - s.thickness);
    let ring = |q: f64| d!("M", cxx, cy - q, "A", q, q, 0, 1, 1, cxx, cy + q, "A", q, q, 0, 1, 1, cxx, cy - q, "Z");
    let pt = |q: f64, a: f64| d!(cxx + q * a.cos(), cy + q * a.sin());
    let col = |i: usize| if cx.k { if i % 2 == 1 { "bg" } else { "accent" } } else { cx.sc(i) };
    #[allow(clippy::neg_cmp_op_on_partial_ord)]
    let no_total = !(tot > 0.0); // auch NaN gilt als „keine Summe“
    if no_total {
        o.push(pa(
            ring(r) + &ring(ri),
            vec![("fill", S(cx.cm())), ("fill-opacity", cx.op(0.25)), ("fill-rule", s_("evenodd"))],
        ));
        o.push(cx.none(cxx, cy + 4.0, Some(mn(13.0, ri / 4.5))));
        return o;
    }
    let mut cum = 0.0;
    let fs = mn(mx((ri * 0.45).floor(), 10.0), 36.0);
    let k = if s.legend { mn(n as f64, (ah / 20.0).floor()) } else { 0.0 };
    let k = if k > 0.0 { k as usize } else { 0 };
    let lx = 16.0 + aw + 16.0;
    let ly = cy - k as f64 * 10.0;
    for (i, &v) in d.iter().enumerate() {
        if v <= 0.0 {
            continue;
        }
        let a0 = -PI / 2.0 + 2.0 * PI * cum / tot;
        let a1 = -PI / 2.0 + 2.0 * PI * (cum + v) / tot;
        let l = if a1 - a0 > PI { 1.0 } else { 0.0 };
        let full = v >= tot;
        cum += v;
        let dd = if full {
            ring(r) + &ring(ri)
        } else {
            "M".to_string()
                + &pt(r, a0)
                + &d!("A", r, r, 0, l, 1)
                + " "
                + &pt(r, a1)
                + "L"
                + &pt(ri, a1)
                + &d!("A", ri, ri, 0, l, 0)
                + " "
                + &pt(ri, a0)
                + "Z"
        };
        o.push(pa(
            dd,
            vec![
                ("fill", S(cx.c(col(i)))),
                ("fill-rule", if full { s_("evenodd") } else { X }),
                ("stroke", S(cx.c(if cx.k { "accent" } else { "card" }))),
                ("stroke-width", N(if cx.k { 1.0 } else { 2.0 })),
            ],
        ));
    }
    let center = if s.center.is_empty() { short(tot) } else { s.center.clone() };
    o.push(txt(cxx, cy + fs * 0.35, fs, &cx.ct(), &trunc(&center, fit(ri * 2.0 - 8.0, fs)), Some("middle"), Some(700.0), vec![]));
    for (i, &di) in d.iter().enumerate().take(k) {
        let y = ly + i as f64 * 20.0;
        o.push(rc(N(lx), N(y + 5.0), 10.0, 10.0, N(2.0), vec![("fill", S(cx.c(col(i)))), ("stroke", if cx.k { S(cx.c("accent")) } else { X })]));
        let name = if i < s.labels.len() { s.labels[i].clone() } else { format!("Teil {}", i + 1) };
        o.push(txt(lx + 16.0, y + 14.0, 12.0, &cx.ct(), &trunc(&name, fit(w - 16.0 - lx - 68.0, 12.0)), None, None, vec![]));
        let pct = fmt((di * 1000.0 / tot + 0.5).floor() / 10.0) + "%";
        o.push(txt(w - 16.0, y + 14.0, 12.0, &cx.cm(), &pct, Some("end"), None, vec![]));
    }
    o
}

fn r_pattern(cx: &Cx, s: &Spec, w: f64, h: f64) -> Vec<String> {
    let z = s.size;
    let c = cx.c(&s.color);
    let id = &s.idprefix;
    let mut o = if s.bg { cx.card(w, h) } else { vec![] };
    if s.kind == "confetti" {
        let mut rng = Prng::new(s.seed);
        let mut g = String::new();
        let count = s.density as usize;
        for _ in 0..count {
            let x = rng.next_f64() * w;
            let y = rng.next_f64() * h;
            let k = (rng.next_f64() * 2.0).floor();
            let f = cx.c(cx.th.series[((rng.next_f64() * cx.th.series.len() as f64).floor() as usize) % cx.th.series.len()]);
            let q = z * 0.3 + rng.next_f64() * z * 0.4;
            let tr = "rotate(".to_string() + &d!((rng.next_f64() * 360.0).floor(), x, y) + ")";
            if k != 0.0 {
                g.push_str(&rc(N(x - q / 2.0), N(y - q * 0.2), q, q * 0.4, N(1.0), vec![("fill", S(f)), ("transform", S(tr))]));
            } else {
                g.push_str(&ci(x, y, q / 2.0, vec![("fill", S(f))]));
            }
        }
        o.push(df(&el(
            "clipPath",
            &[("id", S(format!("{}-clip", id)))],
            Some(&rc(X, X, w, h, N(cx.th.radius), vec![])),
        )));
        o.push(el("g", &[("clip-path", S(format!("url(#{}-clip)", id))), ("opacity", cx.op(s.opacity))], Some(&g)));
        return o;
    }
    let tw = if s.kind == "waves" { z * 2.0 } else { z };
    let m = z / 2.0;
    let ln = || -> At { vec![("fill", s_("none")), ("stroke", S(c.clone())), ("stroke-width", N(s.stroke))] };
    let cell = match s.kind {
        "grid" => pa(d!("M", z, 0, "L", 0, 0, "L", 0, z), ln()),
        "dots" => ci(m, m, s.stroke, vec![("fill", S(c.clone()))]),
        "stripes" => rc(X, X, s.stroke, z, X, vec![("fill", S(c.clone()))]),
        _ => pa(d!("M", 0, m, "Q", m, 0, z, m, "T", tw, m), ln()),
    };
    o.push(df(&el(
        "pattern",
        &[
            ("id", S(format!("{}-pat", id))),
            ("width", N(tw)),
            ("height", N(z)),
            ("patternUnits", s_("userSpaceOnUse")),
            ("patternTransform", if s.kind == "stripes" { S(format!("rotate({})", fmt(s.angle))) } else { X }),
        ],
        Some(&cell),
    )));
    o.push(rc(X, X, w, h, N(cx.th.radius), vec![("fill", S(format!("url(#{}-pat)", id))), ("opacity", cx.op(s.opacity))]));
    o
}

/// Rückgabe der Typen mit eigener Hülle: `(Breite, Höhe, viewBox oder leer, Elemente)`.
type Shell = (f64, f64, String, Vec<String>);

fn r_badge(cx: &Cx, s: &Spec) -> Shell {
    let h = s.height;
    let fs = h * 0.46;
    let cw = fs * 0.6;
    let nl = len(&s.label);
    let lw = if nl > 0 { nl as f64 * cw + h * 0.8 } else { 0.0 };
    let vw = len(&s.value) as f64 * cw + h * 0.8;
    let w = lw + vw;
    let r = if cx.th.radius == 20.0 { h / 2.0 } else { mn(cx.th.radius, h) / 2.0 };
    let c = cx.c(&s.color);
    let y = h / 2.0 + fs * 0.35;
    let mut a: At = vec![("fill", S(cx.c("elevated")))];
    a.extend(cx.ln());
    let mut o = vec![cx.fr(w, h, r, a), rc(N(lw), X, vw, h, N(r), vec![("fill", S(c.clone()))])];
    if lw != 0.0 && r != 0.0 {
        o.push(rc(N(lw), X, mn(r, vw), h, X, vec![("fill", S(c.clone()))]));
    }
    if nl > 0 {
        o.push(txt(lw / 2.0, y, fs, &cx.ct(), &s.label, Some("middle"), Some(500.0), vec![]));
    }
    o.push(txt(lw + vw / 2.0, y, fs, cx.ink(&s.color), &s.value, Some("middle"), Some(700.0), vec![]));
    (w, h, String::new(), o)
}

fn r_icon(cx: &Cx, s: &Spec) -> Shell {
    let mut o = vec![];
    if s.bg {
        let mut a: At = vec![("fill", S(cx.c("card")))];
        a.extend(cx.ln());
        a.push(("stroke-width", N(0.5)));
        o.push(rc(N(-4.0), N(-4.0), 32.0, 32.0, N(cx.th.radius / 2.0), a));
    }
    let path = ICONS.iter().find(|x| x.0 == s.name).map_or("", |x| x.1);
    let mut a: At = vec![("fill", s_("none")), ("stroke", S(cx.c(&s.color))), ("stroke-width", N(s.stroke))];
    a.extend(rj());
    o.push(pa(path.to_string(), a));
    (s.size, s.size, (if s.bg { "-4 -4 32 32" } else { "0 0 24 24" }).to_string(), o)
}

fn r_monogram(cx: &Cx, s: &Spec) -> Shell {
    let z = s.size;
    let c = z / 2.0;
    let q = c - 1.0;
    let k = 0.8660254037844386;
    let n = len(&s.text);
    let fs = z * (if n < 2 { 0.5 } else if n < 3 { 0.42 } else { 0.34 });
    let id = format!("{}-grad", s.idprefix);
    let fill = if cx.k { cx.c("accent") } else { format!("url(#{})", id) };
    let mut o = vec![];
    if !cx.k {
        let stops = el("stop", &[("offset", N(0.0)), ("stop-color", S(cx.c(&s.color)))], None)
            + &el("stop", &[("offset", N(1.0)), ("stop-color", S(cx.c(&s.color2)))], None);
        o.push(df(&el(
            "linearGradient",
            &[("id", S(id.clone())), ("x1", N(0.0)), ("y1", N(0.0)), ("x2", N(1.0)), ("y2", N(1.0))],
            Some(&stops),
        )));
    }
    let f: At = vec![("fill", S(fill))];
    o.push(match s.shape {
        "circle" => ci(c, c, q, f),
        "square" => rc(N(1.0), N(1.0), z - 2.0, z - 2.0, N(if cx.th.radius != 0.0 { z * 0.22 } else { 0.0 }), f),
        _ => pa(d!("M", c, 1, "L", c + q * k, c - q / 2.0, "L", c + q * k, c + q / 2.0, "L", c, z - 1.0, "L", c - q * k, c + q / 2.0, "L", c - q * k, c - q / 2.0, "Z"), f),
    });
    if n > 0 {
        o.push(txt(c, c + fs * 0.35, fs, cx.ink(&s.color), &s.text, Some("middle"), Some(700.0), vec![]));
    }
    (z, z, String::new(), o)
}

fn r_gradient(cx: &Cx, s: &Spec, w: f64, h: f64) -> Vec<String> {
    use std::f64::consts::PI;
    let id = format!("{}-grad", s.idprefix);
    let a = s.angle * PI / 180.0;
    let cs = a.cos() * 50.0;
    let sn = a.sin() * 50.0;
    let pc = |v: f64| fmt(v) + "%";
    let mut st = String::new();
    let mut stop = |offset: f64, t: &str| st.push_str(&el("stop", &[("offset", N(offset)), ("stop-color", S(cx.c(t)))], None));
    if cx.k {
        stop(0.0, "accent");
        stop(0.5, "accent");
        stop(0.5, "bg");
        stop(1.0, "bg");
    } else {
        stop(0.0, &s.from);
        if !s.via.is_empty() {
            stop(0.5, &s.via);
        }
        stop(1.0, &s.to);
    }
    let grad = if s.kind == "linear" {
        el(
            "linearGradient",
            &[("id", S(id.clone())), ("x1", S(pc(50.0 - cs))), ("y1", S(pc(50.0 - sn))), ("x2", S(pc(50.0 + cs))), ("y2", S(pc(50.0 + sn)))],
            Some(&st),
        )
    } else {
        el("radialGradient", &[("id", S(id.clone())), ("cx", S(pc(s.cx))), ("cy", S(pc(s.cy))), ("r", s_("75%"))], Some(&st))
    };
    let mut o = vec![
        df(&grad),
        cx.fr(w, h, cx.th.radius, vec![("fill", S(format!("url(#{})", id))), ("stroke", if cx.k { S(cx.c("accent")) } else { X })]),
    ];
    let tx = trunc(&s.text, fit(w - 48.0, 12.0));
    if !tx.is_empty() {
        let fs = mn(mn(mx(h * 0.16, 12.0), 72.0), (w - 48.0) / (len(&tx) as f64 * 0.6));
        let ex: At = if cx.k {
            vec![("stroke", S(cx.c("bg"))), ("stroke-width", N(3.0)), ("paint-order", s_("stroke"))]
        } else {
            vec![]
        };
        let fill = if cx.k { cx.c("accent") } else { cx.ink(&s.from).to_string() };
        o.push(txt(w / 2.0, h / 2.0 + fs * 0.35, fs, &fill, &tx, Some("middle"), Some(700.0), ex));
    }
    o
}

fn r_graph(cx: &Cx, s: &Spec) -> Shell {
    let nd = &s.nodes;
    let nn = nd.len();
    let (nw, nh, gx, gy) = (136.0_f64, 48.0_f64, 56.0_f64, 20.0_f64);
    let top = 16.0 + (if s.heading { 28.0 } else { 0.0 });
    let id = format!("{}-arrow", s.idprefix);
    let ix = |k: &str| nd.iter().position(|n| n.id == k).unwrap_or(0);
    let mut pos: Vec<(f64, f64)> = vec![];
    let (cw, mut ch);
    if nn == 0 {
        cw = 320.0;
        ch = top + 80.0;
    } else if nd.iter().all(|n| n.xy.is_some()) {
        pos = nd.iter().map(|n| n.xy.map_or((16.0, top), |(x, y)| (16.0 + x, top + y))).collect();
        let xm = nd.iter().fold(f64::NEG_INFINITY, |a, n| mx(a, n.xy.map_or(0.0, |p| p.0)));
        let ym = nd.iter().fold(f64::NEG_INFINITY, |a, n| mx(a, n.xy.map_or(0.0, |p| p.1)));
        cw = 16.0 + xm + nw + 16.0;
        ch = top + ym + nh + 16.0;
    } else {
        let mut rk = vec![0usize; nn];
        let mut go = true;
        let mut it = 0usize;
        while go {
            let more = it < nn;
            it += 1;
            if !more {
                break;
            }
            go = false;
            for e in &s.edges {
                let (a, b) = (ix(&e.from), ix(&e.to));
                if a != b && rk[b] < rk[a] + 1 && rk[a] + 1 < nn {
                    rk[b] = rk[a] + 1;
                    go = true;
                }
            }
        }
        let layers = rk.iter().copied().max().unwrap_or(0) + 1;
        let mut cnt = vec![0usize; layers];
        let mut idx = vec![0usize; nn];
        for (i, &r) in rk.iter().enumerate() {
            idx[i] = cnt[r];
            cnt[r] += 1;
        }
        let mc = cnt.iter().copied().max().unwrap_or(1) as f64;
        let hh = mc * nh + (mc - 1.0) * gy;
        pos = rk
            .iter()
            .enumerate()
            .map(|(i, &r)| {
                let c = cnt[r] as f64;
                (16.0 + r as f64 * (nw + gx), top + (hh - (c * nh + (c - 1.0) * gy)) / 2.0 + idx[i] as f64 * (nh + gy))
            })
            .collect();
        let lc = cnt.len() as f64;
        cw = 2.0 * 16.0 + lc * nw + (lc - 1.0) * gx;
        ch = top + hh + 16.0;
    }
    let p = |k: &str| pos.get(ix(k)).copied().unwrap_or((0.0, 0.0));
    if s.edges.iter().any(|e| e.from != e.to && p(&e.to).0 <= p(&e.from).0 + nw) {
        ch += 28.0;
    }
    let mut o = cx.card(cw, ch);
    o.extend(cx.head(s, cw));
    if nn == 0 {
        o.push(cx.none(160.0, top + 36.0, None));
    } else {
        o.push(df(&el(
            "marker",
            &[
                ("id", S(id.clone())),
                ("viewBox", s_("0 0 10 10")),
                ("refX", N(9.0)),
                ("refY", N(5.0)),
                ("markerWidth", N(7.0)),
                ("markerHeight", N(7.0)),
                ("orient", s_("auto-start-reverse")),
            ],
            Some(&pa("M0 0L10 5L0 10Z".to_string(), vec![("fill", S(cx.cm()))])),
        )));
        for e in &s.edges {
            if e.from == e.to {
                continue;
            }
            let ((ax, ay), (bx, by)) = (p(&e.from), p(&e.to));
            let bk = bx <= ax + nw;
            let sx = if bk { ax + nw / 2.0 } else { ax + nw };
            let sy = ay + (if bk { nh } else { nh / 2.0 });
            let tx = if bk { bx + nw / 2.0 } else { bx };
            let ty = by + (if bk { nh } else { nh / 2.0 });
            let dx = if bk { 0.0 } else { mx(24.0, (tx - sx) / 2.0) };
            let dy = if bk { 40.0 } else { 0.0 };
            o.push(pa(
                d!("M", sx, sy, "C", sx + dx, sy + dy, tx - dx, ty + dy, tx, ty),
                vec![
                    ("fill", s_("none")),
                    ("stroke", S(cx.cm())),
                    ("stroke-width", N(1.5)),
                    ("stroke-dasharray", if bk { s_("4 4") } else { X }),
                    ("marker-end", S(format!("url(#{})", id))),
                ],
            ));
        }
        for (i, n) in nd.iter().enumerate() {
            let (x, y) = pos.get(i).copied().unwrap_or((0.0, 0.0));
            let sum: u64 = n.ty.chars().map(|c| c as u64).sum();
            let c = cx.c(cx.sc((sum % cx.th.series.len() as u64) as usize));
            o.push(rc(N(x), N(y), nw, nh, N(mn(cx.th.radius, 10.0)), vec![("fill", S(cx.c("elevated"))), ("stroke", S(c.clone())), ("stroke-opacity", cx.op(0.45))]));
            o.push(rc(N(x + 6.0), N(y + 10.0), 3.0, nh - 20.0, N(1.5), vec![("fill", S(c.clone()))]));
            let lab = if n.label.is_empty() { &n.id } else { &n.label };
            o.push(txt(x + 16.0, y + 21.0, 12.0, &cx.ct(), &trunc(lab, fit(nw - 22.0, 12.0)), None, Some(600.0), vec![]));
            if !n.ty.is_empty() {
                o.push(txt(x + 16.0, y + 36.0, 10.0, &cx.cm(), &trunc(&n.ty, fit(nw - 22.0, 10.0)), None, None, vec![]));
            }
        }
    }
    (if s.width != 0.0 { s.width } else { cw }, if s.height != 0.0 { s.height } else { ch }, d!(0, 0, cw, ch), o)
}

// ───────────────────────────── Einstieg ─────────────────────────────

/// Erzeugt das SVG aus einem geparsten JSON-Wert.
pub fn generate(spec: &Json) -> Result<String, SpecError> {
    let sp = normalize(spec)?;
    let th = &THEMES[sp.theme];
    let cx = Cx { th, vars: sp.vars, k: sp.theme == 2 };
    let (w, h, vb, body): Shell = match sp.ty {
        Ty::Bar => (sp.width, sp.height, String::new(), r_bar(&cx, &sp, sp.width, sp.height)),
        Ty::Line => (sp.width, sp.height, String::new(), r_line(&cx, &sp, sp.width, sp.height)),
        Ty::Donut => (sp.width, sp.height, String::new(), r_donut(&cx, &sp, sp.width, sp.height)),
        Ty::Pattern => (sp.width, sp.height, String::new(), r_pattern(&cx, &sp, sp.width, sp.height)),
        Ty::Gradient => (sp.width, sp.height, String::new(), r_gradient(&cx, &sp, sp.width, sp.height)),
        Ty::Badge => r_badge(&cx, &sp),
        Ty::Icon => r_icon(&cx, &sp),
        Ty::Monogram => r_monogram(&cx, &sp),
        Ty::Graph => r_graph(&cx, &sp),
    };
    let vb = if vb.is_empty() { d!(0, 0, w, h) } else { vb };
    let ti = esc(&sp.title);
    let font = if cx.vars { format!("var(--font-main, {})", th.font) } else { th.font.to_string() };
    Ok(format!(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{}\" height=\"{}\" viewBox=\"{}\" role=\"img\" aria-label=\"{}\" font-family=\"{}\">\n<title>{}</title>\n{}\n</svg>",
        fmt(w),
        fmt(h),
        vb,
        ti,
        font,
        ti,
        body.join("\n")
    ))
}

/// Parst `text` als JSON und erzeugt das SVG. Ungültiges JSON → Fehlerfeld `spec`.
pub fn generate_str(text: &str) -> Result<String, SpecError> {
    match json::parse(text) {
        Ok(v) => generate(&v),
        Err(e) => bad("spec", &e),
    }
}

/// Wie [`generate_str`] für Rohbytes: ungültiges UTF-8 wird wie in Node durch U+FFFD ersetzt.
pub fn generate_bytes(bytes: &[u8]) -> Result<String, SpecError> {
    generate_str(&String::from_utf8_lossy(bytes))
}

#[cfg(test)]
mod tests;
