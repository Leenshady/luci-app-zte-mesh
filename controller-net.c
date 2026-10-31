/* Private Linux bridges + a lab veth wire ONLY after CLONE_NEWNET is verified.
 * No host interface movement/attachment, route or real device address.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
struct request { struct nlmsghdr n; struct ifinfomsg i; uint8_t attrs[256]; };
static int attr(struct request *q, unsigned short type, const void *data, size_t length)
{
    size_t pos = NLMSG_ALIGN(q->n.nlmsg_len), bytes = RTA_LENGTH(length);
    struct rtattr *a;
    if (pos + RTA_ALIGN(bytes) > sizeof(*q)) return -1;
    a = (struct rtattr *)((uint8_t *)q + pos);
    a->rta_type = type; a->rta_len = (unsigned short)bytes;
    if (length) memcpy(RTA_DATA(a), data, length);
    q->n.nlmsg_len = (uint32_t)(pos + RTA_ALIGN(bytes));
    return 0;
}
static void end_attr(struct request *q, size_t at)
{
    ((struct rtattr *)((uint8_t *)q + at))->rta_len = (unsigned short)(q->n.nlmsg_len - at);
}
static int exchange(struct request *q)
{
    struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
    struct timeval limit = {1, 0};
    uint8_t response[4096];
    struct nlmsghdr *message;
    ssize_t n;
    int fd, remaining, result = -1;
    fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit))) goto done;
    if (sendto(fd, q, q->n.nlmsg_len, 0, (struct sockaddr *)&kernel, sizeof(kernel)) != (ssize_t)q->n.nlmsg_len) goto done;
    n = recv(fd, response, sizeof(response), 0);
    if (n < 0) goto done;
    remaining = (int)n;
    for (message = (struct nlmsghdr *)response; NLMSG_OK(message, remaining); message = NLMSG_NEXT(message, remaining)) {
        if (message->nlmsg_seq == q->n.nlmsg_seq && message->nlmsg_type == NLMSG_ERROR &&
            message->nlmsg_len >= NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
            int error = ((struct nlmsgerr *)NLMSG_DATA(message))->error;
            if (error) errno = -error;
            else result = 0;
            break;
        }
    }
done:
    { int saved = errno;
      if (result) fprintf(stderr, "ZTE_CONTROLLER_NET seq=%u status=-1 errno=%d\n", q->n.nlmsg_seq, saved);
      close(fd); errno = saved; }
    return result;
}
static void start(struct request *q, unsigned int sequence, int create)
{
    memset(q, 0, sizeof(*q));
    q->n.nlmsg_len = NLMSG_LENGTH(sizeof(q->i)); q->n.nlmsg_type = RTM_NEWLINK;
    q->n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | (create ? NLM_F_CREATE | NLM_F_EXCL : 0);
    q->n.nlmsg_seq = sequence;
    q->i.ifi_family = AF_UNSPEC; q->i.ifi_flags = IFF_UP; q->i.ifi_change = IFF_UP;
}
/* The AL MAC is the identity bridge br0 (and the veth-lab ingress, which must match): 02:00:00:HH:19:LL.
 * LL is the router's LAN MAC last byte and HH a hash of its other five, which the init script computes; two
 * boxes on one segment then differ in sixteen bits instead of eight. A collision here is silent -- a 1905
 * stack drops frames whose source AL MAC is its own, so both boxes act as a single node, the topology stays
 * at one entry and nothing is logged.
 *
 * ZTE_MESH_AL_MAC carries the whole address. ZTE_MESH_AL_LAST (hex byte) still overrides LL alone, which is
 * what a test run wants when it presents itself as a new controller: a real child remembers a controller by
 * its AL MAC and skips the whole AP-autoconfiguration when it sees the same one again after a link-up.
 * Anything unparsable, or with the locally-administered bit clear (the vendor's route adapter refuses those
 * with ENODEV), falls back to the built-in 02:00:00:00:19:06. */
static int parse_mac(const char *text, uint8_t out[6])
{
    int at;
    for (at = 0; at < 6; at++) {
        char *end = NULL;
        unsigned long value = strtoul(text, &end, 16);
        if (end == text || value > 0xff) return -1;
        out[at] = (uint8_t)value;
        if (at < 5) {
            if (*end != ':') return -1;
            text = end + 1;
        } else if (*end) {
            return -1;
        }
    }
    return 0;
}
static void al_mac_bytes(uint8_t out[6])
{
    static const uint8_t builtin[6] = {2, 0, 0, 0, 0x19, 6};
    const char *text = getenv("ZTE_MESH_AL_MAC");
    uint8_t candidate[6];
    if (text && *text) {
        if (!parse_mac(text, candidate) && (candidate[0] & 2)) {
            memcpy(out, candidate, sizeof(candidate));
            return;
        }
        fprintf(stderr, "ZTE_CONTROLLER_NET al_mac=rejected value=%s\n", text);
    }
    memcpy(out, builtin, sizeof(builtin));
    if ((text = getenv("ZTE_MESH_AL_LAST")) && *text)
        out[5] = (uint8_t)(strtoul(text, NULL, 16) & 0xff);
}
/* The two placeholder ends the daemon never uses as its identity: the sandbox LAN bridge and the veth peer.
 * They must differ from the AL MAC, and from each other, or the namespace ends up with two devices on one
 * address -- the very confusion the split into br0/veth-lab exists to avoid. An odd tag in the high bit of
 * byte 3 keeps all three apart while leaving bytes 0-2 and 4-5 recognisable. */
