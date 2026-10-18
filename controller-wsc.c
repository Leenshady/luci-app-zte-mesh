/* PUBLIC isolated WSC enrollee. Real RNG/DH; no factory/host credentials.
 * First observe M2 structure; this does not claim key authentication/install.
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
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "mesh-bridge.h"
#include "controller-wsc-crypto.h"

typedef struct bignum_st BIGNUM;
typedef struct bignum_ctx BN_CTX;
extern int RAND_bytes(unsigned char *, int);
extern BIGNUM *BN_new(void);
extern int BN_hex2bn(BIGNUM **, const char *);
extern BIGNUM *BN_bin2bn(const unsigned char *, int, BIGNUM *);
extern int BN_set_word(BIGNUM *, unsigned long);
extern int BN_mod_exp(BIGNUM *, const BIGNUM *, const BIGNUM *, const BIGNUM *, BN_CTX *);
extern int BN_num_bits(const BIGNUM *);
extern int BN_bn2bin(const BIGNUM *, unsigned char *);
extern void BN_clear_free(BIGNUM *);
extern BN_CTX *BN_CTX_new(void);
extern void BN_CTX_free(BN_CTX *);
static unsigned int u16(const uint8_t *p) { return ((unsigned int)p[0] << 8) | p[1]; }
static int attr(uint8_t *out, size_t cap, size_t *used, unsigned int type, const void *data, size_t n)
{
    if (*used > cap || cap - *used < 4 || n > cap - *used - 4 || n > 65535) return -1;
    out[(*used)++] = (uint8_t)(type >> 8); out[(*used)++] = (uint8_t)type;
    out[(*used)++] = (uint8_t)(n >> 8); out[(*used)++] = (uint8_t)n;
    memcpy(out + *used, data, n); *used += n; return 0;
}
static int scalar(uint8_t *out, size_t cap, size_t *used, unsigned int type, unsigned int value, size_t n)
{
    uint8_t bytes[4]; size_t i;
    if (!n || n > 4) return -1;
    for (i = 0; i < n; i++) bytes[n - 1 - i] = (uint8_t)(value >> (i * 8));
    return attr(out, cap, used, type, bytes, n);
}
/* RFC 3526 group 5, exactly the WPS 1536-bit MODP group. */
static const char prime[] =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E08"
    "8A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD"
    "3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625"
    "E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A8"
    "99FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163"
    "BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3A"
    "D961C62F356208552BB9ED529077096966D670C354E4ABC9804"
    "F1746C08CA237327FFFFFFFFFFFFFFFF";
