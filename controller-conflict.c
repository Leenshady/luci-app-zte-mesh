/* Another node on the LAN that presents the same AL MAC.
 *
 * The AL MAC is the identity the 1905 stack knows a controller by, and that stack drops every frame whose
 * source AL MAC equals its own. So two boxes with the same one are invisible to each other: both send
 * Search/Discovery, neither answers, the topology stays at a single entry and nothing is logged. From the
 * outside that is indistinguishable from "there is no neighbour at all" -- which is why it is worth naming.
 *
 * The frames are nevertheless on the wire. A packet socket bound to 0x893a sees a copy of each before the
 * 1905 stack decides what to do with it, so the condition is directly observable: a frame whose *source* is
 * our AL MAC but which we did not send. Frames we did send arrive at the same socket flagged
 * PACKET_OUTGOING, which is what tells the two apart -- checking the address alone would make every one of
 * our own transmissions look like a conflict.
 */
#include "controller-conflict.h"
#include <linux/if_packet.h>
#include <string.h>

int controller_conflict_from_peer(const uint8_t *frame, size_t bytes, unsigned int pkttype, const uint8_t own[6])
{
    if (!frame || !own || bytes < 14)
        return 0;
    if (pkttype == PACKET_OUTGOING)     /* ours, echoed back by the socket layer */
        return 0;
    if (frame[12] != 0x89 || frame[13] != 0x3a)
        return 0;
    return memcmp(frame + 6, own, 6) == 0;
}
