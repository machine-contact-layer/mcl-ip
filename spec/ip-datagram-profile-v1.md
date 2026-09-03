# MCL IP-DATAGRAM profile v1

Status: **Candidate**
Transport: `MCL_IP`, `transport_id = 2`
Profile identifier: **not yet assigned** — see §9
Satisfies: `mcl-core/governance/V1_SCOPE.md` §5.6, release gate item 7

## 0. What this document is, and why it is Candidate rather than Stable

This is the **normative** profile. Every parameter below is fixed *by this
document*, not by whatever `mcl-ip/src/ip_binding.c` happens to do. That
distinction is the entire point: a profile whose parameters are defined by a
reference implementation cannot be implemented independently, and independent
implementation is what MCL v1.0 has to demonstrate.

It is published at **Candidate** because the profile registry requires a second
independent implementation to have interoperated with a profile before a
Standards Action assignment may be proposed, while the independent
implementation needs a specification to be written against. `V1_SCOPE.md` §5.6
breaks that loop by separating the two acts. §9 states where this profile sits
in that sequence.

## 1. Scope

**This profile defines carriage only.**

```text
IN SCOPE
    how one MCL Link frame occupies one IP datagram
    what a receiver does with a malformed datagram
    the size contract, and how it interacts with path MTU
    what an IP endpoint means, and what it does not
    the frame check requirement

NOT IN SCOPE
    which UDP port to use            -- deliberately, see section 3
    how to find an unknown MCL peer  -- deliberately, see section 8
    stream carriage                  -- separate profile, not v1
    confidentiality or authentication -- none, see section 10
```

## 2. Framing

> **A datagram carries exactly one Link frame and nothing else.**

The datagram boundary **IS** the frame boundary. There is no length prefix,
because the transport already delimits.

```text
+--------------------------------------------------+
| IP header | UDP header |     one Link frame       |
+--------------------------------------------------+
                         |<--- the entire payload -->|
```

A receiver **MUST**:

| Condition | Behaviour |
|---|---|
| Payload decodes as exactly one Link frame, consuming every byte | accept |
| Payload has bytes remaining after a complete frame | **refuse the whole datagram** |
| Payload is shorter than the frame it declares | refuse |
| Payload is empty | refuse |
| Frame is otherwise invalid per Link | refuse |

**Trailing bytes are refused, never ignored.** A datagram carrying a frame plus
extra bytes is not a valid frame with debris; it is indistinguishable from a
concatenation, and silently ignoring the remainder is how two implementations
come to disagree about where the next frame starts. Refusing the whole datagram
is the only behaviour that cannot drift.

A refusal is **local**. The receiver drops the datagram and MUST NOT reply. On
an open medium a rule that answered malformed bytes with a frame would turn any
transmitter in range into a source of replies from every MCL node that hears it
— the same reasoning that removed five NACK reasons from the Link controls.

## 3. Ports

> **This profile assigns no UDP port, and MCL v1.0 claims none.**

An implementation listens on whatever port its deployment configures. The port
is not part of the profile, is not part of conformance, and a peer **MUST NOT**
infer MCL support from a port number.

**Why, since a fixed port is the obvious design.** A globally reserved default
would have to be registered, and an unregistered value in the private/dynamic
range fossilising into a cross-OEM standard is worse than having none — it would
effectively make "everyone defaults to this unregistered port" a normative part
of MCL. Configurability does not solve first contact either, because two
strangers still have to know *which* configured value the other chose.

The endpoint arrives through the protocol instead: a `TRANSPORT_OFFER` carries
`endpoint_token`, resolved on the candidate transport. That is how a peer learns
where to send, and it works without any party guessing a port.

`49913` appears in `mcl-ip/registries/ip-profiles-v0.1.json` as the reference
harness's experimental default. It is **not** an MCL service port, has no
standing in this profile, and MUST NOT be described as one.

## 4. Size contract

Two limits apply and **the smaller wins**:

```text
protocol limit   MCL_LINK_FRAME_MAX_SIZE          (8 + 16 + 1024 = 1048 bytes)
path limit       path_mtu - IP header - UDP header
```

```text
family   IP header   UDP header   overhead
IPv4        20            8          28
IPv6        40            8          48
```

Consequences an implementer must not get wrong:

- **A large MTU does not raise the maximum.** Above roughly 1 KiB the protocol
  is the constraint, not the path. Sizing a buffer from a jumbo MTU sizes it for
  a frame that can never legally be encoded.
- **An MTU too small for a minimal frame yields no usable carriage.** The
  minimum Link frame is 8 bytes, so an IPv6 path below 56 bytes cannot carry
  MCL at all. An implementation MUST report this rather than truncate.
- **A receiver MUST accept any legal frame up to the negotiated maximum.** The
  negotiated value comes from `CAPABILITY`/`NEGOTIATION`
  (`mcl-link/spec/link-negotiation-v1.md`), whose floor is 42 bytes. A binding
  whose reassembly limit is below the peer's largest legal frame fails only
  under load, which is the worst way to fail.

This profile does **not** fragment. A frame that does not fit the path MTU is
not sent, and the sender is told. IP fragmentation may occur beneath this layer;
that is the network's business and is invisible here.

