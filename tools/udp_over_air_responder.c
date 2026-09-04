/*
 * MCL-IP over-air responder.
 *
 * The other half of udp_over_air_peer.c. That tool sends and judges; this one
 * receives, validates and answers. Until now the only responder that existed
 * was the ESP32 sketch in hardware/esp32-udp-peer/, which meant the responding
 * side of every over-air run needed a soldered board and an Arduino toolchain.
 * This is the same behaviour as portable C99, so any machine with a socket can
 * be the peer -- a second laptop, a phone under Termux, a container.
 *
 * It contains no protocol logic of its own. Every arriving datagram is judged
 * by mcl-ip and mcl-link and every reply is built by mcl-link, which is the
 * point: the code under test is the code that ships.
 *
 * WHAT IT ANSWERS, AND WHY SILENCE IS NOT AN OPTION
 * -------------------------------------------------
 * A well-formed frame is answered with ACK carrying this peer's PRESENCE. Any
 * frame the binding or the Link decoder refuses is answered with NACK carrying
 * nothing -- a refusal is not an occasion to assert anything about ourselves.
 *
 * It never simply drops a datagram. A responder that fell silent on malformed
 * input would be indistinguishable over a radio from one that had crashed, and
 * the initiator could not tell a refusal from a lost packet. Refusing out loud
 * is what makes the negative cases measurable.
 *
 * MAJORS
 * ------
 * --major 0 replies with a major-0 PRESENCE (11 bytes, carries machine_class).
 * --major 1 replies with a major-1 PRESENCE (10 bytes, no machine_class) and is
 * the Stable path. The choice affects only what this peer EMITS: what it
 * accepts is whatever the decoder accepts, because a responder is not entitled
 * to an opinion about which assigned major a peer may use.
 *
 * Usage:
 *   udp_over_air_responder [--port N] [--bind A.B.C.D] [--major 0|1]
 *                          [--count N] [--source-ref 0xHEX] [--quiet]
 *
 * --count N exits after N datagrams so a scripted run terminates on its own;
 * without it the tool runs until interrupted.
 *
 * --bind matters for the same reason it matters in the initiator: binding to
 * the address of the interface facing the peer keeps the traffic on that
 * interface instead of letting the routing table choose another one.
 */

#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200112L
#endif

#include "mcl/contact.h"
#include "mcl/ip_binding.h"
#include "mcl/link.h"
#include "mcl/wire.h"

#include <stdarg.h>
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
   typedef int mcl_socklen_t;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   typedef int mcl_socket_t;
#  define MCL_INVALID_SOCKET (-1)
#  define mcl_close_socket close
   typedef socklen_t mcl_socklen_t;
#endif

#define DEFAULT_PORT 5555

/* Contact reference this peer puts in the frames it sends. Not an identity. */
#define DEFAULT_SOURCE_REF 0xA4D01DEEu

static uint32_t g_source_ref = DEFAULT_SOURCE_REF;
static uint8_t  g_major = MCL_WIRE_EXPERIMENTAL_MAJOR;
static int      g_quiet = 0;

static unsigned long g_rx_total = 0;
static unsigned long g_rx_accepted = 0;
static unsigned long g_rx_rejected = 0;
static unsigned long g_tx_total = 0;
static uint16_t      g_tx_sequence = 0;

/*
 * One contact, begun on transport 2, so that a TRANSPORT_OFFER can be judged by
 * the code that owns the rule rather than by an `if` written here.
 *
 * This matters more than it looks. profile_id 0 is reserved and transport_id 0
 * names no binding, and a responder that reimplemented those two checks would
 * be testing its own copy of the rule instead of the library's. A real defect
 * of exactly this shape -- a reserved profile accepted -- was found during the
 * dual-transport migration campaign, and it was found because the check lived
 * in one place.
 */
static mcl_contact_t g_contact;

static uint8_t g_rx_buf[1600];
static uint8_t g_tx_buf[MCL_LINK_FRAME_MAX_SIZE];
static uint8_t g_wire_buf[MCL_WIRE_TIER0_MAX_SIZE];

static const char *ip_status_name(mcl_ip_status_t st)
{
    switch (st) {
    case MCL_IP_OK:                   return "OK";
    case MCL_IP_ERR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case MCL_IP_ERR_RANGE:            return "RANGE";
    case MCL_IP_ERR_TRUNCATED:        return "TRUNCATED";
    case MCL_IP_ERR_NONCANONICAL:     return "NONCANONICAL";
    case MCL_IP_ERR_UNSUPPORTED:      return "UNSUPPORTED";
    default:                          return "UNKNOWN";
    }
}

static void logline(const char *fmt, ...);

