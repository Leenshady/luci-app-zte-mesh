#ifndef ZTE_CONTROLLER_PEER_H
#define ZTE_CONTROLLER_PEER_H
#include <stddef.h>
#include <stdint.h>

/* What one received 1905 frame leaves on disk for the topology page.
 *
 * The controller's database does not hold the indicator light, the number of associated clients, how many BSS a
 * child reports as up, or whether it reached us over a direct link or a relay. All of it rides in frames the daemon
 * receives anyway: a HigherLayerResponse carries the light in vendor sub-TLV 0x21 {AL MAC[6], status} (the master
 * reads it in _topoLightStatusHandle), an AP Metrics Response lists the station count of each BSS, and a Topology
 * Response lists the child's BSS interfaces. The software version rides the AP Capability Report, in the vendor's
 * own TLV 0xd4; the master's daemon extracts it into its own device table and prints it on its CLI, but never
 * writes it into the peer rows, so a page that reads the database alone shows it empty. The answers to a client
 * action - a BTM report (0x8015) and a Steering Completed (0x8017) - name the *client*, and are filed under that
 * MAC rather than under the agent that sent them, because that is the key the action table uses.
 *
 * The latest values go to `<peers dir>/<MAC>.<kind>`, written atomically, and the service reads them for state.json.
 * The directory is /run/zte-mesh/peers unless ZTE_MESH_PEERS_DIR says otherwise - that is the one thing here a host
 * test can point at a temporary directory, which is what makes "two children at once" checkable without two radios.
 *
 * Decoding lives in controller-1905.c and controller-steer.c; this file only decides what to write and writes it.
 */

/* 1 or 2 = the number of files this frame produced, 0 = the frame says nothing the page shows (a fragment, another
 * vendor's frame, the daemon's status chatter, or a MAC beyond the flood budget below). A complete, unfragmented
 * 1905 CMDU is the only thing looked at.
 *
 * The budget is per process, i.e. per daemon lifetime: anybody on the LAN can send a 1905 frame with any AL MAC, so
 * at most 32 distinct MACs are recorded. Without that a forged flood could fill the tmpfs with digest files. */
int controller_peer_note(const uint8_t *frame, size_t bytes);

#endif
