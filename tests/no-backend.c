#define _GNU_SOURCE
#include <dlfcn.h>

int main(void)
{
    int (*init)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "DBShmCliInit");
    return init ? init() : 3;
}
