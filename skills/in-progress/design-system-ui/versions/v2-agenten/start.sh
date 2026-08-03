#!/usr/bin/env bash
# Design System UI - launcher. Boots the zero-dependency Python backend and
# prints the URLs a human or agent needs. No pip install required.
set -euo pipefail

cd "$(dirname "$0")"

PORT="${PORT:-8000}"

echo "Design System UI"
echo "  UI:      http://localhost:${PORT}/"
echo "  agents:  http://localhost:${PORT}/llms.txt"
echo "  health:  http://localhost:${PORT}/health"
echo

exec python3 server.py
