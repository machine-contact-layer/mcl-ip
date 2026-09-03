/*
 * MCL-IP over-air host peer.
 *
 * Exercises the MCL-IP binding against a real peer over a real UDP path, in
 * both directions. It is a host tool, not a unit test: it needs a radio, a
 * network and a second machine, so it lives outside the test suite and is
 * built only when MCL_IP_BUILD_TOOLS is enabled.
 *
 * What it is actually for
 * ----------------------
 * That well-formed frames arrive is the easy half and proves little. The half
 * that matters is that a peer refuses malformed frames over a real channel in
 * exactly the way the specification requires, so most of the cases below are
 * negative: truncation, trailing bytes, an unknown class, a reserved bit, a
 * future major version, a corrupted integrity field, and a payload length that
 * overstates what was sent. A binding that quietly accepted any of these would
 * pass a happy-path demonstration and fail in the field.
 *
 * The tool contains no protocol logic of its own. Every frame it sends is
 * built by mcl-link and every reply is checked by mcl-ip and mcl-link, which
 * is the point: the code under test is the code that ships.
 *
 * Usage:
 *   udp_over_air_peer [--peer A.B.C.D] [--port N] [--bind A.B.C.D] [--count N]
 *
 * --bind matters. Binding to the address of the interface facing the peer
 * keeps the traffic on that interface instead of letting the routing table
 * choose another one.
 */

#include "mcl/ip_binding.h"
#include "mcl/link.h"
#include "mcl/wire.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET mcl_socket_t;
#  define MCL_INVALID_SOCKET INVALID_SOCKET
#  define mcl_close_socket closesocket
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <errno.h>
   typedef int mcl_socket_t;
#  define MCL_INVALID_SOCKET (-1)
#  define mcl_close_socket close
#endif

#define DEFAULT_PEER  "192.168.4.1"
#define DEFAULT_PORT  5555
#define REPLY_TIMEOUT_MS 1500

static int checks_run = 0;
static int checks_failed = 0;

#define CHECK(cond, msg) do {                                        \
    ++checks_run;                                                    \
    if (!(cond)) {                                                   \
        ++checks_failed;                                             \
        printf("    FAIL: %s\n", (msg));                             \
    }                                                                \
} while (0)

/* Contact reference this host puts in the frames it sends. Not an identity. */
#define HOST_SOURCE_REF 0xC0FFEE01u

typedef struct {
    mcl_socket_t sock;
    struct sockaddr_in peer;
} link_t;

static void make_presence(mcl_wire_tier0_t *obj)
{
    memset(obj, 0, sizeof(*obj));
    obj->kind = MCL_WIRE_KIND_PRESENCE;
    obj->priority = 2u;
    obj->source_ref = HOST_SOURCE_REF;
    obj->body.presence.machine_class = 7u;
    obj->body.presence.capability_tag = 0x112233u;   /* 24-bit field */
    obj->body.presence.ttl = 60u;
}

static void make_hazard(mcl_wire_tier0_t *obj)
{
    memset(obj, 0, sizeof(*obj));
    obj->kind = MCL_WIRE_KIND_HAZARD;
    obj->priority = 0u;                 /* P0_CRITICAL */
    obj->source_ref = HOST_SOURCE_REF;
    obj->body.hazard.hazard_class = 2u;
    obj->body.hazard.severity = 3u;
    obj->body.hazard.confidence = 80u;
    obj->body.hazard.x = -120;
    obj->body.hazard.y = 340;
    obj->body.hazard.z = 0;
    obj->body.hazard.radius = 250u;
    obj->body.hazard.ttl = 10u;
}

/* Build a valid frame carrying a Tier-0 object. Returns encoded length, or 0. */
static size_t build_frame(uint8_t *out, size_t cap,
                          mcl_link_frame_class_t cls,
                          uint8_t flags,
                          uint16_t sequence,
                          const mcl_wire_tier0_t *obj)
{
    static uint8_t wire_buf[MCL_WIRE_TIER0_MAX_SIZE];
    mcl_link_frame_t f;
    size_t wire_len = 0u, written = 0u;

    memset(&f, 0, sizeof(f));
    f.frame_class = cls;
    f.flags = flags;
    f.source_ref = HOST_SOURCE_REF;
    f.sequence = sequence;

    if (obj != NULL) {
        if (mcl_wire_tier0_encode(obj, wire_buf, sizeof(wire_buf), &wire_len) != MCL_WIRE_OK) {
            return 0u;
        }
        f.payload = wire_buf;
        f.payload_len = (uint16_t)wire_len;
    }

    if (mcl_link_frame_encode(&f, out, cap, &written) != MCL_LINK_OK) {
        return 0u;
    }
    return written;
}

