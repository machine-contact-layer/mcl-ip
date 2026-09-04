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

## Status

**Not one status. Two, and they are deliberately different.**

| | Status |
|---|---|
| `transport_id = 2` (`MCL_IP`) | **Stable.** MCL Standards Action, 2026-09-04. Frozen; the record is in [`mcl-link/registries/transport-ids-v0.1.json`](../mcl-link/registries/transport-ids-v0.1.json). |
| [`spec/ip-datagram-profile-v1.md`](spec/ip-datagram-profile-v1.md) — `profile_id = 1` | **Stable.** MCL Standards Action, 2026-09-04. One Link frame per datagram; the datagram boundary is the frame boundary. |
| [`spec/binding-v0.md`](spec/binding-v0.md) | **Research Draft.** The wider binding — stream carriage, endpoint negotiation, MTU behaviour — is not frozen and is not a basis for an implementation. |
| `profile_id = 192` | **Experimental Use, permanently.** It was never relabelled: profile 1 is a separate assignment. Evidence gathered under 192 stays evidence about 192. |

So: the datagram profile is safe to build against and the rest of this
repository is not. If you need a single sentence — **what v1.0 freezes here is
one profile under one transport identifier, and nothing else.**

There is **no registered MCL UDP port.** Discovery is not part of Stable IP:
the endpoint arrives in a `TRANSPORT_OFFER`. Port 49913 appears in this
repository as an experimental reference-harness default and is not an
assignment.

The reference implementation is C99, freestanding, and contains **no network
stack**, so nothing here opens a socket for you.

### Evidence

**`E4 MULTI_DEVICE_OVER_AIR`** — 2026-09-02. Windows laptop and an ESP32-S3 SoftAP
peer exchanged MCL Link frames over 2.4 GHz UDP. 111 datagrams each way, 103
accepted, 8 deliberately malformed frames refused, zero loss. Full record and
limits in [`evidence/e4-udp-2g4-20260902/`](evidence/e4-udp-2g4-20260902/).

The eight refusals are the result that matters: truncation, a trailing byte, an
unknown class, a reserved bit, a future major version, a corrupted frame check,
a corrupted payload and an overstated length were each refused with the status
the specification requires.

**`E4 MULTI_DEVICE_OVER_AIR`** — 2026-09-04. A commodity Android 14 handset
(arm64-v8a) as a third IP peer over its own 2.4 GHz SoftAP, in both directions
and at both Wire/Link majors. 142 checks, 0 failed; 400 sustained frames, 0
lost, 0 retries. Full record and limits in
[`evidence/e4-android-udp-20260904/`](evidence/e4-android-udp-20260904/); harness
in [`hardware/android-udp-peer/`](hardware/android-udp-peer/).

That run exercised the **Stable** path end to end: major-1 Link frames carrying
a major-1 `PRESENCE` and a `TRANSPORT_OFFER` on `profile_id` 1, and refusals of
reserved `profile_id` 0, reserved `transport_id` 0, and a Candidate object
offered at the Stable major.

**Not** independent interoperability. Both ends compile the same sources, so a
shared misreading of the specification would pass on both sides.

Reproduce with [`hardware/esp32-udp-peer/`](hardware/esp32-udp-peer/).

### Security

An IP connection establishes nothing about the peer. `security_claim` in an
endpoint offer is what the offering peer *says* it will do, never evidence that
it did. See [`SECURITY.md`](https://github.com/machine-contact-layer/mcl-core/blob/main/SECURITY.md).
