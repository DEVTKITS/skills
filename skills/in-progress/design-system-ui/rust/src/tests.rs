//! Unit-Tests: fmt, PRNG, Text, JSON-Parser, Normalisierung, Golden-Parität und Fuzz.
use super::json::{parse, Json, MAX_DEPTH};
use super::*;
use std::path::PathBuf;
use std::time::Instant;

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..")
}

fn gen(s: &str) -> Result<String, SpecError> {
    generate_str(s)
}

fn field(s: &str) -> String {
    gen(s).expect_err("Fehler erwartet").field
}

// ───────────── fmt ─────────────

#[test]
fn fmt_grenzfaelle_aus_der_spec() {
    let cases: [(f64, &str); 8] = [
        (0.005, "0.01"),
        (1.005, "1"),
        (-0.005, "-0.01"),
        (2.675, "2.68"),
        (1e-7, "0"),
        (123456.789, "123456.79"),
        (-0.0, "0"),
        (0.07, "0.07"),
    ];
    for (n, e) in cases {
        assert_eq!(fmt(n), e, "fmt({})", n);
    }
}

#[test]
fn fmt_weitere_werte() {
    assert_eq!(fmt(0.0), "0");
    assert_eq!(fmt(-0.004), "0");
    assert_eq!(fmt(5.0), "5");
    assert_eq!(fmt(0.5), "0.5");
    assert_eq!(fmt(0.25), "0.25");
    assert_eq!(fmt(0.05), "0.05");
    assert_eq!(fmt(-12.3456), "-12.35");
    assert_eq!(fmt(100.0), "100");
    assert_eq!(fmt(1e9), "1000000000");
    assert_eq!(fmt(f64::NAN), "0");
    assert_eq!(fmt(f64::INFINITY), "0");
    assert_eq!(fmt(f64::NEG_INFINITY), "0");
}

#[test]
fn fmt_eigenschaften_ueber_viele_werte() {
    // Unabhängige Gegenrechnung: dieselbe Regel mit ganzzahliger Nachbearbeitung der Zeichenkette.
    let mut r = Prng::new(7);
    for _ in 0..20000 {
        let n = (r.next_f64() - 0.5) * 10f64.powi((r.next_f64() * 10.0) as i32);
        let s = fmt(n);
        assert!(!s.contains("e"), "{}", s);
        assert!(s != "-0" && !s.ends_with('.'), "{}", s);
        if let Some((_, f)) = s.split_once('.') {
            assert!(f.len() <= 2 && !f.ends_with('0'), "{} -> {}", n, s);
        }
        let back: f64 = s.parse().unwrap();
        assert!((back - n).abs() <= 0.005 + 1e-9 * n.abs().max(1.0), "{} -> {}", n, s);
    }
}

// ───────────── PRNG ─────────────

#[test]
fn prng_entspricht_der_referenz() {
    // Werte aus node: sd=(sd*1664525+1013904223)%2^32, Ergebnis sd/2^32
    let mut p = Prng::new(1);
    assert_eq!(p.next_f64(), 0.23645552527159452);
    assert_eq!(p.next_f64(), 0.3692706737201661);
    assert_eq!(p.next_f64(), 0.5042420323006809);
    let mut p = Prng::new(4294967295);
    assert_eq!(p.next_f64(), 0.2356804204173386);
    assert_eq!(p.next_f64(), 0.18786314339376986);
    assert_eq!(Prng::new(0).next_f64(), 0.23606797284446657);
}

#[test]
fn prng_bleibt_im_bereich_und_laeuft_nicht_ueber() {
    let mut p = Prng::new(u64::MAX); // wird auf 32 Bit maskiert
    for _ in 0..100_000 {
        let v = p.next_f64();
        assert!((0.0..1.0).contains(&v));
    }
}

// ───────────── Text ─────────────

