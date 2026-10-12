#!/usr/bin/env bash
# Thin wrapper — same args as cut_clip.py
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$ROOT/cut_clip.py" "$@"
