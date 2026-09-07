# MCL-IP against the autonomous node, from the independent implementation

A short harness with an unusual property: **both halves of the exchange are
written from the specification, and neither shares code with the other.**

The DFR1154 autonomous node's scenario 3 listens on a UDP port of its own and
echoes back any valid MCL Link frame. This sends those frames from
[`mcl-core/conformance/independent/mcl_independent.py`](../../../mcl-core/conformance/independent/mcl_independent.py)
— the clean-room Python implementation — over a real 2.4 GHz radio, and checks
what comes back and what does not.

```sh
# join the node's SoftAP on the project adapter, then:
python udp_check.py --host 192.168.4.1 --port 47100
```

## What it exercises

| | |
|---|---|
| Stable `IP-DATAGRAM` carriage | a Link frame in one datagram, both directions |
| **Link major 1** | the Stable major, and major 0 beside it |
| the frame check | CRC-32 derived from the polynomial on both sides |
| optional fields | session and sequence present |
| refusals | truncated, empty, garbage, and an unimplemented Link major |

The negative cases matter as much as the positive ones. A responder that echoes
whatever it is sent looks identical to one that validates, until the day it
forwards something malformed into an application.

## What it is not

**Stranger discovery over IP.** MCL defines no global discovery port and this
does not invent one. The node's own port is harness configuration and is
recorded as such; what keeps the run zero-prior is that the node is told
nothing about the peer and learns its address from the datagram that arrives —
provenance `FROM_BEARER`, visible in `/api/result`.

**Not E6.** The Python and the C were written by the same author from the same
documents. It catches an implementation that disagrees with its specification,
not a specification both halves misread.

## What it found

On its first run, that **no independent implementation had ever decoded a
Stable-major Link frame.** Link major 1 was cut on 2026-09-04;
`mcl-link/spec/link-v0.md`'s layout section still read *"`link_major` is 0 for
this draft"*; the clean-room implementation was written from that sentence and
refused major 1 outright. Every C4 and C5 case ran at major 0, so nothing
noticed.

The document is corrected, the implementation accepts both majors, and C4 now
carries the Stable major in both directions plus a major neither implements.
That is what a clean-room implementation is *for*: it reads the specification
literally, and a sentence that is no longer true produces a defect instead of a
shrug.

## Runs

- [`runs/20260908-stable-major-over-air.log`](runs/20260908-stable-major-over-air.log)
  — 9 of 9, both majors, with the node's own log and provenance beside it.
