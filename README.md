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

## Status

Private research repository. Pre-v0.1. See [`spec/binding-v0.md`](spec/binding-v0.md).
