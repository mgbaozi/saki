#!/bin/zsh

set -euo pipefail

SAKI_TEST_RUNNER_DIR="${0:A:h}"
SAKI_TEST_RUNNER_ROOT="${SAKI_TEST_RUNNER_DIR:h}"
SAKI_TEST_PORT="${1:-}"

if [[ -z "$SAKI_TEST_PORT" ]]; then
  print -u2 -- "usage: scripts/run-firmware-tests.zsh /dev/cu.usbmodemXXXXXX"
  exit 2
fi

if [[ "$SAKI_TEST_PORT" != /dev/cu.usb* || ! -e "$SAKI_TEST_PORT" ]]; then
  print -u2 -- "invalid or unavailable serial port: $SAKI_TEST_PORT"
  exit 2
fi

source "$SAKI_TEST_RUNNER_DIR/env-idf.zsh"
cd "$SAKI_TEST_RUNNER_ROOT/firmware/test_app"

print -- "This temporarily replaces the Saki display firmware with the Unity test image."
print -- "Press Ctrl-] to leave the monitor; flash firmware/build/saki.bin afterwards."
idf.py -B build-unity -p "$SAKI_TEST_PORT" flash monitor
