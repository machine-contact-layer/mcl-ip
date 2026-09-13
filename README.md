<p align="center">
  <img src="https://raw.githubusercontent.com/machine-contact-layer/.github/main/profile/banner.png" alt="Machine Contact Layer (MCL) banner: black and white checkerboard with the OJOBIT wordmark" width="100%">
</p>

<h1 align="center">MCL-IP</h1>

<p align="center"><strong>MCL over the network you already have — UDP, TCP, or anything that moves a datagram.</strong></p>

<p align="center">
  IP transport binding for the Machine Contact Layer (MCL): carry
  machine-to-machine contact frames over UDP on Wi-Fi, Ethernet or any datagram
  network, with a Stable datagram profile. Freestanding C99, no network stack
  required.
</p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-ip/actions/workflows/ci.yml"><img alt="CI status" src="https://github.com/machine-contact-layer/mcl-ip/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/machine-contact-layer/mcl-ip/blob/main/LICENSE"><img alt="License: Apache-2.0" src="https://img.shields.io/badge/license-Apache--2.0-blue"></a>
  <img alt="IP-DATAGRAM profile 1: Stable" src="https://img.shields.io/badge/IP--DATAGRAM%20profile%201-Stable-brightgreen">
  <img alt="Language: freestanding C99" src="https://img.shields.io/badge/C99-freestanding-informational">
</p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-sdk"><b>SDK</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-core"><b>MCL overview</b></a> ·
  <a href="spec/ip-datagram-profile-v1.md"><b>Specification</b></a> ·
  <a href="evidence/"><b>Evidence</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-core/blob/main/REPORTING.md"><b>Report a defect</b></a>
</p>

---

When two machines can already reach each other over IP, that is usually the
right place for the contact to live. MCL-IP is the binding for it — and the most
common destination when a contact migrates off a slower bootstrap medium such as
BLE or [acoustic](https://github.com/machine-contact-layer/mcl-ap).

If your machines are on the same network today, this binding plus `MCL Base 1`
in [mcl-sdk](https://github.com/machine-contact-layer/mcl-sdk) is all you need:
no discovery, no microphone and no rendezvous step.

It is part of the [Machine Contact Layer](https://github.com/machine-contact-layer/mcl-core),
an open protocol for machine-to-machine discovery, contact and transport
migration.

## What MCL-IP provides

- **IP-DATAGRAM profile 1 (Stable)** — one MCL Link frame per datagram; the
  datagram boundary is the frame boundary
- **Endpoint representation** for transport offers, so a peer learns where to
  reach you on IP
- **A freestanding C99 reference** with no allocation, no global mutable state
  and no network stack: you connect it to the sockets your platform already has

## Use it

The normal path is [mcl-sdk](https://github.com/machine-contact-layer/mcl-sdk):
its `MCL-BASE-DEPLOYMENT-1` runs on IP-DATAGRAM profile 1, and you supply a send
function and hand received datagrams to the SDK.

Using the binding directly:

- [`include/mcl/ip_binding.h`](include/mcl/ip_binding.h) — public API
- [`src/ip_binding.c`](src/ip_binding.c) — implementation
- [`tests/test_ip_binding.c`](tests/test_ip_binding.c) — round trips and refusal cases

The library builds under `/W4 /WX`, and the compiled object references no libc
symbol, so it links on a freestanding target.

There is **no registered MCL UDP port**, and discovery is not part of the Stable
IP profile: the endpoint arrives in a `TRANSPORT_OFFER` or is arranged out of
band. Port 49913 appears in this repository only as a reference-harness default.

## Maturity

| | Status |
|---|---|
| `transport_id = 2` (`MCL_IP`) | **Stable.** Recorded in [`mcl-link/registries/transport-ids-v0.1.json`](https://github.com/machine-contact-layer/mcl-link/blob/main/registries/transport-ids-v0.1.json). |
| [`spec/ip-datagram-profile-v1.md`](spec/ip-datagram-profile-v1.md) — `profile_id = 1` | **Stable.** Safe to build against. |
| [`spec/binding-v0.md`](spec/binding-v0.md) | **Research Draft.** Stream carriage, endpoint negotiation and MTU behaviour are not frozen. |
| `profile_id = 192` | **Experimental Use**, permanently. Profile 1 is a separate assignment. |

## Scope and non-goals

MCL-IP covers carriage of MCL Link frames over IP, endpoint representation,
datagram mapping, and preserving the session across a handoff. It does not
define IP itself, Wi-Fi or Ethernet hardware, NAT traversal, cloud identity,
application APIs or MCL semantics.

## Verified on hardware

- **Laptop ↔ ESP32-S3 over 2.4 GHz UDP.** 111 datagrams each way, 103 accepted,
  8 deliberately malformed frames refused with the status the specification
  requires, zero loss — [`evidence/e4-udp-2g4-20260902/`](evidence/e4-udp-2g4-20260902/),
  rig in [`hardware/esp32-udp-peer/`](hardware/esp32-udp-peer/).
- **Android 14 handset as an IP peer**, both directions, at both Wire/Link
  majors — the Stable path end to end: major-1 frames carrying `PRESENCE` and a
  `TRANSPORT_OFFER` on profile 1, and refusal of reserved identifiers and of a
  Candidate object at the Stable major. 142 checks, 0 failed; 400 sustained
  frames, 0 lost — [`evidence/e4-android-udp-20260904/`](evidence/e4-android-udp-20260904/),
  harness in [`hardware/android-udp-peer/`](hardware/android-udp-peer/).

## Security

An IP connection establishes nothing about the peer. `security_claim` in an
endpoint offer is what the offering peer *says* it will do, never evidence that
it did. For authentication and confidentiality, run MCL inside DTLS or a
controlled network. See [`SECURITY.md`](https://github.com/machine-contact-layer/mcl-core/blob/main/SECURITY.md).

## Related repositories

[mcl-core](https://github.com/machine-contact-layer/mcl-core) ·
[mcl-link](https://github.com/machine-contact-layer/mcl-link) ·
[mcl-sdk](https://github.com/machine-contact-layer/mcl-sdk) ·
[mcl-ble](https://github.com/machine-contact-layer/mcl-ble) ·
[mcl-ap](https://github.com/machine-contact-layer/mcl-ap)

## License

Apache-2.0. See [`LICENSE`](LICENSE).
