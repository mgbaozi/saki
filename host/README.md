# Saki Mac Host

The Host runs on macOS. It receives filtered Codex and Claude Code lifecycle hook events,
normalizes them into Saki status snapshots, and sends those snapshots to the display.

Create the development environment and run offline checks without a connected
device:

```zsh
python3.12 -m venv host/.venv
host/.venv/bin/pip install -e 'host[dev]'
scripts/check.zsh
host/.venv/bin/saki-host demo
```

The package version is `0.5.0`. USB remains available without
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
soak diagnostics.

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

## Extensible adapters and multiple sessions (0.5.0)

Codex and Claude Code adapters normalize allowlisted lifecycle metadata, filtered goals and
HMAC identities. The registry keeps at most 32 sessions and projects a stable
four-item full set (latest submission first, then attention-ranked sidebar items);
older firmware receives the same main session. Private checkpoints
restore as unconfirmed. On user submission, the hook reads task_title or prompt
in memory and derives a goal using local filtering and first-sentence truncation
(up to 48 characters and 96 UTF-8 bytes). This is not semantic summarization or
complete anonymization: retained text is copied verbatim, and a short prompt may
become the entire title. Rules remove recognized secrets, URLs and absolute paths;
relative paths, filenames and unrecognized sensitive content may remain.

The derived goal travels through local IPC, is stored in the private checkpoint,
and is sent to the device as task.title. The raw hook object and a separate full
prompt field are not persisted or forwarded. Transcripts, tool arguments/results
and model replies are not used to derive goals. Normal service sync logs omit goal
text; explicit `hook --stdout` diagnostics include it. See the
[0.5.0 privacy guidance](../docs/versions/0.5.0/USER_GUIDE.md#隐私与安全).

Use `saki-host hooks install|check|uninstall --source claude_code` (or `codex`),
choosing user or explicit project scope once. `saki-host sessions list` reads the
latest checkpoint; `sessions forget <id|all>` requests display-only cleanup.
See the [0.5.0 guide](../docs/versions/0.5.0/USER_GUIDE.md) for configuration preservation,
source limitations and troubleshooting.
