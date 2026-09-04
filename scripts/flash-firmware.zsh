#!/bin/zsh

set -euo pipefail

SAKI_SCRIPT_DIR="${0:A:h}"
SAKI_REPO_ROOT="${SAKI_SCRIPT_DIR:h}"
SAKI_FLASH_PORT="${1:-}"
SAKI_PROFILE="${2:-dev}"

if [[ -z "$SAKI_FLASH_PORT" ]]; then
  print -u2 -- "usage: scripts/flash-firmware.zsh /dev/cu.usbmodemXXXXXX [dev|release]"
  exit 2
fi

if [[ "$SAKI_FLASH_PORT" != /dev/cu.usb* || ! -e "$SAKI_FLASH_PORT" ]]; then
  print -u2 -- "invalid or unavailable serial port: $SAKI_FLASH_PORT"
  exit 2
fi

if [[ "$SAKI_PROFILE" != "dev" && "$SAKI_PROFILE" != "release" ]]; then
  print -u2 -- "invalid profile: $SAKI_PROFILE (expected dev or release)"
  exit 2
fi

SAKI_BUILD_DIR="$SAKI_REPO_ROOT/firmware/build-$SAKI_PROFILE"
if [[ ! -f "$SAKI_BUILD_DIR/saki.bin" ]]; then
  print -u2 -- "firmware is not built: $SAKI_BUILD_DIR/saki.bin"
  print -u2 -- "run scripts/build-firmware-profile.zsh $SAKI_PROFILE first"
  exit 2
fi

source "$SAKI_SCRIPT_DIR/env-idf.zsh"
cd "$SAKI_REPO_ROOT/firmware"

idf.py -B "$SAKI_BUILD_DIR" -DSAKI_BUILD_PROFILE="$SAKI_PROFILE" \
  -p "$SAKI_FLASH_PORT" flash
