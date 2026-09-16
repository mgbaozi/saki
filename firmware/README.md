# Saki firmware

ESP-IDF firmware for the 正点原子 ATK-DNESP32S3B3 / ESP32S3 BOX3.

The initial baseline is derived from the vendor `01_lvgl_transplant` example. It retains the verified ESP32-S3, 16 MiB Flash, 8 MiB Octal PSRAM, ST7789, CHSC5432 and AW9523B configuration.

Build from the repository root:

```zsh
scripts/build-firmware.zsh
# Explicit equivalent:
scripts/build-firmware-profile.zsh dev

# Release candidate only:
scripts/build-firmware-profile.zsh release
```

The profiles generate independent `firmware/build-dev/sdkconfig` and
`firmware/build-release/sdkconfig` files. The developer image reports
`0.4.5-dev`; the formal 0.4.5 release profile reports `0.4.5`. Do not run
`idf.py set-target`: the reviewed ESP32-S3 board configuration is loaded from
`config/sdkconfig.vendor`.

After entering ROM download mode, flash an already-built profile explicitly:

```zsh
scripts/flash-firmware.zsh /dev/cu.usbmodemXXXXXX dev
scripts/flash-firmware.zsh /dev/cu.usbmodemXXXXXX release
```

Build the standalone ESP-IDF Unity protocol test image from the repository root:

```zsh
scripts/build-firmware-tests.zsh
```

This builds `firmware/test_app/build-unity/saki_firmware_tests.bin` for ESP32-S3 without
changing the connected device. Running the tests on hardware temporarily replaces
the display firmware, so capture the Unity result first and then flash the normal
`firmware/build-dev/saki.bin` image back to the board.

After putting the board into ROM download mode, run and monitor the test image with:

```zsh
scripts/run-firmware-tests.zsh /dev/cu.usbmodemXXXXXX
```

The test runner uses USB Serial/JTAG as its primary console. Press `*` in the Unity
menu to repeat all tests and `Ctrl-]` to leave the monitor.

## 0.4 multi-session development

A negotiated `multi-session` mode accepts complete, bounded four-item display
sets. The parser validates a peer-owned candidate before transport arbitration
and one UI queue copy. Legacy hello/status/clear retain their single-task mode.
The main view follows the latest user submission, with up to three colored status
entries in a right sidebar. Tapping an entry temporarily opens it; new submissions
or a 15-second timeout return to the default main view. Offline contracts and dev/release/Unity builds
pass; display, touch, transport switching and runtime memory still require target
hardware validation. See [0.4 TASKS](../docs/versions/0.4.0/TASKS.md).
