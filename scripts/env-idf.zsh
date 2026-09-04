#!/bin/zsh

# Source this file from zsh so the ESP-IDF environment remains active:
#   source scripts/env-idf.zsh

SAKI_ENV_SCRIPT_PATH="${(%):-%N}"
SAKI_ENV_SCRIPT_DIR="${SAKI_ENV_SCRIPT_PATH:A:h}"
SAKI_ENV_REPO_ROOT="${SAKI_ENV_SCRIPT_DIR:h}"
SAKI_ENV_DEFAULT_IDF_PATH="${SAKI_ENV_REPO_ROOT:h:h}/esp-idf-v5.5.3"

export SAKI_IDF_PATH="${SAKI_IDF_PATH:-$SAKI_ENV_DEFAULT_IDF_PATH}"

if [[ -z "${SAKI_PYENV_ROOT:-}" && -n "${HOME:-}" && -d "$HOME/.pyenv" ]]; then
  export SAKI_PYENV_ROOT="$HOME/.pyenv"
fi

if [[ -n "${SAKI_PYENV_ROOT:-}" ]]; then
  export PATH="$SAKI_PYENV_ROOT/shims:$SAKI_PYENV_ROOT/bin:$PATH"
fi

saki_env_fail() {
  print -u2 -- "saki env: $1"
  return 1
}

if [[ "$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>&1)" != "3.12" ]]; then
  saki_env_fail "expected Python 3.12"
  return 1 2>/dev/null || exit 1
fi

if [[ ! -f "$SAKI_IDF_PATH/export.sh" ]]; then
  saki_env_fail "ESP-IDF export.sh not found"
  return 1 2>/dev/null || exit 1
fi

if ! source "$SAKI_IDF_PATH/export.sh"; then
  saki_env_fail "failed to activate ESP-IDF"
  return 1 2>/dev/null || exit 1
fi

if [[ "$(idf.py --version 2>&1)" != "ESP-IDF v5.5.3" ]]; then
  saki_env_fail "expected ESP-IDF v5.5.3"
  return 1 2>/dev/null || exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
  saki_env_fail "ninja is not available"
  return 1 2>/dev/null || exit 1
fi

if ! command -v esptool.py >/dev/null 2>&1; then
  saki_env_fail "esptool.py is not available"
  return 1 2>/dev/null || exit 1
fi

unset -f saki_env_fail
unset SAKI_ENV_SCRIPT_PATH SAKI_ENV_SCRIPT_DIR SAKI_ENV_REPO_ROOT SAKI_ENV_DEFAULT_IDF_PATH
print -- "Saki ESP-IDF environment ready."
