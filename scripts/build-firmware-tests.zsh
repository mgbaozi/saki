#!/bin/zsh

set -euo pipefail

SAKI_TEST_SCRIPT_DIR="${0:A:h}"
SAKI_TEST_REPO_ROOT="${SAKI_TEST_SCRIPT_DIR:h}"

source "$SAKI_TEST_SCRIPT_DIR/env-idf.zsh"
cd "$SAKI_TEST_REPO_ROOT/firmware/test_app"

idf.py -B build-unity build
