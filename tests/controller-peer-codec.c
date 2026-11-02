/* Host tests for what a received 1905 frame leaves in peers/.
 *
 * The claim under test is A21: two children at once. The controller writes one file per (MAC, kind), so a second
 * child must not overwrite the first, the light and the counts of the same child are separate files, an answer to a
 * client action is filed under the client and not under the agent that sent it, and the frame the daemon churns out
 * twice a second leaves nothing at all. The frame builders are the same shapes tests/controller-1905-codec.c checks
 * the decoder against, so these bytes are the ones the decoder already agrees on.
 *
 * controller_peer_note() writes into $ZTE_MESH_PEERS_DIR (default /run/zte-mesh/peers), which is what lets this run
 * against a temporary directory instead of a daemon. The reader of these files, controller-service.c read_peer(),
 * takes one "key=value" per line and treats a missing key as "no data" - which is why a count the frame did not
 * carry is left out rather than written as a zero.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../controller-1905.h"
#include "../controller-peer.h"
#include "../controller-steer.h"

static unsigned int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "peer codec failure line %d\n", __LINE__); exit(1); } } while (0)

static char peers[256];
static const uint8_t controller_mac[6] = {2, 0, 0, 0, 0x19, 0x03};
static const uint8_t child_a[6] = {2, 0, 0, 0, 0x19, 0xe3};
static const uint8_t child_b[6] = {2, 0, 0, 0, 0x19, 0xe4};
static const uint8_t al_other[6] = {2, 0, 0, 0, 0x19, 0xe5};
static const uint8_t relay[6] = {2, 0, 0, 0, 0x19, 0xe6};
static const uint8_t phone[6] = {0x3c, 0x5a, 0x37, 0x01, 0x02, 0x03};

/* The file a note would have written, as text: "" when it is not there. */
static void peer_text(const uint8_t *mac, const char *kind, char *text, size_t room)
{
    char path[300];
    FILE *f;
    size_t n = 0;
    text[0] = 0;
    if (snprintf(path, sizeof(path), "%s/%02x:%02x:%02x:%02x:%02x:%02x.%s", peers,
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], kind) >= (int)sizeof(path))
        return;
    f = fopen(path, "r");
    if (!f)
        return;
    n = fread(text, 1, room - 1, f);
    fclose(f);
    text[n] = 0;
}

static int peer_absent(const uint8_t *mac, const char *kind)
{
    char text[96];
    peer_text(mac, kind, text, sizeof(text));
    return !text[0];
}

#define EXPECT(mac, kind, want) do { \
    char seen_[96]; \
    peer_text((mac), (kind), seen_, sizeof(seen_)); \
    CHECK(!strcmp(seen_, (want))); \
} while (0)

/* How many distinct MACs the directory holds, and how many half-written temporary names survived - the latter must
 * be zero, because the service would read one as if it were a value. */
static unsigned int distinct_macs(unsigned int *temps)
{
    static char seen[64][18];
    unsigned int count = 0, k;
    DIR *dir;
    struct dirent *entry;
    *temps = 0;
    dir = opendir(peers);
    if (!dir)
        return 0;
    while ((entry = readdir(dir))) {
        size_t name = strlen(entry->d_name);
        if (entry->d_name[0] == '.')
            continue;
        if (name >= 2 && !strcmp(entry->d_name + name - 2, ".t")) {
            (*temps)++;
            continue;
        }
        if (name < 17)
            continue;
        for (k = 0; k < count; k++)
            if (!memcmp(seen[k], entry->d_name, 17))
                break;
        if (k == count && count < 64) {
            memcpy(seen[count], entry->d_name, 17);
            seen[count][17] = 0;
            count++;
        }
    }
    closedir(dir);
    return count;
}

/* Ethernet + CMDU header, sent by `src` straight to this router, last fragment of one-frame message. */
static size_t frame_open(uint8_t *frame, const uint8_t *src, unsigned int type, unsigned int mid)
{
    memset(frame, 0, CONTROLLER_1905_MAX_BYTES);
    memcpy(frame, controller_mac, 6);
    memcpy(frame + 6, src, 6);
    frame[12] = 0x89;
    frame[13] = 0x3a;
    frame[16] = (uint8_t)(type >> 8);
    frame[17] = (uint8_t)type;
    frame[18] = (uint8_t)(mid >> 8);
    frame[19] = (uint8_t)mid;
    frame[21] = 0x80;
    return CONTROLLER_1905_HEADER;
}