/* This peer's own presence object, sent back inside an ACK. */
static void fill_presence(mcl_wire_tier0_t *obj)
{
    memset(obj, 0, sizeof(*obj));
    obj->kind = MCL_WIRE_KIND_PRESENCE;
    obj->priority = 2u;                      /* P2_NORMAL */
    obj->source_ref = g_source_ref;
    /*
     * Written unconditionally and dropped by the encoder at major 1. Writing it
     * only at major 0 would hide an encoder that forgot to drop it, because the
     * struct member would be zero either way.
     */
    obj->body.presence.machine_class = 3u;
    obj->body.presence.capability_tag = 0x17C0DEu;   /* 24-bit field */
    obj->body.presence.ttl = 30u;
}

static void send_reply(mcl_socket_t sock,
                       const struct sockaddr_in *to,
                       mcl_link_frame_class_t cls,
                       uint32_t destination_ref)
{
    mcl_link_frame_t f;
    size_t wire_len = 0u, frame_len = 0u;

    memset(&f, 0, sizeof(f));
    f.frame_class = cls;
    f.flags = MCL_LINK_FLAG_DESTINATION | MCL_LINK_FLAG_SEQUENCE |
              MCL_LINK_FLAG_FRAME_CHECK;
    f.source_ref = g_source_ref;
    f.destination_ref = destination_ref;
    f.sequence = g_tx_sequence;

    if (cls == MCL_LINK_CLASS_ACK) {
        mcl_wire_tier0_t obj;
        fill_presence(&obj);
        if (mcl_wire_tier0_encode_at_major(g_major, &obj, g_wire_buf,
                                           sizeof(g_wire_buf),
                                           &wire_len) != MCL_WIRE_OK) {
            logline("MCLIP TXERR wire_encode major=%u", (unsigned)g_major);
            return;
        }
        f.payload = g_wire_buf;
        f.payload_len = (uint16_t)wire_len;
    }

    /*
     * The reply frame is emitted at the SAME Link major this peer was asked to
     * speak. Answering a major-1 initiator in major-0 frames would still decode
     * -- both layouts are byte-identical -- and would silently make a Stable
     * run partly an experimental one.
     */
    if (mcl_link_frame_encode_at_major(g_major, &f, g_tx_buf, sizeof(g_tx_buf),
                                       &frame_len) != MCL_LINK_OK) {
        logline("MCLIP TXERR frame_encode");
        return;
    }

    if (sendto(sock, (const char *)g_tx_buf, (int)frame_len, 0,
               (const struct sockaddr *)to, sizeof(*to)) != (int)frame_len) {
        logline("MCLIP TXERR sendto");
        return;
    }
    ++g_tx_sequence;
    ++g_tx_total;
}

static void handle_datagram(mcl_socket_t sock, const struct sockaddr_in *from,
                            int len)
{
    mcl_ip_status_t ipst;
    mcl_link_status_t lst;
    mcl_link_frame_t frame;
    size_t consumed = 0u;

    ++g_rx_total;

    /*
     * Validate at the binding boundary first. On a datagram transport the
     * datagram boundary IS the frame boundary, so trailing bytes are a
     * rejection and not something to read past.
     */
    ipst = mcl_ip_datagram_validate(g_rx_buf, (size_t)len);
    if (ipst != MCL_IP_OK) {
        ++g_rx_rejected;
        logline("MCLIP RX n=%d REJECT ip=%s", len, ip_status_name(ipst));
        send_reply(sock, from, MCL_LINK_CLASS_NACK, 0u);
        return;
    }

    lst = mcl_link_frame_decode(g_rx_buf, (size_t)len, &frame, &consumed);
    if (lst != MCL_LINK_OK) {
        /* Unreachable while the validator agrees with the decoder; logged so a
         * future divergence between them is visible rather than silent. */
        ++g_rx_rejected;
        logline("MCLIP RX n=%d REJECT link=%ld", len, (long)lst);
        send_reply(sock, from, MCL_LINK_CLASS_NACK, 0u);
        return;
    }

    if (frame.frame_class == MCL_LINK_CLASS_CONTACT ||
        frame.frame_class == MCL_LINK_CLASS_DATA) {
        mcl_wire_tier0_t obj;
        size_t wire_consumed = 0u;
        mcl_wire_status_t wst = mcl_wire_tier0_decode(frame.payload,
                                                      (size_t)frame.payload_len,
                                                      &obj, &wire_consumed);
        if (wst != MCL_WIRE_OK || wire_consumed != (size_t)frame.payload_len) {
            ++g_rx_rejected;
            logline("MCLIP RX n=%d cls=%u REJECT wire=%ld consumed=%u/%u",
                    len, (unsigned)frame.frame_class, (long)wst,
                    (unsigned)wire_consumed, (unsigned)frame.payload_len);
            send_reply(sock, from, MCL_LINK_CLASS_NACK, frame.source_ref);
            return;
        }
        if (obj.kind == MCL_WIRE_KIND_TRANSPORT_OFFER) {
            /*
             * Judged by mcl-link, not here. A well-formed offer leaves the
             * contact OFFERED, so it is abandoned immediately afterwards: this
             * tool measures whether an offer is ACCEPTABLE, and is not running
             * a migration.
             */
            mcl_link_status_t cst = mcl_contact_record_offer(
                &g_contact,
                obj.body.transport_offer.migration_ref,
                obj.body.transport_offer.transport_id,
                obj.body.transport_offer.profile_id,
                obj.body.transport_offer.endpoint_token,
                obj.body.transport_offer.validity);
            if (cst != MCL_LINK_OK) {
                ++g_rx_rejected;
                logline("MCLIP RX n=%d OFFER REJECT contact=%ld "
                        "transport=%u profile=%u",
                        len, (long)cst,
                        (unsigned)obj.body.transport_offer.transport_id,
                        (unsigned)obj.body.transport_offer.profile_id);
                send_reply(sock, from, MCL_LINK_CLASS_NACK, frame.source_ref);
                return;
            }
            (void)mcl_contact_abandon_migration(&g_contact);
        }

        ++g_rx_accepted;
        logline("MCLIP RX n=%d cls=%u lmaj=%u src=%08lX seq=%u kind=%u ACCEPT",
                len, (unsigned)frame.frame_class, (unsigned)frame.link_major,
                (unsigned long)frame.source_ref, (unsigned)frame.sequence,
                (unsigned)obj.kind);
    } else {
        ++g_rx_accepted;
        logline("MCLIP RX n=%d cls=%u lmaj=%u src=%08lX seq=%u ACCEPT nosem",
                len, (unsigned)frame.frame_class, (unsigned)frame.link_major,
                (unsigned long)frame.source_ref, (unsigned)frame.sequence);
    }

    send_reply(sock, from, MCL_LINK_CLASS_ACK, frame.source_ref);
}