static int public_key(uint8_t out[192], uint8_t secret[32])
{
    BIGNUM *p = NULL, *g = BN_new(), *x = NULL, *y = BN_new();
    BN_CTX *ctx = BN_CTX_new();
    int bytes, result = -1;
    if (!g || !y || !ctx || RAND_bytes(secret, 32) != 1 ||
        !BN_hex2bn(&p, prime) || !(x = BN_bin2bn(secret, 32, NULL)) ||
        !BN_set_word(g, 2) || !BN_mod_exp(y, g, x, p, ctx)) goto done;
    bytes = (BN_num_bits(y) + 7) / 8;
    if (bytes <= 0 || bytes > 192) goto done;
    memset(out, 0, 192);
    if (BN_bn2bin(y, out + 192 - bytes) != bytes) goto done;
    result = 0;
done:
    /* Caller owns/wipes the exponent after the response has been verified. */
    BN_clear_free(x); BN_clear_free(y); BN_clear_free(g); BN_clear_free(p); BN_CTX_free(ctx);
    return result;
}
int controller_wsc_m2_matches(const uint8_t *frame, size_t bytes, const uint8_t controller[6], const uint8_t nonce[16])
{
    static const uint8_t peer[6] = {2, 0, 0, 0, 0x19, 0x0a};
    size_t at = 22, n; unsigned int matched = 0;
    if (!frame || !controller || !nonce || bytes < 25 || bytes > 1600 ||
        memcmp(frame, peer, 6) || memcmp(frame + 6, controller, 6) ||
        u16(frame + 12) != 0x893a || frame[14] || frame[15] ||
        u16(frame + 16) != 9 || frame[20] || frame[21] != 0x80) return 0;
    while (bytes - at >= 3) {
        unsigned int type = frame[at]; n = u16(frame + at + 1); at += 3;
        if (n > bytes - at) return 0;
        if (!type) {
            if (n || !matched) return 0;
            for (; at < bytes; at++) if (frame[at]) return 0;
            return 1;
        }
        if (type == 0x11) {
            size_t p = 0;
            unsigned int message = 0, enrollee = 0, registrar = 0, key = 0, encrypted = 0, auth = 0;
            while (n - p >= 4) {
                unsigned int tag = u16(frame + at + p), len = u16(frame + at + p + 2); p += 4;
                if (len > n - p) return 0;
                if (tag == 0x1022 && len == 1 && frame[at + p] == 5) message++;
                if (tag == 0x101a && len == 16 && !memcmp(frame + at + p, nonce, 16)) enrollee++;
                if (tag == 0x1039 && len == 16) registrar++;
                if (tag == 0x1032 && len == 192) key++;
                if (tag == 0x1018 && len >= 32 && !(len % 16)) encrypted++;
                if (tag == 0x1005 && len == 8) auth++;
                p += len;
            }
            if (p != n || message != 1 || enrollee != 1 || registrar != 1 || key != 1 || encrypted != 1 || auth != 1) return 0;
            matched++;
        }
        at += n;
    }
    return 0;
}
int controller_inject_wsc_m1(void)
{
    static const uint8_t peer[6] = {2, 0, 0, 0, 0x19, 0x0a};
    static const uint8_t device_type[8] = {0, 6, 0, 0x50, 0xf2, 4, 0, 1};
    static const uint8_t radio[11] = {2, 0, 0, 0, 0x19, 0x0b, 1, 1, 81, 20, 0};
    struct sockaddr_ll target = { .sll_family = AF_PACKET, .sll_protocol = htons(0x893a), .sll_halen = 6 };
    struct ifreq interface = {0}; struct timespec start, now;
    struct pollfd ready;
    uint8_t frame[1400] = {0}, received[1601], nonce[16], uuid[16], pub[192], controller[6], secret[32];
    uint8_t *wsc = frame + 39;
    size_t used = 0, length; unsigned int tries;
    int fd = -1, result = -1;
    const char *sandbox = getenv("ZTE_MESH_DB_SANDBOX");
    if (!sandbox || strcmp(sandbox, "1")) goto done;
    if (RAND_bytes(nonce, 16) != 1 || RAND_bytes(uuid, 16) != 1 || public_key(pub, secret)) goto done;
#define A(t,p,n) do { if (attr(wsc, 1000, &used, (t), (p), (n))) goto done; } while (0)
#define V(t,v,n) do { if (scalar(wsc, 1000, &used, (t), (v), (n))) goto done; } while (0)
    V(0x104a, 0x10, 1); V(0x1022, 4, 1); A(0x1047, uuid, 16); A(0x1020, peer, 6);
    A(0x101a, nonce, 16); A(0x1032, pub, 192);
    V(0x1004, 0x0020, 2); V(0x1010, 8, 2); V(0x100d, 1, 1); V(0x1008, 0x0080, 2); V(0x1044, 1, 1);
    A(0x1021, "PUBLIC-LAB", 10); A(0x1023, "MAP-TEST", 8); A(0x1024, "1", 1); A(0x1042, "PUBLIC", 6);
    A(0x1054, device_type, 8); A(0x1011, "MAP-TEST", 8);
    V(0x103c, 1, 1); V(0x1002, 0, 2); V(0x1012, 4, 2); V(0x1009, 0, 2); V(0x102d, 0x80000000u, 4);
#undef A
#undef V
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0); if (fd < 0) goto done;
    strcpy(interface.ifr_name, "br0");
    if (ioctl(fd, SIOCGIFHWADDR, &interface) || interface.ifr_hwaddr.sa_family != 1) goto done;
    memcpy(controller, interface.ifr_hwaddr.sa_data, 6); close(fd); fd = -1;
    memcpy(frame, controller, 6); memcpy(frame + 6, peer, 6);
    frame[12] = 0x89; frame[13] = 0x3a; frame[17] = 9; frame[18] = 0x12; frame[19] = 5; frame[21] = 0x80;
    frame[22] = 0x85; frame[24] = sizeof(radio); memcpy(frame + 25, radio, sizeof(radio));
    frame[36] = 0x11; frame[37] = (uint8_t)(used >> 8); frame[38] = (uint8_t)used;
    length = 39 + used + 3;
    target.sll_ifindex = (int)if_nametoindex("lab-peer"); memcpy(target.sll_addr, controller, 6);
    if (!target.sll_ifindex) goto done;
    fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(0x893a));
    if (fd < 0 || bind(fd, (struct sockaddr *)&target, sizeof(target)) ||
        sendto(fd, frame, length, 0, (struct sockaddr *)&target, sizeof(target)) != (ssize_t)length ||
        clock_gettime(CLOCK_MONOTONIC, &start)) goto done;
    printf("ZTE_CONTROLLER_PACKET fixture=wsc-m1 private_only=1 send_status=0 real_rng_dh=1 body_logged=0\n"); fflush(NULL);
    ready.fd = fd; ready.events = POLLIN;
    for (tries = 0; tries < 512; tries++) {
        long elapsed; int status; ssize_t bytes;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) goto done;
        elapsed = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed >= 4000) break;
        status = poll(&ready, 1, (int)(4000 - elapsed));
        if (status < 0 && errno == EINTR) continue;
        if (status <= 0 || !(ready.revents & POLLIN)) break;
        bytes = recv(fd, received, sizeof(received), MSG_DONTWAIT);
        if (bytes < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (bytes < 0) break;
        if (controller_wsc_m2_matches(received, (size_t)bytes, controller, nonce)) {
            struct mb_profile wanted = { .security = MB_WPA2_PSK_AES, .enabled = 1,
                .ssid_length = 19, .key_length = 20, .ssid = "zte-controller-test", .key = "public-test-only-key" };
            struct controller_wsc_check check = {0};
            size_t at = 22;
            int verified = -1;
            while ((size_t)bytes - at >= 3) {
                unsigned int type = received[at], n = u16(received + at + 1); at += 3;
                if (n > (size_t)bytes - at || !type) break;
                if (type == 0x11) {
                    verified = controller_wsc_verify_m2(wsc, used, received + at, n, secret, &wanted, &check);
                    break;
                }
                at += n;
            }
            printf("ZTE_CONTROLLER_PACKET fixture=wsc-m2 private_only=1 peer_received=1 cmdu=9 nonce_match=1 encrypted_settings=1 authenticator_present=1 bytes=%u cryptographic_verified=%u keywrap_verified=%u profile_match=%u map_teardown=%u stage=%u body_logged=0\n",
                   (unsigned int)bytes, check.authenticator_verified && check.keywrap_verified,
                   check.keywrap_verified, check.profile_matched, check.map_teardown, check.stage);
            result = verified;
            mb_wipe(&wanted, sizeof(wanted)); mb_wipe(&check, sizeof(check));
            break;
        }
    }
done:
    if (fd >= 0) close(fd);
    mb_wipe(frame, sizeof(frame)); mb_wipe(received, sizeof(received)); mb_wipe(nonce, sizeof(nonce));
    mb_wipe(uuid, sizeof(uuid)); mb_wipe(pub, sizeof(pub)); mb_wipe(secret, sizeof(secret));
    return result;
}
