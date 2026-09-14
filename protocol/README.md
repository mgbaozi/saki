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
