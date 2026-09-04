#!/bin/zsh

set -euo pipefail

SAKI_SCRIPT_DIR="${0:A:h}"
exec "$SAKI_SCRIPT_DIR/build-firmware-profile.zsh" dev
