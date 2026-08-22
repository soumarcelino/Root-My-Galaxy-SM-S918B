#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    void *handle;
    const char *error;
    if (argc != 2) {
        fprintf(stderr, "usage: %s PAYLOAD.so\n", argv[0]);
        return 64;
    }
    dlerror();
    handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        error = dlerror();
        fprintf(stderr, "dlopen: %s\n", error ? error : "unknown");
        return 70;
    }
    return 0;
}