/* The same, with the relay bit set: a neighbor forwarded it, so `src` is that neighbor and not the node the
 * message is about. */
static size_t frame_from_relay(uint8_t *frame, const uint8_t *src, unsigned int type, unsigned int mid)
{
    size_t at = frame_open(frame, src, type, mid);
    frame[21] = 0xc0;
    return at;
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

/* AP Metrics (TLV 0x94): BSSID, channel utilisation, then a big-endian u16 station count. */
static size_t put_ap_metrics(uint8_t *frame, size_t at, const uint8_t *bssid, unsigned int stations)
{
    uint8_t body[9];
    memset(body, 0, sizeof(body));
    memcpy(body, bssid, 6);
    body[6] = 40;
    body[7] = (uint8_t)(stations >> 8);
    body[8] = (uint8_t)stations;
    return put_tlv(frame, at, CONTROLLER_TLV_AP_METRICS, body, sizeof(body));
}

/* AP Operational BSS (TLV 0x83): radio count, per radio {identifier[6], interface count}, then per interface
 * {BSSID[6], ssid length, ssid} - the order ap_bss_interfaces() walks. */
static size_t put_ap_bss(uint8_t *frame, size_t at, const uint8_t *bssid, unsigned int interfaces)
{
    static const uint8_t radio_id[6] = {2, 'M', 'A', 'P', '2', '4'};
    uint8_t body[128];
    size_t payload = 0;
    unsigned int i;
    memset(body, 0, sizeof(body));
    body[payload++] = 1;
    memcpy(body + payload, radio_id, sizeof(radio_id));
    payload += sizeof(radio_id);
    body[payload++] = (uint8_t)interfaces;
    for (i = 0; i < interfaces; i++) {
        memcpy(body + payload, bssid, 6);
        payload += 6;
        body[payload++] = 3;
        memcpy(body + payload, "abc", 3);
        payload += 3;
    }
    return put_tlv(frame, at, CONTROLLER_TLV_AP_OPERATIONAL_BSS, body, payload);
}

/* ZTE vendor TLV (0x0b) holding sub-TLV 0x21 {AL MAC[6], status} - the indicator light. */
static size_t put_vendor_light(uint8_t *frame, size_t at, const uint8_t *al_mac, int on)
{
    uint8_t vendor[16];
    memset(vendor, 0, sizeof(vendor));
    vendor[0] = 0x00; vendor[1] = 0x19; vendor[2] = 0xc6;
    vendor[3] = 0x02; vendor[4] = 0x00; vendor[5] = 0x09;
    vendor[6] = 0x21; vendor[7] = 0x00; vendor[8] = 0x07;
    memcpy(vendor + 9, al_mac, 6);
    vendor[15] = (uint8_t)(on ? 1 : 0);
    return put_tlv(frame, at, CONTROLLER_TLV_VENDOR, vendor, sizeof(vendor));
}

static void two_children(void)
{
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    size_t at;
    unsigned int temps;

    at = put_ap_metrics(frame, frame_open(frame, child_a, 0x800c, 7), child_a, 3);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    at = put_ap_metrics(frame, frame_open(frame, child_b, 0x800c, 8), child_b, 7);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);

    /* Both are still there: the second child did not overwrite the first, and each keeps its own count. */
    EXPECT(child_a, "metrics", "stations=3\nhops=1\n");
    EXPECT(child_b, "metrics", "stations=7\nhops=1\n");
    CHECK(distinct_macs(&temps) == 2 && temps == 0);

    /* A second report from one child replaces its own value, and only its own. */
    at = put_ap_metrics(frame, frame_open(frame, child_a, 0x800c, 9), child_a, 1);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(child_a, "metrics", "stations=1\nhops=1\n");
    EXPECT(child_b, "metrics", "stations=7\nhops=1\n");

    /* The BSS count rides a different message (Topology Response) and a different TLV, and lands in the same file -
     * replacing it, because a file holds only what the latest frame said. */
    at = put_ap_bss(frame, frame_open(frame, child_b, 0x0003, 10), child_b, 2);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(child_b, "metrics", "bss=2\nhops=1\n");
    EXPECT(child_a, "metrics", "stations=1\nhops=1\n");
}