#[test]
fn esc_und_clean() {
    assert_eq!(esc("<a href=\"x\">&'</a>"), "&lt;a href=&quot;x&quot;&gt;&amp;&apos;&lt;/a&gt;");
    assert_eq!(esc("äöü😀"), "äöü😀");
    assert_eq!(clean("a\u{0}b\nc\td\u{7f}e\u{fffe}f\u{ffff}g", 100), "abcdefg");
    assert_eq!(clean("\u{80}x", 5), "\u{80}x", "C1 bleibt");
    assert_eq!(clean("😀😀😀😀", 3), "😀😀😀", "Codepunkte, nicht Bytes");
    assert_eq!(clean("abc", 0), "");
}

#[test]
fn trunc_und_short() {
    assert_eq!(trunc("hallo", 5.0), "hallo");
    assert_eq!(trunc("hallo welt", 5.0), "hall…");
    assert_eq!(trunc("hallo", 0.0), "");
    assert_eq!(trunc("hallo", -3.0), "");
    assert_eq!(trunc("hallo", 1.0), "…");
    assert_eq!(trunc("", -1.0), "");
    assert_eq!(trunc("😀😀😀", 2.0), "😀…");
    assert_eq!(trunc("abc", f64::NAN), "…");
    assert_eq!(short(999999.0), "1000k");
    assert_eq!(short(9999.0), "9999");
    assert_eq!(short(1e6), "1M");
    assert_eq!(short(12345.0), "12.35k");
    assert_eq!(short(-25000.0), "-25k");
}

#[test]
fn pfadtext_trennt_nur_zahlen() {
    assert_eq!(d!("M", 1, 11, "V", 1), "M1 11V1");
    assert_eq!(d!(0, 0, 480, 280), "0 0 480 280");
    assert_eq!(d!("A", 44, 44, 0, 1, 1, 100, 104), "A44 44 0 1 1 100 104");
}

// ───────────── JSON ─────────────

#[test]
fn json_grundtypen() {
    assert_eq!(parse("null"), Ok(Json::Null));
    assert_eq!(parse(" true "), Ok(Json::Bool(true)));
    assert_eq!(parse("false"), Ok(Json::Bool(false)));
    assert_eq!(parse("\"x\""), Ok(Json::Str("x".into())));
    assert_eq!(parse("[]"), Ok(Json::Arr(vec![])));
    assert_eq!(parse("{}"), Ok(Json::Obj(vec![])));
    assert_eq!(parse("\t\r\n [ 1 , 2 ] \n"), Ok(Json::Arr(vec![Json::Num(1.0), Json::Num(2.0)])));
}

#[test]
fn json_zahlenformate() {
    let n = |s: &str| match parse(s) {
        Ok(Json::Num(x)) => x,
        o => panic!("{} -> {:?}", s, o),
    };
    assert_eq!(n("0"), 0.0);
    assert_eq!(n("-0"), 0.0);
    assert!(n("-0").is_sign_negative());
    assert_eq!(n("12.5"), 12.5);
    assert_eq!(n("1e3"), 1000.0);
    assert_eq!(n("1E+3"), 1000.0);
    assert_eq!(n("25e-1"), 2.5);
    assert_eq!(n("-1.5e2"), -150.0);
    assert_eq!(n("0.1"), 0.1);
    assert_eq!(n("123456789012345678901234567890"), 1.2345678901234568e29);
    assert!(n("1e999").is_infinite(), "wie JSON.parse: Infinity");
    assert_eq!(n("1e-999"), 0.0);
    assert_eq!(n("1e99999999999999999999"), f64::INFINITY);
    for bad in ["01", "-", "+1", ".5", "1.", "1e", "1e+", "--1", "0x10", "1_0", "NaN", "Infinity", "-Infinity", "1.2.3", "1e1.5", "٣"] {
        assert!(parse(bad).is_err(), "sollte ungültig sein: {}", bad);
    }
}

