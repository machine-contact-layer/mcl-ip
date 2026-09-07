#ifndef MCL_IP_BINDING_H
#define MCL_IP_BINDING_H

#include <stddef.h>
#include <stdint.h>

#include "mcl/endpoint_rendezvous.h"  /* the beacon this binding places in a discovery datagram */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MCL-IP binding v0 (research draft).
 *
 * Carriage of MCL Link frames over IP-capable transports, and the endpoint
 * representation an MCL TRANSPORT_OFFER references.
 *
 * Scope discipline: this module contains no sockets, no DNS, no NAT traversal
 * and no OS calls. It is freestanding C99 with no allocation and no global
 * mutable state, exactly like Wire and Link. Deciding how bytes reach the
 * network is the integrator's job; deciding what those bytes mean is MCL's.
 * The charter forbids physical technology leaking upward, and that cuts both
 * ways: a binding describes a mapping, it does not become a network stack.
 */

typedef int32_t mcl_ip_status_t;
enum {
    MCL_IP_OK = 0,
    MCL_IP_ERR_INVALID_ARGUMENT = 1,
    MCL_IP_ERR_RANGE = 2,
    MCL_IP_ERR_TRUNCATED = 3,
    MCL_IP_ERR_NONCANONICAL = 4,
    MCL_IP_ERR_UNSUPPORTED = 5
};

/* Transport identifier assigned to this binding in mcl-link. */
#define MCL_IP_TRANSPORT_ID 0x02u

/* ---------- Address families ---------- */

typedef uint8_t mcl_ip_address_family_t;
enum {
    MCL_IP_AF_UNSPECIFIED = 0u,
    MCL_IP_AF_IPV4        = 1u,
    MCL_IP_AF_IPV6        = 2u,
    /*
     * A peer that will not disclose a routable address before trust is
     * established may offer an opaque local reference instead, which the
     * integrator resolves by its own policy. First contact must not force
     * address disclosure.
     */
    MCL_IP_AF_LOCAL_REF   = 3u,
    MCL_IP_AF_COUNT       = 4u
};

/* ---------- Carriage modes ---------- */

typedef uint8_t mcl_ip_mode_t;
enum {
    /* One Link frame per datagram. The datagram boundary is the frame boundary. */
    MCL_IP_MODE_DATAGRAM = 0u,
    /* Length-delimited Link frames over a reliable byte stream. */
    MCL_IP_MODE_STREAM   = 1u,
    MCL_IP_MODE_COUNT    = 2u
};

/*
 * Security posture advertised with an endpoint. This is a claim about what the
 * offering peer says it will do, never evidence that it did. Local policy
 * decides what a claim is worth.
 */
typedef uint8_t mcl_ip_security_t;
enum {
    MCL_IP_SEC_NONE_CLAIMED = 0u,
    MCL_IP_SEC_TLS_CLAIMED  = 1u,
    MCL_IP_SEC_DTLS_CLAIMED = 2u,
    MCL_IP_SEC_COUNT        = 3u
};

#define MCL_IP_ADDRESS_MAX_SIZE 16u

/*
 * Endpoint offer, referenced by an MCL TRANSPORT_OFFER.
 *
 * Canonical encoding, network byte order:
 *
 *   u8   address_family
 *   u8   mode
 *   u8   security_claim
 *   u8   address_size
 *   u8   address[address_size]
 *   u16  port
 *   u16  validity_s        0 means unspecified, not infinite
 *
 * address_size is fixed by family: 4 for IPv4, 16 for IPv6, 0 for unspecified,
 * and 1..16 for an opaque local reference. Any other combination is
 * non-canonical and is rejected.
 */
typedef struct {
    mcl_ip_address_family_t family;
    mcl_ip_mode_t mode;
    mcl_ip_security_t security_claim;
    uint8_t address[MCL_IP_ADDRESS_MAX_SIZE];
    uint8_t address_size;
    uint16_t port;
    uint16_t validity_s;
} mcl_ip_endpoint_t;

#define MCL_IP_ENDPOINT_MIN_SIZE 8u
#define MCL_IP_ENDPOINT_MAX_SIZE (8u + MCL_IP_ADDRESS_MAX_SIZE)

size_t mcl_ip_endpoint_encoded_size(const mcl_ip_endpoint_t *endpoint);

mcl_ip_status_t mcl_ip_endpoint_encode(
    const mcl_ip_endpoint_t *endpoint,
    uint8_t *out,
    size_t out_capacity,
    size_t *written);

mcl_ip_status_t mcl_ip_endpoint_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_ip_endpoint_t *endpoint,
    size_t *consumed);

/* ---------- Carriage ---------- */