static void one_frame_several_keys(void)
{
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    size_t at;

    /* A frame that carries both counts writes one file with both on separate lines - the shape read_peer() walks -
     * and two AP Metrics TLVs in one frame are summed, because the factory daemon reports one BSS per TLV. */
    at = frame_open(frame, child_a, 0x800c, 14);
    at = put_ap_metrics(frame, at, child_a, 6);
    at = put_ap_bss(frame, at, child_a, 3);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(child_a, "metrics", "stations=6\nbss=3\nhops=1\n");

    at = frame_open(frame, child_a, 0x800c, 15);
    at = put_ap_metrics(frame, at, child_a, 6);
    at = put_ap_metrics(frame, at, child_b, 4);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(child_a, "metrics", "stations=10\nhops=1\n");

    /* A count of zero is still a count: the file has to say so, or the page cannot tell "nobody associated" from
     * "no report yet". */
    at = put_ap_metrics(frame, frame_open(frame, child_a, 0x800c, 16), child_a, 0);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(child_a, "metrics", "stations=0\nhops=1\n");
}

static void relayed_frames_are_the_senders(void)
{
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    size_t at;

    /* Counts have no AL MAC of their own, so they are filed under the node that put the frame on the wire, and the
     * relay bit becomes hops=2 on that file. The light below is the one field that names its own node. */
    at = put_ap_metrics(frame, frame_from_relay(frame, relay, 0x800c, 17), relay, 4);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(relay, "metrics", "stations=4\nhops=2\n");
}

static void light_is_another_file(void)
{
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    size_t at;

    /* The light rides a vendor sub-TLV naming the AL MAC the child reports for itself, which is not the ethernet
     * source once a relay is involved - so the file follows the reported MAC, and the relay gives the hops. */
    at = put_vendor_light(frame, frame_from_relay(frame, relay, 0x000e, 11), al_other, 1);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 2);
    EXPECT(al_other, "light", "light=1\n");
    EXPECT(al_other, "metrics", "hops=2\n");
    CHECK(peer_absent(child_a, "light"));

    /* Only a HigherLayerResponse (0x000e) is read for it: the same sub-TLV number in another message type is a
     * different field, so a node must not get a light from a frame that does not report one. */
    at = put_vendor_light(frame, frame_open(frame, child_a, 0x0004, 12), al_other, 1);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 0);
    EXPECT(al_other, "light", "light=1\n");

    /* The latest frame wins, including the hop count: a direct report replaces the one that came through the
     * relay. */
    at = put_vendor_light(frame, frame_open(frame, child_a, 0x000e, 18), al_other, 0);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 2);
    EXPECT(al_other, "light", "light=0\n");
    EXPECT(al_other, "metrics", "hops=1\n");

    /* Light and counts in one frame are two files, and the note reports both: one child owns them. */
    at = frame_open(frame, child_b, 0x000e, 13);
    at = put_vendor_light(frame, at, child_b, 1);
    at = put_ap_metrics(frame, at, child_b, 5);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 2);
    EXPECT(child_b, "light", "light=1\n");
    EXPECT(child_b, "metrics", "stations=5\nhops=1\n");
}

static void actions_follow_the_client(void)
{
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    uint8_t body[24];
    size_t at;

    /* A BTM report (0x8015) is about the client, and the action table keys on the client - so does the file.
     * Status 0 is "accept"; anything else is a refusal. */
    memset(body, 0, sizeof(body));
    memcpy(body, child_a, 6);
    memcpy(body + 6, phone, 6);
    body[12] = 0;
    memcpy(body + 13, child_b, 6);
    at = put_tlv(frame, frame_open(frame, child_a, STEER_MSG_BTM_REPORT, 21), STEER_TLV_BTM_REPORT, body, 19);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(phone, "btm", "ok=1\n");
    CHECK(peer_absent(child_a, "btm"));

    body[12] = 1; /* reject */
    at = put_tlv(frame, frame_open(frame, child_b, STEER_MSG_BTM_REPORT, 22), STEER_TLV_BTM_REPORT, body, 19);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(phone, "btm", "ok=0\n");

    /* Steering Completed (0x8017): the client info TLV is {source BSSID[6], client[6]}, and it confirms the move. */
    memset(body, 0, sizeof(body));
    memcpy(body, child_a, 6);
    memcpy(body + 6, phone, 6);
    at = put_tlv(frame, frame_open(frame, child_a, STEER_MSG_COMPLETED, 23), STEER_TLV_CLIENT_INFO, body, 12);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(phone, "btm", "ok=1\n");

    /* A completed message without the client info TLV names nobody, so it is not an answer to anything - and it
     * does not spend a place in the peer budget either. */
    at = put_tlv(frame, frame_open(frame, child_a, STEER_MSG_COMPLETED, 24), STEER_TLV_BSSID, child_a, 6);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 0);
    CHECK(peer_absent(child_b, "btm"));
}

