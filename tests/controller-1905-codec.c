/* Host tests for the 1905 decoder: the layouts it implements were checked against the Wireshark ieee1905
 * dissector (tshark 3.6.2) on constructed frames, and the vendor container against the 61 vendor TLVs of a
 * 107-frame capture from the lab. The fixture in captured_tests() is one of those frames verbatim, so a change
 * that only satisfies the constructed ones still has to survive real bytes.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../controller-1905.h"

static unsigned int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "1905 codec failure line %d\n", __LINE__); exit(1); } } while (0)

static const uint8_t agent[6] = {2, 0, 0, 0, 0x19, 0xe3};
static const uint8_t controller[6] = {2, 0, 0, 0, 0x19, 0x03};

/* One captured frame: CMDU 0x0006 Link Metric Response from 02:00:00:00:19:e3, message id 0xcffe, 260 bytes.
 * Its Vendor Specific TLV (0x0b, 232 bytes) holds two 0x49 sub-TLVs of 110 bytes each; that shape is what the
 * vendor walk implements. */
static const char *captured_hex =
    "0200000019030200000019e3893a00000006cffe00800b00e80019c602000949"
    "006e0200000019e3000000000000000000000000000000000000000000000000"
    "0000000000000000000400000000080000000000000000080000000000000000"
    "0800000000000000000800000000000000000200000400000000040000000004"
    "0000000004000000000400000000010049006e02000000190300000000000000"
    "0000000000000000000000000000000000000000000000000000040000000008"
    "0000000000000000080000000000000000080000000000000000080000000000"
    "0000000200000400000000040000000004000000000400000000040000000001"
    "00000000";

static size_t unhex(const char *text, uint8_t *out, size_t room)
{
    size_t i, n = 0;
    for (i = 0; text[i] && text[i + 1] && n < room; i += 2) {
        unsigned int byte;
        if (sscanf(text + i, "%2x", &byte) != 1)
            break;
        out[n++] = (uint8_t)byte;
    }
    return n;
}

/* Ethernet + CMDU header; callers append the TLVs. */
static size_t frame_open(uint8_t *frame, unsigned int type, unsigned int mid, uint8_t flags)
{
    memset(frame, 0, CONTROLLER_1905_MAX_BYTES);
    memcpy(frame, controller, 6);
    memcpy(frame + 6, agent, 6);
    frame[12] = 0x89;
    frame[13] = 0x3a;
    frame[16] = (uint8_t)(type >> 8);
    frame[17] = (uint8_t)type;
    frame[18] = (uint8_t)(mid >> 8);
    frame[19] = (uint8_t)mid;
    frame[21] = flags;
    return CONTROLLER_1905_HEADER;
}

static size_t put_header(uint8_t *frame, size_t at, unsigned int type, size_t length)
{
    frame[at] = (uint8_t)type;
    frame[at + 1] = (uint8_t)(length >> 8);
    frame[at + 2] = (uint8_t)length;
    return at + 3;
}

static size_t put_tlv(uint8_t *frame, size_t at, unsigned int type, const uint8_t *value, size_t length)
{
    at = put_header(frame, at, type, length);
    if (value && length)
        memcpy(frame + at, value, length);
    return at + length;
}

static size_t put_eom(uint8_t *frame, size_t at)
{
    return put_header(frame, at, 0, 0);
}

/* AP Operational BSS payload as the dissector lays it out: radio count, then per radio
 * {radio identifier[6], interface count}, then per interface {BSSID[6], SSID length, SSID}. Radios and
 * interfaces carry one-byte identifiers so a truncated copy is still a valid prefix. */
static size_t build_ap_bss(uint8_t *out, unsigned int radios, unsigned int interfaces, const uint8_t *bssid)
{
    size_t at = 0;
    unsigned int r, i;
    out[at++] = (uint8_t)radios;
    for (r = 0; r < radios; r++) {
        memcpy(out + at, "\x02MAP24", 6);
        at += 6;
        out[at++] = (uint8_t)interfaces;
        for (i = 0; i < interfaces; i++) {
            memcpy(out + at, bssid, 6);
            at += 6;
            out[at++] = 3;
            memcpy(out + at, "abc", 3);
            at += 3;
        }
    }
    return at;
}