#[test]
fn json_strings_escapes_unicode() {
    assert_eq!(parse(r#""a\"b\\c\/d\b\f\n\r\t""#), Ok(Json::Str("a\"b\\c/d\u{8}\u{c}\n\r\t".into())));
    assert_eq!(parse(r#""\u0041\u00e4\u00C4""#), Ok(Json::Str("AäÄ".into())));
    assert_eq!(parse(r#""\ud83d\ude00""#), Ok(Json::Str("😀".into())), "Ersatzzeichenpaar = ein Codepunkt");
    assert_eq!(parse("\"äöü€😀\""), Ok(Json::Str("äöü€😀".into())));
    // einzelne Hälften werden zu U+FFFE (clean entfernt sie)
    assert_eq!(parse(r#""a\ud800b""#), Ok(Json::Str("a\u{fffe}b".into())));
    assert_eq!(parse(r#""a\udc00b""#), Ok(Json::Str("a\u{fffe}b".into())));
    assert_eq!(parse(r#""\ud800\u0041""#), Ok(Json::Str("\u{fffe}A".into())));
    assert_eq!(parse(r#""\ud800\ud83d\ude00""#), Ok(Json::Str("\u{fffe}😀".into())));
    assert_eq!(parse(r#""\u0000""#), Ok(Json::Str("\0".into())));
    for bad in ["\"\\x\"", "\"\\u12\"", "\"\\u12G4\"", "\"abc", "\"a\nb\"", "\"a\u{1}b\"", "\"\\", "\"\\u", "'x'", "\"\\ud83d\\u"] {
        assert!(parse(bad).is_err(), "sollte ungültig sein: {:?}", bad);
    }
}

#[test]
fn json_verschachtelt_und_doppelte_schluessel() {
    let v = parse(r#"{"a":{"b":[1,{"c":null},[[]]]},"a ":2,"k":1,"k":2}"#).unwrap();
    assert_eq!(v.get("k"), Some(&Json::Num(2.0)), "letzter Schlüssel gewinnt");
    assert_eq!(v.get("a ").cloned(), Some(Json::Num(2.0)));
    match v.get("a").and_then(|a| a.get("b")) {
        Some(Json::Arr(x)) => assert_eq!(x.len(), 3),
        o => panic!("{:?}", o),
    }
    assert_eq!(v.get("fehlt"), None);
    assert_eq!(Json::Num(1.0).get("x"), None);
}

#[test]
fn json_ungueltig_ohne_panik() {
    let bads = [
        "", " ", "{", "}", "[", "]", "[1,]", "{\"a\":1,}", "{\"a\"}", "{\"a\":}", "{a:1}", "[1 2]", "{\"a\":1 \"b\":2}", "tru", "nul", "truee",
        "[1]x", "{} {}", "// c\n{}", "/* c */ {}", "\u{feff}{}", "{\"a\":1}}", "[[[", "{\"a\":[{\"b\":", "\u{0}", "{,}", "[,1]", "{\"a\"::1}",
    ];
    for b in bads {
        assert!(parse(b).is_err(), "sollte ungültig sein: {:?}", b);
    }
}

#[test]
fn json_tiefe_ist_begrenzt_und_stuerzt_nicht_ab() {
    let ok = format!("{}1{}", "[".repeat(MAX_DEPTH), "]".repeat(MAX_DEPTH));
    assert!(parse(&ok).is_ok());
    let deep = "[".repeat(200_000);
    assert!(parse(&deep).is_err());
    let deep2 = format!("{}{}", "{\"a\":".repeat(100_000), "1");
    assert!(parse(&deep2).is_err());
    // tief verschachtelt in einem ignorierten Schlüssel einer sonst gültigen Spezifikation
    let spec = format!("{{\"type\":\"badge\",\"zzz\":{}1{}}}", "[".repeat(MAX_DEPTH - 5), "]".repeat(MAX_DEPTH - 5));
    assert!(gen(&spec).is_ok());
    let spec = format!("{{\"type\":\"badge\",\"zzz\":{}}}", "[".repeat(1_000_000));
    assert_eq!(field(&spec), "spec");
}

// ───────────── Normalisierung und Fehlerfelder ─────────────

#[test]
fn fehlerfelder() {
    assert_eq!(field("[]"), "spec");
    assert_eq!(field("5"), "spec");
    assert_eq!(field("null"), "spec");
    assert_eq!(field("\"bar\""), "spec");
    assert_eq!(field("{"), "spec");
    assert_eq!(field(""), "spec");
    assert_eq!(field("{}"), "type");
    assert_eq!(field(r#"{"type":5}"#), "type");
    assert_eq!(field(r#"{"type":"constructor"}"#), "type");
    assert_eq!(field(r#"{"type":"Bar"}"#), "type");
    assert_eq!(field(r#"{"type":"bar","width":"480"}"#), "width");
    assert_eq!(field(r#"{"type":"bar","width":true}"#), "width");
    assert_eq!(field(r#"{"type":"bar","width":1e999}"#), "width");
    assert_eq!(field(r#"{"type":"bar","data":[1,"x"]}"#), "data");
    assert_eq!(field(r#"{"type":"bar","data":5}"#), "data");
    assert_eq!(field(r#"{"type":"bar","color":"rgb(1,2,3)"}"#), "color");
    assert_eq!(field(r#"{"type":"bar","color":""}"#), "color");
    assert_eq!(field(r##"{"type":"bar","color":"#12"}"##), "color");
    assert_eq!(field(r##"{"type":"bar","color":"#ggg"}"##), "color");
    assert_eq!(field(r#"{"type":"bar","theme":"neon"}"#), "theme");
    assert_eq!(field(r#"{"type":"bar","heading":"yes"}"#), "heading");
    assert_eq!(field(r#"{"type":"bar","idprefix":"1a"}"#), "idprefix");
    assert_eq!(field(r#"{"type":"bar","idprefix":"a b"}"#), "idprefix");
    assert_eq!(field(r#"{"type":"bar","idprefix":""}"#), "idprefix");
    assert_eq!(field(r#"{"type":"bar","title":5}"#), "title");
    assert_eq!(field(r#"{"type":"bar","labels":[1]}"#), "labels");
    assert_eq!(field(r#"{"type":"bar","labels":[null]}"#), "labels");
    assert_eq!(field(r#"{"type":"icon","name":"nix"}"#), "name");
    assert_eq!(field(r#"{"type":"gradient","via":"nope"}"#), "via");
    assert_eq!(field(r#"{"type":"line","data":[[1],5]}"#), "data");
    assert_eq!(field(r#"{"type":"line","data":[1,[2]]}"#), "data");
    assert_eq!(field(r#"{"type":"graph","nodes":[{"id":"A"},{"id":"A"}]}"#), "nodes");
    assert_eq!(field(r#"{"type":"graph","nodes":[{"type":"x"}]}"#), "nodes");
    assert_eq!(field(r#"{"type":"graph","nodes":[5]}"#), "nodes");
    assert_eq!(field(r#"{"type":"graph","nodes":[{"id":"A","x":1}]}"#), "nodes");
    assert_eq!(field(r#"{"type":"graph","nodes":[{"id":"A","x":1,"y":null}]}"#), "nodes");
    assert_eq!(field(r#"{"type":"graph","nodes":[{"id":"A"}],"edges":[{"from":"A","to":"B"}]}"#), "edges");
    assert_eq!(field(r#"{"type":"graph","edges":[7]}"#), "edges");
    assert_eq!(field(r#"{"type":"graph","nodes":[],"edges":[{"from":"A","to":"B"}]}"#), "edges");
}

#[test]
fn erster_fehler_in_tabellenreihenfolge() {
    // theme (gemeinsam) kommt vor width; width vor data
    assert_eq!(field(r#"{"type":"bar","data":"x","width":"y","theme":"z"}"#), "theme");
    assert_eq!(field(r#"{"type":"bar","data":"x","width":"y"}"#), "width");
    // Feldfehler vor idprefix-Muster
    assert_eq!(field(r#"{"type":"bar","idprefix":"1","width":"y"}"#), "width");
    // idprefix vor Kanten-Prüfung
    assert_eq!(field(r#"{"type":"graph","idprefix":"1","edges":[{"from":"X","to":"Y"}]}"#), "idprefix");
}

#[test]
fn werte_werden_geklemmt_und_normalisiert() {
    let g = |s: &str| gen(s).unwrap();
    assert!(g(r#"{"type":"bar","width":99999,"height":-5}"#).contains("width=\"2400\" height=\"120\""));
    assert!(g(r#"{"type":"bar","width":480.9,"height":280.1}"#).contains("width=\"480\" height=\"280\""));
    assert!(g(r##"{"type":"badge","color":"#ABC","colors":"static"}"##).contains("#aabbcc"));
    assert!(g(r#"{"type":"badge","title":"  "}"#).contains("<title>  </title>"));
    assert!(g(r#"{"type":"badge","title":"\n\t"}"#).contains("<title>Badge</title>"));
    assert!(g(r#"{"type":"badge","unbekannt":{"x":[1,2]},"_impl":["rs"]}"#).starts_with("<svg"));
    // null = Standardwert
    assert_eq!(g(r#"{"type":"bar","width":null,"data":null}"#), g(r#"{"type":"bar"}"#));
    // doppelte Schlüssel: der letzte gewinnt
    assert_eq!(g(r#"{"type":"bar","width":200,"width":300}"#), g(r#"{"type":"bar","width":300}"#));
}

#[test]
fn ausgabe_ohne_zeilenumbruch_am_ende_und_ohne_gefaehrliches() {
    for t in ["bar", "line", "donut", "pattern", "badge", "icon", "monogram", "gradient", "graph"] {
        for th in THEME_NAMES {
            for c in ["vars", "static"] {
                let s = gen(&format!(r#"{{"type":"{}","theme":"{}","colors":"{}","title":"<script>alert(1)</script>\"onload='x'"}}"#, t, th, c)).unwrap();
                assert!(s.starts_with("<svg xmlns=\"http://www.w3.org/2000/svg\""));
                assert!(s.ends_with("</svg>"), "{} {} {}", t, th, c);
                assert!(!s.contains("<script") && !s.contains("<foreignObject") && !s.contains("javascript:"));
                assert!(!s.contains("onload='"), "{}", s);
            }
        }
    }
    // contrast: nur Schwarz und #00ff88
    for t in ["bar", "line", "donut", "pattern", "badge", "icon", "monogram", "gradient", "graph"] {
        let s = gen(&format!(r##"{{"type":"{}","theme":"contrast","colors":"static","color":"#fa1e4e","from":"#123456"}}"##, t)).unwrap();
        for (i, part) in s.split('#').enumerate().skip(1) {
            if s.split('#').nth(i - 1).is_some_and(|prev| prev.ends_with("url(")) {
                continue; // url(#id) ist ein Verweis, keine Farbe
            }
            let h: String = part.chars().take(6).collect();
            assert!(h == "000000" || h == "00ff88", "{}: #{}", t, h);
        }
    }
}

#[test]
fn randfaelle_leer_und_extrem() {
    assert!(gen(r#"{"type":"bar","data":[]}"#).unwrap().contains("— ausstehend —"));
    assert!(gen(r#"{"type":"line","data":[]}"#).unwrap().contains("— ausstehend —"));
    assert!(gen(r#"{"type":"line","data":[[]]}"#).unwrap().contains("— ausstehend —"));
    assert!(gen(r#"{"type":"donut","data":[0,0]}"#).unwrap().contains("— ausstehend —"));
    assert!(gen(r#"{"type":"donut","data":[]}"#).unwrap().contains("— ausstehend —"));
    assert!(gen(r#"{"type":"graph","nodes":[]}"#).unwrap().contains("— ausstehend —"));
    assert!(gen(r#"{"type":"bar","data":[1e9,-1e9,0,1e-9]}"#).is_ok());
    assert!(gen(r#"{"type":"bar","data":[5,5,5,5]}"#).is_ok());
    assert!(gen(r#"{"type":"line","data":[7]}"#).is_ok());
    assert!(gen(r#"{"type":"line","data":[[1],[1,2,3],[],[5]],"labels":["a","b","c"]}"#).is_ok());
    assert!(gen(r#"{"type":"donut","data":[1e9,1e9,1e9],"thickness":5,"width":160,"height":120}"#).is_ok());
    assert!(gen(r#"{"type":"gradient","width":16,"height":16,"text":"Hallo"}"#).is_ok());
    assert!(gen(r#"{"type":"pattern","kind":"confetti","density":400,"seed":4294967295,"size":200}"#).is_ok());
    assert!(gen(r#"{"type":"pattern","kind":"stripes","angle":-90,"size":4,"width":2400,"height":2400}"#).is_ok());
    assert!(gen(r#"{"type":"graph","nodes":[{"id":"A","type":"😀"},{"id":"B"}],"edges":[{"from":"A","to":"A"},{"from":"B","to":"A"},{"from":"A","to":"B"}]}"#).is_ok());
    assert!(gen(r#"{"type":"monogram","text":"😀😀😀😀","shape":"hex"}"#).unwrap().contains("😀😀😀<"));
}

#[test]
fn graph_zyklen_und_grosse_graphen_terminieren() {
    let mut nodes = String::new();
    let mut edges = String::new();
    for i in 0..60 {
        if i > 0 {
            nodes.push(',');
            edges.push(',');
        }
        nodes.push_str(&format!(r#"{{"id":"n{}","type":"t{}"}}"#, i, i % 5));
        edges.push_str(&format!(r#"{{"from":"n{}","to":"n{}"}}"#, i, (i * 7 + 3) % 60));
    }
    let t = Instant::now();
    let s = gen(&format!(r#"{{"type":"graph","nodes":[{}],"edges":[{}]}}"#, nodes, edges)).unwrap();
    assert!(s.ends_with("</svg>"));
    assert!(t.elapsed().as_secs() < 5);
}

#[test]
fn riesige_eingaben_werden_begrenzt() {
    let t = Instant::now();
    // 2 Mio. Zahlen in data: nur die ersten 64 zählen, der Rest wird (geparst und) verworfen
    let big = format!(r#"{{"type":"bar","data":[{}]}}"#, vec!["1.5"; 2_000_000].join(","));
    assert!(gen(&big).is_ok());
    // riesiger Text
    let txt = "x".repeat(20_000_000);
    let s = gen(&format!(r#"{{"type":"badge","label":"{}","value":"{}"}}"#, txt, txt)).unwrap();
    assert!(s.len() < 2000);
    // riesiger Schlüsselvorrat
    let mut keys = String::from(r#"{"type":"bar""#);
    for i in 0..100_000 {
        keys.push_str(&format!(r#","k{}":{}"#, i, i));
    }
    keys.push('}');
    assert!(gen(&keys).is_ok());
    assert!(t.elapsed().as_secs() < 60, "{:?}", t.elapsed());
}

// ───────────── Parität gegen svg/golden (wie run_parity.sh, aber in Rust) ─────────────

#[test]
fn golden_paritaet_alle_rs_fixtures() {
    let fx = root().join("svg/fixtures");
    let mut n = 0;
    for e in std::fs::read_dir(&fx).expect("svg/fixtures fehlt") {
        let p = e.unwrap().path();
        if p.extension().is_none_or(|x| x != "json") {
            continue;
        }
        let text = std::fs::read_to_string(&p).unwrap();
        let v = parse(&text).unwrap();
        let impls = match v.get("_impl") {
            Some(Json::Arr(a)) => a.clone(),
            _ => vec![],
        };
        if !impls.contains(&Json::Str("rs".into())) {
            continue;
        }
        let want = std::fs::read(root().join("svg/golden").join(p.file_stem().unwrap()).with_extension("svg")).unwrap();
        let got = generate(&v).unwrap_or_else(|e| panic!("{:?}: {}", p, e));
        assert!(got.as_bytes() == want.as_slice(), "weicht ab: {:?}", p);
        n += 1;
    }
    assert!(n >= 100, "nur {} Fixtures geprüft", n);
}

#[test]
fn ungueltige_fixtures_nennen_das_erwartete_feld() {
    let dir = root().join("svg/fixtures-invalid");
    let mut n = 0;
    for e in std::fs::read_dir(&dir).expect("svg/fixtures-invalid fehlt") {
        let p = e.unwrap().path();
        if p.extension().is_none_or(|x| x != "json") {
            continue;
        }
        let bytes = std::fs::read(&p).unwrap();
        let text = String::from_utf8_lossy(&bytes).to_string();
        let expect = match std::fs::read_to_string(p.with_extension("expect")) {
            Ok(f) => f.trim().to_string(),
            Err(_) => match parse(&text).ok().and_then(|v| v.get("_error_field").cloned()) {
                Some(Json::Str(f)) => f,
                _ => String::new(),
            },
        };
        let err = generate_bytes(&bytes).expect_err(&format!("{:?} sollte ungültig sein", p));
        assert_eq!(err.field, expect, "{:?}: {}", p, err);
        n += 1;
    }
    assert!(n >= 30, "nur {} ungültige Fixtures", n);
}

// ───────────── Fuzz ─────────────

/// Verstümmelt Eingaben (Byte-Flips, Abschneiden, Einfügen, Verdoppeln, Zahlen/Typen vertauschen).
fn mutate(src: &[u8], r: &mut Prng) -> Vec<u8> {
    let mut b = src.to_vec();
    let rounds = 1 + (r.next_f64() * 4.0) as usize;
    for _ in 0..rounds {
        let n = b.len().max(1);
        let at = (r.next_f64() * n as f64) as usize % n;
        match (r.next_f64() * 9.0) as u32 {
            0 => {
                if !b.is_empty() {
                    b[at] = (r.next_f64() * 256.0) as u8
                }
            }
            1 => b.truncate(at),
            2 => {
                let ins: [&[u8]; 10] = [b"null", b"[", b"{", b"\"", b"\\u", b"1e999", b"-0", b"\\ud800", b",", b"\xff\xfe"];
                let x = ins[(r.next_f64() * 10.0) as usize % 10];
                b.splice(at..at, x.iter().copied());
            }
            3 => {
                if !b.is_empty() {
                    b.remove(at);
                }
            }
            4 => {
                let end = (at + (r.next_f64() * 40.0) as usize).min(b.len());
                let chunk = b[at.min(b.len())..end].to_vec();
                b.splice(at.min(b.len())..at.min(b.len()), chunk);
            }
            5 => {
                let t = String::from_utf8_lossy(&b).replace("12", "1e308").replace("\"bar\"", "\"x\"");
                b = t.into_bytes();
            }
            6 => {
                let t = String::from_utf8_lossy(&b).replace(':', ":[").replace('1', "-1e9");
                b = t.into_bytes();
            }
            7 => {
                let t = String::from_utf8_lossy(&b).replace("true", "\"true\"").replace("false", "0").replace("[", "{");
                b = t.into_bytes();
            }
            _ => {
                let k = (r.next_f64() * 20.0) as usize;
                b.extend(std::iter::repeat_n(b'[', k * 1000));
            }
        }
    }
    b
}

#[test]
fn fuzz_verstuemmelte_fixtures_keine_panik_keine_endlosschleife() {
    let mut seeds: Vec<Vec<u8>> = Vec::new();
    for sub in ["svg/fixtures", "svg/fixtures-invalid"] {
        for e in std::fs::read_dir(root().join(sub)).unwrap() {
            let p = e.unwrap().path();
            if p.extension().is_some_and(|x| x == "json") {
                seeds.push(std::fs::read(p).unwrap());
            }
        }
    }
    assert!(seeds.len() >= 100);
    let mut r = Prng::new(20260717);
    let t = Instant::now();
    let (mut ok, mut err) = (0u32, 0u32);
    let rounds = 3000;
    for i in 0..rounds {
        let src = &seeds[i % seeds.len()];
        let m = mutate(src, &mut r);
        let t1 = Instant::now();
        match generate_bytes(&m) {
            Ok(s) => {
                ok += 1;
                assert!(s.starts_with("<svg") && s.ends_with("</svg>"));
            }
            Err(e) => {
                err += 1;
                assert!(!e.field.is_empty() && !e.msg.contains('\n'), "{:?}", e);
            }
        }
        assert!(t1.elapsed().as_secs() < 5, "Eingabe {} dauerte zu lang", i);
    }
    eprintln!("Fuzz (mutierte Fixtures): {} Eingaben, {} gültig, {} abgelehnt, {:?}", rounds, ok, err, t.elapsed());
    assert!(ok > 100 && err > 100, "Mutationen sollten beides auslösen: ok {} err {}", ok, err);
}

#[test]
fn fuzz_zufallsbytes_und_zufallsjson() {
    let mut r = Prng::new(99);
    for _ in 0..1000 {
        let n = (r.next_f64() * 300.0) as usize;
        let b: Vec<u8> = (0..n).map(|_| (r.next_f64() * 256.0) as u8).collect();
        let _ = generate_bytes(&b);
    }
    // Zufälliges, syntaktisch gültiges JSON mit passenden Schlüsseln
    let keys = [
        "type", "theme", "colors", "idprefix", "seed", "title", "width", "height", "data", "labels", "color", "values", "grid", "heading", "area", "dots",
        "thickness", "center", "legend", "kind", "size", "stroke", "angle", "density", "opacity", "bg", "label", "value", "name", "text", "shape", "color2",
        "from", "to", "via", "cx", "cy", "nodes", "edges", "id", "x", "y",
    ];
    fn rv(r: &mut Prng, keys: &[&str], d: u32) -> String {
        match (r.next_f64() * if d > 3 { 6.0 } else { 9.0 }) as u32 {
            0 => "null".into(),
            1 => ["true", "false"][(r.next_f64() * 2.0) as usize].into(),
            2 => format!("{}", (r.next_f64() - 0.5) * 10f64.powi((r.next_f64() * 12.0) as i32)),
            3 => format!("\"{}\"", ["bar", "line", "donut", "pattern", "badge", "icon", "monogram", "gradient", "graph", "contrast", "static", "#fff", "accent", "x\\u0000y", "A", "B", "grid", "stripes", "circle", "check", "confetti"][(r.next_f64() * 21.0) as usize]),
            4 => "\"\"".into(),
            5 => "-0".into(),
            6 => format!("[{}]", (0..(r.next_f64() * 5.0) as usize).map(|_| rv(r, keys, d + 1)).collect::<Vec<_>>().join(",")),
            _ => format!(
                "{{{}}}",
                (0..(r.next_f64() * 6.0) as usize)
                    .map(|_| format!("\"{}\":{}", keys[(r.next_f64() * keys.len() as f64) as usize], rv(r, keys, d + 1)))
                    .collect::<Vec<_>>()
                    .join(",")
            ),
        }
    }
    let types = ["bar", "line", "donut", "pattern", "badge", "icon", "monogram", "gradient", "graph"];
    let mut ok = 0;
    for i in 0..2000 {
        let body = match rv(&mut r, &keys, 0) {
            s if s.starts_with('{') && s.len() > 2 => s[1..].to_string(),
            _ => "}".to_string(),
        };
        let spec = format!("{{\"type\":\"{}\",{}", types[i % 9], if body == "}" { "\"x\":0}".to_string() } else { body });
        if generate_str(&spec).is_ok() {
            ok += 1;
        }
    }
    assert!(ok > 50, "zu wenige gültige Zufallsspezifikationen: {}", ok);
}
