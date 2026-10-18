/* Factory ARM EABI soft-float boundary probe. No guessed database backend. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(__arm__) && !defined(ZTE_MESH_HOST_TEST)
#error "The vendor preload must be compiled for ARM32, not the OpenWrt host ABI"
#endif

#define EXPORT __attribute__((visibility("default")))
#define PROBE_STOP 78
#define BINDING_FAILURE 79

static int trace_mode;
static unsigned int records;
static __thread int logging;

/* Log API metadata only: no instance/key, returned buffers, PSKs or pointers. */
static void log_record(const char *format, ...)
{
    int saved_errno = errno;
    char buffer[384];
    va_list args;
    int length;
    if (logging)
        return;
    if (__sync_fetch_and_add(&records, 1) >= 128) {
        errno = saved_errno;
        return;
    }
    logging = 1;
    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length > 0) {
        size_t total = (size_t)length < sizeof(buffer) ? (size_t)length : sizeof(buffer) - 1;
        size_t offset = 0;
        while (offset < total) {
            ssize_t count = write(STDERR_FILENO, buffer + offset, total - offset);
            if (count > 0)
                offset += (size_t)count;
            else if (count < 0 && errno == EINTR)
                continue;
            else
                break;
        }
    }
    logging = 0;
    errno = saved_errno;
}

__attribute__((constructor)) static void shim_loaded(void)
{
    const char *mode = getenv("ZTE_MESH_SHIM_MODE");
    const char *allow = getenv("ZTE_MESH_SHIM_ALLOW_TRACE");
    if (!mode || !strcmp(mode, "probe")) {
        log_record("ZTE_MESH_SHIM loaded mode=probe\n");
        return;
    }
    if (!strcmp(mode, "trace") && allow && !strcmp(allow, "1")) {
        trace_mode = 1;
        log_record("ZTE_MESH_SHIM loaded mode=trace\n");
        return;
    }
    log_record("ZTE_MESH_SHIM stop reason=unsupported-or-unapproved-mode exit=78\n");
    _exit(PROBE_STOP);
}

static void *next_symbol(const char *name)
{
    int saved_errno = errno;
    void *function = dlsym(RTLD_NEXT, name);
    if (!function) {
        log_record("ZTE_MESH_SHIM stop reason=missing-real-symbol symbol=%s exit=79\n", name);
        _exit(BINDING_FAILURE);
    }
    errno = saved_errno;
    return function;
}

EXPORT int DBShmCliInit(void)
{
    int (*real_init)(void);
    int result;
    log_record("ZTE_MESH_SHIM call=DBShmCliInit\n");
    if (!trace_mode) {
        /* main ignores the original return value; returning an error is unsafe. */
        log_record("ZTE_MESH_SHIM stop reason=database-backend-not-implemented exit=78\n");
        _exit(PROBE_STOP);
    }
    real_init = (int (*)(void))next_symbol("DBShmCliInit");
    result = real_init();
    /* The factory implementation can return a shared-memory address.
     * Preserve it for ABI compatibility, but never log it as a status code.
     */
    log_record("ZTE_MESH_SHIM return=DBShmCliInit returned=1\n");
    return result;
}

EXPORT int dbAPIGetView(const char *view, const char *instance, void *data)
{
    int (*real_get)(const char *, const char *, void *);
    int result;
    log_record("ZTE_MESH_SHIM call=dbAPIGetView view=%.64s\n", view ? view : "(null)");
    if (!trace_mode) {
        log_record("ZTE_MESH_SHIM stop reason=unexpected-database-read-in-probe exit=78\n");
        _exit(PROBE_STOP);
    }
    real_get = (int (*)(const char *, const char *, void *))next_symbol("dbAPIGetView");
    result = real_get(view, instance, data);
    log_record("ZTE_MESH_SHIM return=dbAPIGetView status=%d\n", result);
    return result;
}
