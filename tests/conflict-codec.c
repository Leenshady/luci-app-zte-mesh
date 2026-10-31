/* What counts as "another node is presenting my AL MAC".
 *
 * The condition this guards against is silent: two routers with the same AL MAC drop each other's frames in
 * the 1905 stack, so the only evidence is the frame itself. The checks below pin the two halves of the
 * decision -- it has to fire for a frame we did not send, and it must never fire for one we did, because
 * every frame this router emits carries the same source address.
 */
#include <linux/if_packet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../controller-conflict.h"

static const uint8_t own[6] = {2, 0, 0, 0, 0x19, 0xe3};
static const uint8_t peer[6] = {2, 0, 0, 0, 0x19, 0x0a};
static uint8_t frame[64];
static unsigned int checks;

#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "conflict codec failure line %d\n", __LINE__); exit(1); } } while (0)

static void build(const uint8_t *source)
{
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xff, 6);                     /* broadcast destination: what a Search uses */
    memcpy(frame + 6, source, 6);
    frame[12] = 0x89;
    frame[13] = 0x3a;
}

int main(void)
{
    static const unsigned int received[] = {PACKET_HOST, PACKET_BROADCAST, PACKET_MULTICAST, PACKET_OTHERHOST};
    unsigned int i, seed = 12345;
    size_t size;

    /* A peer claiming our address, however it was addressed to us. */
    build(own);
    for (i = 0; i < sizeof(received) / sizeof(received[0]); i++)
        CHECK(controller_conflict_from_peer(frame, 60, received[i], own) == 1);
    CHECK(controller_conflict_from_peer(frame, 14, PACKET_BROADCAST, own) == 1);

    /* Ours. The controller's own Search/Discovery look exactly like this on the socket, so treating them as a
     * conflict would report one on every router that has ever started. */
    CHECK(controller_conflict_from_peer(frame, 60, PACKET_OUTGOING, own) == 0);

    /* Another node with its own address, which is the healthy case. */
    build(peer);
    CHECK(controller_conflict_from_peer(frame, 60, PACKET_HOST, own) == 0);

    /* One bit of the source is enough to tell two nodes apart. */
    build(own);
    for (i = 0; i < 6; i++) {
        frame[6 + i] ^= 0x01;
        CHECK(controller_conflict_from_peer(frame, 60, PACKET_HOST, own) == 0);
        frame[6 + i] ^= 0x01;
    }

    /* Not 1905: the socket is protocol-bound, but a frame that slipped through another way is not evidence. */
    build(own);
    frame[12] = 0x08;
    frame[13] = 0x00;
    CHECK(controller_conflict_from_peer(frame, 60, PACKET_BROADCAST, own) == 0);

    /* Short frames, and the arguments a caller could get wrong. */
    build(own);
    for (size = 0; size < 14; size++)
        CHECK(controller_conflict_from_peer(frame, size, PACKET_BROADCAST, own) == 0);
    CHECK(controller_conflict_from_peer(NULL, 60, PACKET_BROADCAST, own) == 0);
    CHECK(controller_conflict_from_peer(frame, 60, PACKET_BROADCAST, NULL) == 0);

    /* Bounded fuzz: never anything but 0/1, and never a hit on our own transmission. */
    for (i = 0; i < 20000; i++) {
        int result;
        size_t pos;
        seed = seed * 1664525u + 1013904223u;
        pos = seed % sizeof(frame);
        frame[pos] ^= (uint8_t)(1u << ((seed >> 24) & 7));
        seed = seed * 1664525u + 1013904223u;
        size = seed % (sizeof(frame) + 1);
        result = controller_conflict_from_peer(frame, size, PACKET_HOST, own);
        if (result != 0 && result != 1)
            return 1;
        if (controller_conflict_from_peer(frame, size, PACKET_OUTGOING, own) != 0)
            return 1;
    }

    printf("conflict-codec: %u assertions; 20000 bounded mutations passed\n", checks);
    return 0;
}