static void sibling_mac(const uint8_t al[6], uint8_t out[6], unsigned char tag)
{
    memcpy(out, al, 6);
    out[3] ^= (uint8_t)(tag | 0x80);
}
static int create_bridge(const char *name, unsigned int sequence, const uint8_t mac[6])
{
    struct request q;
    size_t at;
    start(&q, sequence, 1);
    if (attr(&q, IFLA_IFNAME, name, strlen(name) + 1) || attr(&q, IFLA_ADDRESS, mac, 6)) return -1;
    at = NLMSG_ALIGN(q.n.nlmsg_len);
    if (attr(&q, IFLA_LINKINFO | NLA_F_NESTED, NULL, 0) || attr(&q, IFLA_INFO_KIND, "bridge", 7)) return -1;
    end_attr(&q, at);
    return exchange(&q);
}
static int create_private_wire(const uint8_t al[6])
{
    struct request q;
    struct ifinfomsg peer = { .ifi_family = AF_UNSPEC };
    struct stat prerequisite;
    size_t link, data, endpoint;
    uint8_t peer_mac[6];
    /* Refuse a test requiring module autoload. This prerequisite is present
     * on the current device; no module is installed/loaded by the harness.
     */
    if (stat("/sys/module/veth", &prerequisite)) return -1;
    sibling_mac(al, peer_mac, 2);
    start(&q, 3, 1);
    q.i.ifi_flags = q.i.ifi_change = 0; /* Both ends must exist before IFF_UP. */
    {
        /* Private identity bridge and standalone ingress share their actual
         * AL MAC, including unicast query destinations. No host links. */
        if (attr(&q, IFLA_IFNAME, "veth-lab", 9) ||
            attr(&q, IFLA_ADDRESS, al, 6)) return -1;
    }
    link = NLMSG_ALIGN(q.n.nlmsg_len);
    if (attr(&q, IFLA_LINKINFO | NLA_F_NESTED, NULL, 0) || attr(&q, IFLA_INFO_KIND, "veth", 5)) return -1;
    data = NLMSG_ALIGN(q.n.nlmsg_len);
    if (attr(&q, IFLA_INFO_DATA | NLA_F_NESTED, NULL, 0)) return -1;
    endpoint = NLMSG_ALIGN(q.n.nlmsg_len);
    if (attr(&q, VETH_INFO_PEER | NLA_F_NESTED, &peer, sizeof(peer)) ||
        attr(&q, IFLA_IFNAME, "lab-peer", 9) || attr(&q, IFLA_ADDRESS, peer_mac, 6)) return -1;
    end_attr(&q, endpoint); end_attr(&q, data); end_attr(&q, link);
    if (exchange(&q)) return -1;
    start(&q, 4, 0);
    q.i.ifi_index = (int)if_nametoindex("veth-lab");
    /* Standalone private ingress: bridge RX handlers consume protocol-specific
     * AF_PACKET frames before a socket bound to the port can see them. */
    if (!q.i.ifi_index) return -1;
    if (exchange(&q)) return -1;
    start(&q, 5, 0);
    q.i.ifi_index = (int)if_nametoindex("lab-peer");
    if (!q.i.ifi_index) return -1;
    return exchange(&q);
}
int controller_private_network(void)
{
    uint8_t al[6], lan[6];
    al_mac_bytes(al);
    sibling_mac(al, lan, 1);
    return create_bridge("br-lan", 1, lan) || create_bridge("br0", 2, al) || create_private_wire(al) ? -1 : 0;
}
/* Production wire: identical private identity bridge and ingress, but the
 * peer end is handed to the host namespace, where the init script enslaves it
 * to the LAN bridge. Nothing else crosses; the daemon only sees veth-lab. */
int controller_private_network_service(int host_net)
{
    struct request q;
    if (host_net < 0 || controller_private_network()) return -1;
    start(&q, 6, 0);
    q.i.ifi_flags = q.i.ifi_change = 0;
    q.i.ifi_index = (int)if_nametoindex("lab-peer");
    if (!q.i.ifi_index || attr(&q, IFLA_NET_NS_FD, &host_net, sizeof(host_net))) return -1;
    return exchange(&q);
}
