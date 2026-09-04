#!/bin/zsh

set -euo pipefail

SAKI_SCRIPT_DIR="${0:A:h}"
SAKI_REPO_ROOT="${SAKI_SCRIPT_DIR:h}"
SAKI_PROFILE="${1:-dev}"

case "$SAKI_PROFILE" in
  dev)
    SAKI_VERSION="0.2.0-dev"
    ;;
  release)
    SAKI_VERSION="0.2.0"
    ;;
  *)
    print -u2 -- "usage: scripts/build-firmware-profile.zsh [dev|release]"
    exit 2
    ;;
esac

SAKI_BUILD_DIR="$SAKI_REPO_ROOT/firmware/build-$SAKI_PROFILE"

source "$SAKI_SCRIPT_DIR/env-idf.zsh"
cd "$SAKI_REPO_ROOT/firmware"

print -- "Building Saki firmware $SAKI_VERSION ($SAKI_PROFILE)"
idf.py -B "$SAKI_BUILD_DIR" -DSAKI_BUILD_PROFILE="$SAKI_PROFILE" build
idf.py -B "$SAKI_BUILD_DIR" -DSAKI_BUILD_PROFILE="$SAKI_PROFILE" size

print -- "Firmware: $SAKI_BUILD_DIR/saki.bin"
