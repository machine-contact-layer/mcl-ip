#!/usr/bin/env python3
"""
MCL-IP carriage against the DFR1154 autonomous node, from the INDEPENDENT
implementation.

WHAT THIS IS

The node's scenario 3 listens on a UDP port of its own and echoes back any
valid MCL Link frame. This sends those frames from
`mcl-core/conformance/independent/mcl_independent.py` -- the clean-room Python
implementation that shares no source, no language and no build system with the
reference C -- over a real 2.4 GHz radio, and checks what comes back.

So it exercises two things at once that are usually tested apart: the Stable
`IP-DATAGRAM` carriage rules, and an implementation independent of the
reference code, over air.

WHAT IT IS NOT

Stranger discovery over IP. MCL defines no global discovery port and this does
not invent one: the node's own port is harness configuration, and the node
learns the peer's address from the datagram that arrives rather than being told
it. Nor is it independent interoperability in the E6 sense -- the Python was
written by the same author from the same documents.

USAGE

    # join the node's SoftAP on the project adapter first
    python udp_check.py --host 192.168.4.1 --port 47100
"""

import argparse
import os
import socket
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
INDEPENDENT = os.path.abspath(
    os.path.join(HERE, "..", "..", "..", "mcl-core", "conformance", "independent"))
sys.path.insert(0, INDEPENDENT)

import mcl_independent as mcl  # noqa: E402

# The Link frame class carrying an application object. The independent module
# names only the two classes its own rules turn on (ADAPT, COUNT); the rest are
# numbers from mcl-link/spec/link-v0.md, and naming this one here keeps the
# reference to the specification rather than to the reference implementation.
LINK_CLASS_DATA = 3


def one_case(sock, addr, name, payload, expect_echo=True):
    """Send one datagram and report what came back."""
    # Drain first. A request/response harness that does not will shift every
    # later reply by one after a single loss, and report a string of protocol
    # failures that never happened.
    # setblocking(True) CLEARS the timeout, so the timeout is restored rather
    # than assumed. Without this the first recvfrom after a drain blocks for
    # ever, and the harness looks like a peer that never answered.
    timeout = sock.gettimeout()
    sock.settimeout(0)
    try:
        while True:
            sock.recv(4096)
    except (BlockingIOError, OSError):
        pass
    sock.settimeout(timeout)

    sock.sendto(payload, addr)
    try:
        data, _ = sock.recvfrom(4096)
    except (socket.timeout, ConnectionResetError):
        # ConnectionResetError is Windows surfacing an ICMP port-unreachable on
        # a UDP socket. For this harness it means the same thing as a timeout:
        # nothing answered.
        ok = not expect_echo
        print("  %-46s %s (no reply)" % (name, "ok" if ok else "FAIL"))
        return ok

    if not expect_echo:
        print("  %-46s FAIL (echoed something it should refuse)" % name)
        return False

    if data != payload:
        print("  %-46s FAIL (%d bytes back, not identical)" % (name, len(data)))
        return False

    # Decode what came back with the independent decoder, not by comparing
    # bytes alone: identical bytes that neither side can parse would pass a
    # memcmp and mean nothing.
    try:
        frame = mcl.decode_frame(data)
    except mcl.MclError as exc:
        print("  %-46s FAIL (round trip does not decode: %s)" % (name, exc))
        return False
    print("  %-46s ok (%d bytes, class %d, major %d)"
          % (name, len(data), frame["frame_class"], frame["link_major"]))
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="192.168.4.1")
    parser.add_argument("--port", type=int, default=47100)
    parser.add_argument("--timeout", type=float, default=3.0)
    args = parser.parse_args()

    addr = (args.host, args.port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(args.timeout)

    print("MCL-IP check against %s:%d" % addr)
    print("frames built by the independent Python implementation\n")

    passed = 0
    total = 0

    presence = mcl.encode_tier0(
        "PRESENCE", 0,
        {"source_ref": 0x5A5A5A5A, "capability_tag": 1, "ttl": 60},
        major=mcl.STABLE_MAJOR)

    cases = [
        # THE STABLE MAJOR FIRST. Link major 1 is what v1.0 publishes; major 0
        # is the pre-standard major the layout is byte-identical to, and both
        # are carried here because a decoder must accept both.
        ("PRESENCE in a DATA frame at the Stable Link major",
         mcl.encode_frame(LINK_CLASS_DATA, 0, 0x5A5A5A5A, presence,
                          link_major=mcl.LINK_STABLE_MAJOR), True),
        ("Stable major with a frame check",
         mcl.encode_frame(LINK_CLASS_DATA, mcl.FLAG_FRAME_CHECK,
                          0x5A5A5A5A, presence,
                          link_major=mcl.LINK_STABLE_MAJOR), True),
        ("PRESENCE in a DATA frame, no options",
         mcl.encode_frame(LINK_CLASS_DATA, 0, 0x5A5A5A5A, presence), True),
        ("with a frame check",
         mcl.encode_frame(LINK_CLASS_DATA, mcl.FLAG_FRAME_CHECK,
                          0x5A5A5A5A, presence), True),
        ("with session and sequence",
         mcl.encode_frame(LINK_CLASS_DATA,
                          mcl.FLAG_SESSION | mcl.FLAG_SEQUENCE,
                          0x5A5A5A5A, presence,
                          session_ref=0x0BADF00D, sequence=7), True),
        # Negative controls. A responder that echoes these is not validating.
        ("truncated frame is refused",
         mcl.encode_frame(LINK_CLASS_DATA, 0, 0x5A5A5A5A, presence)[:4], False),
        ("empty datagram is refused", b"", False),
        ("garbage is refused", bytes(range(32)), False),
        # A Link major neither implementation carries. Built by hand: no
        # encoder here will produce one.
        ("an unimplemented Link major is refused",
         bytes([0x23]) + mcl.encode_frame(LINK_CLASS_DATA, 0, 0x5A5A5A5A,
                                          presence)[1:], False),
    ]

    for name, payload, expect in cases:
        total += 1
        if one_case(sock, addr, name, payload, expect):
            passed += 1

    sock.close()
    print("\n%d of %d" % (passed, total))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
