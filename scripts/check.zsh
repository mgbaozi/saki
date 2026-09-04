#!/bin/zsh

set -euo pipefail

SAKI_SCRIPT_DIR="${0:A:h}"
SAKI_REPO_ROOT="${SAKI_SCRIPT_DIR:h}"
SAKI_HOST_PYTHON="$SAKI_REPO_ROOT/host/.venv/bin/python"

if [[ ! -x "$SAKI_HOST_PYTHON" ]]; then
  print -u2 -- "host virtual environment not found; create host/.venv first"
  exit 2
fi

cd "$SAKI_REPO_ROOT"

"$SAKI_HOST_PYTHON" -m pytest host/tests
"$SAKI_HOST_PYTHON" -m ruff check host/src host/tests
git diff --check
git diff --cached --check

if [[ "${1:-}" == "--firmware" ]]; then
  "$SAKI_SCRIPT_DIR/build-firmware.zsh"
fi
