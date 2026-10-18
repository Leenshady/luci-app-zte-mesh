/* Isolated DB experiment launcher. Failure never falls back to the host. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern int controller_private_network(void);
extern int controller_private_network_service(int host_net);

#ifndef SERVICE_ROOT
#define SERVICE_ROOT "/usr/lib/zte-mesh/root" /* test builds may override with -DSERVICE_ROOT */
#endif
#define SERVICE_STATE "/run/zte-mesh"
static volatile sig_atomic_t stop_requested;
static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void fail(const char *operation)
{
    fprintf(stderr, "ZTE_DB_SANDBOX error=%s errno=%d\n", operation, errno);
    _exit(80);
}

static void isolate(const char *root, char **command)
{
    struct stat old_ipc, old_net, old_mnt, ipc, net, mnt;
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct caps[2] = {{0}};
    struct rlimit core = {0, 0}, memory = {128UL * 1024 * 1024, 128UL * 1024 * 1024};
    struct rlimit cpu = {5, 5};
    char path[4096];
    int cap, host_net = -1;
    int service = command[4] && !strcmp(command[4], "--controller-service");
    int startup = service || (command[4] && !strcmp(command[4], "--controller-startup"));
    if (service)
        cpu.rlim_cur = cpu.rlim_max = RLIM_INFINITY;
    if (setpgid(0, 0) < 0)
        fail("setpgid");
    if (service && (host_net = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC)) < 0)
        fail("host-network-reference");
    if (stat("/proc/self/ns/ipc", &old_ipc) || stat("/proc/self/ns/net", &old_net) || stat("/proc/self/ns/mnt", &old_mnt))
        fail("namespace-stat-before");
    if (unshare(CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWNS))
        fail("unshare-ipc-net-mount");
    if (stat("/proc/self/ns/ipc", &ipc) || stat("/proc/self/ns/net", &net) || stat("/proc/self/ns/mnt", &mnt))
        fail("namespace-stat-after");
    if (old_ipc.st_ino == ipc.st_ino || old_net.st_ino == net.st_ino || old_mnt.st_ino == mnt.st_ino) {
        errno = EPERM;
        fail("namespace-not-private");
    }
    if (service ? controller_private_network_service(host_net) : (startup && controller_private_network()))
        fail(service ? "private-service-network" : "private-lab-network");
    if (host_net >= 0)
        close(host_net);
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL))
        fail("private-mount-propagation");
    if (mount(root, root, NULL, MS_BIND, NULL) || mount(NULL, root, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV, NULL))
        fail("read-only-root");
    if (snprintf(path, sizeof(path), "%s/tmp", root) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        fail("root-path-length");
    }
    if (mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "size=8m,mode=1777"))
        fail("private-tmp");
    if (snprintf(path, sizeof(path), "%s/dev/shm", root) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        fail("root-path-length");
    }
    if (mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "size=1m,mode=1777"))
        fail("private-dev-shm");
    if (service) {
        struct stat source, target;
        /* The only writable host path: a root-owned private exchange directory
         * for the host-written config and the daemon's state report. */
        if (snprintf(path, sizeof(path), "%s" SERVICE_STATE, root) >= (int)sizeof(path) ||
            lstat(SERVICE_STATE, &source) || !S_ISDIR(source.st_mode) || source.st_uid != 0 ||
            (source.st_mode & 022) || stat(path, &target) || !S_ISDIR(target.st_mode)) {
            errno = EINVAL;
            fail("service-exchange-directory");
        }
        if (mount(SERVICE_STATE, path, NULL, MS_BIND, NULL) ||
            mount(NULL, path, NULL, MS_BIND | MS_REMOUNT | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL))
            fail("service-exchange-bind");
    }
    if (startup) {
        struct stat source, target;
        if (snprintf(path, sizeof(path), "%s/dev/urandom", root) >= (int)sizeof(path) ||
            stat("/dev/urandom", &source) || !S_ISCHR(source.st_mode) ||
            source.st_rdev != makedev(1, 9) || lstat(path, &target) ||
            !S_ISREG(target.st_mode) || target.st_size != 0) {
            errno = EINVAL;
            fail("readonly-random-source");
        }
        /* The one deliberately permitted character device: kernel entropy.
         * No wireless/switch/flash devices, /proc, or host /dev directory.
         */
        if (mount("/dev/urandom", path, NULL, MS_BIND, NULL) ||
            mount(NULL, path, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NOEXEC, NULL))
            fail("readonly-random-bind");
    }
    if (chdir(root) || chroot(".") || chdir("/"))
        fail("chroot");
    if (setrlimit(RLIMIT_CORE, &core) || setrlimit(RLIMIT_AS, &memory) || setrlimit(RLIMIT_CPU, &cpu))
        fail("resource-limits");
    for (cap = 0; cap <= CAP_LAST_CAP; ++cap)
        if (cap != CAP_IPC_OWNER && (!startup || cap != CAP_NET_RAW) && prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) && errno != EINVAL)
            fail("capability-bounding-set");
    caps[0].effective = (1U << CAP_IPC_OWNER) | (startup ? (1U << CAP_NET_RAW) : 0);
    caps[0].permitted = caps[0].effective;
    if (syscall(SYS_capset, &header, caps) || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
        fail("capabilities");
    /* Existing FDs must not provide an escape to a host directory/device,
     * including descriptors >=1024. COMPAT target supports close_range.
     */
    if (syscall(SYS_close_range, 3U, ~0U, 0U))
        fail("close-inherited-fds");
    {
        /* Captured before clearenv() wipes the inherited environment. Only this one
         * named pair survives into the sandbox, and only for the live service: a
         * public ModelName/Manufacturer opt-in, never a serial, never silent. */
        const char *manufacturer = service ? getenv("ZTE_MESH_REAL_MANUFACTURER") : NULL;
        const char *model_name = service ? getenv("ZTE_MESH_REAL_MODEL") : NULL;
        const char *market_sign = service ? getenv("ZTE_MESH_REAL_MARKET_SIGN") : NULL;
        char manufacturer_buf[64], model_buf[64], market_buf[64];
        if (manufacturer && model_name && strlen(manufacturer) < sizeof(manufacturer_buf) &&
            strlen(model_name) < sizeof(model_buf)) {
            strcpy(manufacturer_buf, manufacturer);
            strcpy(model_buf, model_name);
        } else {
            manufacturer = model_name = NULL;
        }
        if (market_sign && strlen(market_sign) < sizeof(market_buf))
            strcpy(market_buf, market_sign);
        else
            market_sign = NULL;
        clearenv();
        if (setenv("ZTE_MESH_DB_SANDBOX", "1", 1) || setenv("LD_BIND_NOW", "1", 1) ||
            setenv("LD_PRELOAD", "/lib/libzte_mesh_shim.so", 1) ||
            setenv("ZTE_MESH_SHIM_MODE", "trace", 1) || setenv("ZTE_MESH_SHIM_ALLOW_TRACE", "1", 1))
            fail("environment");
        if (manufacturer && (setenv("ZTE_MESH_REAL_MANUFACTURER", manufacturer_buf, 1) ||
                             setenv("ZTE_MESH_REAL_MODEL", model_buf, 1)))
            fail("environment");
        if (market_sign && setenv("ZTE_MESH_REAL_MARKET_SIGN", market_buf, 1))
            fail("environment");
    }
    fprintf(stderr, "ZTE_DB_SANDBOX private_ipc=1 private_net=1 readonly_root=1 caps=%s\n",
            service ? "IPC_OWNER,NET_RAW service_exchange=1 bridge_port=lab-peer" :
            startup ? "IPC_OWNER,NET_RAW private_lab_bridges=2 private_veth=1" : "IPC_OWNER");
    execv(command[0], command);
    fail("execv");
}

