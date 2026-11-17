//! dsui-svg <spec.json> — schreibt exakt das SVG nach stdout (ohne abschließenden Zeilenumbruch).
//! Fehler: Exitcode 1, auf stderr eine Zeile `svgspec: <Feld>: <Meldung>`.
use std::io::Write;
use std::process::ExitCode;

/// Obergrenze für die Eingabedatei (Schutz vor versehentlich riesigen Dateien).
const MAX_BYTES: u64 = 64 * 1024 * 1024;

fn fail(field: &str, msg: &str) -> ExitCode {
    // eine Zeile: Zeilenumbrüche in der Meldung durch Leerzeichen ersetzen
    let msg: String = msg.chars().map(|c| if c == '\n' || c == '\r' { ' ' } else { c }).collect();
    let _ = writeln!(std::io::stderr(), "svgspec: {}: {}", field, msg);
    ExitCode::from(1)
}

fn main() -> ExitCode {
    let path = match std::env::args_os().nth(1) {
        Some(p) => p,
        None => return fail("spec", "Aufruf: dsui-svg <spec.json>"),
    };
    let bytes = match std::fs::metadata(&path) {
        Ok(m) if m.len() > MAX_BYTES => return fail("spec", "Datei zu groß (> 64 MiB)"),
        _ => match std::fs::read(&path) {
            Ok(b) if (b.len() as u64) <= MAX_BYTES => b,
            Ok(_) => return fail("spec", "Datei zu groß (> 64 MiB)"),
            Err(e) => return fail("spec", &format!("Datei nicht lesbar: {}", e)),
        },
    };
    match dsui_svg::generate_bytes(&bytes) {
        Ok(svg) => {
            let mut out = std::io::stdout().lock();
            if out.write_all(svg.as_bytes()).and_then(|_| out.flush()).is_err() {
                return ExitCode::from(1);
            }
            ExitCode::SUCCESS
        }
        Err(e) => fail(&e.field, &e.msg),
    }
}
