/* Bounded PUBLIC 1905 fixtures and header-only peer validation in private netns.
 * No host link, credential/config TLV logging, or synthetic daemon responses.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "controller-packet.h"

static const uint8_t peer[6] = {2, 0, 0, 0, 0x19, 0x0a};
static unsigned int be16(const uint8_t *p) { return ((unsigned int)p[0] << 8) | p[1]; }
int controller_topology_response_matches(const uint8_t *frame, size_t bytes, const uint8_t controller[6])
{
    size_t at = 22, n;
    unsigned int device_info = 0;
    if (!frame || !controller || bytes < 25 || bytes > 1600 ||
        memcmp(frame, peer, 6) || memcmp(frame + 6, controller, 6) ||
        be16(frame + 12) != 0x893a || frame[14] || frame[15] ||
        be16(frame + 16) != 3 || be16(frame + 18) != 0x1202 ||
        frame[20] || frame[21] != 0x80)
        return 0;
    while (bytes - at >= 3) {
        unsigned int type = frame[at];
        n = be16(frame + at + 1); at += 3;
        if (n > bytes - at) return 0;
        if (!type) {
            if (n || device_info != 1) return 0;
            /* Ethernet padding may follow the EOM, but not another message. */
            for (; at < bytes; at++) if (frame[at]) return 0;
            return 1;
        }
        if (type == 3) {
            size_t p = 7;
            unsigned int i;
            if (++device_info != 1 || n < 7 || memcmp(frame + at, controller, 6)) return 0;
            for (i = 0; i < frame[at + 6]; i++) {
                unsigned int media_bytes;
                if (n - p < 9) return 0;
                media_bytes = frame[at + p + 8]; p += 9;
                if (media_bytes > n - p) return 0;
                p += media_bytes;
            }
            if (p != n) return 0;
        }
        at += n;
    }
    return 0;
}
int controller_autoconfig_response_matches(const uint8_t *frame, size_t bytes, const uint8_t controller[6], unsigned int band)
{
    size_t at = 22, n;
    unsigned int role = 0, frequency = 0, service = 0;
    if (!frame || !controller || band > 1 || bytes < 25 || bytes > 1600 ||
        memcmp(frame, peer, 6) || memcmp(frame + 6, controller, 6) ||
        be16(frame + 12) != 0x893a || frame[14] || frame[15] ||
        be16(frame + 16) != 8 || be16(frame + 18) != 0x1203 + band ||
        frame[20] || frame[21] != 0x80) return 0;
    while (bytes - at >= 3) {
        unsigned int type = frame[at];
        n = be16(frame + at + 1); at += 3;
        if (n > bytes - at) return 0;
        if (!type) {
            if (n || role != 1 || frequency != 1 || service != 1) return 0;
            for (; at < bytes; at++) if (frame[at]) return 0;
            return 1;
        }
        if (type == 0x0f && (n != 1 || frame[at] || ++role != 1)) return 0;
        if (type == 0x10 && (n != 1 || frame[at] != band || ++frequency != 1)) return 0;
        if (type == 0x80) {
            size_t i;
            if (!n || frame[at] != n - 1) return 0;
            for (i = 1; i < n; i++) if (!frame[at + i]) service++;
        }
        at += n;
    }
    return 0;
}
static int wait_response(int fd, const uint8_t controller[6], int query)
{
    struct timespec start, now;
    struct pollfd ready = { .fd = fd, .events = POLLIN };
    uint8_t frame[1601];
    unsigned int attempts;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) return -1;
    for (attempts = 0; attempts < 512; attempts++) {
        long elapsed;
        ssize_t bytes;
        int status;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
        elapsed = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed >= 4000) break;
        status = poll(&ready, 1, (int)(4000 - elapsed));
        if (status < 0 && errno == EINTR) continue;
        if (status <= 0 || !(ready.revents & POLLIN)) break;
        bytes = recv(fd, frame, sizeof(frame), MSG_DONTWAIT);
        if (bytes < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (bytes < 0) break;
        if ((query == 1 && controller_topology_response_matches(frame, (size_t)bytes, controller)) ||
            (query >= 2 && controller_autoconfig_response_matches(frame, (size_t)bytes, controller, (unsigned int)query - 2))) {
            if (query == 1)
                printf("ZTE_CONTROLLER_PACKET fixture=topology-response private_only=1 peer_received=1 cmdu=3 mid_match=1 tlv_sane=1 device_info=1 bytes=%u body_logged=0\n", (unsigned int)bytes);
            else
                printf("ZTE_CONTROLLER_PACKET fixture=autoconfig-response private_only=1 peer_received=1 cmdu=8 band=%u mid_match=1 controller_service=1 bytes=%u body_logged=0\n", (unsigned int)query - 2, (unsigned int)bytes);
            memset(frame, 0, sizeof(frame));
            return 0;
        }
    }
    memset(frame, 0, sizeof(frame));
    errno = ETIMEDOUT; return -1;
}
int controller_inject_packet(int query)
{
    static const uint8_t group[6] = {1, 0x80, 0xc2, 0, 0, 0x13};
    struct ifreq interface;
    uint8_t controller[6] = {0};
    struct sockaddr_ll target = { .sll_family = AF_PACKET, .sll_protocol = htons(0x893a), .sll_halen = 6 };
    uint8_t frame[60] = {0};
    const char *private = getenv("ZTE_MESH_DB_SANDBOX");
    int fd, saved, result;
    if (!private || strcmp(private, "1") || (query < 0 || query > 3)) return -1;
    target.sll_ifindex = (int)if_nametoindex("lab-peer");
    if (!target.sll_ifindex) return -1;
    if (query) {
        memset(&interface, 0, sizeof(interface)); strcpy(interface.ifr_name, "br0");
        fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return -1;
        result = ioctl(fd, SIOCGIFHWADDR, &interface); saved = errno;
        close(fd); errno = saved;
        if (result || interface.ifr_hwaddr.sa_family != 1) return -1;
        memcpy(controller, interface.ifr_hwaddr.sa_data, 6);
    }
    memcpy(frame, query ? controller : group, 6); memcpy(frame + 6, peer, 6);
    frame[12] = 0x89; frame[13] = 0x3a;
    frame[17] = query ? 2 : 0;
    frame[18] = 0x12; frame[19] = query ? 2 : 1; frame[21] = 0x80;
    if (!query) {
        frame[22] = 1; frame[24] = 6; memcpy(frame + 25, peer, 6);
        frame[31] = 2; frame[33] = 6; memcpy(frame + 34, peer, 6);
    }
    if (query >= 2) {
        static const uint8_t search[] = {
            0x0d, 0, 1, 0, /* Searched role: registrar. */
            0x0e, 0, 1, 0, /* Band overwritten below. */
            0x80, 0, 2, 1, 1, /* Supported service: MAP Agent. */
            0x81, 0, 2, 1, 0, /* Searched service: MAP Controller. */
            0xb3, 0, 1, 1 /* MultiAP profile 1; no proprietary inputs. */
        };
        memset(frame, 0, sizeof(frame));
        memcpy(frame, group, 6); memcpy(frame + 6, peer, 6);
        frame[12] = 0x89; frame[13] = 0x3a; frame[17] = 7;
        frame[18] = 0x12; frame[19] = (uint8_t)(query + 1); frame[21] = 0xc0;
        frame[22] = 1; frame[24] = 6; memcpy(frame + 25, peer, 6);
        memcpy(frame + 31, search, sizeof(search)); frame[38] = (uint8_t)(query - 2);
    }
    memcpy(target.sll_addr, frame, 6);
    fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(0x893a));
    if (fd < 0) return -1;
    /* Listen before sending; the daemon may respond immediately. */
    if (bind(fd, (struct sockaddr *)&target, sizeof(target))) { saved = errno; close(fd); errno = saved; return -1; }
    result = sendto(fd, frame, sizeof(frame), 0, (struct sockaddr *)&target, sizeof(target)) == (ssize_t)sizeof(frame) ? 0 : -1;
    if (!result && query) result = wait_response(fd, controller, query);
    saved = errno; close(fd); errno = saved;
    return result;
}

