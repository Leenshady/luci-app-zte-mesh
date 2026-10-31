#ifndef CONTROLLER_CONFLICT_H
#define CONTROLLER_CONFLICT_H
#include <stddef.h>
#include <stdint.h>

/* 1 when one received frame came from another node that claims our own AL MAC (controller-conflict.c).
 * `pkttype` is the sockaddr_ll one, so the caller passes what recvfrom(2) reported. */
int controller_conflict_from_peer(const uint8_t *frame, size_t bytes, unsigned int pkttype, const uint8_t own[6]);

#endif
