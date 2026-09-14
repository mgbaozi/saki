# Saki Mac Host

The Host runs on macOS. It receives sanitized Codex lifecycle hook events, normalizes them into Saki status snapshots, and sends those snapshots to the display.

Create the development environment and run offline checks without a connected
device:

```zsh
python3.12 -m venv host/.venv
host/.venv/bin/pip install -e 'host[dev]'
scripts/check.zsh
host/.venv/bin/saki-host demo
```

The development package version is `0.3.0.dev0`. USB remains available without
Bluetooth dependencies. For BLE development on macOS, install the optional
extra and complete first pairing in the foreground:

```zsh
host/.venv/bin/pip install -e 'host[dev,ble]'
host/.venv/bin/saki-host ble list
host/.venv/bin/saki-host ble pair
host/.venv/bin/saki-host doctor --transport ble
host/.venv/bin/saki-host send --transport ble --state idle
host/.venv/bin/saki-host replay protocol/fixtures/v1/sessions/basic.ndjson --transport ble
host/.venv/bin/saki-host ble fuzz --count 40 --seed 20260907
host/.venv/bin/saki-host ble soak --count 100 --duration 10 --report artifacts/ble-smoke.json
```

Hold K2 for at least 2 seconds and release it before 5 seconds to open the
120-second pairing window. Holding K2 for 5 seconds clears all device-side BLE
bonds. Stop the LaunchAgent before running the exclusive BLE cycle, fuzz, or
soak diagnostics. The 0.3 BLE path still requires full target-hardware
acceptance before a formal release.

For the normal long-running setup, install and inspect the macOS LaunchAgent:

```zsh
scripts/saki-service.zsh install
scripts/saki-service.zsh status
scripts/saki-service.zsh logs
```

Codex hooks submit sanitized snapshots to the Host's Unix Domain Socket. Hook
processes never open the USB serial device directly; the single persistent Host
session owns USB/BLE discovery, handshaking, ACK/retry, heartbeat, transport
priority and reconnection. `serve --transport auto` prefers USB and only uses a
previously verified BLE binding as fallback.
