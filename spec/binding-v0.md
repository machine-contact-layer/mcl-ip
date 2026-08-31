# MCL-IP Binding v0

Status: **Research Draft**

## Purpose

Map MCL Link frames onto IP-capable communication without changing MCL Core or MCL Wire semantics.

## Candidate modes

### Datagram mode

One MCL Link frame per datagram where MTU permits.

Research candidates:

- UDP
- QUIC datagrams

### Reliable stream mode

Length-delimited MCL Link frames over a reliable byte stream.

Research candidates:

- TCP
- QUIC streams

## Endpoint offer

An MCL `TRANSPORT_OFFER` may reference:

```text
IPOffer {
    address_family
    address_or_local_reference
    port
    transport_mode
    security_mode?
    validity?
}
```

Exact representation belongs to MCL Wire.

## Handoff

If contact began on another binding, MCL-IP should preserve:

- MCL session reference
- semantic context generation
- peer identity/claim references
- negotiated Core/Wire versions

Handoff MUST NOT implicitly upgrade trust.

## Reliability

MCL Link semantic priority should remain visible even when the underlying IP transport is reliable. Transport reliability does not guarantee application timeliness or semantic freshness.

## Open questions

- UDP vs QUIC as the preferred reference datagram mode
- local-link endpoint discovery without infrastructure
- context migration and duplicate suppression across handoff
- framing overhead for small Tier-0 objects