static unsigned g_retries = 0;

/*
 * Discard anything already queued on the socket.
 *
 * This is not tidiness. On a lossy datagram path a reply that arrived after
 * its exchange timed out is still sitting in the receive queue, and the next
 * recv would return it as though it answered the next request. One lost
 * datagram would then shift every subsequent result by one and report a string
 * of protocol failures that never happened. Draining before each send keeps a
 * radio problem from being read as a specification problem.
 */
static void drain(link_t *l)
{
    uint8_t scratch[1600];
#if defined(_WIN32)
    u_long nonblocking = 1, blocking = 0;
    ioctlsocket(l->sock, FIONBIO, &nonblocking);
    while (recv(l->sock, (char *)scratch, (int)sizeof(scratch), 0) > 0) { }
    ioctlsocket(l->sock, FIONBIO, &blocking);
#else
    while (recv(l->sock, scratch, sizeof(scratch), MSG_DONTWAIT) > 0) { }
#endif
}

/*
 * Send one datagram and wait for a reply, retrying a lost one.
 *
 * A datagram that never arrives is a property of the radio, not of the
 * binding, so it must not be reported as a conformance failure. Retries are
 * counted and printed, because a result that needed them is a weaker result
 * and hiding that would overstate the link.
 */
static int exchange(link_t *l, const uint8_t *tx, size_t tx_len,
                    uint8_t *rx, size_t rx_cap)
{
    const int max_attempts = 4;
    int attempt, n;

    for (attempt = 0; attempt < max_attempts; ++attempt) {
        drain(l);

        if (sendto(l->sock, (const char *)tx, (int)tx_len, 0,
                   (struct sockaddr *)&l->peer, sizeof(l->peer)) != (int)tx_len) {
            return -1;
        }

        n = recv(l->sock, (char *)rx, (int)rx_cap, 0);
        if (n > 0) {
            return n;
        }
        ++g_retries;
    }
    return 0;
}

/*
 * Check a reply is a well-formed frame of the expected class. The reply is put
 * through the same binding validation the board applied to our frame, so a
 * malformed reply fails the run rather than being read past.
 */
static int reply_is(const uint8_t *rx, int n, mcl_link_frame_class_t expect,
                    mcl_link_frame_t *out_frame)
{
    size_t consumed = 0u;

    if (n <= 0) {
        return 0;
    }
    if (mcl_ip_datagram_validate(rx, (size_t)n) != MCL_IP_OK) {
        return 0;
    }
    if (mcl_link_frame_decode(rx, (size_t)n, out_frame, &consumed) != MCL_LINK_OK) {
        return 0;
    }
    return (out_frame->frame_class == expect) ? 1 : 0;
}

static void case_valid_contact(link_t *l)
{
    uint8_t tx[256], rx[256];
    mcl_link_frame_t reply;
    mcl_wire_tier0_t obj, decoded;
    size_t tx_len, wire_consumed = 0u;
    int n;

    printf("  [+] valid CONTACT carrying PRESENCE\n");
    make_presence(&obj);
    tx_len = build_frame(tx, sizeof(tx), MCL_LINK_CLASS_CONTACT,
                         MCL_LINK_FLAG_SEQUENCE | MCL_LINK_FLAG_FRAME_CHECK, 1u, &obj);
    CHECK(tx_len > 0u, "frame built");

    n = exchange(l, tx, tx_len, rx, sizeof(rx));
    CHECK(n > 0, "peer replied");
    CHECK(reply_is(rx, n, MCL_LINK_CLASS_ACK, &reply), "reply is a valid ACK");

    if (n > 0 && reply.payload_len > 0u) {
        CHECK(mcl_wire_tier0_decode(reply.payload, (size_t)reply.payload_len,
                                    &decoded, &wire_consumed) == MCL_WIRE_OK,
              "ACK payload decodes as a Tier-0 object");
        CHECK(wire_consumed == (size_t)reply.payload_len,
              "ACK payload has no undeclared trailing bytes");
        CHECK(decoded.kind == MCL_WIRE_KIND_PRESENCE, "peer answered with PRESENCE");
    }
}

