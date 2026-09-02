# MCL-IP over UDP on 2.4 GHz — 2026-09-02

First execution of the MCL-IP binding between two machines over a real radio.
Before this run the binding had only ever been exercised against itself.

## Claim

**Conformance:** C1 (canonical encoding), C2 (negative decoding), and the
datagram carriage rules of C5, demonstrated across a physical link.

**Evidence level:** `E4 MULTI_DEVICE_OVER_AIR`.

**Explicitly not claimed:** C4 or `E6 INDEPENDENT_INTEROPERABILITY`. Both ends
compile the same `mcl-link`, `mcl-wire` and `mcl-ip` sources. A shared
misreading of the specification would be accepted by both peers and would not
show up anywhere in this result. That gap closes only with a second
implementation written from the specification by someone else.

## Setup

| | |
|---|---|
| Peer A (host) | Windows laptop, Realtek RTL8188EU 802.11n USB adapter |
| Peer B (board) | DFR1154 / ESP32-S3, SoftAP `MCL-IP-TEST`, channel 6 |
| Path | 2.4 GHz, WPA2-AES, board at 192.168.4.1, host at 192.168.4.2 |
| Carriage | UDP port 5555, one Link frame per datagram |
| Host binary | `mcl_ip_udp_over_air_peer` built with `MCL_IP_BUILD_TOOLS=ON` |
| Firmware | `mcl-ip/hardware/esp32-udp-peer`, app partition only at `0x20000` |

The host socket was bound to the 192.168.4.2 address so the traffic could not
leave by the machine's other wireless interface.

## Result

```text
28 checks, 0 failed, 0 datagram retries
sustained exchange: acked=100 lost=0 of 100
board: rx=111 ok=103 rej=8 tx=111 logdrop=0
```

Host and board records agree exactly: 111 datagrams sent, 111 received, 103
accepted, 8 refused, 111 replies. No loss in either direction and no dropped
log lines, so the serial record is complete rather than merely quiet.

## The eight refusals

Each malformed frame was refused, and the reason the board reported is the
reason the specification requires. Nothing was accepted that should not have
been.

| Case | Board reported |
|---|---|
| last byte removed | `TRUNCATED` |
| trailing byte after a complete frame | `NONCANONICAL` |
| unknown frame class | `NONCANONICAL` |
| reserved flag bit set | `NONCANONICAL` |
| future Link major version | `UNSUPPORTED` |
| integrity field corrupted by one bit | `NONCANONICAL` |
| payload corrupted by one bit | `NONCANONICAL` |
| declared payload length exceeds the datagram | `NONCANONICAL` |

Three of these distinctions did not exist before this campaign. `TRUNCATED` was
previously conflated with malformation, and a future major version was reported
as a parse error rather than as a fact about the peer. The rows above are the
first confirmation that the corrected translation behaves as intended across a
real link rather than only in a unit test.

## Two failures found along the way, both ours

Recorded because the first two runs of this experiment produced numbers that
looked like protocol defects and were not.

**A 24-bit field carrying a 32-bit value.** `capability_digest` is 24 bits wide.
Both the host harness and the firmware initialised it with a 32-bit constant,
and `mcl_wire_tier0_encode` refused to encode either. The board could therefore
never build its ACK, so every exchange timed out and the run looked like a dead
link. The library was correct throughout; the range check did exactly its job.

**An instrument that perturbed its own measurement.** The firmware logged one
line per datagram over USB CDC. With no host reading the port, the CDC transmit
buffer filled, `Serial.printf` blocked, and datagrams overflowed the UDP receive
queue while it stalled. This presented as 36% packet loss. An independent ICMP
baseline over the same link measured 0% loss on 200 packets, which is what
separated the logger from the radio. Logging is now dropped rather than blocked
when the buffer is full, and the dropped count is reported, so a run with
missing lines can never be mistaken for a quiet one.

Neither was a defect in the binding. Both would have been easy to report as
channel behaviour, and neither was.

## Reproducing

See [`../../hardware/esp32-udp-peer/README.md`](../../hardware/esp32-udp-peer/README.md).

## Files

| File | Contents |
|---|---|
| `host-output.txt` | host peer output, verbatim |
| `board-serial.log` | board's per-datagram record and final counters |
| `SHA256SUMS.txt` | digests of both |
