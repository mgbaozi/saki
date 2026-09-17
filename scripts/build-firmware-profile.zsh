#!/bin/zsh

set -euo pipefail

SAKI_SCRIPT_DIR="${0:A:h}"
SAKI_REPO_ROOT="${SAKI_SCRIPT_DIR:h}"
SAKI_PROFILE="${1:-dev}"

case "$SAKI_PROFILE" in
  dev)
    SAKI_VERSION="0.5.0-dev"
    ;;
  release)
    SAKI_VERSION="0.5.0"
    ;;
  *)
    print -u2 -- "usage: scripts/build-firmware-profile.zsh [dev|release]"
    exit 2
    ;;
esac

SAKI_BUILD_DIR="$SAKI_REPO_ROOT/firmware/build-$SAKI_PROFILE"
SAKI_CONFIG_FINGERPRINT="$(shasum -a 256 \
  "$SAKI_REPO_ROOT/firmware/config/sdkconfig.vendor" \
  "$SAKI_REPO_ROOT/firmware/sdkconfig.defaults" \
  "$SAKI_REPO_ROOT/firmware/config/sdkconfig.$SAKI_PROFILE" \
  | shasum -a 256 | awk '{print $1}')"
SAKI_PROFILE_SDKCONFIG="$SAKI_BUILD_DIR/sdkconfig.$SAKI_CONFIG_FINGERPRINT"

source "$SAKI_SCRIPT_DIR/env-idf.zsh"
cd "$SAKI_REPO_ROOT/firmware"

print -- "Building Saki firmware $SAKI_VERSION ($SAKI_PROFILE)"
idf.py \
  -B "$SAKI_BUILD_DIR" \
  -DIDF_TARGET=esp32s3 \
  -DSAKI_BUILD_PROFILE="$SAKI_PROFILE" \
  -DSDKCONFIG="$SAKI_PROFILE_SDKCONFIG" \
  build
cp "$SAKI_PROFILE_SDKCONFIG" "$SAKI_BUILD_DIR/sdkconfig"
idf.py \
  -B "$SAKI_BUILD_DIR" \
  -DIDF_TARGET=esp32s3 \
  -DSAKI_BUILD_PROFILE="$SAKI_PROFILE" \
  -DSDKCONFIG="$SAKI_PROFILE_SDKCONFIG" \
  size

print -- "Firmware: $SAKI_BUILD_DIR/saki.bin"
