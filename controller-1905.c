/* 1905 CMDU decoding for the fields the topology page shows.
 *
 * Everything here is a pure function of one Ethernet frame: the daemon's receive hook, a host test and a
 * future offline replay of a pcap all get the same answer from the same bytes. No sockets, no vendor
 * library, no logging - the caller decides what to do with a value.
 */
#include <string.h>
#include "controller-1905.h"

#define ZTE_OUI_BYTES 3

static unsigned int be16(const uint8_t *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

int cmdu_decode(const uint8_t *frame, size_t bytes, struct cmdu_header *header)
{
    if (!frame || !header || bytes < CONTROLLER_1905_HEADER || bytes > CONTROLLER_1905_MAX_BYTES)
        return 0;
    if (be16(frame + 12) != CONTROLLER_1905_ETHER || frame[14] || frame[15])
        return 0;
    header->type = be16(frame + 16);
    header->message_id = be16(frame + 18);
    header->fragment = frame[20];
    header->last_fragment = !!(frame[21] & 0x80);
    header->relayed = !!(frame[21] & 0x40);
    return 1;
}

void tlv_open(struct tlv_cursor *cursor, const uint8_t *frame, size_t bytes)
{
    cursor->frame = frame;
    cursor->bytes = bytes;
    cursor->at = CONTROLLER_1905_HEADER;
    cursor->done = 0;
}

int tlv_next(struct tlv_cursor *cursor, unsigned int *type, const uint8_t **value, size_t *length)
{
    unsigned int t;
    size_t len;
    while (!cursor->done && cursor->frame && cursor->at + 3 <= cursor->bytes) {
        t = cursor->frame[cursor->at];
        len = be16(cursor->frame + cursor->at + 1);
        if (cursor->at + 3 + len > cursor->bytes) {
            cursor->done = 1;
            break;
        }
        cursor->at += 3;
        if (!t) {
            /* End of Message. Ethernet pads short frames after it; nothing after this is a TLV. */
            cursor->done = 1;
            break;
        }
        *type = t;
        *value = cursor->frame + cursor->at;
        *length = len;
        cursor->at += len;
        return 1;
    }
    cursor->done = 1;
    return 0;
}

int zte_vendor_open(struct zte_cursor *cursor, const uint8_t *value, size_t length)
{
    static const uint8_t oui[ZTE_OUI_BYTES] = {0x00, 0x19, 0xc6};
    if (!cursor || !value || length < 6 + 3 || memcmp(value, oui, ZTE_OUI_BYTES))
        return 0;
    cursor->value = value;
    cursor->bytes = length;
    cursor->at = 6;
    cursor->valid = 1;
    return 1;
}

int zte_vendor_next(struct zte_cursor *cursor, unsigned int *type, const uint8_t **value, size_t *length)
{
    unsigned int t;
    size_t len;
    if (!cursor || !cursor->valid)
        return 0;
    while (cursor->at + 3 <= cursor->bytes) {
        t = cursor->value[cursor->at];
        len = be16(cursor->value + cursor->at + 1);
        if (cursor->at + 3 + len > cursor->bytes) {
            cursor->valid = 0;
            break;
        }
        cursor->at += 3;
        if (!t) {
            cursor->valid = 0;
            break;
        }
        *type = t;
        *value = cursor->value + cursor->at;
        *length = len;
        cursor->at += len;
        return 1;
    }
    cursor->valid = 0;
    return 0;
}

/* AP Operational BSS (0x83): radio count, then per radio {radio identifier[6], interface count}, then per
 * interface {BSSID[6], SSID length, SSID}. Returns how many BSS interfaces fit; a frame that stops in the
 * middle of a radio yields the interfaces before it instead of a guess. */
static unsigned int ap_bss_interfaces(const uint8_t *value, size_t length)
{
    unsigned int radios, total = 0, r;
    size_t at = 0;
    if (length < 1)
        return 0;
    radios = value[at++];
    for (r = 0; r < radios; r++) {
        unsigned int interfaces, i;
        if (length - at < 7)
            return total;
        at += 6;
        interfaces = value[at++];
        for (i = 0; i < interfaces; i++) {
            size_t ssid;
            if (length - at < 7)
                return total;
            at += 6;
            ssid = value[at++];
            if (ssid > length - at)
                return total;
            at += ssid;
            total++;
        }
    }
    return total;
}

int node_metrics_decode(const uint8_t *frame, size_t bytes, struct node_metrics *metrics)
{
    struct cmdu_header header;
    struct tlv_cursor cursor;
    unsigned int type;
    const uint8_t *value;
    size_t length;
    if (!metrics)
        return 0;
    memset(metrics, 0, sizeof(*metrics));
    metrics->hops = -1;
    if (!cmdu_decode(frame, bytes, &header))
        return 0;
    /* The relay bit is in the 1905 header, so it is known for every frame: 1 = the sender is a direct
     * neighbor of this router, 2 = a neighbor forwarded it and the sender sits one level further out. */
    metrics->hops = header.relayed ? 2 : 1;
    metrics->fields = NODE_METRIC_HOPS;
    tlv_open(&cursor, frame, bytes);
    while (tlv_next(&cursor, &type, &value, &length)) {
        if (type == CONTROLLER_TLV_AP_METRICS && length >= 9) {
            /* BSSID[6], channel utilisation, u16 station count. One BSS per TLV is what the factory
             * daemon sends; optional estimated-service-parameter bytes may follow, so the fixed nine are
             * the only ones read. Several TLVs in one frame are summed. */
            metrics->stations += be16(value + 7);
            metrics->fields |= NODE_METRIC_STATIONS;
        } else if (type == CONTROLLER_TLV_AP_OPERATIONAL_BSS) {
            unsigned int count = ap_bss_interfaces(value, length);
            if (count) {
                metrics->bss += count;
                metrics->fields |= NODE_METRIC_BSS;
            }
        }
    }
    return 1;
}
