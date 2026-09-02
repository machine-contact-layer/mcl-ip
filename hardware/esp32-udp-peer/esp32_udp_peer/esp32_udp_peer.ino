/*
 * MCL-IP over-air peer for the DFR1154 (ESP32-S3).
 *
 * Brings up a 2.4 GHz SoftAP and answers MCL Link frames carried in UDP
 * datagrams, using the same freestanding mcl-link, mcl-wire and mcl-ip sources
 * the host uses. Nothing here reimplements the protocol; this sketch supplies
 * only the radio, the socket and the serial log.
 *
 * The point of the exercise is not that valid frames arrive. It is that
 * malformed ones are refused over a real channel exactly as the specification
 * requires, so the host sends both.
 *
 * Serial control:
 *   PING    -> MCLPONG
 *   STATUS  -> one line of AP and counter state
 *   RESET   -> zero the counters
 *
 * Every datagram produces one MCLIP line on serial, which is the evidence
 * record for the run.
 */

#include <WiFi.h>
#include <WiFiUdp.h>

extern "C" {
#include "mcl/ip_binding.h"
#include "mcl/link.h"
#include "mcl/wire.h"
}

static const char *kApSsid     = "MCL-IP-TEST";
static const char *kApPassword = "mcl-contact-0";
static const int   kApChannel  = 6;      /* 2.4 GHz only for this campaign */
static const uint16_t kUdpPort = 5555;

/* Contact reference this board puts in the frames it sends. Not an identity. */
static const uint32_t kBoardSourceRef = 0x0D1154EEu;

static WiFiUDP g_udp;

static uint32_t g_rx_total    = 0;
static uint32_t g_rx_accepted = 0;
static uint32_t g_rx_rejected = 0;
static uint32_t g_tx_total    = 0;
static uint16_t g_tx_sequence = 0;

static uint32_t g_log_dropped = 0;

static uint8_t g_rx_buf[1600];
static uint8_t g_tx_buf[MCL_LINK_FRAME_MAX_SIZE];
static uint8_t g_wire_buf[MCL_WIRE_TIER0_MAX_SIZE];

/*
 * Log only when the USB CDC transmit buffer has room.
 *
 * Serial.printf blocks once that buffer fills, which it does immediately if no
 * host is reading the port. While it blocks, arriving datagrams overflow the
 * UDP receive queue, and the run then measures the logger rather than the
 * link. An instrument that perturbs what it measures is worse than no
 * instrument, so a log line is dropped instead and counted, and STATUS reports
 * the count so a run with missing lines is never mistaken for a quiet one.
 */
#define MCL_LOG_HEADROOM 192

static bool log_ready(void)
{
    if (Serial.availableForWrite() >= MCL_LOG_HEADROOM) {
        return true;
    }
    g_log_dropped++;
    return false;
}

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

/* The board's own presence object, sent back inside an ACK. */
static void fill_presence(mcl_wire_tier0_t *obj)
{
    memset(obj, 0, sizeof(*obj));
    obj->kind = MCL_WIRE_KIND_PRESENCE;
    obj->priority = 2u;                       /* P2_NORMAL */
    obj->source_ref = kBoardSourceRef;
    obj->body.presence.machine_class = 3u;
    obj->body.presence.capability_digest = 0x17C0DEu;   /* 24-bit field */
    obj->body.presence.ttl = 30u;
}

/*
 * Reply with a frame of the given class. An ACK carries the board's presence;
 * a NACK carries nothing, because a refusal is not an occasion to assert
 * anything about ourselves.
 */
static void send_reply(const IPAddress &to, uint16_t port,
                       mcl_link_frame_class_t cls, uint32_t destination_ref)
{
    mcl_link_frame_t f;
    size_t wire_len = 0, frame_len = 0;

    memset(&f, 0, sizeof(f));
    f.frame_class = cls;
    f.flags = MCL_LINK_FLAG_DESTINATION | MCL_LINK_FLAG_SEQUENCE | MCL_LINK_FLAG_FRAME_CHECK;
    f.source_ref = kBoardSourceRef;
    f.destination_ref = destination_ref;
    f.sequence = g_tx_sequence;

    if (cls == MCL_LINK_CLASS_ACK) {
        mcl_wire_tier0_t obj;
        fill_presence(&obj);
        if (mcl_wire_tier0_encode(&obj, g_wire_buf, sizeof(g_wire_buf), &wire_len) != MCL_WIRE_OK) {
            Serial.println("MCLIP TXERR wire_encode");
            return;
        }
        f.payload = g_wire_buf;
        f.payload_len = (uint16_t)wire_len;
    } else {
        f.payload = NULL;
        f.payload_len = 0u;
    }

    if (mcl_link_frame_encode(&f, g_tx_buf, sizeof(g_tx_buf), &frame_len) != MCL_LINK_OK) {
        Serial.println("MCLIP TXERR frame_encode");
        return;
    }

    g_udp.beginPacket(to, port);
    g_udp.write(g_tx_buf, frame_len);
    if (g_udp.endPacket() == 1) {
        g_tx_sequence++;
        g_tx_total++;
    } else {
        Serial.println("MCLIP TXERR endPacket");
    }
}

