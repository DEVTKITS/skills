//! Design System UI - Rust user client (standard library only).
//!
//! A tiny terminal client that talks to the backend over raw HTTP so it
//! compiles with just `rustc client.rs` — no crates, no cargo registry.
//!
//!   rustc client.rs -o dsui && ./dsui health
//!   ./dsui patterns
//!   ./dsui execute workflow.json
//!
//! Override the backend with BACKEND_URL (default http://localhost:8000).

use std::env;
use std::fs;
use std::io::{Read, Write};
use std::net::TcpStream;

fn backend() -> (String, u16) {
    let url = env::var("BACKEND_URL").unwrap_or_else(|_| "http://localhost:8000".into());
    let stripped = url.trim_start_matches("http://").trim_start_matches("https://");
    let (host, port) = match stripped.split_once(':') {
        Some((h, p)) => (h.to_string(), p.trim_end_matches('/').parse().unwrap_or(8000)),
        None => (stripped.trim_end_matches('/').to_string(), 8000),
    };
    (host, port)
}

/// Minimal HTTP/1.1 request over a TcpStream. Returns the response body.
fn request(method: &str, path: &str, body: Option<&str>) -> std::io::Result<String> {
    let (host, port) = backend();
    let mut stream = TcpStream::connect((host.as_str(), port))?;

    let body = body.unwrap_or("");
    let req = format!(
        "{method} {path} HTTP/1.1\r\n\
         Host: {host}:{port}\r\n\
         Content-Type: application/json\r\n\
         Content-Length: {len}\r\n\
         Connection: close\r\n\r\n{body}",
        len = body.len()
    );
    stream.write_all(req.as_bytes())?;

    let mut raw = String::new();
    stream.read_to_string(&mut raw)?;
    // Split headers from body on the blank line.
    let body = raw.splitn(2, "\r\n\r\n").nth(1).unwrap_or("").to_string();
    Ok(body)
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let cmd = args.get(1).map(String::as_str).unwrap_or("help");

    let result = match cmd {
        "health" => request("GET", "/health", None),
        "patterns" => request("GET", "/api/patterns", None),
        "llms" => request("GET", "/llms.txt", None),
        "execute" => match args.get(2) {
            Some(path) => match fs::read_to_string(path) {
                Ok(json) => request("POST", "/api/workflow/execute", Some(&json)),
                Err(e) => {
                    eprintln!("cannot read {path}: {e}");
                    std::process::exit(1);
                }
            },
            None => {
                eprintln!("usage: dsui execute <workflow.json>");
                std::process::exit(2);
            }
        },
        _ => {
            println!("Design System UI - Rust client");
            println!("usage: dsui [health|patterns|llms|execute <workflow.json>]");
            return;
        }
    };

    match result {
        Ok(body) => println!("{body}"),
        Err(e) => {
            eprintln!("request failed: {e}");
            std::process::exit(1);
        }
    }
}
