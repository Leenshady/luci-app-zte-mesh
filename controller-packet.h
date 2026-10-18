#ifndef ZTE_CONTROLLER_PACKET_H
#define ZTE_CONTROLLER_PACKET_H
#include <stddef.h>
#include <stdint.h>

/* Validate only a public lab Topology Response; never expose TLV bodies. */
int controller_topology_response_matches(const uint8_t *, size_t, const uint8_t[6]);
int controller_autoconfig_response_matches(const uint8_t *, size_t, const uint8_t[6], unsigned int band);
/* 0 discovery, 1 topology query, 2/3 public MAP search on band 0/1. */
int controller_inject_packet(int query);
int controller_inject_wsc_m1(void);
/* Block until a frame of the given CMDU type appears on the private wire. */
int controller_cmdu_listener(void);
int controller_wait_cmdu(int listener, unsigned int type, unsigned int timeout_ms);
int controller_wsc_m2_matches(const uint8_t *, size_t, const uint8_t[6], const uint8_t[16]);
/* Standard Multi-AP Channel Selection Request (CMDU 0x8006) from the controller to one
 * agent, built the way the factory master builds it (Channel Preference TLV 0x8b with one
 * operating class holding the single target channel, Transmit Power Limit TLV 0x8d) and
 * sent from the daemon's private port. channel: -1 leave unchanged, 0 automatic channel,
 * else a fixed channel (the agent also turns its auto-channel switch off); power: -1 leave
 * unchanged, else EIRP dBm 0..30 (the agent maps it to its 5 transmit-power levels).
 * Returns 0 when the agent's Channel Selection Response (0x8007) arrived (its response code
 * in *code, 0 = accepted), -2 on timeout, -1 on any local error. */
int controller_radio_select(const uint8_t agent[6], unsigned int radio, int channel, int power, int *code);
#endif