static void handle_datagram(int len, const IPAddress &from, uint16_t port)
{
    mcl_ip_status_t ipst;
    mcl_link_status_t lst;
    mcl_link_frame_t frame;
    size_t consumed = 0;

    g_rx_total++;

    /*
     * Validate at the binding boundary first. On a datagram transport the
     * datagram boundary is the frame boundary, so trailing bytes are a
     * rejection and not something to skip past.
     */
    ipst = mcl_ip_datagram_validate(g_rx_buf, (size_t)len);
    if (ipst != MCL_IP_OK) {
        g_rx_rejected++;
        if (log_ready()) {
            Serial.printf("MCLIP RX n=%d REJECT ip=%s\n", len, ip_status_name(ipst));
        }
        send_reply(from, port, MCL_LINK_CLASS_NACK, 0u);
        return;
    }

    lst = mcl_link_frame_decode(g_rx_buf, (size_t)len, &frame, &consumed);
    if (lst != MCL_LINK_OK) {
        /* Unreachable while the validator agrees with the decoder; logged so a
         * future divergence between them is visible rather than silent. */
        g_rx_rejected++;
        if (log_ready()) {
            Serial.printf("MCLIP RX n=%d REJECT link=%ld\n", len, (long)lst);
        }
        send_reply(from, port, MCL_LINK_CLASS_NACK, 0u);
        return;
    }

    if (frame.frame_class == MCL_LINK_CLASS_CONTACT ||
        frame.frame_class == MCL_LINK_CLASS_DATA) {
        mcl_wire_tier0_t obj;
        size_t wire_consumed = 0;
        mcl_wire_status_t wst = mcl_wire_tier0_decode(frame.payload,
                                                      (size_t)frame.payload_len,
                                                      &obj, &wire_consumed);
        if (wst != MCL_WIRE_OK || wire_consumed != (size_t)frame.payload_len) {
            g_rx_rejected++;
            if (log_ready()) {
                Serial.printf("MCLIP RX n=%d cls=%u REJECT wire=%ld consumed=%u/%u\n",
                len, (unsigned)frame.frame_class, (long)wst,
                (unsigned)wire_consumed, (unsigned)frame.payload_len);
            }
            send_reply(from, port, MCL_LINK_CLASS_NACK, frame.source_ref);
            return;
        }
        g_rx_accepted++;
        if (log_ready()) {
            Serial.printf("MCLIP RX n=%d cls=%u src=%08lX seq=%u kind=%u ACCEPT\n",
            len, (unsigned)frame.frame_class,
            (unsigned long)frame.source_ref, (unsigned)frame.sequence,
            (unsigned)obj.kind);
        }
    } else {
        g_rx_accepted++;
        if (log_ready()) {
            Serial.printf("MCLIP RX n=%d cls=%u src=%08lX seq=%u ACCEPT nosem\n",
            len, (unsigned)frame.frame_class,
            (unsigned long)frame.source_ref, (unsigned)frame.sequence);
        }
    }

    send_reply(from, port, MCL_LINK_CLASS_ACK, frame.source_ref);
}

static void handle_serial_line(const String &line)
{
    if (line == "PING") {
        Serial.println("MCLPONG");
    } else if (line == "STATUS") {
        Serial.printf("MCLIP STATUS ssid=%s ip=%s port=%u rx=%lu ok=%lu rej=%lu tx=%lu logdrop=%lu\n",
                      kApSsid, WiFi.softAPIP().toString().c_str(), kUdpPort,
                      (unsigned long)g_rx_total, (unsigned long)g_rx_accepted,
                      (unsigned long)g_rx_rejected, (unsigned long)g_tx_total,
                      (unsigned long)g_log_dropped);
    } else if (line == "RESET") {
        g_rx_total = 0;
        g_rx_accepted = 0;
        g_rx_rejected = 0;
        g_tx_total = 0;
        g_tx_sequence = 0;
        g_log_dropped = 0;
        Serial.println("MCLIP RESET OK");
    }
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPassword, kApChannel);
    delay(200);
    g_udp.begin(kUdpPort);

    Serial.printf("MCLIP READY ssid=%s ip=%s port=%u ch=%d frame_max=%u\n",
                  kApSsid, WiFi.softAPIP().toString().c_str(), kUdpPort,
                  kApChannel, (unsigned)MCL_LINK_FRAME_MAX_SIZE);
}

void loop()
{
    /*
     * Drain every queued datagram before touching anything else. Handling one
     * per iteration lets a burst back up behind the rest of the loop, and that
     * overflow would look like packet loss on the radio.
     */
    int len;
    while ((len = g_udp.parsePacket()) > 0) {
        IPAddress from = g_udp.remoteIP();
        uint16_t port = g_udp.remotePort();
        int n = g_udp.read(g_rx_buf, sizeof(g_rx_buf));
        if (n > 0) {
            handle_datagram(n, from, port);
        }
    }

    while (Serial.available() > 0) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            handle_serial_line(line);
        }
    }
}