static void case_valid_hazard(link_t *l)
{
    uint8_t tx[256], rx[256];
    mcl_link_frame_t reply;
    mcl_wire_tier0_t obj;
    size_t tx_len;
    int n;

    printf("  [+] valid DATA carrying HAZARD at P0\n");
    make_hazard(&obj);
    tx_len = build_frame(tx, sizeof(tx), MCL_LINK_CLASS_DATA,
                         MCL_LINK_FLAG_SEQUENCE | MCL_LINK_FLAG_FRAME_CHECK, 2u, &obj);
    CHECK(tx_len > 0u, "frame built");
    n = exchange(l, tx, tx_len, rx, sizeof(rx));
    CHECK(reply_is(rx, n, MCL_LINK_CLASS_ACK, &reply), "reply is a valid ACK");
}

static void case_valid_keepalive(link_t *l)
{
    uint8_t tx[256], rx[256];
    mcl_link_frame_t reply;
    size_t tx_len;
    int n;

    printf("  [+] KEEPALIVE with no payload\n");
    tx_len = build_frame(tx, sizeof(tx), MCL_LINK_CLASS_KEEPALIVE, 0u, 0u, NULL);
    CHECK(tx_len > 0u, "frame built");
    CHECK(tx_len == MCL_LINK_FRAME_MIN_SIZE, "an empty frame is the minimum size");
    n = exchange(l, tx, tx_len, rx, sizeof(rx));
    CHECK(reply_is(rx, n, MCL_LINK_CLASS_ACK, &reply), "reply is a valid ACK");
}

/*
 * Every negative case below must produce a NACK. A peer that answered ACK
 * would be accepting something the specification requires it to refuse, and a
 * peer that answered nothing at all would be indistinguishable from one that
 * had crashed, so silence fails too.
 */
static void expect_reject(link_t *l, const char *what,
                          const uint8_t *tx, size_t tx_len)
{
    uint8_t rx[256];
    mcl_link_frame_t reply;
    int n;

    printf("  [-] %s\n", what);
    n = exchange(l, tx, tx_len, rx, sizeof(rx));
    CHECK(n > 0, "peer replied rather than falling silent");
    CHECK(reply_is(rx, n, MCL_LINK_CLASS_NACK, &reply), "peer refused with NACK");
}

static void case_negatives(link_t *l)
{
    uint8_t base[256], tx[256];
    mcl_wire_tier0_t obj;
    size_t base_len;

    make_presence(&obj);
    base_len = build_frame(base, sizeof(base), MCL_LINK_CLASS_CONTACT,
                           MCL_LINK_FLAG_SEQUENCE | MCL_LINK_FLAG_FRAME_CHECK, 3u, &obj);
    if (base_len == 0u) {
        printf("  cannot build the base frame for negative cases\n");
        ++checks_failed;
        return;
    }

    memcpy(tx, base, base_len);
    expect_reject(l, "truncated: last byte removed", tx, base_len - 1u);

    memcpy(tx, base, base_len);
    tx[base_len] = 0x00u;
    expect_reject(l, "trailing byte after a complete frame", tx, base_len + 1u);

    memcpy(tx, base, base_len);
    tx[0] = (uint8_t)((tx[0] & 0xF0u) | 0x0Fu);
    expect_reject(l, "unknown frame class", tx, base_len);

    memcpy(tx, base, base_len);
    tx[1] |= 0x80u;
    expect_reject(l, "reserved flag bit set", tx, base_len);

    memcpy(tx, base, base_len);
    tx[0] = (uint8_t)((1u << 4u) | (tx[0] & 0x0Fu));
    expect_reject(l, "future Link major version", tx, base_len);

    memcpy(tx, base, base_len);
    tx[base_len - 1u] ^= 0x01u;
    expect_reject(l, "integrity field corrupted by one bit", tx, base_len);

    memcpy(tx, base, base_len);
    tx[6] ^= 0x01u;
    expect_reject(l, "payload corrupted by one bit under a valid CRC field", tx, base_len);

    /* A payload length that overstates what was actually sent. */
    memcpy(tx, base, base_len);
    tx[8] = 0xFFu;
    tx[9] = 0xFFu;
    expect_reject(l, "declared payload length exceeds the datagram", tx, base_len);
}

