#!/bin/zsh

# Project hooks must not embed the checkout path. Codex starts this wrapper from
# the project, and the wrapper resolves the Host executable from its own path.
set -u

SAKI_HOOK_SCRIPT_DIR="${0:A:h}"
SAKI_HOOK_REPO_ROOT="${SAKI_HOOK_SCRIPT_DIR:h}"
SAKI_HOOK_EXECUTABLE="$SAKI_HOOK_REPO_ROOT/host/.venv/bin/saki-host"

# A missing development venv must not block or fail an Agent action.
if [[ ! -x "$SAKI_HOOK_EXECUTABLE" ]]; then
  exit 0
fi

exec "$SAKI_HOOK_EXECUTABLE" hook "$@"
