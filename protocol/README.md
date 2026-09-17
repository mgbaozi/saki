# Saki protocol contract

`schema/v1` contains the machine-readable contract for protocol version 1. `fixtures/v1` contains sanitized examples used by the Mac Host and firmware interoperability tests.

The wire format is UTF-8 NDJSON. Each compact JSON object is followed by one LF byte. The maximum frame payload is 2048 bytes, excluding LF.

JSON Schema `maxLength` counts Unicode code points, while the firmware limits in
[`SPEC.md`](../docs/versions/0.2.0/SPEC.md) are UTF-8 byte limits. Host and firmware
implementations must enforce the byte limits in addition to Schema validation.
`status.elapsed_ms` is limited to the JSON safe-integer range
`0..9007199254740991`; this keeps the Schema, Python Host and cJSON firmware
parser numerically consistent.

`pong.runtime` is an optional v1 extension introduced by the `0.2.0-dev`
firmware. Saki 0.3 adds optional BLE and transport-arbitration counters without
changing protocol version 1. Older firmware may omit them; newer peers must
ignore additional runtime metrics they do not recognize. All reported heap and
stack values use bytes. Compatibility and arbitration behavior are specified in
the [0.3.0 specification](../docs/versions/0.3.0/SPEC.md).

Fixtures under `sessions/` contain one wire message per line and must remain
free of credentials, private paths and real conversation content. The paired
`handoff-*-usb.ndjson` and `handoff-*-ble.ndjson` files model the two physical
legs of one logical Host session; transport metadata is intentionally not added
to the v1 wire messages.

## 0.4 capability-gated complete display sets

Devices advertise `multi-session`; hosts select it with a second hello containing
`mode:"multi-session"` and require the same mode in the response. Only then may
hosts send the new `sessions` family. The frame carries 0–4 complete items and
`total`, `hidden_attention`, `capacity_rejected`; items add source, run_id,
revision and fresh to the existing snapshot fields. Item 0 is the latest accepted
user submission and the default main view; up to three remaining items form the
attention-ranked sidebar. Parsers preserve this order. Empty arrays clear the set.
There are no partial item updates or multi-frame transactions. The entire compact
frame must fit 2048 bytes; the Host shortens optional UTF-8 display text to fit.

Global session/seq and transport arbitration govern the complete set. ACK
`applied:true` commits it; exact byte-for-byte retries can receive
`applied:false,committed:true` only while that same sequence is still committed.
Ordinary stale/conflicting messages do not get committed confirmation. A committed
multi-session Host session rejects legacy writes even after a legacy re-hello.
Fresh Host sessions can choose legacy mode; older firmware receives the same latest-submission focus.

Schema cannot express all byte limits, duplicate task identities or cross-field
counts: Host/firmware tests additionally enforce them. JSON depth is bounded at
16 and embedded/escaped NUL or trailing non-whitespace is rejected. See the
[0.4 specification](../docs/versions/0.4.0/SPEC.md) for compatibility and recovery.

## 0.5 generic source labels

Devices that support product-independent source labels advertise `generic-source`
alongside `multi-session`. The capability becomes active only after the Host's
second hello selects `mode:"multi-session"`; a capability on a legacy status
session does not authorize generic sources.

With `generic-source`, each non-`legacy` item uses a 1–32 byte lowercase ASCII
source key matching `^[a-z][a-z0-9_]{0,31}$`, a 32-character lowercase hexadecimal
HMAC task ID, and a non-empty `agent.name` of at most 32 UTF-8 bytes. Firmware
displays the supplied bounded name and does not map product keys to labels.
`legacy` remains reserved for compatibility and displays as `Agent`.

Without the capability, 0.5 Hosts keep the 0.4.5 `codex` and `claude_code` keys,
but project any future registered source as `legacy` with the label `Agent`.
The downgrade changes only the outgoing copy; in-memory source identity and HMAC
task identity remain unchanged. Old firmware therefore never receives an unknown
source that could reject the complete display set.
