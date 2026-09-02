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

Canonical encoding, network byte order, minimum 8 bytes:

```text
u8   address_family     0 unspecified, 1 IPv4, 2 IPv6, 3 opaque local reference
u8   mode               0 datagram, 1 stream
u8   security_claim     0 none, 1 TLS claimed, 2 DTLS claimed
u8   address_size
u8   address[address_size]
u16  port
u16  validity_s         0 means unspecified, not infinite
```

`address_size` is fixed by family: 4 for IPv4, 16 for IPv6, 0 for unspecified,
1..16 for an opaque local reference. Any other combination is non-canonical and
MUST be rejected rather than interpreted.

The opaque local reference exists so that first contact does not force a peer to
disclose a routable address before it has any reason to trust the asker. The
integrator resolves it by local policy.

`security_claim` is what the offering peer says it will do. It is never evidence
that it did. Local policy decides what a claim is worth.

## Carriage

### Datagram mode

One Link frame per datagram; the datagram boundary is the frame boundary.
Trailing bytes are rejected, because on a message-oriented transport they
indicate a malformed or concatenated datagram, and ignoring them is how framing
confusion becomes semantic confusion.

UDP and QUIC datagrams already detect corruption, so the Link frame's integrity
field is permitted but not required in this mode.

### Stream mode

A reliable byte stream has no message boundaries, so each Link frame is preceded
by its total length:

```text
u16 frame_size
u8  link_frame[frame_size]
```

The prefix duplicates information the frame already carries, deliberately. It
lets a receiver skip a frame it cannot decode without losing stream
synchronisation, which is the difference between dropping one frame and dropping
the connection. A zero-length record is a framing error, not an empty frame.

## MTU

Datagram mode subtracts the IP and UDP headers from the path MTU: 28 bytes for
IPv4, 48 for IPv6. An MTU that cannot carry a minimum Link frame carries
nothing, and the binding says so rather than silently truncating.

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
