# MCL-IP

`mcl-ip` defines an optional IP/network binding for the Machine Contact Layer.

MCL is not anti-network. IP is a richer transport that peers may use directly or negotiate after first contact over another binding such as MCL-AP.

## Scope

- carriage of MCL Link frames over IP-capable transports
- endpoint representation and negotiation
- datagram/stream mapping
- session/context preservation across handoff
- transport-specific reliability and MTU behavior
- conformance vectors for IP carriage

## Non-goals

MCL-IP does not define:

- IP itself
- Wi-Fi or Ethernet hardware
- NAT traversal infrastructure
- cloud identity
- application-specific APIs
- MCL semantics


## Implementation status

The reference implementation is present, freestanding C99, with no allocation
and no global mutable state. It contains **no network stack**: how bytes reach the
medium is the integrator's decision. A binding describes a mapping; it does not
become a network stack.

Verified: builds under `/W4 /WX`, and the compiled object references no libc
symbol (no `memcpy`, `memset`, `malloc`, or stdio), so it links on a
freestanding target.

- `include/mcl/ip_binding.h` — public API
- `src/ip_binding.c` — implementation
- `tests/test_ip_binding.c` — round trips and the negative cases

**Status: Research Draft.** Nothing here is frozen. Assigned transport id
`0x02` is provisional until Candidate Specification maturity.

## Status

Private research repository. Pre-v0.1. See [`spec/binding-v0.md`](spec/binding-v0.md).

### Evidence

**`E4 MULTI_DEVICE_OVER_AIR`** — 2026-09-02. Windows laptop and an ESP32-S3 SoftAP
peer exchanged MCL Link frames over 2.4 GHz UDP. 111 datagrams each way, 103
accepted, 8 deliberately malformed frames refused, zero loss. Full record and
limits in [`evidence/e4-udp-2g4-20260902/`](evidence/e4-udp-2g4-20260902/).

The eight refusals are the result that matters: truncation, a trailing byte, an
unknown class, a reserved bit, a future major version, a corrupted frame check,
a corrupted payload and an overstated length were each refused with the status
the specification requires.

**Not** independent interoperability. Both ends compile the same sources, so a
shared misreading of the specification would pass on both sides.

Reproduce with [`hardware/esp32-udp-peer/`](hardware/esp32-udp-peer/).

### Security

An IP connection establishes nothing about the peer. `security_claim` in an
endpoint offer is what the offering peer *says* it will do, never evidence that
it did. See [`SECURITY.md`](https://github.com/machine-contact-layer/mcl-core/blob/main/SECURITY.md).
