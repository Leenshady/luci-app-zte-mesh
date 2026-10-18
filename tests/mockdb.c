#include <errno.h>
#include <stdint.h>
#include <unistd.h>

int DBShmCliInit(void)
{
    static const char marker[] = "MOCK_INIT_CALLED\n";
    if (write(1, marker, sizeof(marker) - 1) < 0)
        return -1;
    errno = EBUSY;
    return 17;
}

int dbAPIGetView(const char *view, const char *instance, void *data)
{
    (void)view;
    (void)instance;
    *(uint32_t *)data = 0x11223344;
    errno = EINVAL;
    return -45;
}
