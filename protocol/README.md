# Saki protocol contract

`schema/v1` contains the machine-readable contract for protocol version 1. `fixtures/v1` contains sanitized examples used by the Mac Host and firmware interoperability tests.

The wire format is UTF-8 NDJSON. Each compact JSON object is followed by one LF byte. The maximum frame payload is 2048 bytes, excluding LF.

JSON Schema `maxLength` counts Unicode code points, while the firmware limits in
[`SPEC.md`](../docs/versions/0.2.0/SPEC.md) are UTF-8 byte limits. Host and firmware
implementations must enforce the byte limits in addition to Schema validation.

`pong.runtime` is an optional v1 extension introduced by the `0.2.0-dev`
firmware. Older firmware may omit it; newer peers must ignore additional
runtime metrics they do not recognize. All reported heap and stack values use
bytes.

Fixtures under `sessions/` contain one wire message per line and must remain free of credentials, private paths and real conversation content.
