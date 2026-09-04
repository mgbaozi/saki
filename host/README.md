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

The development package version is `0.2.0.dev0`; the first formal Host release
will be `0.2.0`, matching the firmware release.

For the normal long-running setup, install and inspect the macOS LaunchAgent:

```zsh
scripts/saki-service.zsh install
scripts/saki-service.zsh status
scripts/saki-service.zsh logs
```

Codex hooks submit sanitized snapshots to the Host's Unix Domain Socket. Hook
processes never open the USB serial device directly; the single persistent Host
session owns device discovery, handshaking, ACK/retry, heartbeat and hot-plug
reconnection.