static void usage(const char *argv0)
{
    printf("usage: %s [--port N] [--bind A.B.C.D] [--major 0|1] [--count N]\n"
           "          [--source-ref 0xHEX] [--quiet]\n", argv0);
}

int main(int argc, char **argv)
{
    const char *bind_ip = NULL;
    int port = DEFAULT_PORT;
    long count = -1;             /* -1 = run until interrupted */
    long handled = 0;
    int i;
    mcl_socket_t sock;
    struct sockaddr_in local;
#if defined(_WIN32)
    WSADATA wsa;
#endif

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            bind_ip = argv[++i];
        } else if (strcmp(argv[i], "--major") == 0 && i + 1 < argc) {
            g_major = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            count = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--source-ref") == 0 && i + 1 < argc) {
            g_source_ref = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--quiet") == 0) {
            g_quiet = 1;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (g_major != MCL_WIRE_EXPERIMENTAL_MAJOR &&
        g_major != MCL_WIRE_STABLE_MAJOR) {
        printf("--major must be 0 or 1; %u is not an assigned major\n",
               (unsigned)g_major);
        return 2;
    }

#if defined(_WIN32)
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
#endif

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == MCL_INVALID_SOCKET) {
        printf("socket() failed\n");
        return 1;
    }

    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons((unsigned short)port);
    local.sin_addr.s_addr = (bind_ip != NULL) ? inet_addr(bind_ip) : INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
        printf("bind() to %s:%d failed\n",
               (bind_ip != NULL) ? bind_ip : "0.0.0.0", port);
        mcl_close_socket(sock);
        return 1;
    }

    printf("MCL-IP over-air responder\n");
    printf("=========================\n");
    printf("bind=%s port=%d major=%u source_ref=%08lX frame_max=%u\n",
           (bind_ip != NULL) ? bind_ip : "0.0.0.0", port,
           (unsigned)g_major, (unsigned long)g_source_ref,
           (unsigned)MCL_LINK_FRAME_MAX_SIZE);
    if (mcl_contact_begin(&g_contact, MCL_CONTACT_ROLE_RESPONDER,
                          g_source_ref, 2u) != MCL_LINK_OK) {
        printf("mcl_contact_begin failed\n");
        mcl_close_socket(sock);
        return 1;
    }

    printf("MCLIP READY\n");
    fflush(stdout);

    while (count < 0 || handled < count) {
        struct sockaddr_in from;
        mcl_socklen_t fromlen = (mcl_socklen_t)sizeof(from);
        int n = recvfrom(sock, (char *)g_rx_buf, (int)sizeof(g_rx_buf), 0,
                         (struct sockaddr *)&from, &fromlen);
        if (n <= 0) {
            continue;
        }
        handle_datagram(sock, &from, n);
        ++handled;
    }

    printf("MCLIP DONE rx=%lu accept=%lu reject=%lu tx=%lu\n",
           g_rx_total, g_rx_accepted, g_rx_rejected, g_tx_total);
    fflush(stdout);

    mcl_close_socket(sock);
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}

static void logline(const char *fmt, ...)
{
    va_list ap;
    if (g_quiet) {
        return;
    }
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}
