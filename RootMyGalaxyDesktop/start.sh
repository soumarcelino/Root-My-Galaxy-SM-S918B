#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venv"
PYTHON_BIN="$VENV_DIR/bin/python3"
REQUIREMENTS="$SCRIPT_DIR/requirements.txt"
STAMP="$VENV_DIR/.requirements.sha256"

if ! command -v python3 >/dev/null 2>&1; then
    echo "error: python3 not found on PATH" >&2
    exit 1
fi

if [[ ! -x "$PYTHON_BIN" ]]; then
    echo "[setup] Creating virtualenv at $VENV_DIR ..."
    if ! python3 -m venv "$VENV_DIR"; then
        echo "error: failed to create the virtualenv (on Debian/Ubuntu try: sudo apt install python3-venv)" >&2
        exit 1
    fi
fi

# Only reinstall when requirements.txt actually changed since the last
# successful install, so normal launches stay fast (no pip invocation).
current_hash="$(sha256sum "$REQUIREMENTS" | cut -d' ' -f1)"
installed_hash="$(cat "$STAMP" 2>/dev/null || true)"
if [[ "$current_hash" != "$installed_hash" ]]; then
    echo "[setup] Installing/updating Python dependencies (first run downloads PyQt6, ~90MB) ..."
    "$PYTHON_BIN" -m pip install --quiet --upgrade pip
    "$PYTHON_BIN" -m pip install --quiet -r "$REQUIREMENTS"
    echo "$current_hash" > "$STAMP"
fi

exec "$PYTHON_BIN" "$SCRIPT_DIR/root_gui.py" "$@"