int main(int argc, char **argv)
{
    struct timespec started, now, pause_time = {0, 20000000};
    char root[4096];
    int status, timeout_seconds;
    siginfo_t information;
    pid_t child, waited;
    int service = argc == 7 && !strcmp(argv[6], "--controller-service");
    if ((argc != 6 && argc != 7) || !realpath(argv[1], root) ||
        (service ? strcmp(argv[1], SERVICE_ROOT) || strcmp(root, SERVICE_ROOT)
                 : strncmp(argv[1], "/tmp/zte-mesh-db.", 17) || strncmp(root, "/tmp/zte-mesh-db.", 17))) {
        fprintf(stderr, "usage: ns-run /tmp/zte-mesh-db.XXXXXX/root /lib/ld-linux.so.3 --library-path /lib /bin/db-engine-probe [--bridge-test|--bridge-rollback-test|--controller-startup]\n"
                        "       ns-run " SERVICE_ROOT " /lib/ld-linux.so.3 --library-path /lib /bin/db-engine-probe --controller-service\n");
        return 64;
    }
    if (strcmp(argv[2], "/lib/ld-linux.so.3") || strcmp(argv[3], "--library-path") ||
        strcmp(argv[4], "/lib") || strcmp(argv[5], "/bin/db-engine-probe")) {
        fprintf(stderr, "ZTE_DB_SANDBOX refuse=unexpected-executable\n");
        return 64;
    }
    if (argc == 7 && strcmp(argv[6], "--controller-startup") && !service) {
        struct stat input, output;
        if ((strcmp(argv[6], "--bridge-test") && strcmp(argv[6], "--bridge-rollback-test")) ||
            fstat(STDIN_FILENO, &input) || fstat(STDOUT_FILENO, &output) ||
            !S_ISFIFO(input.st_mode) || !S_ISFIFO(output.st_mode)) {
            fprintf(stderr, "ZTE_DB_SANDBOX refuse=bridge-requires-fixed-mode-and-pipes\n");
            return 64;
        }
    }
    timeout_seconds = service ? 0 : argc == 7 && !strcmp(argv[6], "--controller-startup") ? 50 : 15;
    if (service) {
        struct sigaction action = {0};
        action.sa_handler = request_stop;
        if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL) || sigaction(SIGHUP, &action, NULL))
            fail("signal-handlers");
    }
    clock_gettime(CLOCK_MONOTONIC, &started);
    child = fork();
    if (child < 0)
        fail("fork");
    if (!child)
        isolate(root, argv + 2);
    (void)setpgid(child, child);
    for (;;) {
        memset(&information, 0, sizeof(information));
        if (waitid(P_PID, child, &information, WEXITED | WNOHANG | WNOWAIT)) {
            if (errno == EINTR)
                continue;
            fail("waitid");
        }
        if (information.si_pid == child)
            break;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (stop_requested) {
            (void)kill(-child, SIGKILL);
            (void)waitpid(child, &status, 0);
            fprintf(stderr, "ZTE_DB_SANDBOX stop=requested\n");
            return 0;
        }
        if (timeout_seconds && now.tv_sec - started.tv_sec >= timeout_seconds) {
            (void)kill(-child, SIGKILL);
            (void)waitpid(child, &status, 0);
            fprintf(stderr, "ZTE_DB_SANDBOX stop=timeout\n");
            return 124;
        }
        nanosleep(&pause_time, NULL);
    }
    /* Hold the zombie PID until group cleanup: no PID/PGID reuse race. */
    (void)kill(-child, SIGKILL);
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child)
        fail("waitpid");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
