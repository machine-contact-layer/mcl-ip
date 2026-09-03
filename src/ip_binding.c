/*
 * MCL-IP binding v0 reference implementation.
 *
 * Freestanding C99. No sockets, no allocation, no global mutable state.
 */

#include "mcl/ip_binding.h"
#include "mcl/link.h"

/*
 * Byte copy that the compiler may not rewrite into a call to memcpy.
 *
 * A plain indexed loop is pattern-matched into memcpy by every optimising
 * compiler, which reintroduces a libc dependency into an object that must link
 * on a freestanding target with no C library present. The volatile destination
 * defeats that rewrite. mcl-wire solves the same problem the same way.
 */
static void mcl_ip_copy_bytes(uint8_t *dst, const uint8_t *src, size_t count)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    size_t i;

    for (i = 0u; i < count; ++i) {
        out[i] = src[i];
    }
}

/* Address size required by each family, or 0xFF when variable. */
static uint8_t mcl_ip_family_address_size(mcl_ip_address_family_t family)
{
    switch (family) {
    case MCL_IP_AF_UNSPECIFIED: return 0u;
    case MCL_IP_AF_IPV4:        return 4u;
    case MCL_IP_AF_IPV6:        return 16u;
    case MCL_IP_AF_LOCAL_REF:   return 0xFFu;   /* 1..16, caller chooses */
    default:                    return 0xFEu;   /* invalid */
    }
}

static uint8_t mcl_ip_endpoint_valid(const mcl_ip_endpoint_t *endpoint)
{
    uint8_t required;

    if (endpoint == NULL) {
        return 0u;
    }
    if (endpoint->mode >= MCL_IP_MODE_COUNT) {
        return 0u;
    }
    if (endpoint->security_claim >= MCL_IP_SEC_COUNT) {
        return 0u;
    }
    if (endpoint->address_size > MCL_IP_ADDRESS_MAX_SIZE) {
        return 0u;
    }

    required = mcl_ip_family_address_size(endpoint->family);
    if (required == 0xFEu) {
        return 0u;
    }
    if (required == 0xFFu) {
        /* An opaque local reference must actually reference something. */
        return (endpoint->address_size >= 1u) ? 1u : 0u;
    }
    return (endpoint->address_size == required) ? 1u : 0u;
}

size_t mcl_ip_endpoint_encoded_size(const mcl_ip_endpoint_t *endpoint)
{
    if (mcl_ip_endpoint_valid(endpoint) == 0u) {
        return 0u;
    }
    return (size_t)MCL_IP_ENDPOINT_MIN_SIZE + (size_t)endpoint->address_size;
}

mcl_ip_status_t mcl_ip_endpoint_encode(
    const mcl_ip_endpoint_t *endpoint,
    uint8_t *out,
    size_t out_capacity,
    size_t *written)
{
    size_t need;
    size_t pos = 0u;

    if (endpoint == NULL || out == NULL || written == NULL) {
        return MCL_IP_ERR_INVALID_ARGUMENT;
    }
    if (mcl_ip_endpoint_valid(endpoint) == 0u) {
        return MCL_IP_ERR_RANGE;
    }

    need = mcl_ip_endpoint_encoded_size(endpoint);
    if (out_capacity < need) {
        return MCL_IP_ERR_RANGE;
    }

    out[pos++] = endpoint->family;
    out[pos++] = endpoint->mode;
    out[pos++] = endpoint->security_claim;
    out[pos++] = endpoint->address_size;

    mcl_ip_copy_bytes(out + pos, endpoint->address, (size_t)endpoint->address_size);
    pos += (size_t)endpoint->address_size;

    out[pos++] = (uint8_t)(endpoint->port >> 8u);
    out[pos++] = (uint8_t)(endpoint->port & 0xFFu);
    out[pos++] = (uint8_t)(endpoint->validity_s >> 8u);
    out[pos++] = (uint8_t)(endpoint->validity_s & 0xFFu);

    *written = pos;
    return MCL_IP_OK;
}