/* Sustained exchange: loss, ordering and round-trip time over the real path. */
static void case_sustained(link_t *l, int count)
{
    uint8_t tx[256], rx[256];
    mcl_link_frame_t reply;
    mcl_wire_tier0_t obj;
    int i, acked = 0, lost = 0;
    size_t tx_len;

    printf("  [=] sustained exchange of %d frames\n", count);
    make_presence(&obj);

    for (i = 0; i < count; ++i) {
        int n;
        tx_len = build_frame(tx, sizeof(tx), MCL_LINK_CLASS_CONTACT,
                             MCL_LINK_FLAG_SEQUENCE | MCL_LINK_FLAG_FRAME_CHECK,
                             (uint16_t)(100 + i), &obj);
        if (tx_len == 0u) { ++lost; continue; }
        n = exchange(l, tx, tx_len, rx, sizeof(rx));
        if (n > 0 && reply_is(rx, n, MCL_LINK_CLASS_ACK, &reply)) {
            ++acked;
        } else {
            ++lost;
        }
    }

    printf("      acked=%d lost=%d of %d\n", acked, lost, count);
    /*
     * Loss on a radio link is expected and is not a protocol defect, so this
     * asserts only that the path carried a clear majority. What would be a
     * defect is a corrupted frame being acknowledged, and reply_is above
     * rejects that case for every single exchange.
     */
    CHECK(acked * 2 > count, "a majority of frames completed a round trip");
}

int main(int argc, char **argv)
{
    const char *peer_ip = DEFAULT_PEER;
    const char *bind_ip = NULL;
    int port = DEFAULT_PORT;
    int count = 50;
    int i;
    link_t l;
#if defined(_WIN32)
    WSADATA wsa;
    DWORD timeout = REPLY_TIMEOUT_MS;
#else
    struct timeval timeout;
    timeout.tv_sec = REPLY_TIMEOUT_MS / 1000;
    timeout.tv_usec = (REPLY_TIMEOUT_MS % 1000) * 1000;
#endif

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc)       { peer_ip = argv[++i]; }
        else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc)  { bind_ip = argv[++i]; }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)  { port = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = atoi(argv[++i]); }
        else {
            printf("usage: %s [--peer IP] [--bind IP] [--port N] [--count N]\n", argv[0]);
            return 2;
        }
    }

#if defined(_WIN32)
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
#endif

    l.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (l.sock == MCL_INVALID_SOCKET) {
        printf("socket() failed\n");
        return 1;
    }

    if (bind_ip != NULL) {
        struct sockaddr_in local;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_port = 0;
        local.sin_addr.s_addr = inet_addr(bind_ip);
        if (bind(l.sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
            printf("bind() to %s failed; refusing to run on an unknown interface\n", bind_ip);
            mcl_close_socket(l.sock);
            return 1;
        }
    }

    setsockopt(l.sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    memset(&l.peer, 0, sizeof(l.peer));
    l.peer.sin_family = AF_INET;
    l.peer.sin_port = htons((unsigned short)port);
    l.peer.sin_addr.s_addr = inet_addr(peer_ip);

    printf("MCL-IP over-air peer\n");
    printf("====================\n");
    printf("peer=%s:%d bind=%s frame_max=%u\n\n",
           peer_ip, port, (bind_ip != NULL) ? bind_ip : "(default route)",
           (unsigned)MCL_LINK_FRAME_MAX_SIZE);

    case_valid_contact(&l);
    case_valid_hazard(&l);
    case_valid_keepalive(&l);
    case_negatives(&l);
    case_sustained(&l, count);

    mcl_close_socket(l.sock);
#if defined(_WIN32)
    WSACleanup();
#endif

    printf("\n%d checks, %d failed, %u datagram retries\n",
           checks_run, checks_failed, g_retries);
    return (checks_failed == 0) ? 0 : 1;
}