/* ZTE's Device Inventory TLV (0xd4) as the factory daemon lays it out: [length][text] for the serial number, the
 * software version and the hardware version, then a radio count. The numbers here stay inside every field the
 * decoder reads; the count and the records after it are never reached. */
static size_t put_inventory(uint8_t *frame, size_t at, const char *software)
{
    uint8_t body[192];
    size_t payload = 0, len = strlen(software);
    body[payload++] = 5;
    memcpy(body + payload, "SN123", 5);
    payload += 5;
    body[payload++] = (uint8_t)len;
    memcpy(body + payload, software, len);
    payload += len;
    body[payload++] = 9;
    memcpy(body + payload, "ZXHN Z506", 9);
    payload += 9;
    body[payload++] = 0;
    return put_tlv(frame, at, CONTROLLER_TLV_DEVICE_INVENTORY, body, payload);
}

static void version_is_another_file(void)
{
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    size_t at;

    /* The version a child reports about itself is not a number, so it does not share the metrics digest: it gets
     * its own file and the page shows it as reported. The report also carries the hop count, so one frame leaves
     * two files. */
    at = put_inventory(frame, frame_open(frame, child_b, CONTROLLER_MSG_AP_CAPABILITY_REPORT, 50), "V1.0.0.2B2.8000");
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 2);
    EXPECT(child_b, "software", "V1.0.0.2B2.8000\n");
    EXPECT(child_b, "metrics", "hops=1\n");

    /* The same TLV number inside another message type is another field, and a capability report that carries no
     * inventory says nothing the page has: neither writes, and neither touches what the node already reported. */
    at = put_inventory(frame, frame_open(frame, child_b, 0x0004, 51), "V9.9.9");
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 0);
    EXPECT(child_b, "software", "V1.0.0.2B2.8000\n");
    at = put_tlv(frame, frame_open(frame, child_b, CONTROLLER_MSG_AP_CAPABILITY_REPORT, 52),
                 CONTROLLER_TLV_AL_MAC, child_b, 6);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 0);

    /* The latest report wins, and only for the node that sent it. */
    at = put_inventory(frame, frame_open(frame, child_b, CONTROLLER_MSG_AP_CAPABILITY_REPORT, 53), "V1.0.0.3B1.1000");
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 2);
    EXPECT(child_b, "software", "V1.0.0.3B1.1000\n");
    CHECK(peer_absent(child_a, "software"));

    /* An empty version field is "not reported", not "no version": the file the node filled earlier stays. */
    at = put_inventory(frame, frame_open(frame, child_b, CONTROLLER_MSG_AP_CAPABILITY_REPORT, 54), "");
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 0);
    EXPECT(child_b, "software", "V1.0.0.3B1.1000\n");

    /* A version that is not text is not a version either. */
    at = put_inventory(frame, frame_open(frame, child_b, CONTROLLER_MSG_AP_CAPABILITY_REPORT, 55), "V1.0\x01");
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 0);
    EXPECT(child_b, "software", "V1.0.0.3B1.1000\n");
}