/* Listen on the private wire. Open this BEFORE triggering the daemon: it may
 * answer immediately and a late listener would miss the frame. */
int controller_cmdu_listener(void)
{
    struct sockaddr_ll target = { .sll_family = AF_PACKET, .sll_protocol = htons(0x893a), .sll_halen = 6 };
    const char *private = getenv("ZTE_MESH_DB_SANDBOX");
    int fd, saved;
    if (!private || strcmp(private, "1")) return -1;
    target.sll_ifindex = (int)if_nametoindex("lab-peer");
    if (!target.sll_ifindex) return -1;
    fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(0x893a));
    if (fd < 0) return -1;
    if (bind(fd, (struct sockaddr *)&target, sizeof(target))) { saved = errno; close(fd); errno = saved; return -1; }
    return fd;
}

/* Wait for any frame carrying the given 1905 CMDU type; only the type is
 * inspected, nothing from the frame is kept or logged. Closes the listener. */
int controller_wait_cmdu(int fd, unsigned int type, unsigned int timeout_ms)
{
    struct pollfd ready;
    uint8_t frame[1600];
    struct timespec start, now;
    int result = -1, saved;
    if (fd < 0 || clock_gettime(CLOCK_MONOTONIC, &start)) goto done;
    ready.fd = fd; ready.events = POLLIN;
    for (;;) {
        long elapsed;
        ssize_t bytes;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) goto done;
        elapsed = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed >= (long)timeout_ms) { errno = ETIMEDOUT; goto done; }
        if (poll(&ready, 1, (int)((long)timeout_ms - elapsed)) <= 0) { errno = ETIMEDOUT; goto done; }
        bytes = recv(fd, frame, sizeof(frame), MSG_DONTWAIT);
        if (bytes < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (bytes < 0) goto done;
        if (bytes >= 22 && frame[12] == 0x89 && frame[13] == 0x3a &&
            (((unsigned int)frame[16] << 8) | frame[17]) == type) { result = 0; goto done; }
    }