mcl_ip_status_t mcl_ip_endpoint_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_ip_endpoint_t *endpoint,
    size_t *consumed)
{
    size_t pos = 0u;
    size_t i;
    uint8_t address_size;

    if (in == NULL || endpoint == NULL || consumed == NULL) {
        return MCL_IP_ERR_INVALID_ARGUMENT;
    }
    if (in_size < (size_t)MCL_IP_ENDPOINT_MIN_SIZE) {
        return MCL_IP_ERR_TRUNCATED;
    }

    endpoint->family = in[0];
    endpoint->mode = in[1];
    endpoint->security_claim = in[2];
    address_size = in[3];
    pos = 4u;

    if (address_size > MCL_IP_ADDRESS_MAX_SIZE) {
        return MCL_IP_ERR_RANGE;
    }
    if (in_size < (size_t)MCL_IP_ENDPOINT_MIN_SIZE + (size_t)address_size) {
        return MCL_IP_ERR_TRUNCATED;
    }

    {
        volatile uint8_t *addr = (volatile uint8_t *)endpoint->address;
        for (i = 0u; i < (size_t)MCL_IP_ADDRESS_MAX_SIZE; ++i) {
            addr[i] = 0u;
        }
    }
    mcl_ip_copy_bytes(endpoint->address, in + pos, (size_t)address_size);
    endpoint->address_size = address_size;
    pos += (size_t)address_size;

    endpoint->port = (uint16_t)(((uint16_t)in[pos] << 8u) | (uint16_t)in[pos + 1u]);
    pos += 2u;
    endpoint->validity_s = (uint16_t)(((uint16_t)in[pos] << 8u) | (uint16_t)in[pos + 1u]);
    pos += 2u;

    /*
     * Validate after parsing so that a structurally readable but semantically
     * impossible endpoint is rejected rather than half-accepted. An IPv4
     * family carrying a 16-byte address is not a decoding we may guess at.
     */
    if (mcl_ip_endpoint_valid(endpoint) == 0u) {
        return MCL_IP_ERR_NONCANONICAL;
    }

    *consumed = pos;
    return MCL_IP_OK;
}

/*
 * Translate a Link decode failure into this binding's vocabulary.
 *
 * The distinction that matters to a caller is whether more bytes could help.
 * Truncation says yes; every other failure says these bytes are not a frame
 * and never will be. An incompatible major version is reported separately
 * because it is a statement about the peer, not about the bytes: local policy
 * may want to react to it rather than simply drop.
 */
static mcl_ip_status_t mcl_ip_translate_link_status(mcl_link_status_t st)
{
    switch (st) {
    case MCL_LINK_ERR_TRUNCATED:
        return MCL_IP_ERR_TRUNCATED;
    case MCL_LINK_ERR_INCOMPATIBLE_VERSION:
        return MCL_IP_ERR_UNSUPPORTED;
    case MCL_LINK_ERR_INVALID_ARGUMENT:
        return MCL_IP_ERR_INVALID_ARGUMENT;
    default:
        /* Unknown class, reserved bits set, oversize payload, failed frame check. */
        return MCL_IP_ERR_NONCANONICAL;
    }
}

mcl_ip_status_t mcl_ip_datagram_validate(
    const uint8_t *datagram,
    size_t datagram_size)
{
    mcl_link_frame_t frame;
    size_t consumed = 0u;
    mcl_link_status_t st;

    if (datagram == NULL) {
        return MCL_IP_ERR_INVALID_ARGUMENT;
    }

    st = mcl_link_frame_decode(datagram, datagram_size, &frame, &consumed);
    if (st != MCL_LINK_OK) {
        return mcl_ip_translate_link_status(st);
    }
    if (consumed != datagram_size) {
        /* A datagram boundary is a frame boundary. Trailing bytes are not ours. */
        return MCL_IP_ERR_NONCANONICAL;
    }

    return MCL_IP_OK;
}

mcl_ip_status_t mcl_ip_stream_wrap(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t *out,
    size_t out_capacity,
    size_t *written)
{
    if (frame == NULL || out == NULL || written == NULL) {
        return MCL_IP_ERR_INVALID_ARGUMENT;
    }
    if (frame_size == 0u || frame_size > (size_t)MCL_LINK_FRAME_MAX_SIZE) {
        /* The prefix could express 65535, but no legal Link frame is that
         * large. Bounding it here keeps a reader from being told to expect
         * bytes that could never form a frame. */
        return MCL_IP_ERR_RANGE;
    }
    if (out_capacity < (size_t)MCL_IP_STREAM_PREFIX_SIZE + frame_size) {
        return MCL_IP_ERR_RANGE;
    }

    out[0] = (uint8_t)(frame_size >> 8u);
    out[1] = (uint8_t)(frame_size & 0xFFu);
    mcl_ip_copy_bytes(out + MCL_IP_STREAM_PREFIX_SIZE, frame, frame_size);

    *written = (size_t)MCL_IP_STREAM_PREFIX_SIZE + frame_size;
    return MCL_IP_OK;
}

