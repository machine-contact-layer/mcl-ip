# MCL-IP over-air peer (ESP32-S3)

A second machine to talk to. The MCL-IP binding is otherwise only ever exercised
against itself, and a binding that has never met a peer over a real radio has
not been tested in the way that matters.

The board brings up a 2.4 GHz SoftAP, listens for MCL Link frames in UDP
datagrams, and answers each one. The host tool
[`tools/udp_over_air_peer.c`](../../tools/udp_over_air_peer.c) drives it.

## What this measures, and what it does not

It measures whether two machines running MCL exchange Link frames over a real
UDP path and, more importantly, whether the receiver **refuses malformed frames
exactly as the specification requires**. Most of the host cases are negative for
that reason: truncation, a trailing byte, an unknown frame class, a reserved
flag bit, a future major version, a corrupted integrity field, and a payload
length that overstates the datagram.

It does **not** demonstrate independent interoperability. Both ends compile the
same `mcl-link`, `mcl-wire` and `mcl-ip` sources, so a shared misreading of the
specification would pass on both sides and be invisible here. Independent
interoperability (C4 / E6) needs a second implementation written from the
specification by someone else, and nothing in this directory substitutes for
that.

## The firmware is not a copy of the protocol

`build-firmware.ps1` stages the canonical sources out of the sibling
repositories into a throwaway build tree and deletes it afterwards. No protocol
source is duplicated into this directory, so the firmware and the host cannot
drift apart, and the script prints the SHA-256 of every source it staged so a
run can be tied to exact inputs.

The sketch supplies only the radio, the socket and the serial log. It contains
no encoding, no decoding and no validation of its own.

## Running it

```powershell
# 1. Build. Compiles only; never uploads.
.\build-firmware.ps1

# 2. Write the application partition, and nothing else.
.\flash-app-only.ps1 -PortName COM3

# 3. Join the board's access point from the interface you intend to use.
netsh wlan connect name="MCL-IP-TEST" interface="Wi-Fi 2"

# 4. Run the host peer, bound to that interface's address.
mcl_ip_udp_over_air_peer.exe --peer 192.168.4.1 --bind <that interface IP> --count 50
```

Build the host tool with `-DMCL_IP_BUILD_TOOLS=ON`. It is off by default because
it needs hardware and cannot run in the ordinary test suite.

`--bind` is not optional in practice. Without it the routing table chooses the
interface, and on a machine with more than one radio the traffic may leave by
the wrong one, which produces a result that describes a different link from the
one being measured.

## Flashing safety

Only the application partition at `0x20000` is written. The bootloader at `0x0`
and the partition table at `0x8000` are untouched, which is what makes the
operation reversible from an application-only backup.

`flash-app-only.ps1` refuses to run unless a factory application backup exists,
so a board cannot be put into a state it cannot be returned from. It also
verifies that the port really is the ESP32-S3 native USB device before writing.

Uploading through the Arduino toolchain is deliberately not used: its upload
step can also rewrite the bootloader and the partition table, which would make
the factory application unrecoverable from that backup.

## Serial interface

| Command | Response |
|---|---|
| `PING` | `MCLPONG` |
| `STATUS` | one line of AP state and the accept/reject counters |
| `RESET` | zeroes the counters |

Every datagram produces one `MCLIP` line, which is the per-run evidence record.
The board's own counters are the ground truth for what it received: a host that
reports a failure while the board reports a clean rejection is describing a lost
datagram, not a protocol defect, and the two records have to be read together.
