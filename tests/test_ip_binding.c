/*
 * MCL-IP binding v0 tests.
 *
 * Endpoint canonicalisation, datagram boundary discipline, and stream
 * resynchronisation, including the negative cases.
 */

#include "mcl/ip_binding.h"
#include "mcl/link.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do {                                      \
    ++tests_run;                                                   \
    if (!(cond)) {                                                 \
        ++tests_failed;                                            \
        printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    }                                                              \
} while (0)

static const uint8_t k_presence[] = {
    0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x00u, 0x00u, 0x01u, 0x3Cu
};

static size_t build_frame(uint8_t *out, size_t capacity, uint16_t seq)
{
    mcl_link_frame_t f;
    size_t written = 0u;

    memset(&f, 0, sizeof(f));
    f.frame_class = MCL_LINK_CLASS_DATA;
    f.flags = MCL_LINK_FLAG_SEQUENCE;
    f.source_ref = 0x0A0B0C0Du;
    f.sequence = seq;
    f.payload = k_presence;
    f.payload_len = (uint16_t)sizeof(k_presence);

    if (mcl_link_frame_encode(&f, out, capacity, &written) != MCL_LINK_OK) {
        return 0u;
    }
    return written;
}

static void test_endpoint_round_trip_ipv4(void)
{
    mcl_ip_endpoint_t tx, rx;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] IPv4 endpoint round trip\n");

    memset(&tx, 0, sizeof(tx));
    tx.family = MCL_IP_AF_IPV4;
    tx.mode = MCL_IP_MODE_DATAGRAM;
    tx.security_claim = MCL_IP_SEC_DTLS_CLAIMED;
    tx.address[0] = 192u; tx.address[1] = 168u;
    tx.address[2] = 1u;   tx.address[3] = 42u;
    tx.address_size = 4u;
    tx.port = 5353u;
    tx.validity_s = 300u;

    CHECK(mcl_ip_endpoint_encoded_size(&tx) == MCL_IP_ENDPOINT_MIN_SIZE + 4u,
          "IPv4 endpoint size");
    CHECK(mcl_ip_endpoint_encode(&tx, buf, sizeof(buf), &written) == MCL_IP_OK,
          "IPv4 encode");
    CHECK(mcl_ip_endpoint_decode(buf, written, &rx, &consumed) == MCL_IP_OK,
          "IPv4 decode");
    CHECK(consumed == written, "IPv4 consumed all bytes");
    CHECK(rx.family == MCL_IP_AF_IPV4, "family preserved");
    CHECK(rx.port == 5353u, "port preserved");
    CHECK(rx.validity_s == 300u, "validity preserved");
    CHECK(rx.security_claim == MCL_IP_SEC_DTLS_CLAIMED, "security claim preserved");
    CHECK(memcmp(rx.address, tx.address, 4u) == 0, "address preserved");
}

static void test_endpoint_round_trip_ipv6_and_localref(void)
{
    mcl_ip_endpoint_t tx, rx;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;
    unsigned i;

    printf("[TEST] IPv6 and opaque local reference round trip\n");

    memset(&tx, 0, sizeof(tx));
    tx.family = MCL_IP_AF_IPV6;
    tx.mode = MCL_IP_MODE_STREAM;
    tx.security_claim = MCL_IP_SEC_TLS_CLAIMED;
    for (i = 0u; i < 16u; ++i) { tx.address[i] = (uint8_t)(i + 1u); }
    tx.address_size = 16u;
    tx.port = 443u;

    CHECK(mcl_ip_endpoint_encode(&tx, buf, sizeof(buf), &written) == MCL_IP_OK,
          "IPv6 encode");
    CHECK(written == MCL_IP_ENDPOINT_MAX_SIZE, "IPv6 uses the maximum size");
    CHECK(mcl_ip_endpoint_decode(buf, written, &rx, &consumed) == MCL_IP_OK,
          "IPv6 decode");
    CHECK(memcmp(rx.address, tx.address, 16u) == 0, "IPv6 address preserved");

    memset(&tx, 0, sizeof(tx));
    tx.family = MCL_IP_AF_LOCAL_REF;
    tx.mode = MCL_IP_MODE_DATAGRAM;
    tx.address[0] = 0x7Fu;
    tx.address_size = 1u;
    CHECK(mcl_ip_endpoint_encode(&tx, buf, sizeof(buf), &written) == MCL_IP_OK,
          "local reference encode");
    CHECK(mcl_ip_endpoint_decode(buf, written, &rx, &consumed) == MCL_IP_OK,
          "local reference decode");
    CHECK(rx.address_size == 1u, "local reference size preserved");
}

