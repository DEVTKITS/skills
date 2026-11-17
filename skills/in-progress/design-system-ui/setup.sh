#!/usr/bin/env bash
# Richtet die optionale Python-Umgebung ein (DuckDB für Wissensbasis und Speicher).
# Alles andere im Projekt braucht nur die Python-Standardbibliothek.
set -euo pipefail
cd "$(dirname "$0")"
python3 -m venv .venv
.venv/bin/pip install -q --upgrade pip
.venv/bin/pip install -q -r requirements.txt
.venv/bin/python -c "import duckdb; print('DuckDB', duckdb.__version__, 'bereit')"
