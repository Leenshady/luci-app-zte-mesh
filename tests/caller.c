#include <errno.h>
#include <stdint.h>
#include <stdio.h>

int DBShmCliInit(void);
int dbAPIGetView(const char *, const char *, void *);

int main(void)
{
    uint32_t data = 0;
    int result = DBShmCliInit();
    if (result != 17 || errno != EBUSY)
        return 1;
    result = dbAPIGetView("TestView", "secret-credential-not-for-logs", &data);
    if (result != -45 || errno != EINVAL || data != 0x11223344)
        return 2;
    puts("ABI_PASSTHROUGH_OK");
    return 0;
}
