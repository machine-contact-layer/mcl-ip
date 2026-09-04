# E4 — MCL-IP over Wi-Fi against an Android handset, 2026-09-04

A third platform. The MCL-IP binding had met a Windows laptop and an ESP32-S3;
both are machines this project chose. A commodity Android phone running a
vendor build nobody here controls is a different kind of peer, and a binding
that has only ever met hardware its author selected has not been tested in the
way that matters.

## The rig

```text
iQOO 9 (vivo I2017)                      Windows laptop
Android 14, API 34, arm64-v8a            Wi-Fi 2 = Realtek RTL8188EU
Snapdragon 888 (SM8350)                  802.11n, 2.4 GHz, 72 Mbps
kernel 5.4.281, SELinux Enforcing
                                         Wi-Fi 1 present on the same subnet
SoftAP, 2.4 GHz  10.13.97.34             and deliberately NOT used
                        \               /
                         \  UDP :5570-5573
                          \           /
                           10.13.97.149
```

The phone is the access point **and** one of the two MCL peers. Every datagram
crossed its 2.4 GHz SoftAP.

**adb carried no MCL traffic.** It pushed two binaries, started a process and
captured its stdout. If adb had been the transport this would be a measurement
of a USB cable.

**`--bind` was not optional.** The laptop has two wireless interfaces on this
subnet, so without binding to `10.13.97.149` the routing table would have chosen
one and the record would describe a radio other than the one named above.

## What ran on the phone

A statically linked aarch64 executable in `/data/local/tmp`, built from the
canonical `mcl-link`, `mcl-wire` and `mcl-ip` sources. `SOURCES.txt` carries the
SHA-256 of every staged file and of both binaries, so "the phone ran the same
code the laptop ran" is a fact a reader can check rather than a claim.

No application was installed. Nothing was left on the device.

## Result

Four cells, 100 sustained frames each, in one session.

| Cell | Checks | Failed | Sustained | Lost | Retries |
|---|--:|--:|--:|--:|--:|
| phone responds, major 0 | 31 | 0 | 100/100 | 0 | 0 |
| phone responds, major 1 | 40 | 0 | 100/100 | 0 | 0 |
| phone initiates, major 0 | 31 | 0 | 100/100 | 0 | 0 |
| phone initiates, major 1 | 40 | 0 | 100/100 | 0 | 0 |

**142 checks, 0 failed. 400 sustained frames, 0 lost, 0 retries.**

Both directions are run because an encoder defect and a decoder defect look
identical from one side. Both majors are run because major 1 is what v1.0
freezes and major 0 is what every earlier record was gathered under.

## The refusals are the result

Twenty-two of the thirty-five distinct cases are negative. A peer that carries
well-formed frames and also accepts malformed ones has implemented a demo.

At both majors: a truncated frame, a trailing byte after a complete frame, an
unknown frame class, a reserved flag bit, a future Link major, a corrupted
integrity field, a payload corrupted under a valid CRC field, a declared payload
length exceeding the datagram, and **a frame carrying no frame check at all** —
legal at the Link layer, malformed under this profile (`spec/ip-datagram-profile-v1.md`
§6).

At major 1 additionally: `TRANSPORT_OFFER` naming reserved `profile_id` 0,
`TRANSPORT_OFFER` naming reserved `transport_id` 0, and `HAZARD` — a Candidate
object — refused at the Stable major by the encoder before it could be sent.

Each was answered with a NACK, not with silence. A peer that fell silent would
be indistinguishable over a radio from one that had crashed.

## What this does NOT establish

- **Not independent interoperability.** Both ends compile the same
  `mcl-link`, `mcl-wire` and `mcl-ip` sources. A shared misreading of the
  specification would pass on both sides and be invisible here. That is what
  `mcl-core/conformance/independent/` is for, and nothing in this directory
  substitutes for it.
- **Not evidence that MCL builds against Bionic.** The binaries link
  statically against aarch64 glibc. The processor, the kernel, the Wi-Fi stack
  and the radio are the phone's; the C library is not. The "different toolchain,
  different architecture" requirement of `V1_SCOPE.md` §5.9(c) is met by the
  ESP32-S3, not by this.
- **Not a claim about Android as a platform MCL supports.** There is no
  Android binding, no APK, no service and no lifecycle handling. Android appears
  here as a third peer on an IP network, which is all the IP binding asks of it.
- **Not a throughput or latency measurement.** Nothing here was tuned, and one
  session on one link in one room says nothing about either.
- **Not a range or interference result.** Both devices were on a desk.

## Reproducing it

```powershell
cd mcl-ip\hardware\android-udp-peer
.\build-android.ps1
# then, with the phone's SoftAP up and this laptop joined to it:
.\run-android-trials.ps1 -PeerIp <phone> -BindIp <this laptop> -WindowsBin <dir> -Count 100
```

The addresses are arguments and not defaults, because a harness that remembers
a network is a harness that will one day measure the wrong one.