done:
    saved = errno; if (fd >= 0) close(fd); memset(frame, 0, sizeof(frame)); errno = saved;
    return result;
}

/* ---- Channel Selection Request (see controller-packet.h) ---- */
static uint8_t opclass_20mhz(unsigned int radio, int channel)
{
    if (radio == 0)
        return channel == 14 ? 82 : 81;
    if (channel <= 48) return 115;
    if (channel <= 64) return 118;
    if (channel <= 144) return 121;
    if (channel <= 161) return 124;
    return 125;
}

int controller_radio_select(const uint8_t agent[6], unsigned int radio, int channel, int power, int *code)
{
    /* The fixed radio identifiers a ZTE agent uses ("MAP24G"/"MAP5GL"; seen in its own
     * AP-autoconfig and operating-channel reports and in `sendcmd multiapd.map.slave dev`). */
    static const uint8_t radio_id[2][6] = { {'M', 'A', 'P', '2', '4', 'G'}, {'M', 'A', 'P', '5', 'G', 'L'} };
    static uint16_t message_id = 0xe000;
    struct sockaddr_ll target = { .sll_family = AF_PACKET, .sll_protocol = htons(0x893a), .sll_halen = 6 };
    struct ifreq interface;
    uint8_t frame[96], reply[1600], source[6];
    struct timespec start, now;
    size_t used = 22;
    int fd = -1, result = -1, saved, probe;
    const char *private = getenv("ZTE_MESH_DB_SANDBOX");
    uint16_t mid;
    if (!agent || !code || radio > 1 || !private || strcmp(private, "1") || channel < -1 || channel > 165 ||
        power < -1 || power > 30 || (channel < 0 && power < 0))
        return -1;
    *code = -1;
    target.sll_ifindex = (int)if_nametoindex("veth-lab"); /* the daemon's actual port */
    if (!target.sll_ifindex)
        return -1;
    memset(&interface, 0, sizeof(interface)); strcpy(interface.ifr_name, "br0");
    probe = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (probe < 0) return -1;
    saved = ioctl(probe, SIOCGIFHWADDR, &interface); close(probe);
    if (saved || interface.ifr_hwaddr.sa_family != 1) return -1;
    memcpy(source, interface.ifr_hwaddr.sa_data, 6);
    mid = message_id++;
    memset(frame, 0, sizeof(frame));
    memcpy(frame, agent, 6); memcpy(frame + 6, source, 6);
    frame[12] = 0x89; frame[13] = 0x3a;
    frame[16] = 0x80; frame[17] = 0x06;            /* CMDU 0x8006 */
    frame[18] = (uint8_t)(mid >> 8); frame[19] = (uint8_t)mid;
    frame[21] = 0x80;                              /* last fragment */
    if (channel >= 0) {                            /* Channel Preference TLV */
        frame[used++] = 0x8b; frame[used++] = 0; frame[used++] = 11;
        memcpy(frame + used, radio_id[radio], 6); used += 6;
        frame[used++] = 1;                         /* one operating class */
        frame[used++] = opclass_20mhz(radio, channel);
        frame[used++] = 1;                         /* with exactly one channel */
        frame[used++] = (uint8_t)channel;
        frame[used++] = 14 << 4;                   /* preference 14, reason 0 */
    }
    if (power >= 0) {                              /* Transmit Power Limit TLV */
        frame[used++] = 0x8d; frame[used++] = 0; frame[used++] = 7;
        memcpy(frame + used, radio_id[radio], 6); used += 6;
        frame[used++] = (uint8_t)power;
    }
    frame[used++] = 0; frame[used++] = 0; frame[used++] = 0; /* End of Message */
    if (used < 60) used = 60;
    memcpy(target.sll_addr, agent, 6);
    fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(0x893a));
    if (fd < 0) return -1;
    /* Listen before sending: the agent answers within milliseconds. */
    if (bind(fd, (struct sockaddr *)&target, sizeof(target))) goto done;
    if (sendto(fd, frame, used, 0, (struct sockaddr *)&target, sizeof(target)) != (ssize_t)used) goto done;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) goto done;
    for (;;) {
        struct pollfd ready = { .fd = fd, .events = POLLIN };
        long elapsed;
        ssize_t bytes;
        size_t at;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) goto done;
        elapsed = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed >= 3000) { result = -2; goto done; }
        if (poll(&ready, 1, (int)(3000 - elapsed)) <= 0) { result = -2; goto done; }
        bytes = recv(fd, reply, sizeof(reply), MSG_DONTWAIT);
        if (bytes < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (bytes < 25) { if (bytes < 0) goto done; continue; }
        if (memcmp(reply + 6, agent, 6) || reply[12] != 0x89 || reply[13] != 0x3a ||
            be16(reply + 16) != 0x8007 || be16(reply + 18) != mid)
            continue;
        for (at = 22; at + 3 <= (size_t)bytes;) {
            size_t length = be16(reply + at + 1);
            if (reply[at] == 0 || at + 3 + length > (size_t)bytes) break;
            if (reply[at] == 0x8e && length >= 7 && !memcmp(reply + at + 3, radio_id[radio], 6)) {
                *code = reply[at + 9];
                result = 0;
                goto done;
            }
            at += 3 + length;
        }
        *code = -1; result = 0; goto done; /* a response without our radio's status */
    }
done:
    saved = errno;
    if (fd >= 0) close(fd);
    memset(reply, 0, sizeof(reply));
    errno = saved;
    return result;
}
