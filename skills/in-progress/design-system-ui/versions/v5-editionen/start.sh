#!/usr/bin/env bash
# Design System UI — Starter. Zwei Editionen, eine Codebasis, keine
# Abhängigkeiten (reine Python-Standardbibliothek).
#
#   ./start.sh                 Normal-Edition:  Hamburger-Menü, Kacheln,
#                              neun Oberflächen. Du baust, Gemma bewertet.
#   ./start.sh --openclaw      OpenCLAW-Edition: alles aus Normal PLUS die
#                              Agenten-Shell, die auf Zuruf baut und optimiert.
#
# Beides lässt sich parallel betreiben:
#   PORT=8000 ./start.sh &
#   PORT=8001 ./start.sh --openclaw &
set -euo pipefail

cd "$(dirname "$0")"

PORT="${PORT:-8000}"
EDITION="${EDITION:-normal}"

while [ $# -gt 0 ]; do
  case "$1" in
    --openclaw)        EDITION="openclaw" ;;
    --normal)          EDITION="normal" ;;
    --port)            shift; PORT="$1" ;;
    -h|--help)
      sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) echo "Unbekannte Option: $1 (--help für Hilfe)" >&2; exit 2 ;;
  esac
  shift
done

case "$EDITION" in
  normal)   BANNER="Normal — Hamburger-Menü, Kacheln, neun Oberflächen" ;;
  openclaw) BANNER="OpenCLAW — plus Agenten-Shell (nicht OpenCloud, nicht Claude)" ;;
  *) echo "Unbekannte Edition '$EDITION' (normal|openclaw)" >&2; exit 2 ;;
esac

echo "Design System UI · Edition: $EDITION"
echo "  $BANNER"
echo
echo "  UI:      http://localhost:${PORT}/"
echo "  Agenten: http://localhost:${PORT}/llms.txt"
echo "  Status:  http://localhost:${PORT}/health"
echo

export PORT EDITION
exec python3 server.py