/*
 * Datagram mode.
 *
 * A datagram carries exactly one Link frame and nothing else. Trailing bytes
 * are rejected: on a message-oriented transport they indicate a malformed or
 * concatenated datagram, and silently ignoring them is how framing confusion
 * becomes semantic confusion.
 *
 * UDP and QUIC datagrams already detect corruption, so an integrity field on
 * the Link frame is permitted but not required here.
 */
mcl_ip_status_t mcl_ip_datagram_validate(
    const uint8_t *datagram,
    size_t datagram_size);

/*
 * Stream mode.
 *
 * A reliable byte stream has no message boundaries, so each Link frame is
 * preceded by a u16 total length in network byte order:
 *
 *   u16 frame_size
 *   u8  link_frame[frame_size]
 *
 * The prefix is redundant with the frame's own self-description, and that is
 * deliberate: it lets a receiver skip a frame it cannot decode without losing
 * stream synchronisation, which is the difference between dropping one frame
 * and dropping the connection.
 */
/* ---------- Endpoint rendezvous ----------
 *
 * Where the transport-neutral rendezvous beacon (mcl-link/rendezvous.h) is
 * placed for IP: the payload of a discovery datagram sent to a link-local
 * destination, so a peer learns the concrete address from the datagram's own
 * source address rather than from anything carried in the offer.
 *
 * The offer therefore never has to contain an IP address, which keeps it small
 * on the first-contact medium and avoids announcing a machine's network
 * topology to everyone in earshot.
 *
 * PORT. 49913 is in the dynamic/private range and is NOT registered with IANA.
 * It is provisional and this project holds no assignment. A deployment that
 * needs a stable well-known port must register one; squatting on an assigned
 * port would collide with its owner, and claiming registration we do not have
 * would be worse than admitting we have none.
 *
 * DESTINATION. The binding does not mandate one, because the right choice
 * differs by deployment: IPv4 subnet broadcast, IPv6 link-local multicast, or a
 * SoftAP's broadcast address are all reasonable and none is portable to the
 * others. What must be agreed is the port and the payload, which is what is
 * defined here.
 */
#define MCL_IP_RENDEZVOUS_PORT 49913u
#define MCL_IP_RENDEZVOUS_DATAGRAM_SIZE MCL_RENDEZVOUS_BEACON_SIZE

/*
 * Build the discovery datagram payload advertising `endpoint_token`.
 *
 * The datagram carries the beacon and nothing else. The address the peer needs
 * is the datagram's source address, which the receiver already has from its
 * socket; repeating it in the payload would add a second copy that could
 * disagree with the first, which is how NAT and multi-homing bugs begin.
 */
mcl_ip_status_t mcl_ip_rendezvous_datagram_encode(
    uint32_t endpoint_token,
    uint8_t *out,
    size_t out_capacity,
    size_t *written);

/*
 * Receive side: does this datagram payload advertise `expected_token`?
 * Returns 0 for anything else, including malformed input, because a socket
 * bound to a broadcast port receives unrelated traffic as a matter of course.
 */
uint8_t mcl_ip_rendezvous_datagram_matches(
    const uint8_t *data,
    size_t size,
    uint32_t expected_token);

#define MCL_IP_STREAM_PREFIX_SIZE 2u

mcl_ip_status_t mcl_ip_stream_wrap(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t *out,
    size_t out_capacity,
    size_t *written);

/*
 * Read one length-delimited frame from a stream buffer.
 *
 * Returns MCL_IP_ERR_TRUNCATED when the buffer does not yet hold a complete
 * record, which is the normal case on a stream and means "call again with more
 * bytes", not "the peer is broken". On success `frame` points into `in` and
 * `consumed` covers the prefix plus the frame.
 */
mcl_ip_status_t mcl_ip_stream_next(
    const uint8_t *in,
    size_t in_size,
    const uint8_t **frame,
    size_t *frame_size,
    size_t *consumed);

/* ---------- Maximum transmission unit ---------- */

/*
 * Largest Link frame that fits a given path MTU in datagram mode, after IP and
 * UDP headers.
 *
 * Two limits apply and the smaller wins. Below roughly 1 KiB the path is the
 * constraint; above it the protocol is, because no legal Link frame exceeds
 * MCL_LINK_FRAME_MAX_SIZE. A large MTU therefore does not raise the answer, and
 * a caller that sized a buffer from a jumbo MTU would be sizing it for a frame
 * that could never be encoded.
 *
 * Returns 0 when the MTU cannot carry even a minimal frame.
 */
size_t mcl_ip_max_frame_for_mtu(
    mcl_ip_address_family_t family,
    size_t path_mtu);

#ifdef __cplusplus
}
#endif

#endif /* MCL_IP_BINDING_H */
