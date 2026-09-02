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
