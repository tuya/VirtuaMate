#!/usr/bin/env bash
# VirtuaMate startup script
# Usage:  ./scripts/start.sh
#         ./scripts/start.sh /path/to/VirtuaMate   (override install dir)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="${1:-$(dirname "$SCRIPT_DIR")}"
BIN="$PROJECT_DIR/dist/VirtuaMate_1.0.0/VirtuaMate_1.0.0.elf"
LOG="$PROJECT_DIR/virtuamate.log"

if [ ! -x "$BIN" ]; then
    echo "[ERROR] Binary not found: $BIN"
    echo "        Run 'tos.py build' first."
    exit 1
fi

# Ensure resources are reachable (paths in config are relative)
cd "$PROJECT_DIR"

# Graphics environment — try Wayland first, fall back to X11
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    export DISPLAY=:0
fi

echo "[VirtuaMate] $(date '+%Y-%m-%d %H:%M:%S')  starting …"
echo "  bin : $BIN"
echo "  cwd : $(pwd)"
echo "  log : $LOG"

exec "$BIN" >> "$LOG" 2>&1
