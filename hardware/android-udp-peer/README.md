# MCL-IP over-air peer (Android)

A third machine to talk to, and the least controlled one available: a commodity
handset running a vendor Android build that nobody on this project chose.

The phone brings up its 2.4 GHz SoftAP, the laptop joins it on a named
interface, and the two exchange MCL Link frames in UDP datagrams — in both
directions and at both Wire/Link majors.

## What this measures, and what it does not

It measures whether MCL crosses a real Wi-Fi link between a laptop and a phone
and, more importantly, whether each end **refuses malformed frames exactly as
the specification requires**. Most of the cases are negative for that reason.

It does **not** demonstrate independent interoperability: both ends compile the
same `mcl-link`, `mcl-wire` and `mcl-ip` sources, so a shared misreading of the
specification would pass on both sides and be invisible here. See
`mcl-core/conformance/independent/`.

It is also **not** an Android port. There is no APK, no service, no binding and
no lifecycle handling. Android appears here as a third peer on an IP network,
which is all the IP binding asks of anything.

## adb is deployment, never transport

`adb` pushes two binaries, starts a process and captures stdout. The MCL traffic
goes over Wi-Fi. Keeping that boundary is the difference between testing a
protocol over a radio and testing a USB cable.

## The binaries are not a copy of the protocol

`build-android.ps1` stages the canonical sources out of the sibling
repositories, prints the SHA-256 of every staged file, and writes
`build/out/SOURCES.txt`. No protocol source is duplicated into this directory,
so the phone and the laptop cannot drift apart. The build tree is gitignored.

Two executables are produced, because the campaign runs both directions:

| Binary | Role |
|---|---|
| `mcl_ip_responder_arm64` | the phone answers; the laptop drives |
| `mcl_ip_peer_arm64` | the phone drives; the laptop answers |

They are statically linked against **aarch64 glibc, not Bionic**. Nothing on the
phone supplies a loader for `/data/local/tmp`, and the point of the exercise is
the phone's radio, kernel and network stack rather than its C library. Stated
plainly because it bounds the claim: this is not evidence that MCL builds
against Bionic, and the "different toolchain, different architecture"
requirement of `mcl-core/governance/V1_SCOPE.md` §5.9(c) is met by the ESP32-S3.

## Running it

```powershell
# 1. Cross-compile. Needs gcc-aarch64-linux-gnu in the WSL distro.
.\build-android.ps1

# 2. Build the Windows half.
cmake -S ..\.. -B <build> -DMCL_IP_BUILD_TOOLS=ON -A x64
cmake --build <build> --config Release

# 3. Turn on the phone's hotspot and join it from the interface you intend
#    to measure. Then find both addresses:
adb shell ip -o -4 addr show      # the phone on its SoftAP
ipconfig                          # this laptop on that SoftAP

# 4. Run all four cells and write the evidence record.
.\run-android-trials.ps1 -PeerIp <phone> -BindIp <laptop> -WindowsBin <dir> -Count 100
```

Addresses are required arguments and have no defaults. A harness that remembers
a network is a harness that will eventually measure the wrong one — and this
laptop has two wireless interfaces on the same subnet, so `-BindIp` decides
which radio the result is actually about.

No SSID or passphrase appears in this directory. Joining the access point is a
step the operator performs; a test harness is not a place to keep credentials.

## Cleaning up

```powershell
adb shell rm -rf /data/local/tmp/mcl
```

Nothing is installed, so that is the whole of it.

## Evidence

`E4 MULTI_DEVICE_OVER_AIR` — 2026-09-04, in
[`../../evidence/e4-android-udp-20260904/`](../../evidence/e4-android-udp-20260904/):
four cells, 142 checks, 0 failed, 400 sustained frames with 0 lost and 0
retries, against an iQOO 9 on Android 14.