mcl_ip_status_t mcl_ip_stream_next(
    const uint8_t *in,
    size_t in_size,
    const uint8_t **frame,
    size_t *frame_size,
    size_t *consumed)
{
    size_t declared;

    if (in == NULL || frame == NULL || frame_size == NULL || consumed == NULL) {
        return MCL_IP_ERR_INVALID_ARGUMENT;
    }
    if (in_size < (size_t)MCL_IP_STREAM_PREFIX_SIZE) {
        return MCL_IP_ERR_TRUNCATED;
    }

    declared = ((size_t)in[0] << 8u) | (size_t)in[1];
    if (declared == 0u) {
        /* A zero-length record carries nothing and desynchronises nothing
         * useful; treat it as a framing error rather than an empty frame. */
        return MCL_IP_ERR_NONCANONICAL;
    }
    if (declared > (size_t)MCL_LINK_FRAME_MAX_SIZE) {
        /*
         * A length no legal frame could have means the stream is already
         * desynchronised. Reporting it as non-canonical rather than truncated
         * matters: truncated would tell the caller to wait for bytes that are
         * never going to arrive, and to buffer up to 64 KiB while waiting.
         */
        return MCL_IP_ERR_NONCANONICAL;
    }
    if (in_size < (size_t)MCL_IP_STREAM_PREFIX_SIZE + declared) {
        return MCL_IP_ERR_TRUNCATED;
    }

    *frame = in + MCL_IP_STREAM_PREFIX_SIZE;
    *frame_size = declared;
    *consumed = (size_t)MCL_IP_STREAM_PREFIX_SIZE + declared;
    return MCL_IP_OK;
}

size_t mcl_ip_max_frame_for_mtu(
    mcl_ip_address_family_t family,
    size_t path_mtu)
{
    size_t overhead;

    switch (family) {
    case MCL_IP_AF_IPV4:
        overhead = 20u + 8u;   /* minimum IPv4 header + UDP header */
        break;
    case MCL_IP_AF_IPV6:
        overhead = 40u + 8u;   /* IPv6 header + UDP header */
        break;
    default:
        return 0u;
    }

    if (path_mtu <= overhead) {
        return 0u;
    }
    if (path_mtu - overhead < (size_t)MCL_LINK_FRAME_MIN_SIZE) {
        return 0u;
    }
    if (path_mtu - overhead > (size_t)MCL_LINK_FRAME_MAX_SIZE) {
        /* A large MTU does not make a larger frame legal. */
        return (size_t)MCL_LINK_FRAME_MAX_SIZE;
    }

    return path_mtu - overhead;
}

/* ---------- Endpoint rendezvous ---------- */

mcl_ip_status_t mcl_ip_rendezvous_datagram_encode(
    uint32_t endpoint_token,
    uint8_t *out,
    size_t out_capacity,
    size_t *written)
{
    size_t beacon_written = 0u;
    mcl_link_status_t lst;

    if (out == NULL || written == NULL) {
        return MCL_IP_ERR_INVALID_ARGUMENT;
    }

    lst = mcl_rendezvous_beacon_encode(
        MCL_IP_TRANSPORT_ID, endpoint_token, out, out_capacity, &beacon_written);
    if (lst == MCL_LINK_ERR_INVALID_ARGUMENT) {
        return MCL_IP_ERR_INVALID_ARGUMENT;
    }
    if (lst != MCL_LINK_OK) {
        return MCL_IP_ERR_RANGE;
    }

    *written = beacon_written;
    return MCL_IP_OK;
}

uint8_t mcl_ip_rendezvous_datagram_matches(
    const uint8_t *data,
    size_t size,
    uint32_t expected_token)
{
    return mcl_rendezvous_beacon_matches(
        data, size, MCL_IP_TRANSPORT_ID, expected_token);
}
