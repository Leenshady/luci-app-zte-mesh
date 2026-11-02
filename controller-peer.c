#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "controller-1905.h"
#include "controller-peer.h"
#include "controller-steer.h"

#define PEERS_KNOWN 32

static const char *peers_dir(void)
{
    const char *dir = getenv("ZTE_MESH_PEERS_DIR");
    return dir && *dir ? dir : "/run/zte-mesh/peers";
}

/* One file per (MAC, kind), through a temporary name in the same directory and then renamed, so the service never
 * reads a half value. 1 = the file is in place. */
static int write_peer(const uint8_t *mac, const char *kind, const char *text, size_t bytes)
{
    char path[256], temp[264];
    int fd;
    if (snprintf(path, sizeof(path), "%s/%02x:%02x:%02x:%02x:%02x:%02x.%s", peers_dir(),
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], kind) >= (int)sizeof(path) ||
        snprintf(temp, sizeof(temp), "%s.t", path) >= (int)sizeof(temp))
        return 0;
    fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0)
        return 0;
    {
        ssize_t written = write(fd, text, bytes);
        int closed = close(fd);
        if (written == (ssize_t)bytes && !closed && !rename(temp, path))
            return 1;
    }
    (void)unlink(temp);
    return 0;
}

/* Is this MAC one of the PEERS_KNOWN the daemon will record? 1 = admitted (a known MAC, or a new one with room
 * left), 0 = the budget is full and this frame is from a stranger. */
static int admit(const uint8_t *mac)
{
    static uint8_t known[PEERS_KNOWN][6];
    static unsigned int count;
    unsigned int k;
    for (k = 0; k < count; k++)
        if (!memcmp(known[k], mac, 6))
            return 1;
    if (count >= PEERS_KNOWN)
        return 0;
    memcpy(known[count++], mac, 6);
    return 1;
}

int controller_peer_note(const uint8_t *frame, size_t bytes)
{
    struct cmdu_header header;
    struct tlv_cursor cursor;
    struct node_metrics metrics;
    const uint8_t *value, *mac = frame + 6;
    unsigned int type;
    size_t length, used = 0;
    int light = -1, written = 0;
    char text[72];
    char software[CONTROLLER_INVENTORY_TEXT + 1];
    int n;

    if (bytes < CONTROLLER_1905_HEADER || !cmdu_decode(frame, bytes, &header) ||
        !header.last_fragment || header.fragment)
        return 0;

    /* The answers to a client action: a BTM report (0x8015) says whether the client accepted, and a Steering
     * Completed (0x8017) says it left. Both name the client, which is what the action table keys on, so the file is
     * written for the client's MAC and not for the agent that sent the frame. */
    if (header.type == STEER_MSG_BTM_REPORT) {
        struct steer_btm_report report;
        if (!steer_decode_btm_report(frame, bytes, &report))
            return 0;
        if (!admit(report.client))
            return 0;
        n = snprintf(text, sizeof(text), "ok=%d\n", report.status == 0 ? 1 : 0);
        return n > 0 && write_peer(report.client, "btm", text, (size_t)n);
    }
    if (header.type == STEER_MSG_COMPLETED) {
        struct steer_completed done;
        static const char accepted[] = "ok=1\n";
        if (!steer_decode_completed(frame, bytes, &done) || !admit(done.client))
            return 0;
        return write_peer(done.client, "btm", accepted, sizeof(accepted) - 1);
    }

    software[0] = 0;
    tlv_open(&cursor, frame, bytes);
    while (tlv_next(&cursor, &type, &value, &length)) {
        struct zte_cursor zte;
        /* The version only means something inside an AP Capability Report: the factory daemon puts this TLV in
         * that message and reads it back from it, and the number is its own, not the standard's. */
        if (type == CONTROLLER_TLV_DEVICE_INVENTORY) {
            if (header.type == CONTROLLER_MSG_AP_CAPABILITY_REPORT)
                device_inventory_software(value, length, software, sizeof(software));
            continue;
        }
        /* Only a HigherLayerResponse carries the light: the same sub-TLV number inside another message type is a
         * different field, and the master reads it from the response too. */
        if (header.type != 0x000e || type != CONTROLLER_TLV_VENDOR || !zte_vendor_open(&zte, value, length))
            continue;
        while (zte_vendor_next(&zte, &type, &value, &length))
            if (type == 0x21 && length >= 7) {
                light = value[6] ? 1 : 0;
                mac = value; /* the AL MAC the child reports for itself */
            }
    }
    if (!node_metrics_decode(frame, bytes, &metrics))
        return 0;
    /* Only frames that describe the link or its load are worth a file: the daemon's status chatter (CMDU 0x0004 on
     * the lab link, ~2/s) says nothing the page shows, and rewriting a file for it would churn the tmpfs for no
     * gain. */
    if (!(metrics.fields & (NODE_METRIC_STATIONS | NODE_METRIC_BSS)) &&
        header.type != 0x0003 && header.type != 0x0006 && header.type != 0x000e && light < 0 && !software[0])
        return 0;
    if (!admit(mac))
        return 0;

    /* A version is not a number, and it does not go stale the way a station count does, so it gets its own file
     * with the text in it instead of a key in the metrics digest. */
    if (software[0]) {
        n = snprintf(text, sizeof(text), "%s\n", software);
        if (n > 0 && (size_t)n < sizeof(text))
            written += write_peer(mac, "software", text, (size_t)n);
    }
    if (light >= 0) {
        n = snprintf(text, sizeof(text), "light=%d\n", light);
        if (n > 0)
            written += write_peer(mac, "light", text, (size_t)n);
    }
    /* A count that the frame did not carry is left out, so the reader sees "no data" instead of a zero that looks
     * measured. */
    if (metrics.fields & NODE_METRIC_STATIONS)
        used += (size_t)snprintf(text + used, sizeof(text) - used, "stations=%u\n", metrics.stations);
    if (metrics.fields & NODE_METRIC_BSS)
        used += (size_t)snprintf(text + used, sizeof(text) - used, "bss=%u\n", metrics.bss);
    if (metrics.hops > 0)
        used += (size_t)snprintf(text + used, sizeof(text) - used, "hops=%d\n", metrics.hops);
    if (used)
        written += write_peer(mac, "metrics", text, used);
    return written;
}