## 5. Endpoint semantics

An IP endpoint is `(address family, address, port)`.

```text
family     UNSPECIFIED | IPV4 | IPV6 | LOCAL_REF
address    4 bytes for IPv4, 16 for IPv6
port       16 bits
```

**An endpoint is a place to send bytes. It is not an identity.**

- Receiving a frame from an address establishes that something at that address
  transmitted. It establishes nothing about who.
- An `endpoint_token` in a `TRANSPORT_OFFER` is a rendezvous reference resolved
  by the candidate transport. It is not an address, not a credential, and not a
  name.
- An implementation **MUST NOT** treat address equality as contact continuity.
  Continuity is `session_ref`, and even that is correlation rather than
  authentication.

`LOCAL_REF` exists so an endpoint can be named without exposing a routable
address. It carries no address bytes.

## 6. Frame check

> A sender **MUST** set `MCL_LINK_FLAG_FRAME_CHECK` and include the CRC-32 on
> every frame sent under this profile. A receiver **MUST** verify it and refuse
> a frame that fails.

This is the profile-scoped `frame_check` requirement that
`mcl-link/spec/link-class-disposition-v1.md` §6 records as having had nowhere
normative to live. It lives here.

**Why require it when UDP already has a checksum.** The UDP checksum is optional
over IPv4 and is weak; more importantly, a frame that crosses a transport
migration was checked by whatever carried it *previously*, and the Link frame
check is the only integrity that travels with the frame rather than with the
medium. Requiring it per profile rather than in Link keeps the choice with the
carriage that knows its own error characteristics.

**It is a CRC. It is not integrity in the security sense** and detects only
accidental corruption. Anyone who can write to the medium can recompute it.

## 7. Error behaviour, exhaustively

| Input | Result |
|---|---|
| Valid frame, all bytes consumed | accepted |
| Trailing bytes after a complete frame | refused, whole datagram |
| Truncated frame | refused |
| Empty payload | refused |
| Payload larger than `MCL_LINK_FRAME_MAX_SIZE` | refused before decoding |
| Frame check absent | refused (§6) |
| Frame check present and wrong | refused |
| Unknown Link frame class | refused |
| Reserved Link frame class (`ADAPT`) | refused |
| Reserved flag bit set | refused |
| Unsupported Link major | refused |
| Frame addressed to another contact | not delivered to this contact |

Every row is a **silent local drop**. No row generates a reply.

## 8. Discovery is not part of this profile

An MCL peer whose endpoint is already known — because a `TRANSPORT_OFFER`
carried it — needs no discovery, and that is the case v1 supports.

Finding an unknown MCL peer directly over IP is a **separate profile**, not
written and not required for v1.0. Making an autonomous discovery mechanism
mandatory in order to ship a carriage profile would be requiring the harder
problem to be solved first, for no benefit to the case that actually occurs.

The reference implementation's rendezvous beacon in a link-local discovery
datagram is **experimental**. It is not required, not part of conformance, and
an implementation that never sends one is fully conformant to this profile.

## 9. Where this profile sits in the promotion sequence

```text
1. This document, normative and complete, at Candidate          <-- HERE
2. Independent implementation written against THIS document,
   interoperating using the Experimental Use profile value 192
3. That interoperability satisfies the registry promotion gate
4. Standards Action assignment of the final Stable profile value
5. C4/C5 re-run with the final assigned bytes
```

**Step 5 is not ceremony.** `profile_id` travels inside `TRANSPORT_OFFER` and
`TRANSPORT_ACCEPT`, so changing it changes the bytes that were tested. Evidence
gathered under profile 192 is evidence about profile 192.

**Profile 192 MUST NOT simply be relabelled Stable.** `REGISTRY_POLICY.md` is
explicit that Experimental Use values are not globally interoperable
assignments, and renaming one does not make it one.

## 10. What this profile does not provide

- **No confidentiality.** Every byte is in the clear.
- **No peer authentication.** Nothing here establishes who sent a datagram.
- **No replay protection.** A recorded datagram replayed later is a valid
  datagram.
- **No denial-of-service resistance.** Anyone who can reach the endpoint can
  send to it.

These are properties of MCL v1.0 as a whole, not gaps in this profile.
`V1_SCOPE.md` §4.4 defers cryptography conspicuously rather than quietly. A
deployment needing any of the above places this profile inside a transport that
provides it; `MCL_IP_SEC_TLS_CLAIMED` and `MCL_IP_SEC_DTLS_CLAIMED` let an
endpoint *claim* such a wrapper, and a claim is not verification.

## 11. Conformance

An implementation conforms to IP-DATAGRAM v1 when it:

1. sends exactly one Link frame per datagram, filling the payload exactly;
2. refuses every row in §7 without replying;
3. sets and verifies the frame check per §6;
4. respects the §4 size contract, including that the protocol limit caps the
   path limit;
5. treats endpoints per §5, never as identities;
6. requires no port assignment, and infers nothing from one;
7. requires no discovery mechanism.

Conformance is **not** established by passing the reference implementation's
unit tests. It is established by interoperating with an implementation that does
not share this one's code.
