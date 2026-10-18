#ifndef CONTROLLER_WSC_CRYPTO_H
#define CONTROLLER_WSC_CRYPTO_H
#include <stddef.h>
#include <stdint.h>
#include "mesh-bridge.h"
/* PUBLIC lab peer only. No plaintext/key material is returned or logged. */
struct controller_wsc_check {
    unsigned int stage;
    unsigned int authenticator_verified, keywrap_verified, profile_matched;
    unsigned int map_teardown;
};
int controller_wsc_verify_m2(const uint8_t *, size_t, const uint8_t *, size_t,
                           const uint8_t[32], const struct mb_profile *,
                           struct controller_wsc_check *);
#endif