static void test_endpoint_rejects_inconsistent_family(void)
{
    mcl_ip_endpoint_t ep;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] family and address size must agree\n");

    memset(&ep, 0, sizeof(ep));
    ep.family = MCL_IP_AF_IPV4;
    ep.mode = MCL_IP_MODE_DATAGRAM;
    ep.address_size = 16u;   /* an IPv4 family cannot carry 16 bytes */
    CHECK(mcl_ip_endpoint_encoded_size(&ep) == 0u, "inconsistent has no size");
    CHECK(mcl_ip_endpoint_encode(&ep, buf, sizeof(buf), &written) == MCL_IP_ERR_RANGE,
          "inconsistent not encodable");

    /* Same inconsistency arriving from the wire must be rejected, not guessed. */
    ep.address_size = 4u;
    CHECK(mcl_ip_endpoint_encode(&ep, buf, sizeof(buf), &written) == MCL_IP_OK,
          "valid IPv4 encodes");
    buf[3] = 16u;   /* claim 16 address bytes under family IPv4 */
    CHECK(mcl_ip_endpoint_decode(buf, sizeof(buf), &ep, &consumed)
              == MCL_IP_ERR_NONCANONICAL,
          "inconsistent decode rejected as non-canonical");
}

static void test_endpoint_rejects_unknown_enums(void)
{
    mcl_ip_endpoint_t ep;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] unknown family, mode and security claim are rejected\n");

    memset(&ep, 0, sizeof(ep));
    ep.family = MCL_IP_AF_IPV4;
    ep.mode = MCL_IP_MODE_DATAGRAM;
    ep.address_size = 4u;
    CHECK(mcl_ip_endpoint_encode(&ep, buf, sizeof(buf), &written) == MCL_IP_OK,
          "baseline encodes");

    buf[0] = (uint8_t)MCL_IP_AF_COUNT;
    CHECK(mcl_ip_endpoint_decode(buf, written, &ep, &consumed) != MCL_IP_OK,
          "unknown family rejected");

    (void)mcl_ip_endpoint_encode(&ep, buf, sizeof(buf), &written);
    buf[0] = MCL_IP_AF_IPV4;
    buf[1] = (uint8_t)MCL_IP_MODE_COUNT;
    CHECK(mcl_ip_endpoint_decode(buf, written, &ep, &consumed) != MCL_IP_OK,
          "unknown mode rejected");

    buf[1] = MCL_IP_MODE_DATAGRAM;
    buf[2] = (uint8_t)MCL_IP_SEC_COUNT;
    CHECK(mcl_ip_endpoint_decode(buf, written, &ep, &consumed) != MCL_IP_OK,
          "unknown security claim rejected");
}

static void test_endpoint_truncation(void)
{
    mcl_ip_endpoint_t ep;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u, n;

    printf("[TEST] endpoint truncation rejected at every length\n");

    memset(&ep, 0, sizeof(ep));
    ep.family = MCL_IP_AF_IPV6;
    ep.mode = MCL_IP_MODE_STREAM;
    ep.address_size = 16u;
    (void)mcl_ip_endpoint_encode(&ep, buf, sizeof(buf), &written);

    for (n = 0u; n < written; ++n) {
        CHECK(mcl_ip_endpoint_decode(buf, n, &ep, &consumed) != MCL_IP_OK,
              "short endpoint never decodes");
    }
}

static void test_datagram_boundary_is_frame_boundary(void)
{
    uint8_t frame[128];
    uint8_t padded[160];
    size_t frame_size;

    printf("[TEST] datagram carries exactly one frame\n");

    frame_size = build_frame(frame, sizeof(frame), 7u);
    CHECK(frame_size > 0u, "frame built");
    CHECK(mcl_ip_datagram_validate(frame, frame_size) == MCL_IP_OK,
          "exact datagram accepted");

    memcpy(padded, frame, frame_size);
    padded[frame_size] = 0x00u;
    CHECK(mcl_ip_datagram_validate(padded, frame_size + 1u) == MCL_IP_ERR_NONCANONICAL,
          "trailing byte rejected, never ignored");

    CHECK(mcl_ip_datagram_validate(frame, frame_size - 1u) == MCL_IP_ERR_TRUNCATED,
          "a short datagram reports truncation");

    /*
     * A datagram that is complete but carries a class this version does not
     * define must not be reported as truncated. On a datagram transport there
     * is no "wait for more bytes", so a truncation report would be a lie the
     * caller cannot act on.
     */
    memcpy(padded, frame, frame_size);
    padded[0] = (uint8_t)((padded[0] & 0xF0u) | 0x0Fu);
    CHECK(mcl_ip_datagram_validate(padded, frame_size) == MCL_IP_ERR_NONCANONICAL,
          "unknown frame class is non-canonical, not truncated");

    memcpy(padded, frame, frame_size);
    padded[0] = (uint8_t)((1u << 4u) | (padded[0] & 0x0Fu));
    CHECK(mcl_ip_datagram_validate(padded, frame_size) == MCL_IP_ERR_UNSUPPORTED,
          "a future Link major version is reported as unsupported");
    CHECK(mcl_ip_datagram_validate(NULL, frame_size) == MCL_IP_ERR_INVALID_ARGUMENT,
          "null datagram rejected");
}

