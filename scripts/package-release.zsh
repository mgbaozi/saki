#!/bin/zsh

set -euo pipefail

SAKI_SCRIPT_DIR="${0:A:h}"
SAKI_REPO_ROOT="${SAKI_SCRIPT_DIR:h}"
SAKI_VERSION="0.2.0"
SAKI_MODE="candidate"

if [[ "${1:-}" == "--final" ]]; then
  SAKI_MODE="final"
  shift
fi
if [[ $# -ne 0 ]]; then
  print -u2 -- "usage: scripts/package-release.zsh [--final]"
  exit 2
fi

SAKI_BUILD_DIR="$SAKI_REPO_ROOT/firmware/build-release"
SAKI_ARTIFACTS_DIR="$SAKI_REPO_ROOT/artifacts"
SAKI_PACKAGE_NAME="saki-$SAKI_VERSION"
if [[ "$SAKI_MODE" == "candidate" ]]; then
  SAKI_PACKAGE_NAME="$SAKI_PACKAGE_NAME-candidate"
fi
SAKI_OUTPUT_DIR="$SAKI_ARTIFACTS_DIR/$SAKI_PACKAGE_NAME"
SAKI_ARCHIVE="$SAKI_ARTIFACTS_DIR/$SAKI_PACKAGE_NAME.zip"
SAKI_ARCHIVE_CHECKSUM="$SAKI_ARCHIVE.sha256"

if [[ -e "$SAKI_OUTPUT_DIR" || -e "$SAKI_ARCHIVE" || -e "$SAKI_ARCHIVE_CHECKSUM" ]]; then
  print -u2 -- "release output already exists: $SAKI_PACKAGE_NAME"
  print -u2 -- "move or remove the existing output before packaging again"
  exit 2
fi

SAKI_GIT_COMMIT="UNBORN"
if git -C "$SAKI_REPO_ROOT" rev-parse --verify HEAD >/dev/null 2>&1; then
  SAKI_GIT_COMMIT="$(git -C "$SAKI_REPO_ROOT" rev-parse HEAD)"
fi
SAKI_GIT_BRANCH="$(git -C "$SAKI_REPO_ROOT" branch --show-current)"
SAKI_GIT_STATUS="clean"
if [[ -n "$(git -C "$SAKI_REPO_ROOT" status --porcelain=v1)" ]]; then
  SAKI_GIT_STATUS="dirty"
fi

if [[ "$SAKI_MODE" == "final" ]]; then
  if [[ "$SAKI_GIT_COMMIT" == "UNBORN" || "$SAKI_GIT_STATUS" != "clean" ]]; then
    print -u2 -- "final packaging requires an existing clean Git commit"
    exit 2
  fi
  SAKI_HOST_VERSION="$($SAKI_REPO_ROOT/host/.venv/bin/python -c \
    'from saki_host import __version__; print(__version__)')"
  if [[ "$SAKI_HOST_VERSION" != "$SAKI_VERSION" ]]; then
    print -u2 -- "final Host version is $SAKI_HOST_VERSION; expected $SAKI_VERSION"
    exit 2
  fi
fi

"$SAKI_SCRIPT_DIR/build-firmware-profile.zsh" release

SAKI_BUILT_VERSION="$($SAKI_REPO_ROOT/host/.venv/bin/python -c \
  'import json, sys; print(json.load(open(sys.argv[1]))["project_version"])' \
  "$SAKI_BUILD_DIR/project_description.json")"
if [[ "$SAKI_BUILT_VERSION" != "$SAKI_VERSION" ]]; then
  print -u2 -- "release firmware reports $SAKI_BUILT_VERSION; expected $SAKI_VERSION"
  exit 2
fi

source "$SAKI_SCRIPT_DIR/env-idf.zsh"
mkdir -p "$SAKI_ARTIFACTS_DIR"
SAKI_TEMP_DIR="$(mktemp -d "$SAKI_ARTIFACTS_DIR/.saki-release.XXXXXX")"
trap '[[ -d "$SAKI_TEMP_DIR" ]] && rm -rf "$SAKI_TEMP_DIR"' EXIT INT TERM

cp "$SAKI_BUILD_DIR/bootloader/bootloader.bin" "$SAKI_TEMP_DIR/bootloader.bin"
cp "$SAKI_BUILD_DIR/partition_table/partition-table.bin" \
  "$SAKI_TEMP_DIR/partition-table.bin"
cp "$SAKI_BUILD_DIR/saki.bin" "$SAKI_TEMP_DIR/saki.bin"
cp "$SAKI_BUILD_DIR/sdkconfig" "$SAKI_TEMP_DIR/sdkconfig.release"
cp "$SAKI_REPO_ROOT/firmware/dependencies.lock" "$SAKI_TEMP_DIR/dependencies.lock"
cp "$SAKI_REPO_ROOT/LICENSE" "$SAKI_TEMP_DIR/LICENSE"
cp "$SAKI_REPO_ROOT/NOTICE" "$SAKI_TEMP_DIR/NOTICE"
mkdir -p "$SAKI_TEMP_DIR/licenses"
cp "$SAKI_REPO_ROOT/firmware/components/saki_ui/fonts/licenses/NotoSansCJK-OFL-1.1.txt" \
  "$SAKI_TEMP_DIR/licenses/NotoSansCJK-OFL-1.1.txt"

esptool.py --chip esp32s3 merge_bin \
  --output "$SAKI_TEMP_DIR/saki-$SAKI_VERSION-full.bin" \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 16MB \
  0x0 "$SAKI_TEMP_DIR/bootloader.bin" \
  0x8000 "$SAKI_TEMP_DIR/partition-table.bin" \
  0x10000 "$SAKI_TEMP_DIR/saki.bin"

SAKI_IDF_VERSION="$(idf.py --version)"
SAKI_PYTHON_VERSION="$(python3 --version)"
SAKI_ESPTOOL_VERSION="$(esptool.py version | tail -n 1)"
SAKI_NINJA_VERSION="$(ninja --version)"
SAKI_CMAKE_VERSION="$(cmake --version | head -n 1)"
SAKI_HOST_VERSION="$($SAKI_REPO_ROOT/host/.venv/bin/python -c \
  'from saki_host import __version__; print(__version__)')"
SAKI_PYSERIAL_VERSION="$($SAKI_REPO_ROOT/host/.venv/bin/python -c \
  'import serial; print(serial.__version__)')"
SAKI_LOCK_HASH="$(shasum -a 256 "$SAKI_TEMP_DIR/dependencies.lock" | awk '{print $1}')"
SAKI_GENERATED_AT="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

{
  print -r -- "saki_version=$SAKI_VERSION"
  print -r -- "artifact_mode=$SAKI_MODE"
  print -r -- "firmware_version=$SAKI_BUILT_VERSION"
  print -r -- "host_source_version=$SAKI_HOST_VERSION"
  print -r -- "git_commit=$SAKI_GIT_COMMIT"
  print -r -- "git_branch=${SAKI_GIT_BRANCH:-DETACHED}"
  print -r -- "git_status=$SAKI_GIT_STATUS"
  print -r -- "generated_at_utc=$SAKI_GENERATED_AT"
  print -r -- "esp_idf=$SAKI_IDF_VERSION"
  print -r -- "python=$SAKI_PYTHON_VERSION"
  print -r -- "esptool=$SAKI_ESPTOOL_VERSION"
  print -r -- "ninja=$SAKI_NINJA_VERSION"
  print -r -- "cmake=$SAKI_CMAKE_VERSION"
  print -r -- "pyserial=$SAKI_PYSERIAL_VERSION"
  print -r -- "target=esp32s3"
  print -r -- "flash_size=16MB"
  print -r -- "flash_mode=dio"
  print -r -- "flash_frequency=80m"
  print -r -- "dependencies_lock_sha256=$SAKI_LOCK_HASH"
} > "$SAKI_TEMP_DIR/BUILD-INFO.txt"

{
  print -r -- "# Saki $SAKI_VERSION firmware"
  print -r -- ""
  print -r -- "This package targets the ATK-DNESP32S3B3 / ESP32S3 BOX3 with 16 MB Flash."
  print -r -- "It does not contain a factory-flash backup or any device-specific credentials."
  print -r -- "License and attribution terms are provided in LICENSE and NOTICE."
  print -r -- ""
  print -r -- "## Enter download mode"
  print -r -- ""
  print -r -- "Hold K0, briefly press RST, wait one second, then release K0. Re-detect the"
  print -r -- "dynamic /dev/cu.usbmodem* port before flashing."
  print -r -- ""
  print -r -- "## Flash the merged image"
  print -r -- ""
  print -r -- '```zsh'
  print -r -- "esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXXXX --baud 460800 write_flash 0x0 saki-$SAKI_VERSION-full.bin"
  print -r -- '```'
  print -r -- ""
  print -r -- "Alternatively, flash the split images:"
  print -r -- ""
  print -r -- '```zsh'
  print -r -- "esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXXXX --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 saki.bin"
  print -r -- '```'
  print -r -- ""
  print -r -- "After flashing, briefly press RST without holding K0. Verify checksums with:"
  print -r -- ""
  print -r -- '```zsh'
  print -r -- "shasum -a 256 -c SHA256SUMS"
  print -r -- '```'
} > "$SAKI_TEMP_DIR/README.md"

(
  cd "$SAKI_TEMP_DIR"
  shasum -a 256 \
    bootloader.bin \
    partition-table.bin \
    saki.bin \
    "saki-$SAKI_VERSION-full.bin" \
    sdkconfig.release \
    dependencies.lock \
    LICENSE \
    NOTICE \
    licenses/NotoSansCJK-OFL-1.1.txt \
    BUILD-INFO.txt \
    README.md > SHA256SUMS
  shasum -a 256 -c SHA256SUMS
)

mv "$SAKI_TEMP_DIR" "$SAKI_OUTPUT_DIR"
(
  cd "$SAKI_ARTIFACTS_DIR"
  /usr/bin/zip -X -q -r "${SAKI_PACKAGE_NAME}.zip" "$SAKI_PACKAGE_NAME"
  shasum -a 256 "${SAKI_PACKAGE_NAME}.zip" > "${SAKI_PACKAGE_NAME}.zip.sha256"
)

print -- "Release package: $SAKI_OUTPUT_DIR"
print -- "Archive: $SAKI_ARCHIVE"
print -- "Archive checksum: $SAKI_ARCHIVE_CHECKSUM"
print -- "Mode: $SAKI_MODE (Git $SAKI_GIT_COMMIT, $SAKI_GIT_STATUS)"