static void header_tests(void)
{
    struct cmdu_header header;
    struct node_metrics metrics;
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    size_t length = frame_open(frame, 3, 0x1202, 0x80);

    CHECK(cmdu_decode(frame, length, &header));
    CHECK(header.type == 3 && header.message_id == 0x1202 && header.fragment == 0);
    CHECK(header.last_fragment == 1 && header.relayed == 0);
    CHECK(node_metrics_decode(frame, length, &metrics));
    CHECK(metrics.hops == 1 && metrics.fields == NODE_METRIC_HOPS);

    /* A relayed frame is one level further out, and that is all the header can say about depth. */
    frame[21] = 0xc0;
    CHECK(cmdu_decode(frame, length, &header) && header.relayed == 1 && header.last_fragment == 1);
    CHECK(node_metrics_decode(frame, length, &metrics) && metrics.hops == 2 && metrics.fields == NODE_METRIC_HOPS);
    frame[21] = 0x80;

    /* Every reason to reject a frame, one at a time. */
    CHECK(!cmdu_decode(NULL, length, &header));
    CHECK(!cmdu_decode(frame, length, NULL));
    CHECK(!cmdu_decode(frame, CONTROLLER_1905_HEADER - 1, &header));
    CHECK(!cmdu_decode(frame, CONTROLLER_1905_MAX_BYTES + 1, &header));
    frame[12] = 0x88; CHECK(!cmdu_decode(frame, length, &header)); frame[12] = 0x89;
    frame[13] = 0x3b; CHECK(!cmdu_decode(frame, length, &header)); frame[13] = 0x3a;
    frame[14] = 1; CHECK(!cmdu_decode(frame, length, &header)); frame[14] = 0;
    frame[15] = 1; CHECK(!cmdu_decode(frame, length, &header)); frame[15] = 0;
    CHECK(!node_metrics_decode(frame, 0, &metrics));
    CHECK(metrics.hops == -1 && metrics.fields == 0); /* a rejected frame leaves "unknown", not zero */
    CHECK(!node_metrics_decode(frame, length, NULL));
}

static void tlv_walk_tests(void)
{
    struct tlv_cursor cursor;
    unsigned int type;
    const uint8_t *value;
    size_t length, at;
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    static const uint8_t payload[4] = {1, 2, 3, 4};

    at = frame_open(frame, 3, 1, 0x80);
    at = put_tlv(frame, at, CONTROLLER_TLV_AL_MAC, agent, 6);
    at = put_tlv(frame, at, 0x55, payload, 0);
    at = put_eom(frame, at);
    tlv_open(&cursor, frame, at);
    CHECK(tlv_next(&cursor, &type, &value, &length) && type == CONTROLLER_TLV_AL_MAC && length == 6 && !memcmp(value, agent, 6));
    CHECK(tlv_next(&cursor, &type, &value, &length) && type == 0x55 && length == 0);
    CHECK(!tlv_next(&cursor, &type, &value, &length)); /* End of Message ends the walk */
    CHECK(!tlv_next(&cursor, &type, &value, &length)); /* and stays ended */

    /* A TLV whose length runs past the frame yields everything before it and nothing after: padding after the
     * EOM is normal (Ethernet minimum frame size), a lying length is not. */
    at = frame_open(frame, 3, 1, 0x80);
    at = put_tlv(frame, at, CONTROLLER_TLV_AL_MAC, agent, 6);
    put_header(frame, at, CONTROLLER_TLV_AP_METRICS, 0xffff);
    tlv_open(&cursor, frame, at + 3);
    CHECK(tlv_next(&cursor, &type, &value, &length) && type == CONTROLLER_TLV_AL_MAC);
    CHECK(!tlv_next(&cursor, &type, &value, &length));

    /* Nothing at all after the header. */
    at = frame_open(frame, 3, 1, 0x80);
    tlv_open(&cursor, frame, at);
    CHECK(!tlv_next(&cursor, &type, &value, &length));
}