static void test_stream_round_trip_and_resync(void)
{
    uint8_t frame[128];
    uint8_t stream[512];
    const uint8_t *out_frame = NULL;
    size_t frame_size, pos = 0u, written = 0u, out_size = 0u, consumed = 0u;
    unsigned i, decoded = 0u;

    printf("[TEST] stream framing round trip\n");

    for (i = 0u; i < 3u; ++i) {
        frame_size = build_frame(frame, sizeof(frame), (uint16_t)i);
        CHECK(mcl_ip_stream_wrap(frame, frame_size, stream + pos,
                                 sizeof(stream) - pos, &written) == MCL_IP_OK,
              "wrap frame");
        pos += written;
    }

    written = 0u;
    while (written < pos) {
        mcl_link_frame_t decoded_frame;
        size_t frame_consumed = 0u;

        if (mcl_ip_stream_next(stream + written, pos - written,
                               &out_frame, &out_size, &consumed) != MCL_IP_OK) {
            break;
        }
        CHECK(mcl_link_frame_decode(out_frame, out_size, &decoded_frame,
                                    &frame_consumed) == MCL_LINK_OK,
              "framed payload decodes as a Link frame");
        CHECK(decoded_frame.sequence == (uint16_t)decoded, "stream order preserved");
        written += consumed;
        ++decoded;
    }
    CHECK(decoded == 3u, "all frames recovered from the stream");
    CHECK(written == pos, "stream fully consumed");
}

static void test_stream_partial_reads(void)
{
    uint8_t frame[128];
    uint8_t stream[256];
    const uint8_t *out_frame = NULL;
    size_t frame_size, written = 0u, out_size = 0u, consumed = 0u, n;

    printf("[TEST] a partial stream record reports truncation, not corruption\n");

    frame_size = build_frame(frame, sizeof(frame), 1u);
    (void)mcl_ip_stream_wrap(frame, frame_size, stream, sizeof(stream), &written);

    for (n = 0u; n < written; ++n) {
        CHECK(mcl_ip_stream_next(stream, n, &out_frame, &out_size, &consumed)
                  == MCL_IP_ERR_TRUNCATED,
              "incomplete record asks for more bytes");
    }
    CHECK(mcl_ip_stream_next(stream, written, &out_frame, &out_size, &consumed)
              == MCL_IP_OK,
          "complete record decodes");

    /* A zero-length record is a framing error, not an empty frame. */
    stream[0] = 0u; stream[1] = 0u;
    CHECK(mcl_ip_stream_next(stream, written, &out_frame, &out_size, &consumed)
              == MCL_IP_ERR_NONCANONICAL,
          "zero-length record rejected");
}

static void test_stream_skips_undecodable_frame(void)
{
    uint8_t frame[128];
    uint8_t stream[512];
    const uint8_t *out_frame = NULL;
    size_t frame_size, pos = 0u, written = 0u, out_size = 0u, consumed = 0u;
    mcl_link_frame_t decoded_frame;
    size_t frame_consumed = 0u;

    printf("[TEST] length prefix preserves sync across an undecodable frame\n");

    /* First record: deliberately corrupted to an unassigned frame class. */
    frame_size = build_frame(frame, sizeof(frame), 0u);
    frame[0] = 0x0Fu;
    (void)mcl_ip_stream_wrap(frame, frame_size, stream + pos, sizeof(stream) - pos, &written);
    pos += written;

    /* Second record: valid. */
    frame_size = build_frame(frame, sizeof(frame), 99u);
    (void)mcl_ip_stream_wrap(frame, frame_size, stream + pos, sizeof(stream) - pos, &written);
    pos += written;

    /* The first record is framed correctly even though its contents are not. */
    CHECK(mcl_ip_stream_next(stream, pos, &out_frame, &out_size, &consumed) == MCL_IP_OK,
          "first record framed");
    CHECK(mcl_link_frame_decode(out_frame, out_size, &decoded_frame, &frame_consumed)
              != MCL_LINK_OK,
          "first record content is undecodable");

    /* Skipping it lands exactly on the next record, which is the point. */
    CHECK(mcl_ip_stream_next(stream + consumed, pos - consumed,
                             &out_frame, &out_size, &consumed) == MCL_IP_OK,
          "second record framed after skipping the first");
    CHECK(mcl_link_frame_decode(out_frame, out_size, &decoded_frame, &frame_consumed)
              == MCL_LINK_OK,
          "second record decodes");
    CHECK(decoded_frame.sequence == 99u, "resynchronised onto the right frame");
}

