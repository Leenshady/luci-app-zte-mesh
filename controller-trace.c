/* Temporary real-daemon bootstrap diagnostics. Preserve all vendor results.
 * No CM/driver success stubs; no log varargs/profile bytes are printed.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "controller-1905.h"
#define EXPORT __attribute__((visibility("default")))

static unsigned int lines, packet_lines, trace_all_lines;
static int (*format_real)(char *, size_t, const char *, va_list);
/* stderr in this sandbox can be a pipe that is already gone, and a dropped
 * diagnostic is not actionable anyway — but the result still has to be consumed
 * by something other than a cast: glibc marks write() __warn_unused_result and
 * GCC rejects `(void)write(...)` under -Werror=unused-result (11.4 does). Assign
 * to a variable and discard that instead, so the warning stays available for the
 * calls where the result does matter. */
static void emit(const char *data, size_t len)
{
    ssize_t written = write(STDERR_FILENO, data, len);
    (void)written;
}
/* The firehose switch: a flag file in the one directory the sandbox leaves writable. */
static int verbose_enabled(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = access("/run/zte-mesh/trace-all", F_OK) == 0;
    return cached;
}
static void message(const char *fmt, ...)
{
    char buffer[384];
    va_list ap;
    int n, saved = errno;
    unsigned int count, limit;
    /* Config-push diagnostics get their own small budget so DB noise cannot hide them. */
    int renew = !!strstr(fmt, "enew");
    int packet = strstr(fmt, " rx1905 ") || strstr(fmt, " tx1905 ") ||
                 strstr(fmt, " packet_bind ") || strstr(fmt, " native_dh_keygen ") ||
                 strstr(fmt, " packet_decision ") || strstr(fmt, " oss_dispatch ") || strstr(fmt, " wsc_status ") || strstr(fmt, " bss_config ");
    /* The value lines ride the firehose budget: they appear exactly when the firehose does (the M2 arrives
     * long after the ordinary budget is gone). */
    int trace_all = !!strstr(fmt, "ZTE_TRACE_ALL") || !!strstr(fmt, "ZTE_TRACE_VALUE");
    if (!format_real) return;
    if (strstr(fmt, " device_event ")) {
        /* device on/off announcements: a small budget of their own so the DB chatter cannot hide them */
        static unsigned int device_lines;
        if (__sync_fetch_and_add(&device_lines, 1) >= 48) { errno = saved; return; }
        va_start(ap, fmt);
        n = format_real(buffer, sizeof(buffer), fmt, ap);
        va_end(ap);
        if (n > 0)
            emit(buffer, (size_t)n < sizeof(buffer) ? (size_t)n : sizeof(buffer) - 1);
        errno = saved;
        return;
    }
    if (renew) {
        static unsigned int renew_lines;
        if (__sync_fetch_and_add(&renew_lines, 1) >= 48) { errno = saved; return; }
        va_start(ap, fmt);
        n = format_real(buffer, sizeof(buffer), fmt, ap);
        va_end(ap);
        if (n > 0)
            emit(buffer, (size_t)n < sizeof(buffer) ? (size_t)n : sizeof(buffer) - 1);
        errno = saved;
        return;
    }
    limit = trace_all ? 20000 : packet ? 64 : 160;
    count = __sync_fetch_and_add(trace_all ? &trace_all_lines : packet ? &packet_lines : &lines, 1);
    if (count >= limit) {
        if (count == limit && !trace_all) {
            static const char ordinary[] = "ZTE_CONTROLLER_TRACE log_budget=ordinary exhausted=1 limit=160\n";
            static const char protocol[] = "ZTE_CONTROLLER_TRACE log_budget=packet exhausted=1 limit=64\n";
            emit(packet ? protocol : ordinary,
                 packet ? sizeof(protocol) - 1 : sizeof(ordinary) - 1);
        }
        errno = saved;
        return;
    }
    va_start(ap, fmt);
    n = format_real(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    if (n > 0)
        emit(buffer, (size_t)n < sizeof(buffer) ? (size_t)n : sizeof(buffer) - 1);
    errno = saved;
}
static void *symbol(const char *name)
{
    void *p = dlsym(RTLD_NEXT, name);
    if (!p) {
        message("ZTE_CONTROLLER_TRACE missing=%s\n", name);
        _exit(79);
    }
    return p;
}
__attribute__((constructor)) static void init(void)
{
    const char *isolated = getenv("ZTE_MESH_DB_SANDBOX");
    if (!isolated || strcmp(isolated, "1"))
        _exit(79);
    format_real = (int (*)(char *, size_t, const char *, va_list))dlsym(RTLD_NEXT, "vsnprintf");
    if (!format_real)
        _exit(79);
    message("ZTE_CONTROLLER_TRACE active real_calls=1 credentials_logged=0\n");
}
EXPORT int OSSInitConfig(void *config)
{
    int (*real)(void *) = symbol("OSSInitConfig");
    int result, saved;
    message("ZTE_CONTROLLER_TRACE call=OSSInitConfig\n");
    result = real(config); saved = errno;
    message("ZTE_CONTROLLER_TRACE return=OSSInitConfig status=%d errno=%d\n", result, saved);
    errno = saved; return result;
}
EXPORT int OSSStart(void *config, void *arg1, void *arg2)
{
    int (*real)(void *, void *, void *) = symbol("OSSStart");
    int result, saved;
    message("ZTE_CONTROLLER_TRACE call=OSSStart\n");
    result = real(config, arg1, arg2); saved = errno;
    message("ZTE_CONTROLLER_TRACE return=OSSStart status=%d errno=%d\n", result, saved);
    errno = saved; return result;
}
EXPORT int socket(int family, int type, int protocol)
{
    int (*real)(int, int, int) = symbol("socket");
    int result = real(family, type, protocol), saved = errno;
    message("ZTE_CONTROLLER_TRACE socket family=%d type=%d protocol=%d success=%d errno=%d\n",
            family, type, protocol, result >= 0, saved);
    errno = saved; return result;
}
EXPORT ssize_t sendto(int fd, const void *data, size_t length, int flags,
                      const struct sockaddr *destination, socklen_t destination_size)
{
    ssize_t (*real)(int, const void *, size_t, int, const struct sockaddr *, socklen_t) = symbol("sendto");
    ssize_t result = real(fd, data, length, flags, destination, destination_size);
    int saved = errno;
    struct sockaddr_storage address;
    socklen_t size = sizeof(address);
    if (data && length >= 22 && !getsockname(fd, (struct sockaddr *)&address, &size) && address.ss_family == AF_PACKET) {
        const unsigned char *bytes = data;
        unsigned int ether = ((unsigned int)bytes[12] << 8) | bytes[13];
        if (ether == 0x893a)
            message("ZTE_CONTROLLER_TRACE tx1905 cmdu=%u bytes=%u sent=%d errno=%d\n",
                    ((unsigned int)bytes[16] << 8) | bytes[17], (unsigned int)length, (int)result, result < 0 ? saved : 0);
    }
    errno = saved; return result;
}
/* What the topology page of the controller shows per child but the database does not hold: the indicator light, the number
 * of associated clients, how many BSS the child reports as up, and whether it reached us over a direct link or through a
 * relay. All of it rides in frames the daemon receives anyway: a HigherLayerResponse carries the light in the vendor sub-TLV
 * 0x21 {AL MAC[6], status} (the master reads it in _topoLightStatusHandle), an AP Metrics Response lists the station count of
 * each BSS, and a Topology Response lists the child's BSS interfaces. controller-1905.c does the parsing - the same decoder
 * is unit-tested on constructed frames and on one captured frame - and keeps no TLV body. The latest values go to
 * /run/zte-mesh/peers/<AL MAC>.<kind>, written atomically; the service reads them for state.json. Only complete,
 * unfragmented frames are looked at. */
static void write_peer(const unsigned char *mac, const char *kind, const char *text, int bytes)
{
    char path[64], temp[72];
    int fd;
    snprintf(path, sizeof(path), "/run/zte-mesh/peers/%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(path + strlen(path), sizeof(path) - strlen(path), ".%s", kind);
    snprintf(temp, sizeof(temp), "%s.t", path);
    fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0)
        return;
    if (write(fd, text, (size_t)bytes) == bytes && !close(fd))
        (void)rename(temp, path);
    else
        (void)unlink(temp);
}
static void note_peer(const unsigned char *f, const size_t n)
{
    struct cmdu_header header;
    struct tlv_cursor cursor;
    struct node_metrics metrics;
    unsigned int type;
    const unsigned char *value, *mac = f + 6;
    size_t length, used = 0;
    int light = -1;
    char text[72];
    int bytes;
    if (!cmdu_decode(f, n, &header) || !header.last_fragment || header.fragment)
        return;
    tlv_open(&cursor, f, n);
    while (tlv_next(&cursor, &type, &value, &length)) {
        struct zte_cursor zte;
        /* Only a HigherLayerResponse carries the light: the same sub-TLV number inside another message type is a
         * different field, and the master reads it from the response too. */
        if (header.type != 0x000e || type != CONTROLLER_TLV_VENDOR || !zte_vendor_open(&zte, value, length))
            continue;
        while (zte_vendor_next(&zte, &type, &value, &length))
            if (type == 0x21 && length >= 7) {
                light = value[6] ? 1 : 0;
                mac = value; /* the AL MAC the child reports for itself */
            }
    }
    if (!node_metrics_decode(f, n, &metrics))
        return;
    /* Only frames that describe the link or its load are worth a file: the daemon's status chatter (CMDU 0x0004 on the lab
     * link, ~2/s) says nothing the page shows, and rewriting a file for it would churn the tmpfs for no gain. */
    if (!(metrics.fields & (NODE_METRIC_STATIONS | NODE_METRIC_BSS)) &&
        header.type != 0x0003 && header.type != 0x0006 && header.type != 0x000e && light < 0)
        return;
    {
        /* Anybody on the LAN can send a 1905 frame with any AL MAC: remember at most 32 distinct MACs per daemon lifetime so a
         * flood of forged frames cannot fill the tmpfs with digest files. */
        static unsigned char known[32][6];
        static unsigned int known_count;
        unsigned int k;
        for (k = 0; k < known_count; k++)
            if (!memcmp(known[k], mac, 6))
                break;
        if (k == known_count) {
            if (known_count >= 32)
                return;
            memcpy(known[known_count++], mac, 6);
        }
    }
    if (light >= 0) {
        bytes = snprintf(text, sizeof(text), "light=%d\n", light);
        write_peer(mac, "light", text, bytes);
    }
    /* A count that the frame did not carry is left out, so the reader sees "no data" instead of a zero that looks measured. */
    if (metrics.fields & NODE_METRIC_STATIONS)
        used += (size_t)snprintf(text + used, sizeof(text) - used, "stations=%u\n", metrics.stations);
    if (metrics.fields & NODE_METRIC_BSS)
        used += (size_t)snprintf(text + used, sizeof(text) - used, "bss=%u\n", metrics.bss);
    if (metrics.hops > 0)
        used += (size_t)snprintf(text + used, sizeof(text) - used, "hops=%d\n", metrics.hops);
    if (used)
        write_peer(mac, "metrics", text, (int)used);
}

EXPORT ssize_t recvfrom(int fd, void *data, size_t length, int flags,
                        struct sockaddr *source, socklen_t *source_size)
{
    ssize_t (*real)(int, void *, size_t, int, struct sockaddr *, socklen_t *) = symbol("recvfrom");
    ssize_t result = real(fd, data, length, flags, source, source_size);
    int saved = errno;
    struct sockaddr_storage address;
    socklen_t size = sizeof(address);
    if (data && result >= 22 && !getsockname(fd, (struct sockaddr *)&address, &size) && address.ss_family == AF_PACKET) {
        const unsigned char *bytes = data;
        if (bytes[12] == 0x89 && bytes[13] == 0x3a)
            note_peer(bytes, (size_t)result);
        if (bytes[12] == 0x89 && bytes[13] == 0x3a)
            message("ZTE_CONTROLLER_TRACE rx1905 cmdu=%u bytes=%u ifindex=%d protocol=%u\n",
                    ((unsigned int)bytes[16] << 8) | bytes[17], (unsigned int)result,
                    ((struct sockaddr_ll *)&address)->sll_ifindex,
                    (unsigned int)ntohs(((struct sockaddr_ll *)&address)->sll_protocol));
    }
    errno = saved; return result;
}
EXPORT int ASEND(unsigned int event, const void *data, unsigned int length, void *destination)
{
    int (*real)(unsigned int, const void *, unsigned int, void *) = symbol("ASEND");
    int result = real(event, data, length, destination), saved = errno;
    if (event == 0x9092)
        message("ZTE_CONTROLLER_TRACE oss_dispatch event=37010 bytes=%u status=%d payload_logged=0\n", length, result);
    /* device on/off announcements (master_send_dev_on_msg and friends), for REPLAY_RESULTS section 15.1 */
    if ((event == 0xa937 || event == 0xa938) && destination)
        message("ZTE_CONTROLLER_TRACE device_event event=0x%x bytes=%u dest=%s status=%d epoch=%ld\n", event, length,
                (const char *)destination, result, (long)time(NULL));
    errno = saved; return result;
}
EXPORT int DH_generate_key(void *dh)
{
    int (*real)(void *) = symbol("DH_generate_key");
    int result = real(dh), saved = errno;
    message("ZTE_CONTROLLER_TRACE native_dh_keygen status=%d material_logged=0\n", result);
    errno = saved; return result;
}
EXPORT int dbAPILstView(const char *view, const char *instance, void *data)
{
    int (*real)(const char *, const char *, void *) = symbol("dbAPILstView");
    int result = real(view, instance, data), saved = errno;
    if (view && data && !strcmp(view, "P1905AcdBssCfg")) {
        uint32_t count = 0;
        if (!result) memcpy(&count, (uint8_t *)data + 8, 4);
        message("ZTE_CONTROLLER_TRACE bss_config list_status=%d count=%u body_logged=0\n", result, count);
    }
    errno = saved; return result;
}
EXPORT int dbAPIGetView(const char *view, const char *instance, void *data)
{
    int (*real)(const char *, const char *, void *) = symbol("dbAPIGetView");
    int result = real(view, instance, data), saved = errno;
    if (view && data && !strcmp(view, "P1905AcdBssCfg")) {
        uint32_t tag = 0, enabled = 0;
        if (!result) {
            memcpy(&tag, (uint8_t *)data + 228, 4);
            memcpy(&enabled, (uint8_t *)data + 260, 4);
        }
        message("ZTE_CONTROLLER_TRACE bss_config get_status=%d tag=%u enabled=%u body_logged=0\n", result, tag, enabled);
    }
    if (view && (!strcmp(view, "IPInterface") || !strcmp(view, "MultiAPDomain")))
        message("ZTE_CONTROLLER_TRACE db_get view=%s instance=%.36s status=%d\n", view, instance ? instance : "", result);
    else if (result || (view && !strcmp(view, "P1905ComCfg")))
        message("ZTE_CONTROLLER_TRACE db_get view=%.36s status=%d\n", view ? view : "", result);
    errno = saved; return result;
}
EXPORT int bind(int fd, const struct sockaddr *address, socklen_t length)
{
    int (*real)(int, const struct sockaddr *, socklen_t) = symbol("bind");
    int result = real(fd, address, length), saved = errno;
    if (address && length >= sizeof(struct sockaddr_ll) && address->sa_family == AF_PACKET)
        message("ZTE_CONTROLLER_TRACE packet_bind ifindex=%d protocol=%u status=%d errno=%d\n",
                ((const struct sockaddr_ll *)address)->sll_ifindex,
                (unsigned int)ntohs(((const struct sockaddr_ll *)address)->sll_protocol), result, saved);
    else if (address && address->sa_family == AF_UNIX && length > sizeof(sa_family_t)) {
        /* The daemon's own OSS socket: its directory has to exist in the private root, so the path is the useful part of
         * a failure here. A socket name carries no credential. */
        char path[108];
        size_t room = length - offsetof(struct sockaddr_un, sun_path);
        if (room >= sizeof(path))
            room = sizeof(path) - 1;
        memcpy(path, ((const struct sockaddr_un *)address)->sun_path, room);
        path[room] = 0;
        message("ZTE_CONTROLLER_TRACE unix_bind path=%.90s status=%d errno=%d\n", path, result, saved);
    } else
        message("ZTE_CONTROLLER_TRACE bind status=%d errno=%d\n", result, saved);
    errno = saved; return result;
}
EXPORT int OssCreateSubProc(const char *name, void *callback, void *arg1, void *arg2)
{
    int (*real)(const char *, void *, void *, void *) = symbol("OssCreateSubProc");
    int result = real(name, callback, arg1, arg2), saved = errno;
    message("ZTE_CONTROLLER_TRACE return=OssCreateSubProc status=%d errno=%d\n", result, saved);
    errno = saved; return result;
}
/* The three WLAN setters that carry an M2 into the configuration database are not traced here: the
 * runtime stand-in for the configuration manager (libzte_agent_mm.so) owns them, because the status it
 * reports is what the daemon acts on and a second interposer would only hide it.
 */
EXPORT int OssSetThdInfo(void *name, unsigned int index, unsigned int priority, unsigned int options)
{
    int (*real)(void *, unsigned int, unsigned int, unsigned int) = symbol("OssSetThdInfo");
    int result = real(name, index, priority, options), saved = errno;
    message("ZTE_CONTROLLER_TRACE return=OssSetThdInfo status=%d errno=%d\n", result, saved);
    errno = saved; return result;
}
EXPORT int vsnprintf(char *buffer, size_t size, const char *format, va_list ap)
{
    Dl_info info;
    char text[241];
    unsigned int i;
    int result, saved;
    if (!format_real)
        format_real = (int (*)(char *, size_t, const char *, va_list))dlsym(RTLD_NEXT, "vsnprintf");
    if (!format_real)
        _exit(79);
    result = format_real(buffer, size, format, ap); saved = errno;
    if (dladdr(__builtin_return_address(0), &info) && info.dli_sname &&
        (!strcmp(info.dli_sname, "ProcUserLog") || !strcmp(info.dli_sname, "ProcOssLogType"))) {
        if (strstr(format, "M1") || strstr(format, "M2") || strstr(format, "WSC"))
            message("ZTE_CONTROLLER_TRACE wsc_status vendor_format=%.120s\n", format);
        /* Opt-in firehose: while a frame shape is still being matched, the daemon's own objections are the
         * only evidence, and they do not all mention M1/M2/WSC. Enabled by a flag file, not an environment
         * variable, because the sandbox hands the daemon a single-variable environment. */
        if (verbose_enabled()) {
            static unsigned int verbose_lines;
            if (__sync_fetch_and_add(&verbose_lines, 1) < 20000)
                message("ZTE_TRACE_ALL vendor_format=%.150s\n", format);
        }
        /* Values rather than shapes, for the handful of lines the agent-mode criterion turns on: whether the
         * M2 was applied (cfgChg), which BSS it went to, and what the WLAN setter returned. Only formats that
         * carry no key material are expanded. */
        if (size > 0 && (strstr(format, "acdcParseM2Msg cfgChg") || strstr(format, "The WSC-%d will cfg") ||
                         strstr(format, "current state") || strstr(format, "M2 total Length") ||
                         strstr(format, "WLAN_ASSERT_RET") || strstr(format, "mesh sync for M2")))
            message("ZTE_TRACE_VALUE text=%.200s\n", buffer);
        if (strstr(format, "Bss") || strstr(format, "bss") || strstr(format, "WLANCFG") ||
            strstr(format, "PWKey") || strstr(format, "WEP") || strstr(format, "Wep") || strstr(format, "BeaconType"))
            message("ZTE_CONTROLLER_TRACE bss_config vendor_format=%.120s\n", format);
        /* Fixed protocol rejection labels retain no MAC/SSID/key varargs and
         * have an independent packet budget; repeated DB errors cannot hide them. */
        if (strstr(format, "No one care the msg,type:"))
            message("ZTE_CONTROLLER_TRACE packet_decision reject=no_registered_handler\n");
        else if (strstr(format, "Unsupported version:"))
            message("ZTE_CONTROLLER_TRACE packet_decision reject=version\n");
        else if (strstr(format, "Bad length of the frame"))
            message("ZTE_CONTROLLER_TRACE packet_decision reject=short_frame\n");
        else if (strstr(format, "dest mac is") && strstr(format, "not local and multicast"))
            message("ZTE_CONTROLLER_TRACE packet_decision reject=destination_not_local\n");
        else if (strstr(format, "not allowed to MESH with the current device"))
            message("ZTE_CONTROLLER_TRACE packet_decision reject=peer_policy\n");
        else if (strstr(format, "had sent,ignore"))
            message("ZTE_CONTROLLER_TRACE packet_decision reject=self_source\n");
        /* The authentication type negotiated for an M2 (FEATURE_MAP section 12): the two vendor formats only, which carry
         * nothing but hex numbers (configured/agent-advertised/final authentication type bit masks). They are level-8
         * messages: nothing appears at the daemon's default log level (replay run 28), so the negotiated type was checked
         * through the agent's own BeaconType read-back instead. */
        if (strstr(format, "Change Auth Type to") || strstr(format, "slave_auth_type"))
            message("ZTE_CONTROLLER_TRACE auth %.80s\n", buffer);
        /* Suppress routine timer/config chatter, keeping startup/error formats.
         * Never copy substituted MAC/key/SSID/serial/body values.
         */
        if (!strstr(format, "fail") && !strstr(format, "Fail") && !strstr(format, "ERROR") &&
            !strstr(format, "error") && !strstr(format, "MASTER") &&
            !strstr(format, "Creating a new") && !strstr(format, "Server")) {
            errno = saved;
            return result;
        }
        for (i = 0; i < sizeof(text) - 1 && format[i]; i++)
            text[i] = (unsigned char)format[i] >= 32 && (unsigned char)format[i] <= 126 ? format[i] : ' ';
        text[i] = 0;
        message("ZTE_CONTROLLER_TRACE vendor_log_format=%s\n", text);
    }
    errno = saved; return result;
}
