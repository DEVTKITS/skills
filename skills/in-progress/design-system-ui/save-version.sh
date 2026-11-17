#!/usr/bin/env bash
# Friert den aktuellen Stand als eigenständige, für sich lauffähige Version
# unter versions/<name>/ ein.
#
#   ./save-version.sh v6-nanobot "Chatbot als Nanobot mit Agent-zu-Agent"
#
# Jede Version enthält alles, was sie zum Laufen braucht — ein Kopieren des
# Ordners genügt, es gibt keine Verweise nach draußen.
set -euo pipefail
cd "$(dirname "$0")"

NAME="${1:-}"
DESC="${2:-}"
[ -n "$NAME" ] || { echo "Aufruf: ./save-version.sh <name> [beschreibung]" >&2; exit 2; }

DEST="versions/$NAME"
[ -e "$DEST" ] && { echo "Version '$NAME' existiert bereits." >&2; exit 1; }

mkdir -p "$DEST"
# Alles außer den Versionen selbst und flüchtigen Verzeichnissen
for item in *; do
  case "$item" in
    versions|__pycache__|node_modules|shots|.venv|data|target) continue ;;
  esac
  cp -r "$item" "$DEST/"
done
rm -rf "$DEST/demo/node_modules" "$DEST/demo/shots" "$DEST/__pycache__" \
       "$DEST/.venv" "$DEST/rust/target" "$DEST/video/node_modules" 2>/dev/null || true
find "$DEST" -name __pycache__ -type d -prune -exec rm -rf {} + 2>/dev/null || true
# link-skills.sh verlinkt jede SKILL.md im Repo — eine archivierte Kopie darf nicht
# als zusätzlicher, doppelter Skill auftauchen.
[ -f "$DEST/SKILL.md" ] && mv "$DEST/SKILL.md" "$DEST/SKILL.archiv.md"

cat > "$DEST/VERSION.md" <<EOF
# $NAME

$DESC

Eingefroren am: $(date -u +%Y-%m-%dT%H:%M:%SZ)
Commit: $(git rev-parse --short HEAD 2>/dev/null || echo "unbekannt")

Eigenständig lauffähig:

    cd versions/$NAME
    ./start.sh                # Normal-Edition, falls vorhanden
    ./test.sh                 # Selbsttest dieser Version
EOF

echo "Version gesichert: $DEST"