static void test_mtu(void)
{
    printf("[TEST] MTU accounting\n");

    /*
     * Below the protocol ceiling the path is what limits the frame, so the
     * header overhead must be subtracted exactly.
     */
    CHECK(mcl_ip_max_frame_for_mtu(MCL_IP_AF_IPV4, 1000u) == 1000u - 28u,
          "IPv4 subtracts IP and UDP headers");
    CHECK(mcl_ip_max_frame_for_mtu(MCL_IP_AF_IPV6, 1000u) == 1000u - 48u,
          "IPv6 subtracts IP and UDP headers");

    /*
     * Above it the protocol is the limit. A path that could carry more does
     * not make a larger frame legal, and reporting the MTU-derived number here
     * would invite a caller to size a buffer for a frame Link would refuse to
     * encode.
     */
    CHECK(mcl_ip_max_frame_for_mtu(MCL_IP_AF_IPV4, 1500u) == MCL_LINK_FRAME_MAX_SIZE,
          "a 1500 byte path is bounded by the protocol, not the path");
    CHECK(mcl_ip_max_frame_for_mtu(MCL_IP_AF_IPV6, 9000u) == MCL_LINK_FRAME_MAX_SIZE,
          "a jumbo path does not raise the maximum frame");

    /* The exact crossover: 1048 + 28 bytes of IPv4/UDP overhead. */
    CHECK(mcl_ip_max_frame_for_mtu(MCL_IP_AF_IPV4, MCL_LINK_FRAME_MAX_SIZE + 28u)
              == MCL_LINK_FRAME_MAX_SIZE,
          "the two limits meet exactly at the crossover MTU");
    CHECK(mcl_ip_max_frame_for_mtu(MCL_IP_AF_IPV4, MCL_LINK_FRAME_MAX_SIZE + 27u)
              == MCL_LINK_FRAME_MAX_SIZE - 1u,
          "one byte below the crossover the path limits again");
    CHECK(mcl_ip_max_frame_for_mtu(MCL_IP_AF_IPV4, 20u) == 0u,
          "MTU below overhead carries nothing");
    CHECK(mcl_ip_max_frame_for_mtu(MCL_IP_AF_IPV4, 30u) == 0u,
          "MTU too small for a minimal frame carries nothing");
    CHECK(mcl_ip_max_frame_for_mtu(MCL_IP_AF_LOCAL_REF, 1500u) == 0u,
          "a local reference has no MTU accounting");
}

static void test_argument_validation(void)
{
    mcl_ip_endpoint_t ep;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u, out_size = 0u;
    const uint8_t *out_frame = NULL;

    printf("[TEST] argument validation\n");

    CHECK(mcl_ip_endpoint_encode(NULL, buf, sizeof(buf), &written)
              == MCL_IP_ERR_INVALID_ARGUMENT, "null endpoint");
    CHECK(mcl_ip_endpoint_decode(NULL, 8u, &ep, &consumed)
              == MCL_IP_ERR_INVALID_ARGUMENT, "null endpoint input");
    CHECK(mcl_ip_stream_wrap(NULL, 8u, buf, sizeof(buf), &written)
              == MCL_IP_ERR_INVALID_ARGUMENT, "null frame to wrap");
    CHECK(mcl_ip_stream_wrap(buf, 0u, buf, sizeof(buf), &written)
              == MCL_IP_ERR_RANGE, "zero-length frame not wrappable");
    CHECK(mcl_ip_stream_next(NULL, 8u, &out_frame, &out_size, &consumed)
              == MCL_IP_ERR_INVALID_ARGUMENT, "null stream input");
    CHECK(mcl_ip_endpoint_encoded_size(NULL) == 0u, "null has no size");
}

int main(void)
{
    printf("MCL-IP binding v0 tests\n");
    printf("=======================\n");

    test_endpoint_round_trip_ipv4();
    test_endpoint_round_trip_ipv6_and_localref();
    test_endpoint_rejects_inconsistent_family();
    test_endpoint_rejects_unknown_enums();
    test_endpoint_truncation();
    test_datagram_boundary_is_frame_boundary();
    test_stream_round_trip_and_resync();
    test_stream_partial_reads();
    test_stream_skips_undecodable_frame();
    test_mtu();
    test_argument_validation();

    printf("\n%d checks, %d failed\n", tests_run, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