static void frames_that_write_nothing(void)
{
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    size_t at;

    /* The daemon's status chatter (CMDU 0x0004 on the lab link, twice a second) arrives with the AL MAC and nothing
     * the page shows, so it must not churn the tmpfs. It is still rewritten nowhere: the file child_a owns keeps
     * the value of the last real report. */
    at = put_tlv(frame, frame_open(frame, child_a, 0x0004, 40), CONTROLLER_TLV_AL_MAC, child_a, 6);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 0);
    EXPECT(child_a, "metrics", "stations=0\nhops=1\n");
    at = frame_open(frame, child_a, 0x0004, 41);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 0);

    /* A frame that is not the last fragment, and a last fragment that is not the first one: in both cases the
     * message is incomplete, so half a value is not written. */
    at = put_ap_metrics(frame, frame_open(frame, child_a, 0x800c, 42), child_a, 2);
    at = put_eom(frame, at);
    frame[21] = 0x00;
    CHECK(controller_peer_note(frame, at) == 0);
    at = put_ap_metrics(frame, frame_open(frame, child_a, 0x800c, 43), child_a, 2);
    at = put_eom(frame, at);
    frame[20] = 1;
    CHECK(controller_peer_note(frame, at) == 0);
    EXPECT(child_a, "metrics", "stations=0\nhops=1\n");

    /* Not a 1905 frame at all, and a buffer too short to hold a header. */
    at = put_ap_metrics(frame, frame_open(frame, child_a, 0x800c, 44), child_a, 2);
    at = put_eom(frame, at);
    frame[13] = 0x3b;
    CHECK(controller_peer_note(frame, at) == 0);
    frame_open(frame, child_a, 0x800c, 45);
    CHECK(controller_peer_note(frame, CONTROLLER_1905_HEADER - 1) == 0);
    CHECK(controller_peer_note(frame, 0) == 0);
}

static void flood_budget(void)
{
    uint8_t frame[CONTROLLER_1905_MAX_BYTES];
    uint8_t mac[6] = {2, 0, 0, 0x0f, 0, 0};
    unsigned int temps, before, admitted = 0, i;
    size_t at;
    int refused = -1;

    /* At most 32 distinct MACs are recorded for a daemon lifetime: anybody on the LAN can send a 1905 frame with
     * any AL MAC, and without that ceiling a flood could fill the tmpfs. Count what the cases above already used
     * and expect the rest of the budget, whatever that turns out to be. */
    before = distinct_macs(&temps);
    CHECK(before > 0 && before < 32);
    for (i = 0; i < 60 && refused < 0; i++) {
        mac[5] = (uint8_t)i;
        at = put_ap_metrics(frame, frame_open(frame, mac, 0x800c, 100 + i), mac, 1);
        if (controller_peer_note(frame, put_eom(frame, at)) == 1) {
            admitted++;
            continue;
        }
        refused = (int)i;
    }
    CHECK(refused >= 0);
    CHECK(admitted == 32 - before);
    CHECK(distinct_macs(&temps) == 32 && temps == 0);

    /* The first stranger inside the budget keeps being rewritten; the one that landed past the ceiling never
     * appears, and its neighbours are unaffected. */
    mac[5] = 0;
    EXPECT(mac, "metrics", "stations=1\nhops=1\n");
    at = put_ap_metrics(frame, frame_open(frame, mac, 0x800c, 200), mac, 9);
    CHECK(controller_peer_note(frame, put_eom(frame, at)) == 1);
    EXPECT(mac, "metrics", "stations=9\nhops=1\n");
    mac[5] = (uint8_t)refused;
    CHECK(peer_absent(mac, "metrics"));
    CHECK(peer_absent(mac, "light"));
}

static void cleanup(void)
{
    DIR *dir = opendir(peers);
    struct dirent *entry;
    char path[512];
    if (dir) {
        while ((entry = readdir(dir))) {
            if (entry->d_name[0] == '.')
                continue;
            snprintf(path, sizeof(path), "%s/%s", peers, entry->d_name);
            (void)unlink(path);
        }
        closedir(dir);
    }
    (void)rmdir(peers);
}

int main(void)
{
    snprintf(peers, sizeof(peers), "/tmp/zte-peer-%ld-XXXXXX", (long)getpid());
    if (!mkdtemp(peers)) {
        fprintf(stderr, "peer codec: cannot make a temporary directory\n");
        return 1;
    }
    setenv("ZTE_MESH_PEERS_DIR", peers, 1);

    two_children();
    one_frame_several_keys();
    relayed_frames_are_the_senders();
    light_is_another_file();
    actions_follow_the_client();
    version_is_another_file();
    frames_that_write_nothing();
    flood_budget();
    cleanup();

    printf("peer codec: %u checks passed\n", checks);
    return 0;
}