static void metrics_tests(void)
{
    struct node_metrics metrics;
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    static const uint8_t bssid_a[6] = {2, 0, 0, 0, 0x19, 0xe4};
    static const uint8_t bssid_b[6] = {2, 0, 0, 0, 0x19, 0xe6};
    uint8_t ap_metrics[9], bss[96];
    size_t at, bss_length, declared;

    /* Two AP Metrics TLVs in one frame are summed: 3 + 2 stations. */
    memset(ap_metrics, 0, sizeof(ap_metrics));
    memcpy(ap_metrics, bssid_a, 6);
    ap_metrics[6] = 40; /* channel utilisation */
    ap_metrics[8] = 3;  /* big-endian u16 station count */
    at = frame_open(frame, 0x800c, 7, 0x80);
    at = put_tlv(frame, at, CONTROLLER_TLV_AP_METRICS, ap_metrics, 9);
    memcpy(ap_metrics, bssid_b, 6);
    ap_metrics[8] = 2;
    at = put_tlv(frame, at, CONTROLLER_TLV_AP_METRICS, ap_metrics, 9);
    at = put_eom(frame, at);
    CHECK(node_metrics_decode(frame, at, &metrics));
    CHECK(metrics.stations == 5 && (metrics.fields & NODE_METRIC_STATIONS));
    CHECK(metrics.hops == 1 && !(metrics.fields & NODE_METRIC_BSS));

    /* Eight bytes is not an AP Metrics TLV (BSSID, utilisation and the u16 count are nine). */
    at = frame_open(frame, 0x800c, 7, 0x80);
    at = put_tlv(frame, at, CONTROLLER_TLV_AP_METRICS, ap_metrics, 8);
    at = put_eom(frame, at);
    CHECK(node_metrics_decode(frame, at, &metrics) && !(metrics.fields & NODE_METRIC_STATIONS));

    /* Two radios, one interface on the first and two on the second: three BSS. */
    bss_length = build_ap_bss(bss, 2, 1, bssid_a);
    at = frame_open(frame, 3, 9, 0x80);
    at = put_tlv(frame, at, CONTROLLER_TLV_AP_OPERATIONAL_BSS, bss, bss_length);
    at = put_eom(frame, at);
    CHECK(node_metrics_decode(frame, at, &metrics));
    CHECK(metrics.bss == 2 && (metrics.fields & NODE_METRIC_BSS));
    CHECK(metrics.stations == 0 && !(metrics.fields & NODE_METRIC_STATIONS));
    CHECK(metrics.hops == 1);

    bss_length = build_ap_bss(bss, 1, 3, bssid_b);
    at = frame_open(frame, 3, 9, 0x80);
    at = put_tlv(frame, at, CONTROLLER_TLV_AP_OPERATIONAL_BSS, bss, bss_length);
    at = put_eom(frame, at);
    CHECK(node_metrics_decode(frame, at, &metrics) && metrics.bss == 3);

    /* The same TLV cut short: the interfaces that no longer fit are not counted, and nothing past the frame is
     * read. 14 bytes is one complete interface, 20 is one and a half. */
    for (declared = 1; declared <= 20; declared++) {
        at = frame_open(frame, 3, 9, 0x80);
        at = put_header(frame, at, CONTROLLER_TLV_AP_OPERATIONAL_BSS, declared);
        memcpy(frame + at, bss, declared);
        at += declared;
        at = put_eom(frame, at);
        CHECK(node_metrics_decode(frame, at, &metrics));
        CHECK(metrics.bss <= 1);
    }

    /* A radio count far past the payload must not walk off it. */
    memset(bss, 0, sizeof(bss));
    bss[0] = 200;
    at = frame_open(frame, 3, 9, 0x80);
    at = put_tlv(frame, at, CONTROLLER_TLV_AP_OPERATIONAL_BSS, bss, 12);
    at = put_eom(frame, at);
    CHECK(node_metrics_decode(frame, at, &metrics) && metrics.bss == 0);
}

