#ifndef ZTE_CONTROLLER_1905_H
#define ZTE_CONTROLLER_1905_H
#include <stddef.h>
#include <stdint.h>

/* What one received 1905 frame says about the node that sent it.
 *
 * Pure decoding: no socket, no vendor library, no logging. The daemon's receive hook calls it on the bytes it
 * already has, and a host test calls it on constructed frames, so the layout below is the only thing that has
 * to be right. The TLV numbers and field offsets were checked against the Wireshark ieee1905 dissector
 * (tshark 3.6.2) on constructed frames, and the vendor sub-TLV container against every vendor TLV of a
 * 107-frame capture from the lab; see docs/research/CAPTURE_1905.md. */

/* IEEE 1905.1a: Ethernet type, and the CMDU header that follows the 14-byte Ethernet header. */
#define CONTROLLER_1905_ETHER 0x893a
#define CONTROLLER_1905_HEADER 22
#define CONTROLLER_1905_MAX_BYTES 2000

/* TLV types used here (the dissector's table, decimal -> value). The rest is skipped. */
#define CONTROLLER_TLV_AL_MAC 0x01
#define CONTROLLER_TLV_VENDOR 0x0b
#define CONTROLLER_TLV_AP_OPERATIONAL_BSS 0x83
#define CONTROLLER_TLV_AP_METRICS 0x94

struct cmdu_header {
    unsigned int type;      /* CMDU message type, e.g. 0x0003 topology response, 0x800c AP metrics response */
    unsigned int message_id;
    unsigned int fragment;
    int last_fragment;      /* header flags bit 7 */
    int relayed;            /* header flags bit 6: a neighbor forwarded the frame, so its sender is not a direct link */
};

/* 1 = a complete 1905 CMDU starting at the Ethernet header, 0 = anything else (wrong ethertype, version,
 * truncated, oversized). Never reads past `bytes`. */
int cmdu_decode(const uint8_t *frame, size_t bytes, struct cmdu_header *header);

struct tlv_cursor { const uint8_t *frame; size_t bytes, at; int done; };
/* Walks the TLV list of a CMDU from the end of its header. End of Message (type 0) stops the walk and is not
 * returned; a TLV whose length runs past the frame stops it too, so a truncated or forged frame yields the
 * TLVs before the damage and nothing after. */
void tlv_open(struct tlv_cursor *cursor, const uint8_t *frame, size_t bytes);
int tlv_next(struct tlv_cursor *cursor, unsigned int *type, const uint8_t **value, size_t *length);

struct zte_cursor { const uint8_t *value; size_t bytes, at; int valid; };
/* ZTE's Vendor Specific TLV (0x0b): OUI 00:19:c6, three more header bytes, then a list of
 * [type(1) length(2) value] sub-TLVs. Confirmed on the lab capture: the sub-TLV walk consumes the payload
 * exactly in all 61 vendor TLVs of 107 frames. 1 = recognized, 0 = another vendor's TLV. */
int zte_vendor_open(struct zte_cursor *cursor, const uint8_t *value, size_t length);
int zte_vendor_next(struct zte_cursor *cursor, unsigned int *type, const uint8_t **value, size_t *length);

/* Bit per field node_metrics_decode() actually found; the others stay at their "unknown" value. */
#define NODE_METRIC_STATIONS 1u
#define NODE_METRIC_BSS 2u
#define NODE_METRIC_HOPS 4u

struct node_metrics {
    unsigned int stations;  /* AP Metrics TLV: associated stations summed over every BSS entry in the frame */
    unsigned int bss;       /* AP Operational BSS TLV: BSS interfaces the node reports as up */
    int hops;               /* 1 = the frame arrived over the direct link, 2 = a neighbor relayed it */
    unsigned int fields;
};

/* 1 = the frame is a 1905 CMDU (metrics then hold at least hops), 0 = not a CMDU. Unknown fields keep the
 * value -1 for hops and 0 with their bit clear for the counts, so a caller can tell "no data" from "zero". */
int node_metrics_decode(const uint8_t *frame, size_t bytes, struct node_metrics *metrics);

#endif