static void vendor_tests(void)
{
    struct zte_cursor cursor;
    unsigned int type;
    const uint8_t *value;
    size_t length, at, payload;
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    uint8_t vendor[32];

    /* OUI 00:19:c6, three header bytes, two sub-TLVs {type[1], length[2], value}. */
    memset(vendor, 0, sizeof(vendor));
    vendor[0] = 0x00; vendor[1] = 0x19; vendor[2] = 0xc6;
    vendor[3] = 0x02; vendor[4] = 0x00; vendor[5] = 0x09;
    vendor[6] = 0x21; vendor[7] = 0x00; vendor[8] = 0x07;
    memcpy(vendor + 9, agent, 6);
    vendor[15] = 0x01;
    vendor[16] = 0x40; vendor[17] = 0x00; vendor[18] = 0x02;
    vendor[19] = 0xaa; vendor[20] = 0xbb;

    at = frame_open(frame, 0x000e, 3, 0x80);
    at = put_header(frame, at, CONTROLLER_TLV_VENDOR, 21);
    payload = at;
    memcpy(frame + payload, vendor, 21);
    at = payload + 21;
    at = put_eom(frame, at);

    CHECK(zte_vendor_open(&cursor, frame + payload, 21));
    CHECK(zte_vendor_next(&cursor, &type, &value, &length) && type == 0x21 && length == 7 && !memcmp(value, agent, 6));
    CHECK(zte_vendor_next(&cursor, &type, &value, &length) && type == 0x40 && length == 2 && value[1] == 0xbb);
    CHECK(!zte_vendor_next(&cursor, &type, &value, &length));
    CHECK(!zte_vendor_next(&cursor, &type, &value, &length)); /* stays ended */

    /* Another vendor's OUI is not parsed as ZTE's; a sub-TLV that overruns ends the walk. The bytes under test
     * are the ones in the frame, so they are patched there rather than in the template. */
    frame[payload + 2] = 0xc7;
    CHECK(!zte_vendor_open(&cursor, frame + payload, 21));
    frame[payload + 2] = 0xc6;
    frame[payload + 7] = 0x00;
    frame[payload + 8] = 0x40; /* claims 64 bytes, 12 remain */
    CHECK(zte_vendor_open(&cursor, frame + payload, 21));
    CHECK(!zte_vendor_next(&cursor, &type, &value, &length));
    CHECK(!zte_vendor_open(&cursor, NULL, 21));
    CHECK(!zte_vendor_open(&cursor, frame + payload, 8)); /* shorter than OUI + header */
}

static void captured_tests(void)
{
    static uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    struct cmdu_header header;
    struct node_metrics metrics;
    struct tlv_cursor cursor;
    struct zte_cursor zte;
    unsigned int type, entries = 0;
    const uint8_t *value;
    size_t length, bytes = unhex(captured_hex, frame, sizeof(frame));

    CHECK(bytes == 260);
    CHECK(cmdu_decode(frame, bytes, &header));
    CHECK(header.type == 6 && header.message_id == 0xcffe);
    CHECK(header.last_fragment == 1 && header.relayed == 0);
    CHECK(!memcmp(frame + 6, agent, 6));

    /* A Link Metric Response carries no station or BSS counts: only the hop count is known, and an unknown
     * count must stay "not reported" instead of becoming zero stations. */
    CHECK(node_metrics_decode(frame, bytes, &metrics));
    CHECK(metrics.fields == NODE_METRIC_HOPS && metrics.hops == 1);
    CHECK(metrics.stations == 0 && metrics.bss == 0);

    /* The captured vendor TLV: OUI, three header bytes, two 0x49 sub-TLVs of 110 bytes each, consuming the
     * 232-byte payload exactly. Entry 0 describes the sender, entry 1 the router it answers. */
    tlv_open(&cursor, frame, bytes);
    CHECK(tlv_next(&cursor, &type, &value, &length) && type == CONTROLLER_TLV_VENDOR && length == 232);
    CHECK(zte_vendor_open(&zte, value, length));
    while (zte_vendor_next(&zte, &type, &value, &length)) {
        CHECK(type == 0x49 && length == 110);
        CHECK(!memcmp(value, entries ? controller : agent, 6));
        entries++;
    }
    CHECK(entries == 2);
    CHECK(!tlv_next(&cursor, &type, &value, &length)); /* the capture ends with EOM and Ethernet padding */
}

int main(void)
{
    static uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    uint32_t seed = 0x19051203;
    unsigned int i;

    header_tests();
    tlv_walk_tests();
    metrics_tests();
    vendor_tests();
    captured_tests();

    /* Deterministic bounded mutations: take the captured frame, flip one bit and cut it at a random length.
     * The eight guard bytes after it must survive, which is what catches a decoder that reads past the end. */
    for (i = 0; i < 100000; i++) {
        struct node_metrics metrics;
        size_t bytes = unhex(captured_hex, frame, sizeof(frame) - 8), cut;
        memset(frame + bytes, 0xa5, 8);
        seed = seed * 1664525u + 1013904223u;
        frame[seed % bytes] ^= (uint8_t)(1u << ((seed >> 24) & 7));
        seed = seed * 1664525u + 1013904223u;
        cut = seed % (bytes + 1);
        if (node_metrics_decode(frame, cut, &metrics) && metrics.hops != 1 && metrics.hops != 2)
            return 1;
        if (frame[bytes] != 0xa5)
            return 1;
    }
    printf("controller-1905-codec: %u assertions; 100000 bounded mutations passed\n", checks);
    return 0;
}
